using System.IO.Compression;
using VrClient.Core.Model;
using VrClient.Core.Modpack;

namespace VrClient.Core.Tests;

public sealed class OptionalModInstallerTests
{
    private sealed class BytesDownloader(byte[] bytes) : IHttpDownloader
    {
        public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default) =>
            Task.FromResult(bytes);
    }

    private static byte[] Zip(string path)
    {
        using var stream = new MemoryStream();
        using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, true))
        {
            using var entry = archive.CreateEntry(path).Open();
            entry.Write([1, 2, 3]);
        }
        return stream.ToArray();
    }

    private static string ReadyGame()
    {
        var dir = Path.Combine(Path.GetTempPath(), "avrcade-optional-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(dir, "BepInEx", "plugins"));
        File.WriteAllBytes(Path.Combine(dir, "winhttp.dll"), [1]);
        File.WriteAllBytes(Path.Combine(dir, "BepInEx", "plugins", "Base.dll"), [1]);
        File.WriteAllText(Path.Combine(dir, "BepInEx", "vrclient-install-manifest.json"), "{}");
        return dir;
    }

    private static Lockfile Lock(params LockedPackage[] packages) =>
        new("peak", "peak", "now", packages);

    [Fact]
    public async Task Adds_plugin_without_changing_base_manifest_and_is_idempotent()
    {
        var dir = ReadyGame();
        try
        {
            var bytes = Zip("BepInEx/plugins/Test.dll");
            var package = new LockedPackage("Author", "Extra", "1.0.0",
                "https://gcdn.thunderstore.io/live/repository/packages/extra.zip", "", bytes.Length);
            var installer = new OptionalModInstaller();
            var first = await installer.InstallAsync(dir, Lock(package), Lock(), new BytesDownloader(bytes));
            Assert.Equal(["Author-Extra"], first);
            Assert.True(File.Exists(Path.Combine(dir, "BepInEx", "plugins", "Author-Extra", "Test.dll")));
            Assert.Equal("{}", File.ReadAllText(Path.Combine(dir, "BepInEx", "vrclient-install-manifest.json")));
            Assert.Empty(await installer.InstallAsync(dir, Lock(package), Lock(), new BytesDownloader(bytes)));
        }
        finally { Directory.Delete(dir, true); }
    }

    [Theory]
    [InlineData("BepInEx/patchers/Hack.dll")]
    [InlineData("BepInEx/plugins/../../escape.dll")]
    public async Task Rejects_unsupported_or_unsafe_archive_without_writing(string archivePath)
    {
        var dir = ReadyGame();
        try
        {
            var bytes = Zip(archivePath);
            var package = new LockedPackage("Author", "Extra", "1.0.0",
                "https://gcdn.thunderstore.io/extra.zip", "", bytes.Length);
            await Assert.ThrowsAsync<InvalidOperationException>(() =>
                new OptionalModInstaller().InstallAsync(dir, Lock(package), Lock(), new BytesDownloader(bytes)));
            Assert.False(File.Exists(Path.Combine(dir, "BepInEx", "vrclient-optional-mods.json")));
        }
        finally { Directory.Delete(dir, true); }
    }

    [Fact]
    public async Task Rejects_dependency_version_that_conflicts_with_pinned_vr_conversion()
    {
        var dir = ReadyGame();
        try
        {
            var bytes = Zip("plugins/Test.dll");
            var newer = new LockedPackage("BepInEx", "BepInExPack_PEAK", "6.0.0",
                "https://gcdn.thunderstore.io/extra.zip", "", bytes.Length);
            var pinned = newer with { Version = "5.0.0" };
            await Assert.ThrowsAsync<InvalidOperationException>(() =>
                new OptionalModInstaller().InstallAsync(dir, Lock(newer), Lock(pinned),
                    new BytesDownloader(bytes)));
        }
        finally { Directory.Delete(dir, true); }
    }
}
