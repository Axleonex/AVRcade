using Avrcade.YapyapVr;
using Xunit;

namespace YapyapVrControls.Tests;

public sealed class LeftHandShortcutsTests
{
    [Fact]
    public void Trigger_opens_inventory_without_ragdoll()
    {
        var controls = new LeftHandShortcuts();
        controls.Update(0.8f, false);
        Assert.True(controls.InventoryPressed);
        Assert.Equal(0f, controls.RagdollTrigger);
    }

    [Fact]
    public void Stick_click_ragdolls_without_opening_inventory()
    {
        var controls = new LeftHandShortcuts();
        controls.Update(0f, true);
        Assert.False(controls.InventoryPressed);
        Assert.Equal(1f, controls.RagdollTrigger);
        controls.Update(0f, false);
        Assert.Equal(0f, controls.RagdollTrigger);
    }

    [Fact]
    public void Held_trigger_and_small_jitter_do_not_toggle_inventory_repeatedly()
    {
        var controls = new LeftHandShortcuts();
        var inventory = new InventoryGestures();
        foreach (var value in new[] { 0.8f, 0.53f, 0.57f, 0.4f, 0.7f })
        {
            controls.Update(value, false);
            Assert.True(inventory.Update(true, controls.InventoryPressed, false, false, 0, 0).Open);
        }
        controls.Update(0.3f, false);
        inventory.Update(true, controls.InventoryPressed, false, false, 0, 0);
        controls.Update(0.8f, false);
        Assert.False(inventory.Update(true, controls.InventoryPressed, false, false, 0, 0).Open);
    }
}
