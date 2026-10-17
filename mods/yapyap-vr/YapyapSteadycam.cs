using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using BepInEx;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
using VrClient.Streaming;

namespace Avrcade.YapyapVr;

/// Optional mono desktop camera. No writes to the tracked source's transform,
/// projection, culling mask or aiming state. UUVR excludes mono cameras.
[BepInPlugin("avrcade.yapyap-steadycam", "AVRcade YAPYAP Spectator", "0.1.0")]
[BepInDependency(YapyapVrPlugin.PluginGuid, BepInDependency.DependencyFlags.HardDependency)]
public sealed class YapyapSteadycam : BaseUnityPlugin
{
    private static YapyapSteadycam? _instance;
    private Camera? _spectator;
    private Camera? _source;
    private bool _requested;
    private bool _initialized;
    private bool _available;
    private float _lastPoseTime;
    private float _nextHeartbeat;
    private string _requestId = "";
    private readonly string _sessionId = Guid.NewGuid().ToString("N");
    private string _processName = "";
    private string? _error;
    private int _processId;
    private string _directory = "";
    private Harmony? _harmony;

    private void Awake()
    {
        _instance = this;
        using (var process = Process.GetCurrentProcess()) { _processId = process.Id; _processName = process.ProcessName; }
        _directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VRClient", "streaming");
        _harmony = new Harmony("avrcade.yapyap-steadycam");
        _harmony.Patch(AccessTools.Method(typeof(Uuvr.UuvrPoseDriver), "UpdateTransform"),
            postfix: new HarmonyMethod(typeof(YapyapSteadycam), nameof(AfterHeadPose)) { priority = Priority.Last });
        // A fresh session GUID isolates this launch even if the OS reuses its PID.
        Logger.LogInfo("Spectator steadycam ready; starts off and waits for AVRcade's toggle.");
    }

    private static void AfterHeadPose(Uuvr.UuvrPoseDriver __instance)
    {
        if (_instance == null || !YapyapVrPlugin.ControlsActive) return;
        var source = YAPYAP.CameraManager.Instance?.MainCamera;
        if (source == null || __instance.GetComponent<Camera>() != source) return;
        _instance.UpdateSpectator(source);
    }

    private void UpdateSpectator(Camera source)
    {
        var now = Time.realtimeSinceStartup;
        var gap = now - _lastPoseTime;
        _lastPoseTime = now;
        _available = source.isActiveAndEnabled;
        if (!_requested || !_available) { RemoveCamera(); return; }
        try
        {
            var changed = _source != source;
            if (changed) RemoveCamera();
            if (_spectator == null)
            {
                var cameraObject = new GameObject("AVRcade-Spectator-Steadycam");
                cameraObject.SetActive(false);
                _spectator = cameraObject.AddComponent<Camera>();
                _spectator.CopyFrom(source);
                _spectator.stereoTargetEye = StereoTargetEyeMask.None;
                _spectator.targetTexture = null;
                _spectator.targetDisplay = 0;
                _spectator.rect = new Rect(0f, 0f, 1f, 1f);
                _spectator.fieldOfView = 68f;
                _spectator.depth = source.depth + 100f;
                var data = _spectator.GetUniversalAdditionalCameraData();
                data.allowXRRendering = false;
                data.renderType = CameraRenderType.Base;
                var sourceData = source.GetUniversalAdditionalCameraData();
                data.renderPostProcessing = sourceData.renderPostProcessing;
                data.volumeLayerMask = sourceData.volumeLayerMask;
                // Use the viewer camera for volume blending, not the HMD transform.
                data.volumeTrigger = _spectator.transform;
                _spectator.ResetProjectionMatrix();
                _spectator.ResetWorldToCameraMatrix();
                cameraObject.SetActive(true);
                _initialized = false;
            }
            var reset = SteadycamMath.ShouldReset(_initialized, changed,
                Vector3.Distance(_spectator.transform.position, source.transform.position), gap);
            var blend = SteadycamMath.Blend(gap);
            _spectator.transform.position = reset ? source.transform.position
                : Vector3.Lerp(_spectator.transform.position, source.transform.position, blend);
            _spectator.transform.rotation = reset ? source.transform.rotation
                : Quaternion.Slerp(_spectator.transform.rotation, source.transform.rotation, blend);
            _source = source;
            _initialized = true;
            _error = null;
        }
        catch (Exception ex)
        {
            RemoveCamera();
            _requested = false;
            _error = "Steadycam camera failed. Turn it on again to retry.";
            Logger.LogWarning("Spectator camera failed: " + ex.GetType().Name);
        }
    }

    private void Update()
    {
        if (Time.realtimeSinceStartup < _nextHeartbeat) return;
        _nextHeartbeat = Time.realtimeSinceStartup + 0.5f;
        if (!YapyapVrPlugin.ControlsActive || Time.realtimeSinceStartup - _lastPoseTime > 1f)
        { _available = false; RemoveCamera(); }
        try
        {
            var requestPath = Path.Combine(_directory, SpectatorProtocol.RequestFileName(_processId, _sessionId));
            if (File.Exists(requestPath))
            {
                var request = JsonUtility.FromJson<SpectatorRequest>(File.ReadAllText(requestPath));
                if (request != null && SpectatorProtocol.IsForSession(request, CreateStatus()) && request.RequestId != _requestId)
                {
                    _requested = request.Enabled;
                    _requestId = request.RequestId;
                    _error = null;
                    if (!_requested) RemoveCamera();
                }
            }
            Directory.CreateDirectory(_directory);
            var status = CreateStatus();
            var path = Path.Combine(_directory, SpectatorProtocol.StatusFileName(_processId, _sessionId));
            var temporary = path + "." + _processId + ".tmp";
            File.WriteAllText(temporary, JsonUtility.ToJson(status), Encoding.UTF8);
            if (File.Exists(path)) File.Replace(temporary, path, null);
            else File.Move(temporary, path);
        }
        catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is ArgumentException)
        { /* A transient file race must not interrupt gameplay. The next heartbeat retries. */ }
    }

    private SpectatorStatus CreateStatus() => new SpectatorStatus
    {
        ProtocolVersion = SpectatorProtocol.Version, GameSlug = "yapyap", AdapterId = "avrcade.yapyap-steadycam",
        ProcessId = _processId, ProcessName = _processName, SessionId = _sessionId, UpdatedUtcTicks = DateTime.UtcNow.Ticks,
        SupportsSteadycam = true, CameraAvailable = _available, Enabled = _spectator != null && _spectator.isActiveAndEnabled,
        // A positive request is acknowledged only after a camera exists.
        RequestId = !_requested || _spectator != null ? _requestId : "",
        CaptureHint = "Capture the YAPYAP game window in OBS.", Error = _error
    };

    private void RemoveCamera()
    {
        if (_spectator != null)
        {
            _spectator.enabled = false;
            Destroy(_spectator.gameObject);
        }
        _spectator = null;
        _source = null;
        _initialized = false;
    }

    private void OnDestroy()
    {
        RemoveCamera();
        _harmony?.UnpatchSelf();
        if (_instance == this) _instance = null;
        try
        {
            File.Delete(Path.Combine(_directory, SpectatorProtocol.StatusFileName(_processId, _sessionId)));
            File.Delete(Path.Combine(_directory, SpectatorProtocol.RequestFileName(_processId, _sessionId)));
        }
        catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is ArgumentException) { }
    }
}
