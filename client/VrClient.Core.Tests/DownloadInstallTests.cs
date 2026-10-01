using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using VrClient.Core;
using VrClient.Core.Model;
using VrClient.Core.Modpack;
using Xunit;

public class DownloadInstallTests
{
    private static byte[] MakeZip(params (string Name, byte[] Content)[] entries)
    {
        using var ms = new MemoryStream();
        using (var zip = new ZipArchive(ms, ZipArchiveMode.Create, leaveOpen: true))
        {
            foreach (var (name, content) in entries)
            {
                var e = zip.CreateEntry(name);
                using var s = e.Open();
                s.Write(content, 0, content.Length);
            }
        }
        return ms.ToArray();
    }

    private sealed class StubDownloader : IHttpDownloader
    {
        private readonly Dictionary<string, byte[]> _byUrl;
        public StubDownloader(Dictionary<string, byte[]> byUrl) => _byUrl = byUrl;
        public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default)
            => Task.FromResult(_byUrl[url]);
    }

    private static (Lockfile Lockfile, StubDownloader Http) Fixture()
    {
        var packZip = MakeZip(
            ("BepInExPack/winhttp.dll", new byte[] { 1, 2, 3 }),
            ("BepInExPack/doorstop_config.ini", System.Text.Encoding.ASCII.GetBytes("[General]")),
            ("BepInExPack/BepInEx/core/BepInEx.dll", new byte[] { 4, 5 }));
        var modZip = MakeZip(
            ("BepInEx/plugins/RepoXR/LCVR.dll", new byte[] { 6, 7, 8 }),
            ("BepInEx/config/README.txt", System.Text.Encoding.ASCII.GetBytes("readme")));
        var lockfile = new Lockfile("repo", "repo", "2026-07-02T00:00:00Z", new List<LockedPackage>
        {
            new("BepInEx", "BepInExPack", "5.4.2100", "https://example.invalid/pack.zip",
                Hashing.Sha256OfBytes(packZip), packZip.Length),
            new("DaXcess", "RepoXR", "1.2.2", "https://example.invalid/mod.zip",
                Hashing.Sha256OfBytes(modZip), modZip.Length)
        });
        var http = new StubDownloader(new Dictionary<string, byte[]>
        {
            ["https://example.invalid/pack.zip"] = packZip,
            ["https://example.invalid/mod.zip"] = modZip
        });
        return (lockfile, http);
    }

    private static string NewTempDir()
    {
        var dir = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        Directory.CreateDirectory(dir);
        return dir;
    }

    [Fact]
    public async Task Download_returns_two_verified_paths()
    {
        var (lockfile, http) = Fixture();
        var cache = NewTempDir();
        try
        {
            var paths = await new PackageDownloader(http).DownloadAllAsync(lockfile, cache);
            Assert.Equal(2, paths.Count);
            Assert.All(paths, p => Assert.True(File.Exists(p)));
        }
        finally { Directory.Delete(cache, recursive: true); }
    }

    [Fact]
    public async Task Download_throws_HashMismatch_on_wrong_lockfile_sha()
    {
        var (lockfile, http) = Fixture();
        var wrong = new string('f', 64);
        var tampered = lockfile with
        {
            Packages = new List<LockedPackage>
            {
                lockfile.Packages[0] with { Sha256 = wrong },
                lockfile.Packages[1]
            }
        };
        var cache = NewTempDir();
        try
        {
            var ex = await Assert.ThrowsAsync<HashMismatchException>(
                () => new PackageDownloader(http).DownloadAllAsync(tampered, cache));
            Assert.Contains("expected=", ex.Message);
            Assert.Contains("actual=", ex.Message);
            Assert.Contains("BepInEx-BepInExPack-5.4.2100", ex.Message);
        }
        finally { Directory.Delete(cache, recursive: true); }
    }

    [Fact]
    public async Task Install_places_winhttp_next_to_exe_and_plugin_under_BepInEx()
    {
        var (lockfile, http) = Fixture();
        var cache = NewTempDir();
        var gameDir = NewTempDir();
        try
        {
            File.WriteAllBytes(Path.Combine(gameDir, "REPO.exe"), Array.Empty<byte>());
            var zips = await new PackageDownloader(http).DownloadAllAsync(lockfile, cache);
            var result = new ModInstaller().Install(zips, gameDir);

            Assert.True(result.Installed);
            Assert.Null(result.RefusalReason);
            Assert.True(File.Exists(Path.Combine(gameDir, "winhttp.dll")));
            Assert.True(File.Exists(Path.Combine(gameDir, "BepInEx", "plugins", "RepoXR", "LCVR.dll")));
            Assert.True(File.Exists(Path.Combine(gameDir, "BepInEx", "vrclient-install-manifest.json")));
        }
        finally
        {
            Directory.Delete(cache, recursive: true);
            Directory.Delete(gameDir, recursive: true);
        }
    }

    [Fact]
    public void Install_maps_peak_and_big_walk_archive_layouts()
    {
        var cache = NewTempDir();
        var gameDir = NewTempDir();
        try
        {
            File.WriteAllBytes(Path.Combine(gameDir, "PEAK.exe"), []);
            var loader = Path.Combine(cache, "loader.zip");
            var peak = Path.Combine(cache, "peak.zip");
            var bigWalk = Path.Combine(cache, "big-walk.zip");
            File.WriteAllBytes(loader, MakeZip(
                ("BepInExPack_PEAK/winhttp.dll", [1]),
                ("BepInExPack_PEAK/BepInEx/core/BepInEx.dll", [2]),
                ("manifest.json", [9])));
            File.WriteAllBytes(peak, MakeZip(
                ("plugins/PeakVR.dll", [3]),
                ("patchers/LCVR.Preload.dll", [4]),
                ("README.md", [9])));
            File.WriteAllBytes(bigWalk, MakeZip(
                (@"BepInEx\plugins\BigWalkVR\BigWalkVR.dll", [5]),
                (@"BepInEx\patchers\BigWalkVR.Preloader.dll", [6])));

            var result = new ModInstaller().Install([loader, peak, bigWalk], gameDir, ["PEAK.exe"]);

            Assert.True(result.Installed);
            Assert.True(new ModInstaller().IsInstalled(gameDir));
            Assert.True(File.Exists(Path.Combine(gameDir, "BepInEx", "plugins", "PeakVR.dll")));
            Assert.True(File.Exists(Path.Combine(gameDir, "BepInEx", "patchers", "LCVR.Preload.dll")));
            Assert.True(File.Exists(Path.Combine(gameDir, "BepInEx", "plugins", "BigWalkVR", "BigWalkVR.dll")));
            Assert.False(File.Exists(Path.Combine(gameDir, "README.md")));
            Assert.False(File.Exists(Path.Combine(gameDir, "manifest.json")));
        }
        finally
        {
            Directory.Delete(cache, recursive: true);
            Directory.Delete(gameDir, recursive: true);
        }
    }

    [Fact]
    public void Uninstall_big_walk_removes_matching_preloader_copies_but_preserves_prior_files()
    {
        var cache = NewTempDir();
        var gameDir = NewTempDir();
        try
        {
            File.WriteAllBytes(Path.Combine(gameDir, "Big Walk.exe"), []);
            File.WriteAllBytes(Path.Combine(gameDir, "phonon.dll"), [2]);
            var archive = Path.Combine(cache, "big-walk.zip");
            File.WriteAllBytes(archive, MakeZip(
                (@"BepInEx\patchers\BigWalkVR.Runtime\openvr_api.dll", [1]),
                (@"BepInEx\patchers\BigWalkVR.Runtime\phonon.dll", [2]),
                (@"BepInEx\patchers\BigWalkVR.Preloader.dll", [3]),
                (@"BepInEx\plugins\BigWalkVR\BigWalkVR.dll", [4])));
            var installer = new ModInstaller();
            Assert.True(installer.Install([archive], gameDir, ["Big Walk.exe"]).Installed);
            File.WriteAllBytes(Path.Combine(gameDir, "openvr_api.dll"), [1]);

            installer.Uninstall(gameDir);

            Assert.False(File.Exists(Path.Combine(gameDir, "openvr_api.dll")));
            Assert.Equal(new byte[] { 2 }, File.ReadAllBytes(Path.Combine(gameDir, "phonon.dll")));
        }
        finally
        {
            Directory.Delete(cache, recursive: true);
            Directory.Delete(gameDir, recursive: true);
        }
    }

    [Fact]
    public async Task Install_refuses_without_REPO_exe()
    {
        var (lockfile, http) = Fixture();
        var cache = NewTempDir();
        var gameDir = NewTempDir();
        try
        {
            var zips = await new PackageDownloader(http).DownloadAllAsync(lockfile, cache);
            var result = new ModInstaller().Install(zips, gameDir);
            Assert.False(result.Installed);
            Assert.NotNull(result.RefusalReason);
        }
        finally
        {
            Directory.Delete(cache, recursive: true);
            Directory.Delete(gameDir, recursive: true);
        }
    }

    [Fact]
    public async Task IsInstalled_true_after_install_false_after_uninstall()
    {
        var (lockfile, http) = Fixture();
        var cache = NewTempDir();
        var gameDir = NewTempDir();
        try
        {
            File.WriteAllBytes(Path.Combine(gameDir, "REPO.exe"), Array.Empty<byte>());
            var installer = new ModInstaller();
            var zips = await new PackageDownloader(http).DownloadAllAsync(lockfile, cache);
            Assert.True(installer.Install(zips, gameDir).Installed);
            Assert.True(installer.IsInstalled(gameDir));

            installer.Uninstall(gameDir);
            Assert.False(installer.IsInstalled(gameDir));
            Assert.False(File.Exists(Path.Combine(gameDir, "winhttp.dll")));
        }
        finally
        {
            Directory.Delete(cache, recursive: true);
            Directory.Delete(gameDir, recursive: true);
        }
    }

    [Fact]
    public void Install_rejects_zip_slip_entry()
    {
        var gameDir = NewTempDir();
        var cache = NewTempDir();
        try
        {
            File.WriteAllBytes(Path.Combine(gameDir, "REPO.exe"), Array.Empty<byte>());
            var evilZip = MakeZip((@"BepInEx\..\..\evil.txt", new byte[] { 9 }));
            var zipPath = Path.Combine(cache, "evil.zip");
            File.WriteAllBytes(zipPath, evilZip);

            var ex = Assert.Throws<InvalidOperationException>(
                () => new ModInstaller().Install(new[] { zipPath }, gameDir));
            Assert.Contains("evil.txt", ex.Message);
            Assert.False(File.Exists(Path.Combine(Path.GetDirectoryName(gameDir)!, "evil.txt")));
        }
        finally
        {
            Directory.Delete(cache, recursive: true);
            Directory.Delete(gameDir, recursive: true);
        }
    }

    [Fact]
    public void Uninstall_rejects_manifest_path_outside_game_directory()
    {
        var gameDir = NewTempDir();
        try
        {
            var manifest = Path.Combine(gameDir, "BepInEx", "vrclient-install-manifest.json");
            Directory.CreateDirectory(Path.GetDirectoryName(manifest)!);
            File.WriteAllText(manifest, "{\"files\":[\"../outside.txt\"]}");

            Assert.Throws<InvalidOperationException>(() => new ModInstaller().Uninstall(gameDir));
            Assert.True(File.Exists(manifest));
        }
        finally
        {
            Directory.Delete(gameDir, recursive: true);
        }
    }
}
