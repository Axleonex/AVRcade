using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Discovery;
using VrClient.Core.Launch;

namespace VrClient.Core.Tests;

// Fixtures exercise discovery and launch binding; no commercial game or headset is started.
public sealed class CatalogueStartupIsolationTests : TempTree
{
    public static TheoryData<string, string, string> Games => new()
    {
        { "big-walk", "1478500", "Big Walk.exe" },
        { "content-warning", "2881650", "Content Warning.exe" },
        { "cyberpunk-2077", "1091500", "bin/x64/Cyberpunk2077.exe" },
        { "escape-the-backrooms", "1943950", "EscapeTheBackrooms/Binaries/Win64/Backrooms-Win64-Shipping.exe" },
        { "gta-san-andreas", "12120", "gta_sa.exe" },
        { "lethal-company", "1966720", "Lethal Company.exe" },
        { "peak", "3527290", "PEAK.exe" },
        { "repo", "3241660", "REPO.exe" },
        { "rv-there-yet", "3949040", "Ride/Binaries/Win64/Ride-Win64-Shipping.exe" },
        { "yapyap", "3834090", "yapyap.exe" }
    };

    private string Install(string disk, string slug, string appId, string executable)
    {
        var game = Path.Combine(Root, disk, "SteamLibrary", "steamapps", "common", slug);
        var binary = Path.Combine(game, executable.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(binary)!);
        File.WriteAllText(binary, "fixture for " + slug);
        File.WriteAllText(GameLaunchBinding.ManifestFor(game, appId),
            $"\"AppState\" {{ \"appid\" \"{appId}\" \"installdir\" \"{slug}\" }}");
        return game;
    }

    [Fact]
    public void MatrixCoversEveryPublicCatalogueGameWithDistinctSteamIdentities()
    {
        var games = new AppController(TestRepoRoot.Find(), (_, _) => null,
            headsetConnected: () => false, gtaSanAndreasLocator: () => null).ListGames();
        Assert.Equal(Games.Count(), games.Count);
        foreach (var row in Games)
        {
            var game = Assert.Single(games.Where(g => g.Slug == (string)row[0]));
            Assert.Equal((string)row[1], game.SteamAppId);
            var slug = game.Slug;
            var relativeConfig = slug switch
            {
                "cyberpunk-2077" => "config/redengine/native/cyberpunk-2077.json",
                "gta-san-andreas" => "config/legacy/gta-san-andreas.json",
                "escape-the-backrooms" or "rv-there-yet" => $"config/unreal/{slug}.uevr.json",
                "yapyap" => "config/catalogue/yapyap.json",
                _ => $"config/games/{slug}.json"
            };
            using var config = JsonDocument.Parse(File.ReadAllText(Path.Combine(TestRepoRoot.Find(), relativeConfig)));
            var root = config.RootElement;
            var executable = slug switch
            {
                "cyberpunk-2077" => root.GetProperty("game").GetProperty("shipping_binary").GetString(),
                "gta-san-andreas" => root.GetProperty("game").GetProperty("executable_name").GetString(),
                "escape-the-backrooms" or "rv-there-yet" => root.GetProperty("executable_roles").GetProperty("shipping_binary").GetString(),
                _ => root.GetProperty("executable_names")[0].GetString()
            };
            Assert.Equal((string)row[2], executable!.Replace('\\', '/'));
        }
        Assert.Equal(games.Count, games.Select(g => g.SteamAppId).Distinct().Count());
    }

    [Theory, MemberData(nameof(Games))]
    public void SelectedInstallationKeepsItsExecutableAndManifestWhenAnotherGameChangesDisks(
        string slug, string appId, string executable)
    {
        var selected = Install("disk A", slug, appId, executable);
        var duplicate = Install("disk B", slug, appId, executable);
        var other = Games.First(row => (string)row[0] != slug);
        var otherGame = Install("disk B", (string)other[0], (string)other[1], (string)other[2]);
        var scanner = new SteamLibraryScanner(Path.Combine(Root, "settings", "libraries.txt"));
        Assert.True(scanner.RememberGameFolder(appId, executable, selected));
        Assert.True(scanner.RememberGameFolder((string)other[1], (string)other[2], otherGame));

        var found = scanner.FindGame(appId, executable);
        Assert.NotNull(found);
        Assert.Equal(selected, found.InstallDir);
        Assert.Equal(Path.Combine(selected, executable.Replace('/', Path.DirectorySeparatorChar)), found.ExecutablePath);
        Assert.True(GameLaunchBinding.MatchesExecutable(found.ExecutablePath, selected, executable));
        Assert.False(GameLaunchBinding.MatchesExecutable(
            Path.Combine(duplicate, executable.Replace('/', Path.DirectorySeparatorChar)), selected, executable));
        var child = GameLaunchBinding.CreateCliStartInfo(TestRepoRoot.Find(), "avrcade-cli.exe");
        GameLaunchBinding.AddGame(child, found.InstallDir, appId);
        Assert.Equal(new[] { "--game-dir", selected, "--steam-manifest", found.ManifestPath }, child.ArgumentList);
        Assert.Equal("fixture for " + slug, File.ReadAllText(found.ExecutablePath));
    }

    [Theory, MemberData(nameof(Games))]
    public void DisconnectedSelectedInstallationRefusesAnotherCopyAndRecoversOnReconnect(
        string slug, string appId, string executable)
    {
        var selected = Install("disk A", slug, appId, executable);
        var duplicate = Install("disk B", slug, appId, executable);
        var scanner = new SteamLibraryScanner(Path.Combine(Root, "settings", "libraries.txt"));
        Assert.True(scanner.RememberGameFolder(appId, executable, selected));
        var library = Directory.GetParent(selected)!.Parent!.Parent!.FullName;
        var disconnected = library + "-disconnected";
        Directory.Move(library, disconnected);
        try
        {
            Assert.Null(scanner.FindGame(appId, executable));
            Assert.Equal(library, scanner.FindOfflineLibrary(appId));
            Assert.True(File.Exists(Path.Combine(duplicate, executable.Replace('/', Path.DirectorySeparatorChar))));
        }
        finally { Directory.Move(disconnected, library); }
        Assert.Equal(selected, scanner.FindGame(appId, executable)?.InstallDir);
        Assert.Null(scanner.FindOfflineLibrary(appId));
    }
}
