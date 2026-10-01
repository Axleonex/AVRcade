using VrClient.Core.Unreal;

public sealed class UevrAttachEvidenceTests
{
    [Fact]
    public void Existing_runtime_key_without_a_new_write_is_not_a_fresh_hook()
    {
        var time = new DateTime(2026, 9, 27, 14, 0, 0, DateTimeKind.Utc);
        var before = new UevrConfigSnapshot(1000, time, true);
        Assert.False(UevrAttachEvidence.IsFreshHook(before, before));
    }

    [Fact]
    public void A_new_runtime_key_or_fresh_config_write_is_hook_evidence()
    {
        var time = new DateTime(2026, 9, 27, 14, 0, 0, DateTimeKind.Utc);
        var before = new UevrConfigSnapshot(1000, time, true);
        Assert.True(UevrAttachEvidence.IsFreshHook(before,
            new UevrConfigSnapshot(1000, time.AddSeconds(1), true)));
        Assert.True(UevrAttachEvidence.IsFreshHook(
            new UevrConfigSnapshot(500, time, false),
            new UevrConfigSnapshot(600, time, true)));
    }
}
