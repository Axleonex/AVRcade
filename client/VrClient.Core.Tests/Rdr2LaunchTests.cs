using VrClient.Core.Launch;
using VrClient.Core.Rage;

namespace VrClient.Core.Tests;

public sealed class Rdr2LaunchTests
{
    [Fact]
    public void Rdr2StoryLaunchPolicy_ForwardsAnExplicitStoryMarkerThroughSteam()
    {
        Assert.Equal(
            ["-dx12", "-vrclient-rdr2-story"],
            Rdr2LaunchPolicy.SteamArguments);
    }

    [Fact]
    public void SteamOwnedLauncher_DryRunUsesSteamAppLaunch()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-steam-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        try
        {
            var steam = Path.Combine(root, "steam.exe");
            File.WriteAllText(steam, "fixture");
            var result = new SteamOwnedLauncher().Launch("1174180", root, dryRun: true);

            Assert.True(result.Success);
            Assert.Equal("dry_run", result.Status);
            Assert.Equal(steam, result.SteamExecutable);
            Assert.Contains("-applaunch 1174180", result.Detail, StringComparison.Ordinal);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void SteamOwnedLauncher_RejectsNonNumericAppId()
    {
        var result = new SteamOwnedLauncher().Launch("rdr2", dryRun: true);
        Assert.False(result.Success);
        Assert.Equal("invalid_app_id", result.Status);
    }

    [Fact]
    public void Rdr2AdapterContract_RequiresBridgeServicesForPinnedBuild()
    {
        var root = TestRepoRoot.Find();
        Assert.NotNull(root);
        var result = Rdr2NativeAdapterContract.Evaluate(root!, "13773296");

        Assert.NotEqual(Rdr2AdapterContractStatus.ManifestMissing, result.Status);
        Assert.Equal("steam-1174180-build-13773296", result.RequiredBuild);
        Assert.False(result.Ready);
    }

    [Fact]
    public void Rdr2AdapterContract_RequiresTheAsiLoaderThatLoadsTheBridge()
    {
        var repoRoot = TestRepoRoot.Find();
        Assert.NotNull(repoRoot);
        var gameRoot = Path.Combine(Path.GetTempPath(), $"vrclient-rdr2-game-{Guid.NewGuid():N}");
        Directory.CreateDirectory(gameRoot);
        try
        {
            foreach (var name in new[]
                     {
                         "ScriptHookRDR2.dll",
                         "vrclient_rdr2_bridge.asi",
                         "vulkan-1.dll",
                         "runtime-profile.json"
                     })
                File.WriteAllText(Path.Combine(gameRoot, name), "fixture");

            var result = Rdr2NativeAdapterContract.Evaluate(repoRoot!, "13773296", gameRoot);

            Assert.False(result.Ready);
            // The game-folder checks only run once the optional native bridge is built in this checkout.
            if (result.AdapterBinary is null) return;
            Assert.Contains("dinput8.dll", result.Detail, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            Directory.Delete(gameRoot, recursive: true);
        }
    }

    [Fact]
    public void Rdr2AdapterContract_RejectsAnInstalledBridgeThatDoesNotMatchVrClient()
    {
        var repoRoot = TestRepoRoot.Find();
        Assert.NotNull(repoRoot);
        var gameRoot = Path.Combine(Path.GetTempPath(), $"vrclient-rdr2-game-{Guid.NewGuid():N}");
        Directory.CreateDirectory(gameRoot);
        try
        {
            foreach (var name in new[]
                     {
                         "ScriptHookRDR2.dll",
                         "dinput8.dll",
                         "vrclient_rdr2_bridge.asi",
                         "vulkan-1.dll",
                         "runtime-profile.json"
                     })
                File.WriteAllText(Path.Combine(gameRoot, name), "not-the-vrclient-artifact");

            var result = Rdr2NativeAdapterContract.Evaluate(repoRoot!, "13773296", gameRoot);

            Assert.False(result.Ready);
            if (result.AdapterBinary is null) return;
            Assert.Contains("does not match", result.Detail, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            Directory.Delete(gameRoot, recursive: true);
        }
    }

    [Fact]
    public void SteamOwnedLauncher_Appends_owned_game_launch_arguments()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-steam-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        try
        {
            var steam = Path.Combine(root, "steam.exe");
            File.WriteAllText(steam, "fixture");
            var result = new SteamOwnedLauncher().Launch(
                "1174180", root, dryRun: true, launchArguments: ["-dx12"]);

            Assert.True(result.Success);
            Assert.Contains("-applaunch 1174180 -dx12", result.Detail, StringComparison.Ordinal);
        }
        finally { Directory.Delete(root, recursive: true); }
    }
}
