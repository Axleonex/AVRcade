using System.IO.Compression;
using System.Text;
using System.Text.Json;
using VrClient.Core.Modpack;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkFrameworkUpdateTests : TempTree
{
    private const string Marker = "red4ext/red4ext.dll";
    private string Game => Path.Combine(Root, "game");
    private static string Hash(string value) => Hashing.Sha256OfBytes(Encoding.UTF8.GetBytes(value));
    private static byte[] Zip(params (string Path, string Data)[] files)
    {
        using var stream = new MemoryStream();
        using (var zip = new ZipArchive(stream, ZipArchiveMode.Create, true))
            foreach (var (path, data) in files)
            { using var writer = new StreamWriter(zip.CreateEntry(path).Open()); writer.Write(data); }
        return stream.ToArray();
    }
    private sealed class Downloads(Dictionary<string, byte[]> files) : IHttpDownloader
    {
        public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default) => Task.FromResult(files[url]);
    }
    private (CyberpunkBackendInstaller Installer, Downloads Downloader) Setup(string installed, bool badBackend = false)
    {
        Directory.CreateDirectory(Path.Combine(Game, "red4ext"));
        File.WriteAllText(Path.Combine(Game, Marker), installed);
        File.WriteAllText(Path.Combine(Game, "red4ext/config.ini"), "personal settings");
        var framework = Zip((Marker, "new framework"), ("red4ext/config.ini", "new defaults"));
        var backend = Zip(("r6/scripts/VR/main.reds", "new backend"));
        var path = Path.Combine(Root, "package.json");
        File.WriteAllText(path, JsonSerializer.Serialize(new {
            backend = new { version = "0.1.9", directories = Array.Empty<string>(),
                files = new[] { new { path = "r6/scripts/VR/main.reds", sha256 = Hash("new backend") } },
                upstream_release = new { download_url = "backend", archive_sha256 = Hashing.Sha256OfBytes(backend), legacy_files = Array.Empty<object>() } },
            frameworks = new[] { new { name = "RED4ext", version = "1.31.0", download_url = "framework",
                archive_sha256 = Hashing.Sha256OfBytes(framework), marker = Marker, marker_sha256 = Hash("new framework"),
                legacy_files = new[] { new { path = Marker, sha256 = Hash("old framework") } } } }
        }));
        return (new(path, Root, Path.Combine(Root, "state"), () => false),
            new(new() { ["framework"] = framework, ["backend"] = badBackend ? [1, 2, 3] : backend }));
    }

    [Fact]
    public async Task ExistingOldFrameworkIsUpdatedWithBackupAndPreferencesPreserved()
    {
        var f = Setup("old framework");
        Assert.Single(f.Installer.Plan(Game).ToDownload);
        var result = await f.Installer.InstallAsync(Game, f.Downloader);
        Assert.True(result.Ok, result.Message);
        Assert.Equal("new framework", File.ReadAllText(Path.Combine(Game, Marker)));
        Assert.Equal("personal settings", File.ReadAllText(Path.Combine(Game, "red4ext/config.ini")));
        var record = Assert.Single(f.Installer.ReadRecord()!.Files.Where(x => x.Framework is not null));
        Assert.Equal("old framework", File.ReadAllText(record.BackupPath!));
        Assert.Empty(f.Installer.Plan(Game).ToDownload);
        Assert.Equal("0.1.9", CyberpunkUpstreamSession.ReadState(Game)!.Version);
    }

    [Fact]
    public async Task UnknownVortexFrameworkIsPreservedAndBackendIsNotInstalled()
    {
        var f = Setup("modified or newer framework");
        var result = await f.Installer.InstallAsync(Game, f.Downloader);
        Assert.False(result.Ok);
        Assert.Contains("Vortex", result.Message);
        Assert.Equal("modified or newer framework", File.ReadAllText(Path.Combine(Game, Marker)));
        Assert.Null(CyberpunkUpstreamSession.ReadState(Game));
    }

    [Fact]
    public async Task BackendDownloadFailureRestoresFramework()
    {
        var f = Setup("old framework", badBackend: true);
        var result = await f.Installer.InstallAsync(Game, f.Downloader);
        Assert.False(result.Ok);
        Assert.Equal("old framework", File.ReadAllText(Path.Combine(Game, Marker)));
        Assert.Null(CyberpunkUpstreamSession.ReadState(Game));
    }

    [Fact]
    public async Task ReinstallKeepsTheOriginalFrameworkBackup()
    {
        var f = Setup("old framework");
        Assert.True((await f.Installer.InstallAsync(Game, f.Downloader)).Ok);
        var first = f.Installer.ReadRecord()!.Files.Single(x => x.Framework is not null).BackupPath;
        Assert.True((await f.Installer.InstallAsync(Game, f.Downloader)).Ok);
        Assert.Equal(first, f.Installer.ReadRecord()!.Files.Single(x => x.Framework is not null).BackupPath);
        Assert.Equal("old framework", File.ReadAllText(first!));
    }
}
