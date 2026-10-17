using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;
using BepInEx.Logging;
using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.LowLevel;
using Valve.VR;

namespace Avrcade.YapyapVr;

internal sealed class SteamVrActionReader
{
    private static readonly string[] ActionNames =
    {
        "A", "B", "X", "Y", "Start", "Select", "LB", "RB",
        "StickLeft", "StickRight", "StickLeftClick", "StickRightClick", "LT", "RT", "LeftGripPose", "RightGripPose"
    };

    private readonly Dictionary<string, ulong> _handles = new(StringComparer.Ordinal);
    private readonly VRActiveActionSet_t[] _activeSets = new VRActiveActionSet_t[1];
    private CVRInput? _input;
    private float _nextInitializeAt;
    private bool _initialized;
    private readonly LeftHandShortcuts _leftShortcuts = new();
    private string _analogAssignments = "";
    private YapyapAnalogState _analog;
    private readonly StringBuilder _controllerType = new(128);
    private float _nextControllerTypeAt;
    private bool _swapLeftAnalog, _swapRightAnalog;

    public bool PausePressed { get; private set; }
    public bool InventoryPressed { get; private set; }

    public bool TryRead(ManualLogSource log, bool menu, out GamepadState state)
    {
        state = default;
        PausePressed = false;
        InventoryPressed = false;
        if (!TryInitialize(log)) return false;
        var update = _input!.UpdateActionState(
            _activeSets, (uint)Marshal.SizeOf(typeof(VRActiveActionSet_t)));
        if (update != EVRInputError.None) return false;

        RefreshAnalogAssignments();
        _analog = new YapyapAnalogState(_swapLeftAnalog, _swapRightAnalog,
            ReadRawAxis("LT").x, ReadRawAxis("RT").x, ReadRawButton("LB"), ReadRawButton("RB"));

        // Do not keep a virtual pad (or inventory mode) alive after a hand disconnects.
        if (!TryReadAxis("StickLeft", out var left) || !TryReadAxis("StickRight", out var right)) return false;
        _leftShortcuts.Update(ReadAxis("LT").x, ReadButton("StickLeftClick"));
        state = new GamepadState
        {
            leftStick = left,
            rightStick = right,
            leftTrigger = _leftShortcuts.RagdollTrigger,
            rightTrigger = Mathf.Clamp01(ReadAxis("RT").x)
        };
        var faces = YapyapControlMap.MapFaceButtons(ReadButton("X"), ReadButton("Y"),
            ReadButton("A"), ReadButton("B"), menu);
        state = state.WithButton(GamepadButton.South, (faces & VrButtons.South) != 0);
        state = state.WithButton(GamepadButton.East, (faces & VrButtons.East) != 0);
        state = state.WithButton(GamepadButton.West, (faces & VrButtons.West) != 0 || ReadButton("RB"));
        state = state.WithButton(GamepadButton.North, (faces & VrButtons.North) != 0);
        state = state.WithButton(GamepadButton.RightShoulder, ReadButton("LB"));
        InventoryPressed = _leftShortcuts.InventoryPressed;
        PausePressed = ReadButton("Start") || ReadButton("StickRightClick");
        state = state.WithButton(GamepadButton.Select, ReadButton("Select"));
        return true;
    }

    private void RefreshAnalogAssignments()
    {
        // Identity profiles need no controller-type query. For customized ones,
        // allow a device change without allocating/querying native strings each frame.
        if (_analogAssignments.Length == 0 || Time.realtimeSinceStartup < _nextControllerTypeAt) return;
        _nextControllerTypeAt = Time.realtimeSinceStartup + 1f;
        var device = "quest-touch";
        var system = OpenVR.System;
        if (system != null)
        {
            var index = system.GetTrackedDeviceIndexForControllerRole(ETrackedControllerRole.LeftHand);
            if (index >= OpenVR.k_unMaxTrackedDeviceCount) return;
            var error = ETrackedPropertyError.TrackedProp_Success;
            _controllerType.Length = 0;
            system.GetStringTrackedDeviceProperty(index, ETrackedDeviceProperty.Prop_ControllerType_String,
                _controllerType, 128, ref error);
            if (error != ETrackedPropertyError.TrackedProp_Success) return;
            if (_controllerType.ToString().IndexOf("knuckles", StringComparison.OrdinalIgnoreCase) >= 0) device = "valve-index";
        }
        _swapLeftAnalog = _analogAssignments.IndexOf(device + "|/user/hand/left/input/trigger/value|/user/hand/left/input/squeeze/", StringComparison.Ordinal) >= 0;
        _swapRightAnalog = _analogAssignments.IndexOf(device + "|/user/hand/right/input/trigger/value|/user/hand/right/input/squeeze/", StringComparison.Ordinal) >= 0;
    }

    private bool TryInitialize(ManualLogSource log)
    {
        if (_initialized) return true;
        if (Time.realtimeSinceStartup < _nextInitializeAt) return false;
        _nextInitializeAt = Time.realtimeSinceStartup + 5f;
        _input = OpenVR.Input;
        if (_input == null) return false;

        var manifest = Path.Combine(Application.dataPath, "StreamingAssets", "SteamVR", "actions.json");
        var custom = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "VRClient", "controllers", "yapyap", "steamvr", "actions.json");
        if (File.Exists(custom)) manifest = custom;
        var assignments = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "VRClient", "controllers", "runtime", "yapyap.bindings");
        if (File.Exists(assignments)) _analogAssignments = File.ReadAllText(assignments);
        if (!File.Exists(manifest)) return false;
        var result = _input.SetActionManifestPath(manifest);
        if (result != EVRInputError.None)
        {
            log.LogWarning($"SteamVR action manifest unavailable: {result}");
            return false;
        }

        ulong actionSet = 0;
        result = _input.GetActionSetHandle("/actions/Xbox", ref actionSet);
        if (result != EVRInputError.None || actionSet == 0) return false;
        foreach (var name in ActionNames)
        {
            ulong handle = 0;
            result = _input.GetActionHandle($"/actions/Xbox/in/{name}", ref handle);
            if (result != EVRInputError.None || handle == 0) return false;
            _handles[name] = handle;
        }

        _activeSets[0].ulActionSet = actionSet;
        _initialized = true;
        log.LogInfo("SteamVR Quest/Index controller actions and handgrip poses connected to YAPYAP.");
        return true;
    }

    internal bool TryReadGripPose(bool left, ETrackingUniverseOrigin origin, out TrackedDevicePose_t pose, bool rendered = false)
    {
        pose = default;
        if (!_initialized || _input == null) return false;
        var data = new InputPoseActionData_t();
        var handle = _handles[left ? "LeftGripPose" : "RightGripPose"];
        var size = (uint)Marshal.SizeOf(typeof(InputPoseActionData_t));
        var result = rendered
            ? _input.GetPoseActionDataForNextFrame(handle, origin, ref data, size, 0)
            : _input.GetPoseActionDataRelativeToNow(handle, origin, 0f, ref data, size, 0);
        if (result != EVRInputError.None || !data.bActive) return false;
        pose = data.pose;
        return pose.bDeviceIsConnected && pose.bPoseIsValid && pose.eTrackingResult == ETrackingResult.Running_OK;
    }

    private bool ReadButton(string name) => name == "LB" ? _analog.LB : name == "RB" ? _analog.RB : ReadRawButton(name);

    private bool ReadRawButton(string name)
    {
        var data = new InputDigitalActionData_t();
        var result = _input!.GetDigitalActionData(
            _handles[name], ref data, (uint)Marshal.SizeOf(typeof(InputDigitalActionData_t)), 0);
        return result == EVRInputError.None && data.bActive && data.bState;
    }

    private Vector2 ReadAxis(string name) => name == "LT" ? new Vector2(_analog.LT, 0f) :
        name == "RT" ? new Vector2(_analog.RT, 0f) : ReadRawAxis(name);

    private Vector2 ReadRawAxis(string name)
    {
        TryReadAxis(name, out var value);
        return value;
    }

    private bool TryReadAxis(string name, out Vector2 value)
    {
        var data = new InputAnalogActionData_t();
        var result = _input!.GetAnalogActionData(
            _handles[name], ref data, (uint)Marshal.SizeOf(typeof(InputAnalogActionData_t)), 0);
        var active = result == EVRInputError.None && data.bActive;
        value = active ? new Vector2(data.x, data.y) : Vector2.zero;
        return active;
    }
}
