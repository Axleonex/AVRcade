using System.Diagnostics;
using System.IO.Compression;
using System.Text.Json;
using VrClient.Core.Modpack;

namespace VrClient.Core.Tests;

public sealed class InstallerBoundaryTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "avrcade-boundary-" + Guid.NewGuid().ToString("N"));
    private readonly List<string> _links = [];
    public InstallerBoundaryTests() => Directory.CreateDirectory(_root);
    public void Dispose()
    {
        foreach (var link in _links) if (Directory.Exists(link)) Directory.Delete(link);
        Directory.Delete(_root, true);
    }
    private string Game(string name)
    {
        var root = Path.Combine(_root, name); Directory.CreateDirectory(root);
        File.WriteAllText(Path.Combine(root, "REPO.exe"), "fixture"); return root;
    }
    private void Link(string link, string destination)
    {
        _links.Add(link);
        if (!OperatingSystem.IsWindows()) { Directory.CreateSymbolicLink(link, destination); return; }
        var start = new ProcessStartInfo("powershell.exe") { UseShellExecute = false, CreateNoWindow = true };
        start.ArgumentList.Add("-NoProfile"); start.ArgumentList.Add("-Command");
        start.ArgumentList.Add("New-Item -ItemType Junction -Path '" + link.Replace("'", "''") + "' -Target '" + destination.Replace("'", "''") + "' | Out-Null");
        using var process = Process.Start(start)!; process.WaitForExit(); Assert.Equal(0, process.ExitCode);
    }

    [Fact]
    public void CyberpunkRecoveryPreservesForeignEditsInsteadOfAdoptingThem()
    {
        var game = Game("disk-a");
        const string relative = "red4ext/plugins/CyberpunkVR_Stereo.dll";
        var store = VrClient.Core.Redengine.CyberpunkUpstreamSession.Store(game);
        Directory.CreateDirectory(store);
        File.WriteAllText(Path.Combine(store, "install.json"), JsonSerializer.Serialize(
            new VrClient.Core.Redengine.CyberpunkUpstreamSession.InstallState("0.1.7", [relative], [])));
        var stored = VrClient.Core.Redengine.CyberpunkUpstreamSession.PayloadPath(game, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(stored)!); File.WriteAllText(stored, "owned original");
        var active = VrClient.Core.Redengine.CyberpunkUpstreamSession.Resolve(game, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(active)!); File.WriteAllText(active, "foreign deployment");
        File.WriteAllText(Path.Combine(store, "active.json"), JsonSerializer.Serialize(new[] { relative }));
        Assert.Throws<IOException>(() => new VrClient.Core.Redengine.CyberpunkUpstreamSession(game, false));
        Assert.Equal("owned original", File.ReadAllText(stored));
        Assert.Equal("foreign deployment", File.ReadAllText(active));
    }

    [Fact]
    public void CyberpunkSessionCannotDeployThroughAnotherGamesLinkedPlugins()
    {
        var game = Game("disk-a"); var other = Game("disk-b");
        Directory.CreateDirectory(Path.Combine(game, "red4ext"));
        Link(Path.Combine(game, "red4ext", "plugins"), other);
        Assert.Throws<InvalidDataException>(() => VrClient.Core.Redengine.CyberpunkUpstreamSession.Resolve(game, "red4ext/plugins/CyberpunkVR_Stereo.dll"));
    }

    [Fact]
    public void CyberpunkStoredPayloadCannotRedirectIntoAnotherInstallation()
    {
        var game = Game("disk-a"); var other = Game("disk-b");
        var store = Path.Combine(game, ".avrcade", "cyberpunk-2077");
        Directory.CreateDirectory(store);
        Link(Path.Combine(store, "payload"), other);
        Assert.Throws<InvalidDataException>(() => VrClient.Core.Redengine.CyberpunkUpstreamSession.PayloadPath(game, "bin/x64/vrport.ini"));
    }

    [Fact]
    public void AnotherGamesLinkedPluginsAreNotReportedAsThisGamesInstallation()
    {
        var game = Game("disk-a"); var other = Game("disk-b");
        Directory.CreateDirectory(Path.Combine(game, "BepInEx"));
        File.WriteAllText(Path.Combine(game, "winhttp.dll"), "fixture");
        File.WriteAllText(Path.Combine(other, "foreign.dll"), "another game's plugin");
        File.WriteAllText(Path.Combine(game, "BepInEx", "vrclient-install-manifest.json"), "{\"files\":[]}");
        Link(Path.Combine(game, "BepInEx", "plugins"), other);
        Assert.False(new ModInstaller().IsInstalled(game));
    }

    [Fact]
    public void InstallDoesNotOverwriteAnotherModsLoaderOrLeavePartialFiles()
    {
        var game = Game("disk-a");
        File.WriteAllText(Path.Combine(game, "winhttp.dll"), "foreign loader");
        var zip = Path.Combine(_root, "fixture.zip");
        using (var archive = ZipFile.Open(zip, ZipArchiveMode.Create))
        {
            using (var writer = new StreamWriter(archive.CreateEntry("BepInExPack/BepInEx/plugins/owned.dll").Open())) writer.Write("new VR plugin");
            using (var writer = new StreamWriter(archive.CreateEntry("BepInExPack/winhttp.dll").Open())) writer.Write("replacement loader");
        }
        Assert.Throws<InvalidOperationException>(() => new ModInstaller().Install([zip], game));
        Assert.Equal("foreign loader", File.ReadAllText(Path.Combine(game, "winhttp.dll")));
        Assert.False(File.Exists(Path.Combine(game, "BepInEx", "plugins", "owned.dll")));
    }

    [Fact]
    public void InstallCannotFollowAModFolderLinkIntoAnotherDiskOrGame()
    {
        var game = Game("disk-a"); var other = Game("disk-b");
        Directory.CreateDirectory(Path.Combine(game, "BepInEx"));
        Link(Path.Combine(game, "BepInEx", "plugins"), other);
        var zip = Path.Combine(_root, "fixture.zip");
        using (var archive = ZipFile.Open(zip, ZipArchiveMode.Create))
        using (var writer = new StreamWriter(archive.CreateEntry("BepInEx/plugins/foreign.dll").Open())) writer.Write("fixture");
        Assert.Throws<InvalidOperationException>(() => new ModInstaller().Install([zip], game));
        Assert.False(File.Exists(Path.Combine(other, "foreign.dll")));
    }

    [Fact]
    public void UninstallCannotFollowAModFolderLinkIntoAnotherGame()
    {
        var game = Game("disk-a"); var other = Game("disk-b");
        Directory.CreateDirectory(Path.Combine(game, "BepInEx"));
        File.WriteAllText(Path.Combine(other, "keep.dll"), "foreign");
        Link(Path.Combine(game, "BepInEx", "plugins"), other);
        File.WriteAllText(Path.Combine(game, "BepInEx", "vrclient-install-manifest.json"),
            JsonSerializer.Serialize(new { files = new[] { "BepInEx/plugins/keep.dll" } }));
        Assert.Throws<InvalidOperationException>(() => new ModInstaller().Uninstall(game));
        Assert.Equal("foreign", File.ReadAllText(Path.Combine(other, "keep.dll")));
    }

    [Fact]
    public void UninstallChecksEveryPathBeforeDeletingAnyFile()
    {
        var game = Game("disk-a");
        Directory.CreateDirectory(Path.Combine(game, "BepInEx", "plugins"));
        var file = Path.Combine(game, "BepInEx", "plugins", "owned.dll");
        File.WriteAllText(file, "keep until all paths are verified");
        File.WriteAllText(Path.Combine(game, "BepInEx", "vrclient-install-manifest.json"),
            JsonSerializer.Serialize(new { files = new[] { "BepInEx/plugins/owned.dll", "../disk-b/foreign.dll" } }));
        Assert.Throws<InvalidOperationException>(() => new ModInstaller().Uninstall(game));
        Assert.True(File.Exists(file));
    }
}
