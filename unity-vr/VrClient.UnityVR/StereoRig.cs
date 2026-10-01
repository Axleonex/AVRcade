using BepInEx.Logging;
using UnityEngine;
using UnityEngine.XR;
using VrClient.UnityVR.Logic;

namespace VrClient.UnityVR;

internal sealed class StereoRig
{
    private readonly ManualLogSource _log;
    private readonly VrGameConfig _config;
    private Transform? _rig;
    private Vector3 _baseLocalPosition;
    private Quaternion _baseLocalRotation;

    internal StereoRig(ManualLogSource log, VrGameConfig config)
    {
        _log = log;
        _config = config;
    }

    internal bool Initialize()
    {
        var source = string.IsNullOrWhiteSpace(_config.CameraObjectName)
            ? Camera.main
            : GameObject.Find(_config.CameraObjectName)?.GetComponent<Camera>();
        if (source is null)
        {
            _log.LogError($"UXR-RIG-FAIL: camera '{_config.CameraObjectName}' not found");
            return false;
        }

        var driven = source;
        if (_config.DuplicateCamera)
        {
            var clone = Object.Instantiate(source.gameObject);
            clone.name = "VRClient-VRCamera";
            driven = clone.GetComponent<Camera>();
            source.enabled = false;
        }

        var rigObject = new GameObject("VRClient-TrackedRig");
        Object.DontDestroyOnLoad(rigObject);
        _rig = rigObject.transform;
        _rig.SetParent(driven.transform.parent, worldPositionStays: false);
        _rig.localPosition = driven.transform.localPosition;
        _rig.localRotation = driven.transform.localRotation;
        _baseLocalPosition = _rig.localPosition;
        _baseLocalRotation = _rig.localRotation;
        driven.transform.SetParent(_rig, worldPositionStays: false);
        driven.transform.localPosition = Vector3.zero;
        driven.transform.localRotation = Quaternion.identity;
        driven.stereoTargetEye = StereoTargetEyeMask.Both;
        _log.LogInfo($"UXR-RIG: camera='{source.name}' mode={(_config.DuplicateCamera ? "duplicate" : "reparent")} stereo=both");
        return true;
    }

    internal void UpdatePose()
    {
        if (_rig is null) return;
        var head = InputDevices.GetDeviceAtXRNode(XRNode.Head);
        var headPosition = head.TryGetFeatureValue(CommonUsages.devicePosition, out var position)
            ? position * _config.WorldScale
            : Vector3.zero;
        var headRotation = head.TryGetFeatureValue(CommonUsages.deviceRotation, out var rotation)
            ? rotation
            : Quaternion.identity;
        _rig.localPosition = _baseLocalPosition + _baseLocalRotation * headPosition;
        _rig.localRotation = _baseLocalRotation * headRotation;
    }
}
