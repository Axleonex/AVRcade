using System.IO.Compression;
using System.Text;
using VrClient.Core.Modpack;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkUpstreamSessionTests : TempTree
{
    private const string Stereo = "red4ext/plugins/CyberpunkVR_Stereo/CyberpunkVR_Stereo.dll";
    private const string Hands = "red4ext/plugins/CyberpunkVR_Hands/CyberpunkVR_Hands.dll";
    private const string Script = "r6/scripts/CyberpunkVRPort_NoAnims/vrport_no_anims.reds";
    private const string Camera = "bin/x64/plugins/cyber_engine_tweaks/mods/CyberpunkVRPort_Stereo/vrcam.json";
    private string Game => Path.Combine(Root, "game");
    private string At(string path) => CyberpunkUpstreamSession.Resolve(Game, path);
    private static string Hash(string text) => Hashing.Sha256OfBytes(Encoding.UTF8.GetBytes(text));
    private sealed class Download(byte[] archive) : IHttpDownloader
    {
        public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default) => Task.FromResult(archive);
    }
    private (CyberpunkBackendPackage Package, Download Download) Fixture()
    {
        Directory.CreateDirectory(Game);
        using var memory = new MemoryStream();
        using (var zip = new ZipArchive(memory, ZipArchiveMode.Create, true))
            foreach (var (path, content) in new[] { (Stereo, "new renderer"), (Script, "new comfort script"),
                         (Camera, "{\"component\":\"vrcam_1400\"}") })
            {
                using var writer = new StreamWriter(zip.CreateEntry(path).Open());
                writer.Write(content);
            }
        var bytes = memory.ToArray();
        return (new CyberpunkBackendPackage("0.1.7",
            [new(Stereo, Hash("new renderer")), new(Script, Hash("new comfort script")),
             new(Camera, Hash("{\"component\":\"vrcam_1400\"}"))], [], [],
            new("https://example.test/release.zip", Hashing.Sha256OfBytes(bytes),
                [new(Stereo, Hash("old renderer")), new(Hands, Hash("old hands"))])), new Download(bytes));
    }
    private void Put(string path, string contents)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(At(path))!);
        File.WriteAllText(At(path), contents);
    }

    [Fact]
    public async Task MigrationParksBothOldPluginsAndVrSessionReturnsToFlat()
    {
        var f = Fixture();
        Put(Stereo, "old renderer");
        Put(Hands, "old hands");
        Put("bin/x64/vrport.ini", "xr_mono_submit=1\nxr_snap_turn=1\n");
        Put(Camera, "{\"component\":\"vrcam_1024\"}");
        await CyberpunkUpstreamInstaller.InstallAsync(Game, f.Package, f.Download, default);
        Assert.DoesNotContain("xr_mono_submit", File.ReadAllText(At("bin/x64/vrport.ini")));
        Assert.Contains("xr_snap_turn=1", File.ReadAllText(At("bin/x64/vrport.ini")));
        Assert.False(File.Exists(At(Stereo)));
        Assert.False(File.Exists(At(Hands)));
        Assert.True(CyberpunkUpstreamSession.HasDependency(Game, Stereo));
        using (var session = new CyberpunkUpstreamSession(Game, vr: true))
        {
            Assert.Equal("new renderer", File.ReadAllText(At(Stereo)));
            Assert.True(File.Exists(At(Script)));
            Assert.False(File.Exists(At(Hands)));
            Assert.Throws<IOException>(() => new CyberpunkUpstreamSession(Game, vr: true));
        }
        Assert.False(File.Exists(At(Stereo)));
        Assert.False(File.Exists(At(Script)));
        CyberpunkUpstreamInstaller.RestorePrevious(Game);
        Assert.Equal("old renderer", File.ReadAllText(At(Stereo)));
        Assert.Equal("old hands", File.ReadAllText(At(Hands)));
        Assert.Contains("xr_mono_submit=1", File.ReadAllText(At("bin/x64/vrport.ini")));
        Assert.Contains("vrcam_1024", File.ReadAllText(At(Camera)));
    }

    [Fact]
    public async Task UnknownOldPluginIsPreservedAndMigrationIsRefused()
    {
        var f = Fixture();
        Put(Hands, "user modified hands");
        await Assert.ThrowsAsync<IOException>(() =>
            CyberpunkUpstreamInstaller.InstallAsync(Game, f.Package, f.Download, default));
        Assert.Equal("user modified hands", File.ReadAllText(At(Hands)));
        Assert.Null(CyberpunkUpstreamSession.ReadState(Game));
    }

    [Fact]
    public async Task BadDownloadCannotRetireTheWorkingPlugin()
    {
        var f = Fixture();
        Put(Stereo, "old renderer");
        await Assert.ThrowsAsync<HashMismatchException>(() =>
            CyberpunkUpstreamInstaller.InstallAsync(Game, f.Package, new Download([1, 2, 3]), default));
        Assert.Equal("old renderer", File.ReadAllText(At(Stereo)));
        Assert.Null(CyberpunkUpstreamSession.ReadState(Game));
    }

    [Fact]
    public async Task VortexRedeploymentIsNotOverwrittenByVrLaunch()
    {
        var f = Fixture();
        await CyberpunkUpstreamInstaller.InstallAsync(Game, f.Package, f.Download, default);
        Put(Stereo, "Vortex deployment");
        Assert.Throws<IOException>(() => new CyberpunkUpstreamSession(Game, vr: true));
        Assert.Equal("Vortex deployment", File.ReadAllText(At(Stereo)));
        Assert.False(File.Exists(At(Script)));
    }
}
