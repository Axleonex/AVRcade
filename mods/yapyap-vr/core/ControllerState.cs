using System;

namespace Avrcade.YapyapVr;

[Flags]
public enum VrButtons
{
    None = 0,
    South = 1 << 0,
    East = 1 << 1,
    West = 1 << 2,
    North = 1 << 3,
    LeftShoulder = 1 << 4,
    RightShoulder = 1 << 5,
    Start = 1 << 6,
    Select = 1 << 7
}

public readonly struct VrControllerState
{
    public VrControllerState(float leftX, float leftY, float rightX, float rightY,
        float leftTrigger, float rightTrigger, float leftGrip, float rightGrip,
        bool leftPrimary, bool leftSecondary, bool rightPrimary, bool rightSecondary,
        bool leftMenu, bool rightMenu)
    {
        LeftX = leftX; LeftY = leftY; RightX = rightX; RightY = rightY;
        LeftTrigger = leftTrigger; RightTrigger = rightTrigger;
        LeftGrip = leftGrip; RightGrip = rightGrip;
        LeftPrimary = leftPrimary; LeftSecondary = leftSecondary;
        RightPrimary = rightPrimary; RightSecondary = rightSecondary;
        LeftMenu = leftMenu; RightMenu = rightMenu;
    }

    public float LeftX { get; }
    public float LeftY { get; }
    public float RightX { get; }
    public float RightY { get; }
    public float LeftTrigger { get; }
    public float RightTrigger { get; }
    public float LeftGrip { get; }
    public float RightGrip { get; }
    public bool LeftPrimary { get; }
    public bool LeftSecondary { get; }
    public bool RightPrimary { get; }
    public bool RightSecondary { get; }
    public bool LeftMenu { get; }
    public bool RightMenu { get; }
}

public readonly struct YapyapGamepadState
{
    public YapyapGamepadState(float moveX, float moveY, float lookX, float lookY,
        float use, float fire, VrButtons buttons)
    {
        MoveX = moveX; MoveY = moveY; LookX = lookX; LookY = lookY;
        Use = use; Fire = fire; Buttons = buttons;
    }

    public float MoveX { get; }
    public float MoveY { get; }
    public float LookX { get; }
    public float LookY { get; }
    public float Use { get; }
    public float Fire { get; }
    public VrButtons Buttons { get; }
}

public static class YapyapControlMap
{
    public static YapyapGamepadState Map(VrControllerState left, VrControllerState right, bool menu = false)
    {
        var buttons = MapFaceButtons(left.LeftPrimary, left.LeftSecondary,
            right.RightPrimary, right.RightSecondary, menu);

        // Keep voice casting on YAPYAP's headset microphone; controller actions
        // cover locomotion, spell firing, interaction, and the in-game menu.
        if (left.LeftGrip >= 0.35f) buttons |= VrButtons.RightShoulder; // Use held item
        if (right.RightGrip >= 0.35f) buttons |= VrButtons.West; // Interact / hold
        if (left.LeftMenu || right.RightMenu) buttons |= VrButtons.Start;

        return new YapyapGamepadState(
            ClampAxis(left.LeftX), ClampAxis(left.LeftY),
            ClampAxis(right.RightX), ClampAxis(right.RightY),
            ClampTrigger(left.LeftTrigger), ClampTrigger(right.RightTrigger), buttons);
    }

    public static VrButtons MapFaceButtons(bool leftPrimary, bool leftSecondary,
        bool rightPrimary, bool rightSecondary, bool menu)
    {
        var buttons = VrButtons.None;
        if (rightPrimary) buttons |= VrButtons.South; // Right A: jump / confirm.
        if (leftSecondary) buttons |= VrButtons.North; // Quest Y / Index left B: drop.
        if (menu)
        {
            if (rightSecondary) buttons |= VrButtons.East; // Right B: menu back.
        }
        else
        {
            if (leftPrimary) buttons |= VrButtons.East; // Quest X / Index left A: duck.
            if (rightSecondary) buttons |= VrButtons.West; // Right B: grab.
        }
        return buttons;
    }

    private static float ClampAxis(float value) => Math.Max(-1f, Math.Min(1f, value));
    private static float ClampTrigger(float value) => Math.Max(0f, Math.Min(1f, value));
}

public static class VrFocusPolicy
{
    // The desktop window is not the input owner while the headset owns the scene.
    public static bool GameHasFocus(bool desktopFocus, bool vrActive, bool sceneInputAvailable) =>
        vrActive ? sceneInputAvailable : desktopFocus;
}

public readonly struct YapyapAnalogState
{
    public readonly float LT, RT;
    public readonly bool LB, RB;
    public YapyapAnalogState(bool swapLeft, bool swapRight, float lt, float rt, bool lb, bool rb)
    {
        LT = swapLeft ? (lb ? 1f : 0f) : lt;
        RT = swapRight ? (rb ? 1f : 0f) : rt;
        LB = swapLeft ? lt >= 0.35f : lb;
        RB = swapRight ? rt >= 0.35f : rb;
    }
}

public static class HeadsetAimGate
{
    public static bool CanUse(bool enabled, bool localOwner, bool sameCamera, int sampleFrame, int frame) =>
        enabled && localOwner && sameCamera && sampleFrame >= 0 && frame >= sampleFrame &&
        frame - sampleFrame <= 2;
}

public enum InventoryDirection { None, Left, Right, Down }

public sealed class InputReleaseGate
{
    private uint _blocked;
    private bool _movementBlocked;
    public bool SuppressMovement { get; private set; }

    public uint Filter(uint buttons, uint consumedMask, bool consuming, float x, float y)
    {
        _blocked &= buttons;
        if (consuming) _blocked |= buttons & consumedMask;
        if (consuming) _movementBlocked = true;
        if (Math.Max(Math.Abs(x), Math.Abs(y)) < 0.3f) _movementBlocked = false;
        SuppressMovement = consuming || _movementBlocked;
        return buttons & ~_blocked;
    }
}

public readonly struct InventoryGestureResult
{
    public InventoryGestureResult(bool open, bool store, InventoryDirection direction)
    { Open = open; Store = store; Direction = direction; }
    public bool Open { get; }
    public bool Store { get; }
    public InventoryDirection Direction { get; }
}

/// One action per press/deflection, with no Unity or network dependencies.
public sealed class InventoryGestures
{
    private bool _open, _toggleHeld, _storeHeld, _cancelHeld, _stickArmed = true;

    public InventoryGestureResult Update(bool available, bool toggle, bool store, bool cancel, float x, float y)
    {
        var wasOpen = _open;
        if (!available) _open = false;
        else if (cancel && !_cancelHeld) _open = false;
        else if (toggle && !_toggleHeld) _open = !_open;

        // A button already down when opening must not stow an item.
        var doStore = available && wasOpen && _open && store && !_storeHeld;
        var direction = InventoryDirection.None;
        var magnitude = Math.Max(Math.Abs(x), Math.Abs(y));
        if (magnitude < 0.3f) _stickArmed = true;
        if (available && wasOpen && _open && _stickArmed && magnitude >= 0.6f)
        {
            if (Math.Abs(x) >= Math.Abs(y)) direction = x < 0 ? InventoryDirection.Left : InventoryDirection.Right;
            else if (y < 0) direction = InventoryDirection.Down;
            _stickArmed = false;
        }
        if (!available || wasOpen != _open) _stickArmed = magnitude < 0.3f;
        _toggleHeld = toggle; _storeHeld = store; _cancelHeld = cancel;
        return new InventoryGestureResult(_open, doStore, direction);
    }

    public void Suspend()
    {
        _open = false;
        _toggleHeld = _storeHeld = _cancelHeld = true;
        _stickArmed = false;
    }
}
