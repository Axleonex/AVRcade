using VrClient.Core.Launch;

namespace VrClient.Core.Tests;

public sealed class GameLaunchBindingTests
{
    [Theory]
    [InlineData("C:\\SteamLibrary\\steamapps\\common\\Game", "D:\\SteamLibrary\\steamapps\\common\\Game\\bin\\Game.exe")]
    [InlineData("W:\\Games\\Game", "X:\\Games\\Game\\bin\\Game.exe")]
    [InlineData("\\\\server-one\\games\\Game", "\\\\server-two\\games\\Game\\bin\\Game.exe")]
    [InlineData("C:\\Games\\Game", "C:\\Games\\Game-backup\\bin\\Game.exe")]
    public void DifferentDriveShareOrPrefixCannotMatchTheSelectedExecutable(string selected, string actual)
    {
        Assert.False(GameLaunchBinding.MatchesExecutable(actual, selected, "bin/Game.exe"));
    }

    [Fact]
    public void ProcessImageQueryCanIdentifyThisTestProcessWithoutModuleEnumeration()
    {
        using var process = System.Diagnostics.Process.GetCurrentProcess();
        var path = GameLaunchBinding.ProcessExecutable(process);
        Assert.NotNull(path);
        Assert.True(File.Exists(path));
    }

    [Fact]
    public void InjectorOwnershipRequiresExactPidStartTimeAndExecutable()
    {
        var executable = Path.Combine(Path.GetTempPath(), "AVRcade", "UEVRInjector.exe");
        var owner = new VrClient.Core.Unreal.UevrSession.InjectorOwner(101, 123456, executable, "Game");
        Assert.True(VrClient.Core.Unreal.UevrSession.MatchesOwner(owner, 101, 123456, executable));
        Assert.False(VrClient.Core.Unreal.UevrSession.MatchesOwner(owner, 202, 123456, executable));
        Assert.False(VrClient.Core.Unreal.UevrSession.MatchesOwner(owner, 101, 999999, executable));
        Assert.False(VrClient.Core.Unreal.UevrSession.MatchesOwner(owner, 101, 123456, executable + ".other"));
        Assert.False(VrClient.Core.Unreal.UevrSession.MatchesOwner(owner, 101, 123456, null));
    }

    [Fact]
    public void InjectionRequiresTheExecutableInTheSelectedInstallation()
    {
        var a = Path.Combine(Path.GetTempPath(), "disk-a", "Game");
        var b = Path.Combine(Path.GetTempPath(), "disk-b", "Game");
        Assert.True(GameLaunchBinding.MatchesExecutable(Path.Combine(a, "bin", "Game.exe"), a, "bin/Game.exe"));
        Assert.False(GameLaunchBinding.MatchesExecutable(Path.Combine(b, "bin", "Game.exe"), a, "bin/Game.exe"));
        Assert.False(GameLaunchBinding.MatchesExecutable(null, a, "bin/Game.exe"));
        Assert.False(GameLaunchBinding.MatchesExecutable(Path.Combine(b, "Game.exe"), a, "../Game/Game.exe/../../disk-b/Game/Game.exe"));
    }

    [Fact]
    public void ChildCliReceivesTheSelectedConfigurationRootForBothNames()
    {
        var root = Path.Combine(Path.GetTempPath(), "AVRcade on disk B");
        var start = GameLaunchBinding.CreateCliStartInfo(root, "avrcade-cli.exe");
        Assert.Equal(root, start.WorkingDirectory);
        Assert.Equal(root, start.Environment["AVRCADE_REPO_ROOT"]);
        Assert.Equal(root, start.Environment["VRCLIENT_REPO_ROOT"]);
        Assert.False(start.UseShellExecute);
    }

    [Theory]
    [InlineData("1091500", "Cyberpunk 2077")]
    [InlineData("1943950", "Escape The Backrooms")]
    [InlineData("3949040", "RV There Yet")]
    public void GameAndManifestArgumentsStayOnTheSameSelectedDisk(string appId, string name)
    {
        var library = Path.Combine(Path.GetTempPath(), "disk B", "SteamLibrary");
        var game = Path.Combine(library, "steamapps", "common", name);
        var start = GameLaunchBinding.CreateCliStartInfo(Path.GetTempPath(), "avrcade-cli.exe");
        GameLaunchBinding.AddGame(start, game, appId);
        Assert.Equal(new[] { "--game-dir", game, "--steam-manifest", Path.Combine(library, "steamapps", "appmanifest_" + appId + ".acf") }, start.ArgumentList);
    }

    [Fact]
    public void ManifestCannotComeFromAnArbitraryParentOrGameIdentity()
    {
        Assert.Throws<InvalidDataException>(() => GameLaunchBinding.ManifestFor(Path.GetTempPath(), "101"));
        Assert.Throws<InvalidDataException>(() => GameLaunchBinding.ManifestFor(Path.GetTempPath(), "../101"));
    }

    [Fact]
    public void PackagedConfigurationWinsOverStaleLegacyRootAndWorkingDirectory()
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-root-" + Guid.NewGuid().ToString("N"));
        try
        {
            foreach (var name in new[] { "installed", "legacy", "working" })
            {
                var marker = Path.Combine(root, name, "config", "modpacks");
                Directory.CreateDirectory(marker);
                File.WriteAllText(Path.Combine(marker, "repo.modpack.json"), "{}");
            }
            Assert.Equal(Path.Combine(root, "installed"), RepoRoot.Find(Path.Combine(root, "installed"),
                Path.Combine(root, "working"), null, Path.Combine(root, "legacy")));
            Assert.Equal(Path.Combine(root, "working"), RepoRoot.Find(Path.Combine(root, "installed"),
                root, Path.Combine(root, "working"), Path.Combine(root, "legacy")));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
    }
}
