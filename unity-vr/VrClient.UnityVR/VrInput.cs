using BepInEx.Logging;
using UnityEngine;
using UnityEngine.XR;

namespace VrClient.UnityVR;

public sealed class VrInput
{
    private readonly ManualLogSource _log;
    private bool _devicesReported;
    private bool _activityReported;

    internal VrInput(ManualLogSource log) => _log = log;

    public Vector2 Move { get; private set; }
    public Vector2 Look { get; private set; }
    public float LeftTrigger { get; private set; }
    public float RightTrigger { get; private set; }

    internal void Poll()
    {
        var left = InputDevices.GetDeviceAtXRNode(XRNode.LeftHand);
        var right = InputDevices.GetDeviceAtXRNode(XRNode.RightHand);
        if (!_devicesReported && (left.isValid || right.isValid))
        {
            _devicesReported = true;
            _log.LogInfo($"UXR-INPUT: bound left={left.isValid} right={right.isValid} actions=move,look,trigger");
        }

        Move = ReadAxis(left);
        Look = ReadAxis(right);
        LeftTrigger = ReadTrigger(left);
        RightTrigger = ReadTrigger(right);
        var active = Move.sqrMagnitude > 0.01f || Look.sqrMagnitude > 0.01f ||
            LeftTrigger > 0.05f || RightTrigger > 0.05f;
        if (active && !_activityReported)
        {
            _activityReported = true;
            _log.LogInfo("UXR-INPUT: controller activity observed");
        }
        else if (!active)
        {
            _activityReported = false;
        }
    }

    private static Vector2 ReadAxis(InputDevice device)
        => device.TryGetFeatureValue(CommonUsages.primary2DAxis, out var value) ? value : Vector2.zero;

    private static float ReadTrigger(InputDevice device)
        => device.TryGetFeatureValue(CommonUsages.trigger, out var value) ? value : 0f;
}
