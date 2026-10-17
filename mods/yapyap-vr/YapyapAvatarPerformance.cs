using System;
using System.Collections.Generic;
using System.Reflection;
using BepInEx.Configuration;
using BepInEx.Logging;
using MagicaCloth2;
using UnityEngine;
using UnityEngine.Rendering;

namespace Avrcade.YapyapVr;

/// Cosmetic changes belong only to the local view rig, never network/game physics.
internal sealed class YapyapAvatarPerformance : IDisposable
{
    private readonly ConfigEntry<bool> _stableClothing, _clothShadows, _diagnostics;
    private readonly ConfigEntry<bool> _clothComfort;
    private readonly ConfigEntry<bool> _headClearance;
    private readonly ConfigEntry<bool> _hoodEyeFade;
    private readonly ConfigEntry<float> _headRadius, _collarBack, _collarDrop;
    private readonly ConfigEntry<float> _hoodTravel, _hoodInertia;
    private readonly ConfigEntry<float> _clothGravity, _sleeveDamping, _hoodDamping, _clothInertia, _clothWind, _hoodBackwardPull;
    private readonly ManualLogSource _log;
    private readonly Dictionary<Behaviour, bool> _simulations = new();
    private readonly Dictionary<Renderer, ShadowCastingMode> _shadows = new();
    private readonly Dictionary<MagicaCloth, ClothSettings> _clothSettings = new();
    private readonly Dictionary<Transform, Vector3> _collarPositions = new();
    private readonly Dictionary<Material, HoodFadeSettings> _hoodMaterials = new();
    private static readonly FieldInfo? RendererSetups = typeof(YAPYAP.PawnMaterial).GetField("rendererSetups", BindingFlags.Instance | BindingFlags.NonPublic);
    private readonly List<YAPYAP.PawnMaterial> _pawnMaterials = new();
    private MagicaSphereCollider? _headCollider;
    private float _headColliderSize = -1f;
    private bool _lastClearance, _lastHoodFade;
    private readonly List<MagicaCloth> _cloth = new();
    private readonly List<SkinnedMeshRenderer> _skinned = new();
    private readonly FrameTiming[] _timings = new FrameTiming[1];
    private YAPYAP.Pawn? _pawn;
    private float _nextScan, _nextReport, _nextClothDirection;
    private bool _lastStable, _lastShadows, _lastComfort;
    private double _frameSeconds;
    private float _worstFrame;
    private int _frames, _reports, _reportedCloth = -1, _reportedShadows = -1;

    internal YapyapAvatarPerformance(ConfigFile config, ManualLogSource log)
    {
        _log = log;
        _stableClothing = config.Bind("Performance", "StableLocalClothing", false,
            "Stop cosmetic cloth simulation on your local avatar. Sleeves follow the animated arm rig without cloth flutter. Other players and gameplay physics are unchanged.");
        _clothShadows = config.Bind("Performance", "ReduceLocalClothShadows", true,
            "Disable shadow casting on this local avatar's explicitly assigned cloth meshes. Hands and clothing remain visible.");
        _diagnostics = config.Bind("Performance", "LogInitialFrameTimes", true,
            "Log twelve five-second frame-time summaries after entering a VR level. CPU/GPU timing is included when Unity provides it; these are not headset compositor FPS measurements.");
        _clothComfort = config.Bind("ClothComfort", "Enabled", true,
            "Keep local robe/sleeve flutter, with stronger downward gravity and a rearward pull on the cloak/cowl. Original parameters are restored when VR controls stop.");
        _clothGravity = config.Bind("ClothComfort", "Gravity", 9.81f, "Local garment gravity strength (0-20). Stock robe/cloak use 5.");
        _sleeveDamping = config.Bind("ClothComfort", "SleeveDamping", 0.35f, "Damping for robe/sleeve oscillation (0-1). Stock value is 0.1.");
        _hoodDamping = config.Bind("ClothComfort", "HoodDamping", 0.45f, "Damping for cloak/cowl oscillation (0-1).");
        _clothInertia = config.Bind("ClothComfort", "MovementInertia", 0.35f, "Garment response to body and arm acceleration (0-1). Lower values reduce sudden faceward swings.");
        _clothWind = config.Bind("ClothComfort", "WindInfluence", 0.2f, "Local garment wind influence (0-2). Flutter remains simulated.");
        _hoodBackwardPull = config.Bind("ClothComfort", "HoodBackwardPull", 0.45f, "Rearward gravity component relative to the avatar's horizontal forward direction (0-1). Zero gives straight-down gravity.");
        _headClearance = config.Bind("ClothComfort", "HeadClearance", true,
            "Add a local MagicaCloth head-clearance sphere and lower/fold the cosmetic collar anchors backward. Does not collide with gameplay objects or move hands/head bones.");
        _hoodEyeFade = config.Bind("ClothComfort", "HoodEyeFade", true,
            "Use the game's camera-depth fade over the whole local cloak/cowl and hat, rather than its selective flatscreen opacity mask. Cloth close to the eyes fades; sleeves and hands keep their existing materials. Restored when VR stops.");
        _headRadius = config.Bind("ClothComfort", "HeadClearanceRadius", 0.2f, "Cloth-only head clearance radius in meters (0.12-0.3).");
        _collarBack = config.Bind("ClothComfort", "CollarBackOffset", 0.1f, "Move only cosmetic cloak collar anchors behind their normal position in meters (0-0.2).");
        _collarDrop = config.Bind("ClothComfort", "CollarDropOffset", 0.06f, "Lower only cosmetic cloak collar anchors in meters (0-0.15).");
        _hoodTravel = config.Bind("ClothComfort", "HoodMaxSwingDistance", 0.06f,
            "Maximum cloak/cowl cloth displacement from its folded animated shape, in meters (0.02-0.15). Limits faceward swing when backing up; sleeves are unchanged.");
        _hoodInertia = config.Bind("ClothComfort", "HoodMovementInertia", 0.05f,
            "Cloak/cowl response to body movement (0-1). Lower values keep the hood following its folded shape when moving backward. Does not change sleeve inertia.");
    }

    internal void Update()
    {
        var pawn = YAPYAP.Pawn.LocalInstance;
        if (!YapyapVrPlugin.ControlsActive || pawn == null || !pawn.isLocalPlayer || pawn.IsAI ||
            !pawn.HasAuthority || pawn.IsDead || pawn.View == null)
        {
            Suspend();
            return;
        }
        if (_pawn != pawn || _lastStable != _stableClothing.Value || _lastShadows != _clothShadows.Value || _lastComfort != _clothComfort.Value || _lastClearance != _headClearance.Value || _lastHoodFade != _hoodEyeFade.Value)
        {
            Suspend();
            _pawn = pawn;
            _lastStable = _stableClothing.Value;
            _lastShadows = _clothShadows.Value;
            _lastComfort = _clothComfort.Value;
            _lastClearance = _headClearance.Value;
            _lastHoodFade = _hoodEyeFade.Value;
            _nextScan = 0f;
            _nextReport = Time.realtimeSinceStartup + 5f;
        }
        if (Time.realtimeSinceStartup >= _nextScan)
        {
            _nextScan = Time.realtimeSinceStartup + 2f;
            if (_lastHoodFade) ApplyHoodEyeFade(pawn);
            // Cosmetics may spawn after the pawn or change with a wardrobe selection.
            _cloth.Clear();
            pawn.View.GetComponentsInChildren(true, _cloth);
            _skinned.Clear();
            if (_lastShadows) pawn.View.GetComponentsInChildren(true, _skinned);
            foreach (var cloth in _cloth)
            {
                if (cloth == null) continue;
                if (_lastStable) StopSimulation(cloth);
                else if (_lastComfort && cloth.enabled && cloth.IsValid() &&
                         (cloth.name == "Magica Cloth Robe" || cloth.name == "Magica Cloth Cloak"))
                {
                    var data = cloth.SerializeData;
                    if (data != null && data.damping != null && data.inertiaConstraint != null && data.wind != null)
                    {
                        if (!_clothSettings.TryGetValue(cloth, out var settings))
                        {
                            settings = new ClothSettings(cloth, cloth.name == "Magica Cloth Cloak");
                            _clothSettings.Add(cloth, settings);
                            _log.LogInfo($"VR cloth comfort: {cloth.name}; original gravity={data.gravity:F2}, damping={data.damping.value:F2}, wind={data.wind.influence:F2}; simulation stays enabled.");
                        }
                        settings.Apply(Bounded(_clothGravity.Value, 9.81f, 20f),
                            Bounded(settings.Cloak ? _hoodDamping.Value : _sleeveDamping.Value, settings.Cloak ? 0.45f : 0.35f, 1f),
                            Bounded(settings.Cloak ? _hoodInertia.Value : _clothInertia.Value, settings.Cloak ? 0.05f : 0.35f, 1f), Bounded(_clothWind.Value, 0.2f, 2f));
                        if (settings.Cloak && settings.LimitHoodSwing(Mathf.Max(0.02f, Bounded(_hoodTravel.Value, 0.06f, 0.15f))))
                            _log.LogInfo($"VR cowl swing limited: {_hoodTravel.Value:F3}m from animated shape; movement inertia={_hoodInertia.Value:F2}; sleeve settings retained.");
                        if (_lastClearance)
                        {
                            EnsureHeadCollider(pawn);
                            if (_headCollider != null) settings.AttachHeadCollider(_headCollider);
                            if (settings.Cloak && data.sourceRenderers != null)
                                foreach (var source in data.sourceRenderers)
                                    if (source is SkinnedMeshRenderer skin && skin.transform.IsChildOf(pawn.View.transform))
                                        foreach (var bone in skin.bones)
                                            if (bone != null && bone.name.StartsWith("CL-Robe-Collar.", StringComparison.Ordinal) &&
                                                bone.IsChildOf(pawn.View.transform) && !_collarPositions.ContainsKey(bone))
                                            {
                                                _collarPositions.Add(bone, bone.localPosition);
                                                _log.LogInfo($"VR cowl clearance anchor: {bone.name}; folding cosmetic collar down/back.");
                                            }
                        }
                    }
                }
                if (!_lastShadows || cloth.SerializeData == null) continue;
                if (cloth.SerializeData.sourceRenderers != null)
                    foreach (var renderer in cloth.SerializeData.sourceRenderers) ReduceShadow(renderer, pawn.View.transform);
                // Bone cloth uses root bones instead of an explicit source mesh list.
                var roots = cloth.SerializeData.rootBones;
                if (roots == null || roots.Count == 0) continue;
                foreach (var renderer in _skinned)
                {
                    if (renderer == null || _shadows.ContainsKey(renderer)) continue;
                    var matches = false;
                    foreach (var bone in renderer.bones)
                    {
                        if (bone == null) continue;
                        foreach (var root in roots)
                            if (root != null && (bone == root || bone.IsChildOf(root))) { matches = true; break; }
                        if (matches) break;
                    }
                    if (matches) ReduceShadow(renderer, pawn.View.transform);
                }
            }
            if (_reportedCloth != _simulations.Count || _reportedShadows != _shadows.Count)
            {
                _reportedCloth = _simulations.Count;
                _reportedShadows = _shadows.Count;
                _log.LogInfo($"Local VR clothing: {_reportedCloth} cosmetic simulations stopped, {_reportedShadows} cloth shadow casters reduced; tracked wrists and meshes retained.");
            }
        }
        if (_lastComfort && Time.realtimeSinceStartup >= _nextClothDirection)
        {
            _nextClothDirection = Time.realtimeSinceStartup + 0.1f;
            var forward = Vector3.ProjectOnPlane(pawn.transform.forward, Vector3.up).normalized;
            var pull = Bounded(_hoodBackwardPull.Value, 0.45f, 1f);
            foreach (var settings in _clothSettings.Values)
                settings.SetDirection((Vector3.down - (settings.Cloak ? forward * pull : Vector3.zero)).normalized);
        }
        ReportFrameTimes();
    }

    private void ApplyHoodEyeFade(YAPYAP.Pawn pawn)
    {
        // PawnMaterial creates separate runtime instances per renderer. Read those
        // exact instances so costume updates still work and shared assets stay untouched.
        _pawnMaterials.Clear();
        pawn.GetComponentsInChildren(true, _pawnMaterials);
        foreach (var owner in _pawnMaterials)
        {
            if (owner == null || !(RendererSetups?.GetValue(owner) is YAPYAP.PawnMaterial.RendererMaterialSetup[] setups)) continue;
            foreach (var setup in setups)
            {
                var renderer = setup?.targetRenderer;
                if (renderer == null || !renderer.transform.IsChildOf(pawn.View.transform) ||
                    (renderer.name != "Player_Character_01_cloak" && renderer.name != "Player_Character_01_Hat") ||
                    setup!.runtimeMaterials == null) continue;
                for (var i = 0; i < setup.runtimeMaterials.Length; i++)
                {
                    var material = setup.runtimeMaterials[i];
                    if (material == null || setup.originalMaterials == null || i >= setup.originalMaterials.Length ||
                        material == setup.originalMaterials[i] || !material.HasProperty("_CameraDepthFadeAlpha") ||
                        !material.HasProperty("_CameraDepthFadeMask") || !material.HasProperty("_CameraDepthFadeLengthOffset")) continue;
                    if (!_hoodMaterials.TryGetValue(material, out var settings))
                    {
                        settings = new HoodFadeSettings(material);
                        _hoodMaterials.Add(material, settings);
                        _log.LogInfo($"VR hood eye fade: {renderer.name}; runtime material={material.name}; previous keyword={settings.Enabled}; native length/offset={material.GetVector("_CameraDepthFadeLengthOffset")}; replacing selective opacity mask only on this accessory.");
                    }
                    settings.Apply();
                }
            }
        }
    }

    private sealed class HoodFadeSettings
    {
        private const string Keyword = "_CAMERADEPTHFADEALPHA_ON";
        private readonly Material _material;
        private readonly Texture? _mask;
        private readonly float _alpha;
        internal bool Enabled { get; }

        internal HoodFadeSettings(Material material)
        {
            _material = material;
            Enabled = material.IsKeywordEnabled(Keyword);
            _alpha = material.GetFloat("_CameraDepthFadeAlpha");
            _mask = material.GetTexture("_CameraDepthFadeMask");
        }

        internal void Apply()
        {
            if (_material == null) return;
            _material.EnableKeyword(Keyword);
            _material.SetFloat("_CameraDepthFadeAlpha", 1f);
            _material.SetTexture("_CameraDepthFadeMask", Texture2D.whiteTexture);
        }

        internal void Restore()
        {
            if (_material == null) return;
            if (Enabled) _material.EnableKeyword(Keyword); else _material.DisableKeyword(Keyword);
            _material.SetFloat("_CameraDepthFadeAlpha", _alpha);
            _material.SetTexture("_CameraDepthFadeMask", _mask);
        }
    }

    private void EnsureHeadCollider(YAPYAP.Pawn pawn)
    {
        if (_headCollider == null)
        {
            var owner = new GameObject("AVRcade-Local-Cloth-Head-Clearance");
            owner.SetActive(false);
            owner.hideFlags = HideFlags.DontSave;
            owner.transform.SetParent(pawn.View.transform, false);
            _headCollider = owner.AddComponent<MagicaSphereCollider>();
            _headCollider.center = Vector3.zero;
            UpdateHeadCollider();
            owner.SetActive(true);
            _log.LogInfo("VR cloth head clearance created: MagicaCloth-only collision; no Unity gameplay collider.");
        }
        UpdateHeadCollider();
    }

    private void UpdateHeadCollider()
    {
        var camera = YAPYAP.CameraManager.Instance?.MainCamera;
        if (_headCollider == null || camera == null) return;
        // Keep the same world eye anchor as rendered hands when stereo matrices are valid.
        var center = camera.transform.position;
        if (camera.stereoEnabled)
        {
            var l = camera.GetStereoViewMatrix(Camera.StereoscopicEye.Left).inverse.GetColumn(3);
            var r = camera.GetStereoViewMatrix(Camera.StereoscopicEye.Right).inverse.GetColumn(3);
            if (HandTrackingMath.TryStereoEyeCenter(new System.Numerics.Vector3(l.x, l.y, l.z),
                    new System.Numerics.Vector3(r.x, r.y, r.z), out var eye))
            {
                var candidate = new Vector3(eye.X, eye.Y, eye.Z);
                if ((candidate - center).sqrMagnitude <= 0.25f) center = candidate;
            }
        }
        _headCollider.transform.position = center + camera.transform.forward * 0.04f;
        var radius = Mathf.Max(0.12f, Bounded(_headRadius.Value, 0.2f, 0.3f));
        var scale = _headCollider.transform.lossyScale;
        var worldScale = Mathf.Max(0.001f, Mathf.Max(Mathf.Abs(scale.x), Mathf.Abs(scale.y), Mathf.Abs(scale.z)));
        var size = radius / worldScale;
        if (Mathf.Abs(size - _headColliderSize) > 0.0001f)
        {
            _headColliderSize = size;
            _headCollider.SetSize(size);
            _headCollider.UpdateParameters();
        }
    }

    internal void LateUpdateClearance()
    {
        if (_pawn == null || !YapyapVrPlugin.ControlsActive || !_lastComfort || !_lastClearance || _lastStable) return;
        UpdateHeadCollider();
        var forward = Vector3.ProjectOnPlane(_pawn.transform.forward, Vector3.up).normalized;
        var offset = -forward * Bounded(_collarBack.Value, 0.1f, 0.2f) +
            Vector3.down * Bounded(_collarDrop.Value, 0.06f, 0.15f);
        foreach (var entry in _collarPositions)
            if (entry.Key != null && entry.Key.parent != null)
                entry.Key.localPosition = entry.Value + entry.Key.parent.InverseTransformVector(offset);
    }

    private static float Bounded(float value, float fallback, float maximum) =>
        float.IsNaN(value) || float.IsInfinity(value) ? fallback : Mathf.Clamp(value, 0f, maximum);

    private sealed class ClothSettings
    {
        private readonly MagicaCloth _cloth;
        private readonly ClothSerializeData _data;
        private readonly float _gravity, _damping, _worldInertia, _localInertia, _wind;
        private readonly float _gravityProperty, _dampingProperty, _worldProperty, _localProperty, _windProperty;
        private readonly bool _useDampingCurve;
        private readonly Unity.Mathematics.float3 _direction;
        private readonly bool _motionCaptured, _usedMotionLimit, _motionCurve;
        private readonly float _motionDistance, _motionStiffness;
        private bool _applied;
        private MagicaSphereCollider? _headCollider;
        internal bool Cloak { get; }

        internal ClothSettings(MagicaCloth cloth, bool cloak)
        {
            _cloth = cloth;
            Cloak = cloak;
            _data = cloth.SerializeData;
            _gravity = _data.gravity;
            _direction = _data.gravityDirection;
            _damping = _data.damping.value;
            _useDampingCurve = _data.damping.useCurve;
            _worldInertia = _data.inertiaConstraint.worldInertia;
            _localInertia = _data.inertiaConstraint.localInertia;
            _wind = _data.wind.influence;
            _gravityProperty = cloth.gravityProperty;
            _dampingProperty = cloth.dampingProperty;
            _worldProperty = cloth.worldInertiaProperty;
            _localProperty = cloth.localInertiaProperty;
            _windProperty = cloth.windInfluenceProperty;
            if (cloak && _data.motionConstraint?.maxDistance != null)
            {
                _motionCaptured = true;
                _usedMotionLimit = _data.motionConstraint.useMaxDistance;
                _motionDistance = _data.motionConstraint.maxDistance.value;
                _motionCurve = _data.motionConstraint.maxDistance.useCurve;
                _motionStiffness = _data.motionConstraint.stiffness;
            }
        }

        internal bool LimitHoodSwing(float distance)
        {
            if (!Cloak || !_motionCaptured || _cloth == null || !_applied || _cloth.SerializeData != _data || _data.motionConstraint?.maxDistance == null) return false;
            var motion = _data.motionConstraint;
            if (motion.useMaxDistance && motion.maxDistance.value == distance && !motion.maxDistance.useCurve && motion.stiffness == 1f) return false;
            motion.useMaxDistance = true;
            motion.maxDistance.value = distance;
            motion.maxDistance.useCurve = false;
            motion.stiffness = 1f;
            _cloth.SetParameterChange();
            return true;
        }

        internal void Apply(float gravity, float damping, float inertia, float wind)
        {
            if (_cloth == null || _cloth.SerializeData != _data) return;
            if (_applied && _data.gravity == gravity && _data.damping.value == damping && !_data.damping.useCurve &&
                _data.inertiaConstraint.worldInertia == inertia && _data.inertiaConstraint.localInertia == inertia &&
                _data.wind.influence == wind) return;
            _applied = true;
            _data.gravity = _cloth.gravityProperty = gravity;
            _data.damping.value = _cloth.dampingProperty = damping;
            _data.damping.useCurve = false;
            _data.inertiaConstraint.worldInertia = _cloth.worldInertiaProperty = inertia;
            _data.inertiaConstraint.localInertia = _cloth.localInertiaProperty = inertia;
            _data.wind.influence = _cloth.windInfluenceProperty = wind;
            _cloth.SetParameterChange();
        }

        internal void SetDirection(Vector3 direction)
        {
            if (_cloth == null || !_applied || _cloth.SerializeData != _data) return;
            var old = _data.gravityDirection;
            if ((new Vector3(old.x, old.y, old.z) - direction).sqrMagnitude < 0.0025f) return;
            _data.gravityDirection = new Unity.Mathematics.float3(direction.x, direction.y, direction.z);
            _cloth.SetParameterChange();
        }

        internal void AttachHeadCollider(MagicaSphereCollider collider)
        {
            if (_cloth == null || _cloth.SerializeData != _data || _data.colliderCollisionConstraint?.colliderList == null) return;
            var list = _data.colliderCollisionConstraint.colliderList;
            if (list.Contains(collider)) return;
            list.Add(collider);
            _headCollider = collider;
            // TeamManager processes the dirty parameter list and updates colliders.
            _cloth.SetParameterChange();
        }

        internal void Restore()
        {
            if (_cloth == null || !_applied || _cloth.SerializeData != _data) return;
            _data.gravity = _gravity;
            _data.gravityDirection = _direction;
            _data.damping.value = _damping;
            _data.damping.useCurve = _useDampingCurve;
            _data.inertiaConstraint.worldInertia = _worldInertia;
            _data.inertiaConstraint.localInertia = _localInertia;
            _data.wind.influence = _wind;
            if (_motionCaptured && _data.motionConstraint?.maxDistance != null)
            {
                _data.motionConstraint.useMaxDistance = _usedMotionLimit;
                _data.motionConstraint.maxDistance.value = _motionDistance;
                _data.motionConstraint.maxDistance.useCurve = _motionCurve;
                _data.motionConstraint.stiffness = _motionStiffness;
            }
            if (_headCollider != null) _data.colliderCollisionConstraint.colliderList.Remove(_headCollider);
            _cloth.gravityProperty = _gravityProperty;
            _cloth.dampingProperty = _dampingProperty;
            _cloth.worldInertiaProperty = _worldProperty;
            _cloth.localInertiaProperty = _localProperty;
            _cloth.windInfluenceProperty = _windProperty;
            _cloth.SetParameterChange();
        }
    }

    private void ReduceShadow(Renderer renderer, Transform localView)
    {
        // A cloth configuration can reference objects outside its subtree.
        if (renderer == null || !renderer.transform.IsChildOf(localView)) return;
        if (!_shadows.ContainsKey(renderer))
        {
            _shadows.Add(renderer, renderer.shadowCastingMode);
            _log.LogInfo($"Local cloth shadow reduction: {renderer.name}; mesh stays visible.");
        }
        renderer.shadowCastingMode = ShadowCastingMode.Off;
    }

    private void StopSimulation(Behaviour simulation)
    {
        if (!simulation.enabled) return;
        if (!_simulations.ContainsKey(simulation))
        {
            _simulations.Add(simulation, simulation.enabled);
            _log.LogInfo($"Stable local clothing: {simulation.name} ({simulation.GetType().FullName}).");
        }
        // MagicaCloth.OnDisable calls EndUse, unregistering its simulation/render work.
        simulation.enabled = false;
    }

    private void ReportFrameTimes()
    {
        if (!_diagnostics.Value || _reports >= 12) return;
        var seconds = Time.unscaledDeltaTime;
        if (seconds > 0f && !float.IsNaN(seconds) && !float.IsInfinity(seconds))
        {
            _frames++;
            _frameSeconds += seconds;
            _worstFrame = Mathf.Max(_worstFrame, seconds);
        }
        if (Time.realtimeSinceStartup < _nextReport || _frames == 0) return;
        _nextReport = Time.realtimeSinceStartup + 5f;
        _reports++;
        var timing = "Unity CPU/GPU timings unavailable";
        if (FrameTimingManager.IsFeatureEnabled())
        {
            FrameTimingManager.CaptureFrameTimings();
            if (FrameTimingManager.GetLatestTimings(1, _timings) != 0)
                timing = $"Unity CPU={_timings[0].cpuFrameTime:F2}ms GPU={_timings[0].gpuFrameTime:F2}ms";
        }
        _log.LogInfo($"VR frame sample {_reports}/12: game average={_frameSeconds * 1000 / _frames:F2}ms, worst={_worstFrame * 1000:F2}ms; {timing}; local cloth={_simulations.Count}, reduced shadows={_shadows.Count}.");
        _frames = 0;
        _frameSeconds = 0;
        _worstFrame = 0f;
    }

    internal void Suspend()
    {
        foreach (var settings in _hoodMaterials.Values) settings.Restore();
        _hoodMaterials.Clear();
        _pawnMaterials.Clear();
        foreach (var settings in _clothSettings.Values) settings.Restore();
        _clothSettings.Clear();
        foreach (var entry in _collarPositions) if (entry.Key != null) entry.Key.localPosition = entry.Value;
        _collarPositions.Clear();
        if (_headCollider != null)
        {
            _headCollider.enabled = false;
            UnityEngine.Object.Destroy(_headCollider.gameObject);
            _headCollider = null;
            _headColliderSize = -1f;
        }
        foreach (var entry in _simulations) if (entry.Key != null) entry.Key.enabled = entry.Value;
        foreach (var entry in _shadows) if (entry.Key != null) entry.Key.shadowCastingMode = entry.Value;
        _simulations.Clear();
        _shadows.Clear();
        _cloth.Clear();
        _skinned.Clear();
        _pawn = null;
        _frames = 0;
        _frameSeconds = 0;
        _worstFrame = 0f;
        _reportedCloth = _reportedShadows = -1;
    }

    public void Dispose() => Suspend();
}
