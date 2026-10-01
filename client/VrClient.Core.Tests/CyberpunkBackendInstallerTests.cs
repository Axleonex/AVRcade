using System.IO.Compression;
using System.Text;
using System.Text.Json;
using VrClient.Core.Modpack;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

/// Installing and removing the Cyberpunk 2077 VR backend, against a stand-in game
/// folder, a stand-in payload and in-memory framework archives.
public sealed class CyberpunkBackendInstallerTests : TempTree
{
    private const string StereoDll = @"red4ext\plugins\CyberpunkVR_Stereo\CyberpunkVR_Stereo.dll";
    private const string StereoInit = @"bin\x64\plugins\cyber_engine_tweaks\mods\CyberpunkVRPort_Stereo\init.lua";
    private const string BridgeDir = @"bin\x64\plugins\cyber_engine_tweaks\mods\CyberpunkVRPort_Stereo\bridge";
    private const string Red4extMarker = @"red4ext\RED4ext.dll";
    private const string CetMarker = @"bin\x64\plugins\cyber_engine_tweaks.asi";

    private readonly Dictionary<string, byte[]> _archives = new();
    private bool _gameRunning;

    private string Game => Path.Combine(Root, "game");
    private string Payload => Path.Combine(Root, "payload");
    private string State => Path.Combine(Root, "state");
    private string InGame(string relative) => Path.Combine(Game, relative);

    private sealed class MapDownloader(IReadOnlyDictionary<string, byte[]> bytes) : IHttpDownloader
    {
        public List<string> Requested { get; } = [];

        public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default)
        {
            Requested.Add(url);
            return Task.FromResult(bytes[url]);
        }
    }

    private static byte[] Zip(params (string Path, string Content)[] entries)
    {
        using var memory = new MemoryStream();
        using (var archive = new ZipArchive(memory, ZipArchiveMode.Create, leaveOpen: true))
            foreach (var (path, content) in entries)
            {
                using var stream = archive.CreateEntry(path).Open();
                stream.Write(Encoding.UTF8.GetBytes(content));
            }
        return memory.ToArray();
    }

    /// A two-file backend and two frameworks, shaped like the shipped package.
    private CyberpunkBackendInstaller NewInstaller(string? red4extSha256 = null, byte[]? cetArchive = null)
    {
        Directory.CreateDirectory(Game);
        string PayloadFile(string relative, string content)
        {
            var path = Path.Combine(Payload, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, content);
            return Hashing.Sha256OfFile(path);
        }

        _archives["https://example.test/red4ext.zip"] = Zip(
            ("red4ext/RED4ext.dll", "red4ext"), ("bin/x64/winmm.dll", "loader"));
        _archives["https://example.test/cet.zip"] = cetArchive ?? Zip(
            ("bin/x64/plugins/cyber_engine_tweaks.asi", "cet"), ("bin/x64/global.ini", "ini"));

        var package = Path.Combine(Root, "backend.json");
        File.WriteAllText(package, JsonSerializer.Serialize(new
        {
            backend = new
            {
                version = "test-1",
                files = new[]
                {
                    new { path = StereoDll, sha256 = PayloadFile(StereoDll, "stereo") },
                    new { path = StereoInit, sha256 = PayloadFile(StereoInit, "init") }
                },
                directories = new[] { BridgeDir }
            },
            frameworks = new[]
            {
                new
                {
                    name = "RED4ext", version = "1.0", download_url = "https://example.test/red4ext.zip",
                    archive_sha256 = red4extSha256 ?? Hashing.Sha256OfBytes(_archives["https://example.test/red4ext.zip"]),
                    marker = Red4extMarker
                },
                new
                {
                    name = "Cyber Engine Tweaks", version = "2.0", download_url = "https://example.test/cet.zip",
                    archive_sha256 = Hashing.Sha256OfBytes(_archives["https://example.test/cet.zip"]),
                    marker = CetMarker
                }
            }
        }));
        return new CyberpunkBackendInstaller(package, Payload, State, () => _gameRunning);
    }

    [Fact]
    public async Task Install_PutsTheBackendAndItsFrameworksIntoAnUnmoddedGame()
    {
        var installer = NewInstaller();

        var outcome = await installer.InstallAsync(Game, new MapDownloader(_archives));

        Assert.True(outcome.Ok, outcome.Message);
        Assert.Equal("stereo", File.ReadAllText(InGame(StereoDll)));
        Assert.Equal("init", File.ReadAllText(InGame(StereoInit)));
        Assert.True(Directory.Exists(InGame(BridgeDir)));
        Assert.Equal("red4ext", File.ReadAllText(InGame(Red4extMarker)));
        Assert.Equal("cet", File.ReadAllText(InGame(CetMarker)));
        Assert.Equal(6, installer.ReadRecord()!.Files.Count);
        Assert.Empty(installer.Plan(Game).ToDownload);
    }

    [Fact]
    public async Task Install_LeavesAFrameworkThePlayerAlreadyHasCompletelyAlone()
    {
        var installer = NewInstaller();
        Directory.CreateDirectory(Path.GetDirectoryName(InGame(Red4extMarker))!);
        File.WriteAllText(InGame(Red4extMarker), "the player's newer red4ext");
        var downloader = new MapDownloader(_archives);

        var outcome = await installer.InstallAsync(Game, downloader);

        Assert.True(outcome.Ok, outcome.Message);
        Assert.Equal(["https://example.test/cet.zip"], downloader.Requested);
        Assert.Equal("the player's newer red4ext", File.ReadAllText(InGame(Red4extMarker)));
        Assert.False(File.Exists(InGame(@"bin\x64\winmm.dll")));
        Assert.Contains("Kept your existing RED4ext", outcome.Message);
    }

    [Fact]
    public async Task Install_WritesNothingWhenADownloadFailsItsHashCheck()
    {
        var installer = NewInstaller(red4extSha256: new string('0', 64));

        await Assert.ThrowsAsync<HashMismatchException>(
            () => installer.InstallAsync(Game, new MapDownloader(_archives)));

        Assert.Empty(Directory.EnumerateFileSystemEntries(Game));
        Assert.Null(installer.ReadRecord());
    }

    [Fact]
    public async Task Install_IsRefusedWhileTheGameRunsOrWhenThePayloadIsIncomplete()
    {
        var installer = NewInstaller();
        _gameRunning = true;
        Assert.False((await installer.InstallAsync(Game, new MapDownloader(_archives))).Ok);
        _gameRunning = false;

        File.WriteAllText(Path.Combine(Payload, StereoDll), "not the pinned build");
        var outcome = await installer.InstallAsync(Game, new MapDownloader(_archives));

        Assert.False(outcome.Ok);
        Assert.Contains("does not include the complete", outcome.Message);
        Assert.Empty(Directory.EnumerateFileSystemEntries(Game));
    }

    [Fact]
    public async Task Install_RefusesAnArchiveEntryThatLeavesTheGameFolderAndRollsBack()
    {
        var hostile = NewInstaller(cetArchive: Zip(("../outside.txt", "escape")));

        var outcome = await hostile.InstallAsync(Game, new MapDownloader(_archives));

        Assert.False(outcome.Ok);
        Assert.False(File.Exists(Path.Combine(Root, "outside.txt")));
        Assert.False(File.Exists(InGame(Red4extMarker)));
        Assert.Null(hostile.ReadRecord());
    }

    [Fact]
    public async Task Uninstall_RemovesOnlyUnchangedVrFilesAndKeepsTheFrameworks()
    {
        var installer = NewInstaller();
        await installer.InstallAsync(Game, new MapDownloader(_archives));
        File.WriteAllText(InGame(StereoInit), "edited by the player");

        var outcome = installer.Uninstall(Game);

        Assert.True(outcome.Ok, outcome.Message);
        Assert.False(File.Exists(InGame(StereoDll)));
        Assert.False(Directory.Exists(Path.GetDirectoryName(InGame(StereoDll))));
        Assert.Equal("edited by the player", File.ReadAllText(InGame(StereoInit)));
        Assert.True(File.Exists(InGame(Red4extMarker)));
        Assert.True(File.Exists(InGame(CetMarker)));
        Assert.All(installer.ReadRecord()!.Files, file => Assert.NotNull(file.Framework));

        Assert.True(installer.Uninstall(Game, removeFrameworks: true).Ok);
        Assert.False(File.Exists(InGame(Red4extMarker)));
        Assert.False(File.Exists(InGame(CetMarker)));
        Assert.Null(installer.ReadRecord());
    }

    [Fact]
    public async Task Uninstall_RestoresTheFileThatWasThereBeforeAvrcade()
    {
        var installer = NewInstaller();
        Directory.CreateDirectory(Path.GetDirectoryName(InGame(StereoDll))!);
        File.WriteAllText(InGame(StereoDll), "upstream build");

        await installer.InstallAsync(Game, new MapDownloader(_archives));
        Assert.Equal("stereo", File.ReadAllText(InGame(StereoDll)));
        // A second install must not lose the original backup.
        await installer.InstallAsync(Game, new MapDownloader(_archives));

        Assert.True(installer.Uninstall(Game).Ok);
        Assert.Equal("upstream build", File.ReadAllText(InGame(StereoDll)));
    }

    [Fact]
    public void Uninstall_WithoutAnInstallRecordTouchesNothing()
    {
        var installer = NewInstaller();
        Directory.CreateDirectory(Path.GetDirectoryName(InGame(StereoDll))!);
        File.WriteAllText(InGame(StereoDll), "stereo");

        Assert.False(installer.Uninstall(Game).Ok);
        Assert.True(File.Exists(InGame(StereoDll)));
    }

    [Fact]
    public void ShippedPackage_CoversEveryFileTheLaunchCheckRequires()
    {
        var native = Path.Combine(TestRepoRoot.Find(), "config", "redengine", "native");
        var package = CyberpunkBackendInstaller.LoadPackage(Path.Combine(native, "cyberpunk-2077-backend.json"));
        using var profile = JsonDocument.Parse(File.ReadAllText(Path.Combine(native, "cyberpunk-2077.json")));

        var provided = package.Files.Select(file => file.Path)
            .Concat(package.Frameworks.Select(framework => framework.Marker))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var required = profile.RootElement.GetProperty("dependencies").EnumerateArray()
            .Select(item => item.GetProperty("path").GetString()!);
        Assert.All(required, path => Assert.Contains(path, provided));

        Assert.All(package.Files, file => Assert.Matches("^[0-9A-Fa-f]{64}$", file.Sha256));
        Assert.All(package.Frameworks, framework =>
        {
            Assert.Matches("^[0-9A-Fa-f]{64}$", framework.ArchiveSha256);
            Assert.StartsWith("https://github.com/", framework.DownloadUrl);
            Assert.Contains($"/releases/download/v{framework.Version}/", framework.DownloadUrl);
        });
    }
}
