using VrClient.Core.Rage;

namespace VrClient.Core.Tests;

public sealed class Rdr2ParallelIsolationTests
{
    [Fact]
    public void Run_scopes_are_title_and_run_specific_and_focus_wait_is_non_modifying()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-isolation-{Guid.NewGuid():N}");
        try
        {
            var first = new Rdr2RunScope(Path.Combine(root, "rdr2"), "run-a");
            var second = new Rdr2RunScope(Path.Combine(root, "neutral-title"), "run-b");
            Assert.NotEqual(first.LockName, second.LockName);
            Assert.NotEqual(first.IpcEndpoint, second.IpcEndpoint);
            Assert.NotEqual(first.EvidenceRoot, second.EvidenceRoot);
            Assert.Equal("waiting_for_xr_focus", second.XrFocusState(runtimeCanGrantFocus: false));
            Assert.False(Directory.Exists(second.EvidenceRoot));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }
}
