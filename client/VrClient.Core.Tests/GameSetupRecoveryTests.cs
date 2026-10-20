using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Config;
using VrClient.Core.Launch;
using VrClient.Core.Modpack;

namespace VrClient.Core.Tests;

public sealed class GameSetupRecoveryTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "avrcade-recovery-" + Guid.NewGuid().ToString("N"));
    private readonly ControllerCustomization _controllers;
    public GameSetupRecoveryTests()
    {
        Directory.CreateDirectory(_root);
        _controllers = new ControllerCustomization(Path.Combine(_root, "controllers"));
    }
    public void Dispose() => Directory.Delete(_root, true);
    private GameView Game(string slug, string disk = "disk-a")
    {
        var root = Path.Combine(_root, disk, slug);
        Directory.CreateDirectory(root);
        File.WriteAllText(Path.Combine(root, "Game.exe"), "fixture");
        if (slug == "cyberpunk-2077")
        {
            Directory.CreateDirectory(Path.Combine(root, "bin", "x64"));
            File.WriteAllText(Path.Combine(root, "bin", "x64", "Cyberpunk2077.exe"), "fixture");
        }
        if (slug == "gta-san-andreas") File.WriteAllText(Path.Combine(root, "gta_sa.exe"), "fixture");
        return new(slug, slug, "101", root, true, "Allow", "fixture", GameEngine.Unity, GameReadiness.Ready, "Ready");
    }
    private string Routes => Path.Combine(_controllers.Root, "runtime", "routes.txt");
    private string Binding(string slug) => Path.Combine(_controllers.Root, "runtime", slug + ".bindings");
    private void SaveRoute(string slug, string directory, string binding)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Routes)!);
        File.AppendAllText(Routes, Path.Combine(directory, "Game.exe") + "|" + binding + "\n");
        File.WriteAllText(Binding(slug), "");
    }

    [Theory]
    [InlineData("big-walk")]
    [InlineData("content-warning")]
    [InlineData("lethal-company")]
    [InlineData("peak")]
    [InlineData("repo")]
    [InlineData("cyberpunk-2077")]
    [InlineData("escape-the-backrooms")]
    [InlineData("rv-there-yet")]
    [InlineData("yapyap")]
    [InlineData("gta-san-andreas")]
    public void EveryCatalogueGameCanRepairItsRoutesWithoutChangingAnotherGame(string slug)
    {
        var game = Game(slug);
        var other = Game("other-game", "disk-b");
        var legacy = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "VRClient", "controllers", "runtime", slug + ".bindings");
        SaveRoute(slug, game.InstallDir!, legacy);
        SaveRoute("other-game", other.InstallDir!, Binding("other-game"));
        var preserved = Path.Combine(other.InstallDir!, "Game.exe") + "|" + Binding("other-game");
        var saved = ControllerCustomization.Identity();
        _controllers.Save(slug, "quest-touch", saved);
        _controllers.SaveAsDefaults(slug, "quest-touch", saved);
        var preference = File.ReadAllBytes(_controllers.PathFor(slug, "quest-touch"));
        var recovery = new GameSetupRecovery(_controllers, _ => false);
        Assert.True(recovery.Recheck(_root, game).Ok);
        Assert.True(recovery.Recheck(_root, game).Ok); // Safe to press more than once.
        var routes = File.ReadAllLines(Routes);
        Assert.Contains(preserved, routes);
        var own = routes.Where(line => line.EndsWith("|" + Binding(slug)));
        if (slug is "big-walk" or "yapyap") Assert.Empty(own); // These use SteamVR manifests.
        else Assert.Single(own);
        Assert.DoesNotContain(routes, line => line.EndsWith("|" + legacy));
        Assert.Equal(preference, File.ReadAllBytes(_controllers.PathFor(slug, "quest-touch")));
        Assert.True(_controllers.HasSelectedDefaults(slug, "quest-touch"));
    }

    [Fact]
    public void ActualCatalogueConfigurationsKeepAllTenGamesInsideSeparateInstallations()
    {
        var source = TestRepoRoot.Find();
        var catalogue = new AppController(source, (_, _) => null, gtaSanAndreasLocator: () => null).ListGames();
        Assert.Equal(10, catalogue.Count);
        foreach (var entry in catalogue)
        {
            var folder = Path.Combine(_root, "disk-" + entry.Engine, "steamapps", "common", entry.Slug);
            Directory.CreateDirectory(folder);
            IEnumerable<string> names;
            if (entry.Engine == GameEngine.Unreal)
            {
                var config = VrClient.Core.Unreal.UevrTools.LoadGame(Path.Combine(source, "config", "unreal", entry.Slug + ".uevr.json"));
                names = new[] { config.LauncherStub, config.ShippingBinaryRelative };
            }
            else if (entry.Slug == "cyberpunk-2077") names = ["bin/x64/Cyberpunk2077.exe"];
            else if (entry.Slug == "gta-san-andreas") names = ["gta_sa.exe"];
            else
            {
                var configPath = Path.Combine(source, "config", "games", entry.Slug + ".json");
                if (!File.Exists(configPath)) configPath = Path.Combine(source, "config", "catalogue", entry.Slug + ".json");
                using var config = JsonDocument.Parse(File.ReadAllText(configPath));
                names = config.RootElement.GetProperty("executable_names").EnumerateArray().Select(item => item.GetString()!).ToArray();
            }
            foreach (var name in names)
            {
                var file = ModUpdateTransaction.Resolve(folder, name.Replace('\\', '/'));
                Directory.CreateDirectory(Path.GetDirectoryName(file)!); File.WriteAllText(file, "fixture");
            }
            Directory.CreateDirectory(Path.GetDirectoryName(Binding(entry.Slug))!);
            File.WriteAllText(Binding(entry.Slug), "");
            var game = entry with { InstallDir = folder };
            var result = new GameSetupRecovery(_controllers, _ => false).Recheck(source, game);
            Assert.True(result.Ok, entry.Slug + ": " + result.Message);
            foreach (var route in File.ReadAllLines(Routes).Where(line => line.EndsWith("|" + Binding(entry.Slug))))
                Assert.StartsWith(folder + Path.DirectorySeparatorChar, route);
        }
    }

    [Fact]
    public void MovingGameAcrossDisksRemovesOnlyItsOldExecutableRoute()
    {
        var before = Game("repo");
        SaveRoute("repo", before.InstallDir!, Binding("repo"));
        var after = Game("repo", "disk-b");
        Assert.True(new GameSetupRecovery(_controllers, _ => false).Recheck(_root, after).Ok);
        var line = Assert.Single(File.ReadAllLines(Routes));
        Assert.StartsWith(after.InstallDir!, line);
        Assert.DoesNotContain(before.InstallDir!, line);
    }

    [Fact]
    public void RecheckDoesNotStealAnExecutableOwnedByAnotherGame()
    {
        var game = Game("repo");
        SaveRoute("repo", game.InstallDir!, Binding("other-game"));
        var before = File.ReadAllBytes(Routes);
        var result = new GameSetupRecovery(_controllers, _ => false).Recheck(_root, game);
        Assert.False(result.Ok);
        Assert.Contains("Another game", result.Message);
        Assert.Equal(before, File.ReadAllBytes(Routes));
    }

    [Fact]
    public void RunningGamePreventsRecoveryWrites()
    {
        var game = Game("repo");
        Assert.False(new GameSetupRecovery(_controllers, _ => true).Recheck(_root, game).Ok);
        Assert.False(Directory.Exists(Path.Combine(game.InstallDir!, ".avrcade")));
        Assert.False(File.Exists(Routes));
    }

    [Fact]
    public void MissingDiskOffersLocateWithoutFallingBackToAnotherGame()
    {
        var game = Game("repo");
        Directory.Delete(game.InstallDir!, true);
        var result = new GameSetupRecovery(_controllers, _ => false).Recheck(_root, game);
        Assert.False(result.Ok);
        Assert.Contains("Locate installed game", result.Message);
        Assert.False(File.Exists(Routes));
    }

    [Fact]
    public void RecoveryRollsBackOnlyTheSelectedGamesInterruptedUpdate()
    {
        var game = Game("repo");
        var other = Game("other-game", "disk-b");
        File.WriteAllText(Path.Combine(other.InstallDir!, "keep.txt"), "other game");
        var relative = "BepInEx/plugins/owned.dll";
        var target = ModUpdateTransaction.Resolve(game.InstallDir!, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.WriteAllText(target, "new");
        var backupRelative = ".avrcade/mod-updates/fixture/backup/owned.dll";
        var backup = ModUpdateTransaction.Resolve(game.InstallDir!, backupRelative);
        Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
        File.WriteAllText(backup, "old");
        var pending = ModUpdateTransaction.Resolve(game.InstallDir!, ".avrcade/mod-updates/pending.json");
        File.WriteAllText(pending, JsonSerializer.Serialize(new { Entries = new[] {
            new { Path = relative, Backup = backupRelative, Existed = true,
                Before = Hashing.Sha256OfFile(backup), After = Hashing.Sha256OfFile(target) } } }));
        var result = new GameSetupRecovery(_controllers, _ => false).Recheck(_root, game);
        Assert.True(result.Ok, result.Message);
        Assert.Equal("old", File.ReadAllText(target));
        Assert.False(File.Exists(pending));
        Assert.Equal("other game", File.ReadAllText(Path.Combine(other.InstallDir!, "keep.txt")));
    }

    [Fact]
    public void ParallelUpdaterLeasePreventsRecheckFromTouchingItsTransaction()
    {
        var game = Game("repo");
        var path = ModUpdateTransaction.Resolve(game.InstallDir!, ".avrcade/mod-updates/update.lock");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var held = new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        Assert.False(new GameSetupRecovery(_controllers, _ => false).Recheck(_root, game).Ok);
        Assert.False(File.Exists(Routes));
    }
}
