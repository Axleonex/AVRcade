using Avrcade.YapyapVr;
using Xunit;

namespace YapyapVrControls.Tests;

public sealed class YapyapControlMapTests
{
    [Fact]
    public void AnalogGripTriggerSwapConvertsValuesPerHandWithoutChangingDefaults()
    {
        var identity = new YapyapAnalogState(false, false, 0.2f, 0.8f, true, false);
        Assert.Equal(0.2f, identity.LT); Assert.Equal(0.8f, identity.RT);
        Assert.True(identity.LB); Assert.False(identity.RB);
        var swapped = new YapyapAnalogState(true, true, 0.2f, 0.8f, true, false);
        Assert.Equal(1f, swapped.LT); Assert.Equal(0f, swapped.RT);
        Assert.False(swapped.LB); Assert.True(swapped.RB);
    }
    [Fact]
    public void Left_primary_is_duck_and_right_B_is_grab()
    {
        Assert.Equal(VrButtons.East, YapyapControlMap.Map(Controller(leftPrimary: true), Controller()).Buttons);
        Assert.Equal(VrButtons.West, YapyapControlMap.Map(Controller(), Controller(rightSecondary: true)).Buttons);
    }

    [Fact]
    public void Menu_B_goes_back_without_sending_grab_or_duck_from_left_button()
    {
        Assert.Equal(VrButtons.East, YapyapControlMap.Map(Controller(leftPrimary: true), Controller(rightSecondary: true), menu: true).Buttons);
        Assert.Equal(VrButtons.None, YapyapControlMap.Map(Controller(leftPrimary: true), Controller(), menu: true).Buttons);
    }
    [Fact]
    public void Maps_each_hand_to_the_existing_gamepad_axes_and_triggers()
    {
        var left = Controller(leftX: -0.5f, leftY: 0.8f, leftTrigger: 0.3f);
        var right = Controller(rightX: 0.4f, rightY: -0.7f, rightTrigger: 0.9f);

        var result = YapyapControlMap.Map(left, right);

        Assert.Equal(-0.5f, result.MoveX);
        Assert.Equal(0.8f, result.MoveY);
        Assert.Equal(0.4f, result.LookX);
        Assert.Equal(-0.7f, result.LookY);
        Assert.Equal(0.3f, result.Use);
        Assert.Equal(0.9f, result.Fire);
    }

    [Fact]
    public void Maps_button_actions_and_applies_a_grip_threshold()
    {
        var left = Controller(leftPrimary: true, leftSecondary: true, leftGrip: 0.35f, leftMenu: true);
        var right = Controller(rightPrimary: true, rightSecondary: true, rightGrip: 0.34f);

        var result = YapyapControlMap.Map(left, right);

        Assert.Equal(VrButtons.South | VrButtons.East | VrButtons.West |
                     VrButtons.North | VrButtons.RightShoulder | VrButtons.Start, result.Buttons);
        Assert.False(result.Buttons.HasFlag(VrButtons.LeftShoulder));
    }

    [Fact]
    public void Clamps_invalid_runtime_axis_and_trigger_values()
    {
        var left = Controller(leftX: -1.5f, leftTrigger: -0.2f);
        var right = Controller(rightTrigger: 1.4f);

        var result = YapyapControlMap.Map(left, right);

        Assert.Equal(-1f, result.MoveX);
        Assert.Equal(0f, result.Use);
        Assert.Equal(1f, result.Fire);
    }

    [Fact]
    public void Right_grip_interacts_without_also_using_the_item()
    {
        var result = YapyapControlMap.Map(Controller(), Controller(rightGrip: 0.35f));
        Assert.Equal(VrButtons.West, result.Buttons);
    }

    [Fact]
    public void Inventory_toggle_and_store_require_release_between_presses()
    {
        var controls = new InventoryGestures();
        Assert.True(controls.Update(true, true, false, false, 0, 0).Open);
        Assert.True(controls.Update(true, true, false, false, 0, 0).Open);
        Assert.True(controls.Update(true, false, true, false, 0, 0).Store);
        Assert.False(controls.Update(true, false, true, false, 0, 0).Store);
        controls.Update(true, false, false, false, 0, 0);
        Assert.True(controls.Update(true, false, true, false, 0, 0).Store);
        Assert.False(controls.Update(true, true, false, false, 0, 0).Open);
    }

    [Fact]
    public void Opening_with_grip_already_held_does_not_store_an_item()
    {
        var controls = new InventoryGestures();
        controls.Update(true, false, true, false, 0, 0);
        Assert.False(controls.Update(true, true, true, false, 0, 0).Store);
    }

    [Fact]
    public void Availability_loss_cancels_mode_and_requires_fresh_toggle()
    {
        var controls = new InventoryGestures();
        controls.Update(true, true, false, false, 0, 0);
        Assert.False(controls.Update(false, true, true, false, 1, 0).Open);
        Assert.False(controls.Update(true, true, true, false, 1, 0).Open);
        controls.Update(true, false, false, false, 0, 0);
        Assert.True(controls.Update(true, true, false, false, 0, 0).Open);
        Assert.False(controls.Update(true, false, false, true, 0, 0).Open);
    }

    [Fact]
    public void Inventory_stick_uses_one_dominant_direction_until_centered()
    {
        var controls = new InventoryGestures();
        controls.Update(true, true, false, false, 0, 0);
        Assert.Equal(InventoryDirection.None, controls.Update(true, false, false, false, 0.59f, 0).Direction);
        Assert.Equal(InventoryDirection.Right, controls.Update(true, false, false, false, 0.8f, -0.7f).Direction);
        Assert.Equal(InventoryDirection.None, controls.Update(true, false, false, false, -0.9f, 0).Direction);
        controls.Update(true, false, false, false, 0, 0);
        Assert.Equal(InventoryDirection.Down, controls.Update(true, false, false, false, 0.1f, -0.8f).Direction);
    }

    [Fact]
    public void Closed_inventory_does_not_consume_locomotion_or_store()
    {
        var controls = new InventoryGestures();
        var result = controls.Update(true, false, true, false, 0.8f, 0.5f);
        Assert.False(result.Open);
        Assert.False(result.Store);
        Assert.Equal(InventoryDirection.None, result.Direction);
        Assert.Equal(0.8f, YapyapControlMap.Map(Controller(leftX: 0.8f), Controller()).MoveX);
    }

    [Fact]
    public void Inventory_actions_and_movement_do_not_leak_until_released()
    {
        var gate = new InputReleaseGate();
        Assert.Equal(0u, gate.Filter(1u, 1u, true, 0.8f, 0));
        Assert.Equal(0u, gate.Filter(1u, 1u, false, 0.8f, 0));
        Assert.True(gate.SuppressMovement);
        gate.Filter(0, 1u, false, 0, 0);
        Assert.False(gate.SuppressMovement);
        Assert.Equal(1u, gate.Filter(1u, 1u, false, 0.8f, 0));
    }

    [Theory]
    [InlineData(true, true, true, 100, 102, true)]
    [InlineData(true, true, true, 100, 103, false)]
    [InlineData(true, true, true, 100, 99, false)]
    [InlineData(true, true, true, -1, 0, false)]
    [InlineData(false, true, true, 100, 100, false)]
    [InlineData(true, false, true, 100, 100, false)]
    [InlineData(true, true, false, 100, 100, false)]
    public void Headset_aim_rejects_stale_disabled_or_foreign_samples(
        bool enabled, bool owner, bool camera, int sample, int frame, bool expected)
    {
        Assert.Equal(expected, HeadsetAimGate.CanUse(enabled, owner, camera, sample, frame));
    }

    private static VrControllerState Controller(
        float leftX = 0, float leftY = 0, float rightX = 0, float rightY = 0,
        float leftTrigger = 0, float rightTrigger = 0, float leftGrip = 0, float rightGrip = 0,
        bool leftPrimary = false, bool leftSecondary = false,
        bool rightPrimary = false, bool rightSecondary = false,
        bool leftMenu = false, bool rightMenu = false) =>
        new(leftX, leftY, rightX, rightY, leftTrigger, rightTrigger, leftGrip, rightGrip,
            leftPrimary, leftSecondary, rightPrimary, rightSecondary, leftMenu, rightMenu);
}
