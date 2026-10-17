namespace Avrcade.YapyapVr;

/// Separate press/release thresholds prevent trigger jitter from toggling inventory twice.
public sealed class LeftHandShortcuts
{
    private const float PressThreshold = 0.55f;
    private const float ReleaseThreshold = 0.35f;
    public bool InventoryPressed { get; private set; }
    public float RagdollTrigger { get; private set; }

    public void Update(float trigger, bool stickClicked)
    {
        InventoryPressed = InventoryPressed ? trigger > ReleaseThreshold : trigger >= PressThreshold;
        RagdollTrigger = stickClicked ? 1f : 0f;
    }
}
