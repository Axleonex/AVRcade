using VrClient.Core.Discovery;

namespace VrClient.Core.Tests;

public sealed class GameIsolationTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "avrcade-isolation-" + Guid.NewGuid().ToString("N"));
    public GameIsolationTests() => Directory.CreateDirectory(_root);
    public void Dispose() => Directory.Delete(_root, true);

    private string Library(string name, params string[] apps)
    {
        var root = Path.Combine(_root, name);
        Directory.CreateDirectory(Path.Combine(root, "steamapps"));
        foreach (var app in apps)
        {
            var game = Path.Combine(root, "steamapps", "common", "Game" + app);
            Directory.CreateDirectory(game);
            File.WriteAllText(Path.Combine(game, "Game.exe"), "fixture");
            File.WriteAllText(Path.Combine(root, "steamapps", "appmanifest_" + app + ".acf"),
                $"\"AppState\" {{ \"appid\" \"{app}\" \"installdir\" \"Game{app}\" }}");
        }
        return root;
    }

    [Fact]
    public void UnreadableSelectionDoesNotSilentlyPickAnotherDisk()
    {
        var a = Library("disk-a", "101"); var b = Library("disk-b", "101", "202");
        var preference = Path.Combine(_root, "libraries.txt");
        var scanner = new SteamLibraryScanner(preference);
        Assert.True(scanner.RememberGameFolder("101", "Game.exe", Path.Combine(a, "steamapps", "common", "Game101")));
        Assert.True(scanner.RememberGameFolder("202", "Game.exe", Path.Combine(b, "steamapps", "common", "Game202")));
        using var held = new FileStream(Path.Combine(preference + ".games", "101.txt"), FileMode.Open, FileAccess.ReadWrite, FileShare.None);
        Assert.Null(scanner.FindGame("101", "Game.exe"));
    }

    [Fact]
    public void LocatingAnotherGameDoesNotSwitchTheFirstGamesSelectedDisk()
    {
        var a = Library("disk-a", "101", "202");
        var b = Library("disk-b", "101", "202");
        var scanner = new SteamLibraryScanner(Path.Combine(_root, "libraries.txt"));
        var selected = Path.Combine(a, "steamapps", "common", "Game101");
        Assert.True(scanner.RememberGameFolder("101", "Game.exe", selected));
        Assert.True(scanner.RememberGameFolder("202", "Game.exe", Path.Combine(b, "steamapps", "common", "Game202")));
        Assert.Equal(selected, scanner.FindGame("101", "Game.exe")?.InstallDir);
    }

    [Fact]
    public void DisconnectedSelectedDiskDoesNotSilentlySelectAnotherCopy()
    {
        var a = Library("disk-a", "101");
        var b = Library("disk-b", "101", "202");
        var scanner = new SteamLibraryScanner(Path.Combine(_root, "libraries.txt"));
        Assert.True(scanner.RememberGameFolder("101", "Game.exe", Path.Combine(a, "steamapps", "common", "Game101")));
        Assert.True(scanner.RememberGameFolder("202", "Game.exe", Path.Combine(b, "steamapps", "common", "Game202")));
        Directory.Move(a, a + "-disconnected");
        Assert.Null(scanner.FindGame("101", "Game.exe"));
        Assert.Equal(a, scanner.FindOfflineLibrary("101"));
    }

    [Fact]
    public void UnreadableLibraryDoesNotHideHealthyLibraries()
    {
        var a = Library("disk-a", "101");
        var b = Library("disk-b", "101");
        File.WriteAllText(Path.Combine(a, "steamapps", "libraryfolders.vdf"),
            "\"path\" \"" + b.Replace("\\", "\\\\") + "\"");
        using var blocked = new FileStream(Path.Combine(a, "steamapps", "appmanifest_101.acf"),
            FileMode.Open, FileAccess.ReadWrite, FileShare.None);
        Assert.Equal(Path.Combine(b, "steamapps", "common", "Game101"),
            new SteamLibraryScanner().FindGame("101", "Game.exe", a)?.InstallDir);
    }

    [Fact]
    public void ManifestCannotSelectAPathOutsideSteamCommon()
    {
        var library = Library("disk-a");
        var outside = Path.Combine(library, "steamapps", "other-game");
        Directory.CreateDirectory(outside);
        File.WriteAllText(Path.Combine(outside, "Game.exe"), "foreign game");
        File.WriteAllText(Path.Combine(library, "steamapps", "appmanifest_101.acf"),
            "\"appid\" \"101\" \"installdir\" \"../other-game\"");
        Assert.Null(new SteamLibraryScanner().FindGame("101", "Game.exe", library));
    }

    [Fact]
    public void ManifestIdentityMustMatchTheRequestedApp()
    {
        var library = Library("disk-a", "101");
        var file = Path.Combine(library, "steamapps", "appmanifest_101.acf");
        File.WriteAllText(file, "\"appid\" \"202\" \"installdir\" \"Game101\"");
        Assert.Null(new SteamLibraryScanner().FindGame("101", "Game.exe", library));
    }

    [Fact]
    public void NestedExecutableStillResolvesInsideTheSelectedGame()
    {
        var library = Library("disk-a", "101");
        var game = Path.Combine(library, "steamapps", "common", "Game101");
        Directory.CreateDirectory(Path.Combine(game, "bin", "x64"));
        File.WriteAllText(Path.Combine(game, "bin", "x64", "Game.exe"), "nested fixture");
        Assert.Equal(game, new SteamLibraryScanner().FindGame("101", "bin/x64/Game.exe", library)?.InstallDir);
        Assert.Null(new SteamLibraryScanner().FindGame("../101", "Game.exe", library));
    }
}
