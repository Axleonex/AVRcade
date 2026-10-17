using System;
using BepInEx.Configuration;
using BepInEx.Logging;
using HarmonyLib;
using RootMotion.FinalIK;
using UnityEngine;
using Valve.VR;
using NVector3 = System.Numerics.Vector3;
using NQuaternion = System.Numerics.Quaternion;

namespace Avrcade.YapyapVr;

/// Uses the avatar's existing IK and prop muscles, without changing multiplayer ownership.
internal sealed class YapyapControllerHands : IDisposable
{
    internal static YapyapControllerHands? Instance { get; private set; }
    private readonly ManualLogSource _log;
    private readonly SteamVrActionReader _actions;
    private readonly ConfigEntry<bool> _enabled;
    private readonly ConfigEntry<bool> _renderSynchronized;
    private readonly ConfigEntry<Vector3> _leftPosition, _rightPosition, _leftRotation, _rightRotation;
    private readonly TrackedDevicePose_t[] _poses = new TrackedDevicePose_t[OpenVR.k_unMaxTrackedDeviceCount];
    private YAPYAP.Pawn? _pawn;
    private FullBodyBipedIK? _ik;
    private YAPYAP.FirstPersonCamFollowIK? _cameraFollow;
    private IKSolverFullBodyBiped? _solver;
    private Hand? _left, _right;
    private float _nextBind, _nextDiagnostic, _poseTime;
    private int _sampleFrame = -1, _status = -1, _errorCount;
    private HandPose _headPose, _leftPose, _rightPose;
    private bool _headValid, _leftValid, _rightValid, _renderSolve;
    private float _nextRenderDiagnostic;
    private int _renderReports;
    private int _lastRenderSolveFrame = -1;

    internal YapyapControllerHands(ConfigFile config, ManualLogSource log, SteamVrActionReader actions)
    {
        Instance = this;
        _log = log;
        _actions = actions;
        _enabled = config.Bind("TrackedHands", "Enabled", true, "Drive the local avatar's hands from controller position and rotation.");
        _renderSynchronized = config.Bind("TrackedHands", "RenderSynchronizedHands", true,
            "Solve hands after the headset's final pose update using matching SteamVR render-frame poses and the stereo eye center. Disable to restore the previous late-update solve.");
        _leftPosition = config.Bind("TrackedHands", "LeftGripOffset", Vector3.zero, "Left wrist offset in controller-local meters (maximum 0.5m).");
        _rightPosition = config.Bind("TrackedHands", "RightGripOffset", Vector3.zero, "Right wrist offset in controller-local meters (maximum 0.5m).");
        _leftRotation = config.Bind("TrackedHands", "LeftWristRotation", Vector3.zero, "Additional left wrist alignment in Euler degrees.");
        _rightRotation = config.Bind("TrackedHands", "RightWristRotation", Vector3.zero, "Additional right wrist alignment in Euler degrees.");
        Application.onBeforeRender += ObserveRenderedHands;
    }

    internal void UpdateBinding()
    {
        var pawn = YAPYAP.Pawn.LocalInstance;
        if (_pawn != pawn || _ik == null || _solver == null) Unbind();
        if (_solver != null || Time.realtimeSinceStartup < _nextBind) return;
        _nextBind = Time.realtimeSinceStartup + 0.5f;
        if (!_enabled.Value || pawn == null || !pawn.isLocalPlayer || pawn.IsAI || pawn.View == null) return;
        var ik = pawn.View.FullBodyBipedIK;
        if (ik == null || !ik.solver.initiated || ik.references.leftHand == null ||
            ik.references.rightHand == null || ik.references.leftForearm == null || ik.references.rightForearm == null ||
            ik.solver.leftHandEffector.target == null || ik.solver.rightHandEffector.target == null) return;
        _pawn = pawn;
        _ik = ik;
        _solver = ik.solver;
        _cameraFollow = pawn.View.FirstPersonCamFollowIK;
        var leftMapping = FindMapping(_solver, ik.references.leftHand);
        var rightMapping = FindMapping(_solver, ik.references.rightHand);
        if (leftMapping == null || rightMapping == null ||
            _solver.leftHandEffector.bone != ik.references.leftHand ||
            _solver.rightHandEffector.bone != ik.references.rightHand)
        {
            _log.LogWarning("VR hands deferred: native limb/effector does not end at the referenced wrist.");
            Unbind();
            return;
        }
        _left = new Hand(_solver.leftHandEffector, leftMapping, ik.references.leftHand,
            WristAlignment(ik.references.leftHand, ik.references.leftForearm, true),
            NativePalmOffset(ik.references.leftHand));
        _right = new Hand(_solver.rightHandEffector, rightMapping, ik.references.rightHand,
            WristAlignment(ik.references.rightHand, ik.references.rightForearm, false),
            NativePalmOffset(ik.references.rightHand));
        _solver.OnPreUpdate += BeforeSolve;
        _solver.OnPostUpdate += AfterSolve;
        _status = -1;
        _renderReports = 0;
        _log.LogInfo($"VR hands bound to local native wrists: left={_left.Bone.name}, right={_right.Bone.name}; native arm blends L={leftMapping.weight:F2}, R={rightMapping.weight:F2}; offsets L={_left.WristOffset.eulerAngles}, R={_right.WristOffset.eulerAngles}.");
        _log.LogInfo($"VR anatomical palm offsets: L={_left.GripOffset} R={_right.GripOffset}; input uses SteamVR handgrip position with confirmed raw orientation.");
    }

    private bool Eligible()
    {
        if (!_enabled.Value || !YapyapVrPlugin.ControlsActive || _pawn == null ||
            _pawn != YAPYAP.Pawn.LocalInstance || !_pawn.isLocalPlayer || _pawn.IsAI ||
            !_pawn.HasAuthority || _pawn.IsDead || !_pawn.gameObject.activeInHierarchy ||
            _ik == null || _solver == null || !_solver.initiated ||
            _pawn.ViewStateMachine?.HandIKComponent?.IsActive != true) return false;
        var state = _pawn.ViewStateMachine.CurrentState;
        return state is not YAPYAP.ViewStateRagdoll && state is not YAPYAP.ViewStateLadderClimbing &&
            state is not YAPYAP.ViewStateEmote && state is not YAPYAP.ViewStateLedgeMantle &&
            state is not YAPYAP.ViewStatePolymorphed && YapyapVrFocus.SceneInputAvailable;
    }

    private void SamplePoses(bool rendered = false)
    {
        if (!rendered && _sampleFrame == Time.frameCount) return;
        _sampleFrame = Time.frameCount;
        _headValid = _leftValid = _rightValid = false;
        var system = OpenVR.System;
        var compositor = OpenVR.Compositor;
        if (system == null || compositor == null) return;
        // All three devices share the compositor's standing/seated origin.
        if (rendered)
        {
            if (compositor.GetLastPoses(_poses, Array.Empty<TrackedDevicePose_t>()) != EVRCompositorError.None) return;
        }
        else system.GetDeviceToAbsoluteTrackingPose(compositor.GetTrackingSpace(), 0f, _poses);
        _poseTime = Time.realtimeSinceStartup;
        _headValid = ReadPose(OpenVR.k_unTrackedDeviceIndex_Hmd, out _headPose);
        _leftValid = ReadGripPose(system.GetTrackedDeviceIndexForControllerRole(ETrackedControllerRole.LeftHand), true,
            compositor.GetTrackingSpace(), out _leftPose, rendered);
        _rightValid = ReadGripPose(system.GetTrackedDeviceIndexForControllerRole(ETrackedControllerRole.RightHand), false,
            compositor.GetTrackingSpace(), out _rightPose, rendered);
    }

    private bool ReadGripPose(uint index, bool left, ETrackingUniverseOrigin origin, out HandPose pose, bool rendered)
    {
        pose = default;
        return ReadPose(index, out var raw) && _actions.TryReadGripPose(left, origin, out var trackedGrip, rendered) &&
            ConvertPose(trackedGrip, out var grip) && HandTrackingMath.TryControllerGripPose(raw, grip, out pose);
    }

    private bool ReadPose(uint index, out HandPose pose)
    {
        pose = default;
        if (index >= _poses.Length) return false;
        return ConvertPose(_poses[index], out pose);
    }

    private static bool ConvertPose(TrackedDevicePose_t raw, out HandPose pose)
    {
        pose = default;
        if (!raw.bDeviceIsConnected || !raw.bPoseIsValid || raw.eTrackingResult != ETrackingResult.Running_OK) return false;
        var m = raw.mDeviceToAbsoluteTracking;
        return HandTrackingMath.TryFromOpenVr(m.m0, m.m1, m.m2, m.m3, m.m4, m.m5,
            m.m6, m.m7, m.m8, m.m9, m.m10, m.m11, out pose);
    }

    private void BeforeSolve()
    {
        // Restore a previous unfinished solve before taking a new native-state snapshot.
        Suspend();
        try
        {
            if (!Eligible()) { LogState(0); return; }
            SamplePoses();
            var manager = YAPYAP.CameraManager.Instance;
            var camera = manager?.MainCamera;
            if (!_headValid || camera == null) { LogState(1); return; }
            var headRotation = Unity(_headPose.Rotation);
            var worldRotation = manager!.cinemachineCam != null
                ? Quaternion.Euler(0f, manager.cinemachineCam.State.RawOrientation.eulerAngles.y, 0f) * headRotation
                : camera.transform.rotation;
            var worldPosition = camera.transform.position;
            if (_renderSolve && _renderSynchronized.Value)
            {
                worldRotation = camera.transform.rotation;
                worldPosition = RenderedEyeCenter(camera);
            }
            var worldHead = new HandPose(Numeric(worldPosition), Numeric(worldRotation));
            var age = Time.realtimeSinceStartup - _poseTime;
            var left = Apply(_left!, _leftValid, _leftPose, worldHead, age, _leftPosition.Value, _leftRotation.Value);
            var right = Apply(_right!, _rightValid, _rightPose, worldHead, age, _rightPosition.Value, _rightRotation.Value);
            LogState(2 | (left ? 4 : 0) | (right ? 8 : 0));
        }
        catch (Exception error)
        {
            // Never leave native targets overridden if sampling/applying failed.
            Suspend();
            if (_errorCount++ < 3) _log.LogWarning($"VR hands deferred to native animation: {error}");
        }
    }

    private bool Apply(Hand hand, bool valid, HandPose pose, HandPose worldHead, float age, Vector3 position, Vector3 rotation)
    {
        if (hand.Bone == null || hand.Effector.target == null ||
            !HandTrackingMath.CanTrack(true, true, true, true, valid, valid, age) ||
            !HandTrackingMath.TryToWorldGrip(_headPose, pose, worldHead, Numeric(position),
                Numeric(Quaternion.Euler(rotation) * hand.WristOffset),
                Numeric(Vector3.Scale(hand.GripOffset, hand.Bone.lossyScale)), out var result)) return false;
        hand.Override(Unity(result.Position), Unity(result.Rotation));
        return true;
    }

    [BeforeRenderOrder(20000)]
    private void ObserveRenderedHands()
    {
        // UUVR updates the headset again onBeforeRender. Replace (rather than
        // add to) the old after-camera-follow solve to avoid a third IK pass.
        if (_renderSynchronized.Value && _lastRenderSolveFrame != Time.frameCount && Eligible())
        {
            _lastRenderSolveFrame = Time.frameCount;
            try
            {
                _renderSolve = true;
                SamplePoses(rendered: true);
                _solver!.Update();
            }
            catch (Exception error)
            {
                if (_errorCount++ < 3) _log.LogWarning($"VR hand render solve deferred: {error}");
            }
            finally { Suspend(); _renderSolve = false; }
        }
        if (_renderReports >= 16 || Time.realtimeSinceStartup < _nextRenderDiagnostic || !Eligible()) return;
        if (_left == null || _right == null ||
            (_left.LastFrame != Time.frameCount && _right.LastFrame != Time.frameCount)) return;
        var camera = YAPYAP.CameraManager.Instance?.MainCamera;
        if (camera == null) return;
        _nextRenderDiagnostic = Time.realtimeSinceStartup + 5f;
        _renderReports++;
        var eyeCenter = RenderedEyeCenter(camera);
        _log.LogInfo($"VR rendered grips: L={_left.GripError:F3}m R={_right.GripError:F3}m; fresh L={_left.LastFrame == Time.frameCount} R={_right.LastFrame == Time.frameCount}; final wrist L={_left.FinalError:F3}m R={_right.FinalError:F3}m; eye-minus-camera={eyeCenter - camera.transform.position}; wrist scales L={_left.Bone.lossyScale} R={_right.Bone.lossyScale}; renderSynchronized={_renderSynchronized.Value}.");
    }

    private static Vector3 RenderedEyeCenter(Camera camera)
    {
        if (camera.stereoEnabled)
        {
            var leftEye = camera.GetStereoViewMatrix(Camera.StereoscopicEye.Left).inverse.GetColumn(3);
            var rightEye = camera.GetStereoViewMatrix(Camera.StereoscopicEye.Right).inverse.GetColumn(3);
            if (HandTrackingMath.TryStereoEyeCenter(new NVector3(leftEye.x, leftEye.y, leftEye.z),
                    new NVector3(rightEye.x, rightEye.y, rightEye.z), out var center) &&
                (Unity(center) - camera.transform.position).sqrMagnitude <= 0.25f)
                return Unity(center);
        }
        return camera.transform.position;
    }

    private void AfterSolve()
    {
        if ((_renderSolve || _cameraFollow == null) && (_left?.Pending == true || _right?.Pending == true) && Time.realtimeSinceStartup >= _nextDiagnostic)
        {
            _nextDiagnostic = Time.realtimeSinceStartup + 5f;
            var camera = YAPYAP.CameraManager.Instance?.MainCamera;
            var origin = camera != null ? camera.transform.position : Vector3.zero;
            _log.LogInfo($"VR wrists solved: L={_left?.Error:F3}m R={_right?.Error:F3}m; tracked L={_left?.Pending} R={_right?.Pending}; target-from-head L={_left?.Desired - origin} R={_right?.Desired - origin}; arm blends L={_left?.Mapping.weight:F2} R={_right?.Mapping.weight:F2}.");
        }
        Suspend();
    }

    internal void AfterCameraFollow(YAPYAP.FirstPersonCamFollowIK follow)
    {
        // Native camera-follow moves the chest/shoulders after PuppetMaster's IK read.
        // Re-solve that same local rig so the rendered wrists retain their tracked targets.
        if (_renderSynchronized.Value || follow != _cameraFollow || !Eligible()) return;
        try { _renderSolve = true; _solver!.Update(); }
        catch (Exception error)
        {
            if (_errorCount++ < 3) _log.LogWarning($"VR hand render solve deferred: {error}");
        }
        finally { Suspend(); _renderSolve = false; }
    }

    private void LogState(int status)
    {
        if (status == _status) return;
        _status = status;
        _log.LogInfo($"VR hands state: eligible={status != 0}, head={status >= 2}, left={(status & 4) != 0}, right={(status & 8) != 0}.");
    }

    internal void Suspend()
    {
        _left?.Restore();
        _right?.Restore();
    }

    private void Unbind()
    {
        Suspend();
        if (_solver != null)
        {
            _solver.OnPreUpdate -= BeforeSolve;
            _solver.OnPostUpdate -= AfterSolve;
        }
        _pawn = null;
        _ik = null;
        _solver = null;
        _cameraFollow = null;
        _left = _right = null;
    }

    public void Dispose()
    {
        Application.onBeforeRender -= ObserveRenderedHands;
        Unbind();
        if (Instance == this) Instance = null;
    }

    private Vector3 NativePalmOffset(Transform wrist)
    {
        Transform? middle = null;
        foreach (var bone in wrist.GetComponentsInChildren<Transform>(true))
        {
            if (!bone.name.Contains("Middle")) continue;
            if (middle == null || (bone.position - wrist.position).sqrMagnitude < (middle.position - wrist.position).sqrMagnitude)
                middle = bone;
        }
        if (middle != null && HandTrackingMath.TryPalmCenter(Numeric(wrist.InverseTransformPoint(middle.position)), out var palm))
            return Unity(palm);
        _log.LogWarning($"Native palm landmarks missing on {wrist.name}; using wrist origin for this avatar.");
        return Vector3.zero;
    }

    private static Quaternion WristAlignment(Transform hand, Transform forearm, bool left)
    {
        Transform? middle = null, thumb = null;
        // Scan only once per avatar. Pick proximal finger bones, not the fingertips.
        foreach (var child in hand.GetComponentsInChildren<Transform>(true))
        {
            if (child == hand) continue;
            var name = child.name.ToLowerInvariant();
            if (name.Contains("middle") && (middle == null || (child.position - hand.position).sqrMagnitude < (middle.position - hand.position).sqrMagnitude)) middle = child;
            if (name.Contains("thumb") && (thumb == null || (child.position - hand.position).sqrMagnitude < (thumb.position - hand.position).sqrMagnitude)) thumb = child;
        }
        var forward = middle != null ? middle.position - hand.position : hand.position - forearm.position;
        if (thumb != null && HandTrackingMath.TryWristAlignment(Numeric(forward),
                Numeric(thumb.position - hand.position), Numeric(hand.rotation), left, out var offset))
            return Unity(offset);
        // Unknown/custom avatar: align to the forearm with a normalized basis.
        var up = hand.up;
        if (forward.sqrMagnitude < 0.00000001f || Vector3.Cross(forward.normalized, up).sqrMagnitude < 0.000001f)
            up = hand.forward;
        return forward.sqrMagnitude < 0.00000001f ? Quaternion.identity :
            Quaternion.Inverse(Quaternion.LookRotation(forward.normalized, up)) * hand.rotation;
    }

    private static IKMappingLimb? FindMapping(IKSolverFullBodyBiped solver, Transform wrist)
    {
        foreach (var mapping in solver.limbMappings)
            if (mapping.bone3 == wrist) return mapping;
        return null;
    }

    private static NVector3 Numeric(Vector3 v) => new(v.x, v.y, v.z);
    private static NQuaternion Numeric(Quaternion q) => new(q.x, q.y, q.z, q.w);
    private static Vector3 Unity(NVector3 v) => new(v.X, v.Y, v.Z);
    private static Quaternion Unity(NQuaternion q) => new(q.X, q.Y, q.Z, q.W);

    private sealed class Hand
    {
        internal readonly IKEffector Effector;
        internal readonly Transform Bone;
        internal readonly IKMappingLimb Mapping;
        internal readonly Quaternion WristOffset;
        internal readonly Vector3 GripOffset;
        private Transform? _target;
        private Vector3 _position, _desired;
        private Quaternion _rotation, _desiredRotation;
        private float _positionWeight, _rotationWeight, _mappingWeight;
        internal bool Pending { get; private set; }
        internal Vector3 Desired => _desired;
        internal int LastFrame { get; private set; } = -1;
        internal float Error => Pending && Bone != null ? Vector3.Distance(Bone.position, _desired) : float.NaN;
        internal float FinalError => Bone != null ? Vector3.Distance(Bone.position, _desired) : float.NaN;
        internal float GripError => Bone != null ? Vector3.Distance(Bone.TransformPoint(GripOffset),
            _desired + _desiredRotation * Vector3.Scale(GripOffset, Bone.lossyScale)) : float.NaN;

        internal Hand(IKEffector effector, IKMappingLimb mapping, Transform bone, Quaternion offset, Vector3 gripOffset)
        { Effector = effector; Mapping = mapping; Bone = bone; WristOffset = offset; GripOffset = gripOffset; }

        internal void Override(Vector3 position, Quaternion rotation)
        {
            _target = Effector.target;
            _position = _target.localPosition;
            _rotation = _target.localRotation;
            _positionWeight = Effector.positionWeight;
            _rotationWeight = Effector.rotationWeight;
            _mappingWeight = Mapping.weight;
            _desired = position;
            _desiredRotation = rotation;
            LastFrame = Time.frameCount;
            Pending = true;
            _target.SetPositionAndRotation(position, rotation);
            Effector.positionWeight = Effector.rotationWeight = 1f;
            // The stock avatar blends the arm solve at 0.5. A fully weighted
            // hand effector alone leaves wrists halfway toward flat animation.
            Mapping.weight = 1f;
        }

        internal void Restore()
        {
            if (!Pending) return;
            Pending = false;
            if (_target != null)
            {
                _target.localPosition = _position;
                _target.localRotation = _rotation;
            }
            Effector.positionWeight = _positionWeight;
            Effector.rotationWeight = _rotationWeight;
            Mapping.weight = _mappingWeight;
            _target = null;
        }
    }
}

[HarmonyPatch(typeof(YAPYAP.FirstPersonCamFollowIK), "UpdateTransform")]
internal static class YapyapTrackedHandsAfterCamera
{
    private static void Postfix(YAPYAP.FirstPersonCamFollowIK __instance) =>
        YapyapControllerHands.Instance?.AfterCameraFollow(__instance);
}
