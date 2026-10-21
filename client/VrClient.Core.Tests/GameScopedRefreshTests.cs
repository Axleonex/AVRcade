using VrClient.Core.App;
using VrClient.Core.Catalog;
using VrClient.Core.Discovery;
using VrClient.Core.Launch;

namespace VrClient.Core.Tests;

public sealed class GameScopedRefreshTests : TempTree
{
    public static IEnumerable<object[]> Games => CatalogueStartupIsolationTests.Games
        .Select(row => new object[] { row[0], row[1] });

    public GameScopedRefreshTests()
    {
        var source = Path.Combine(TestRepoRoot.Find(), "config");
        foreach (var file in Directory.EnumerateFiles(source, "*.json", SearchOption.AllDirectories))
        {
            var target = Path.Combine(Root, "config", Path.GetRelativePath(source, file));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target);
        }
    }

    private AppController Controller(List<string> calls) => new(Root,
        (app, _) => { calls.Add(app); return null; }, headsetConnected: () => false,
        gtaSanAndreasLocator: () => { calls.Add("12120"); return null; });

    [Theory, MemberData(nameof(Games))]
    public void SingleGameRefreshOnlyLocatesTheRequestedInstallation(string slug, string appId)
    {
        var calls = new List<string>();
        var game = Controller(calls).GetGame(slug);
        Assert.NotNull(game);
        Assert.Equal(slug, game.Slug);
        Assert.Equal(appId, game.SteamAppId);
        Assert.Equal(new[] { appId }, calls);
    }

    [Theory, MemberData(nameof(Games))]
    public void ScopedAndFullDiscoveryReturnTheSameFacts(string slug, string appId)
    {
        var controller = Controller([]);
        var scoped = controller.GetGame(slug, false);
        Assert.Equal(appId, scoped?.SteamAppId);
        Assert.Equal(controller.ListGames(false).Single(g => g.Slug == slug), scoped);
    }

    [Theory, MemberData(nameof(Games))]
    public void BrokenUnrelatedGameConfigurationsDoNotBlockSelectedGame(string slug, string appId)
    {
        foreach (var row in Games.Where(row => (string)row[0] != slug))
        {
            var other = (string)row[0];
            var paths = new[] { $"modpacks/{other}.modpack.json", $"games/{other}.json",
                $"unreal/{other}.uevr.json", $"catalogue/{other}.json", $"redengine/native/{other}.json",
                $"legacy/{other}.json" };
            foreach (var relative in paths)
            {
                var path = Path.Combine(Root, "config", relative);
                if (File.Exists(path)) File.WriteAllText(path, "{broken unrelated configuration");
            }
        }
        var calls = new List<string>();
        Assert.Equal(slug, Controller(calls).GetGame(slug)?.Slug);
        Assert.Equal(new[] { appId }, calls);
    }

    [Theory]
    [InlineData("../repo")]
    [InlineData("repo/../../other")]
    [InlineData("repo\\other")]
    public void InvalidSelectionCannotEscapeTheCatalogue(string slug)
    {
        var calls = new List<string>();
        Assert.Throws<ArgumentException>(() => Controller(calls).GetGame(slug));
        Assert.Empty(calls);
    }

    [Fact]
    public void UnknownOrDeferredSelectionDoesNotScanInstalledGames()
    {
        var calls = new List<string>();
        var controller = Controller(calls);
        foreach (var slug in new[] { "unknown-game", "gta-iv", "gta-v", "fallout-3", "fallout-new-vegas" })
            Assert.Null(controller.GetGame(slug));
        Assert.Empty(calls);
        Assert.Equal("repo", controller.GetGame("REPO")?.Slug);
    }

    [Fact]
    public void FilteredCatalogDoesNotParseAnotherModpack()
    {
        File.WriteAllText(Path.Combine(Root, "config", "modpacks", "peak.modpack.json"), "{");
        var catalog = new GameCatalog();
        var game = Assert.Single(catalog.Discover(Path.Combine(Root, "config", "modpacks"),
            Path.Combine(Root, "config", "games"), Path.Combine(Root, "config", "profiles"), "repo"));
        Assert.Equal("repo", game.Slug);
    }

    [Theory]
    [InlineData("repo", "3241660", "REPO.exe")]
    [InlineData("lethal-company", "1966720", "Lethal Company.exe")]
    [InlineData("peak", "3527290", "PEAK.exe")]
    [InlineData("content-warning", "2881650", "Content Warning.exe")]
    [InlineData("big-walk", "1478500", "Big Walk.exe")]
    public void SelectedDiskIsRecheckedAndNeverReplacedByAnotherCopy(string slug, string appId, string executable)
    {
        string Install(string disk)
        {
            var game = Path.Combine(Root, disk, "steamapps", "common", slug);
            Directory.CreateDirectory(game);
            File.WriteAllText(Path.Combine(game, executable), "fixture");
            File.WriteAllText(GameLaunchBinding.ManifestFor(game, appId),
                $"\"appid\" \"{appId}\" \"installdir\" \"{slug}\"");
            return game;
        }
        var selected = Install("disk A"); var duplicate = Install("disk B");
        var scanner = new SteamLibraryScanner(Path.Combine(Root, "settings", "libraries.txt"));
        Assert.True(scanner.RememberGameFolder(appId, executable, duplicate));
        Assert.True(scanner.RememberGameFolder(appId, executable, selected));
        var controller = new AppController(Root, (app, exe) => scanner.FindGame(app, exe)?.InstallDir,
            headsetConnected: () => false, gtaSanAndreasLocator: () => null);
        Assert.Equal(selected, controller.GetGame(slug)?.InstallDir);
        var disk = Path.Combine(Root, "disk A");
        Directory.Move(disk, disk + "-offline");
        try { Assert.Null(controller.GetGame(slug)?.InstallDir); }
        finally { Directory.Move(disk + "-offline", disk); }
        Assert.Equal(selected, controller.GetGame(slug)?.InstallDir);
    }
}