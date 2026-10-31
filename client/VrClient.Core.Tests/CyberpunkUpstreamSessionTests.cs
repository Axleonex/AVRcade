using System.IO.Compression;
using System.Text;
using VrClient.Core.Modpack;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkUpstreamSessionTests : TempTree
{
    [Fact]
    public async Task RepairRecoversInterruptedActivationWithoutADownload()
    {
        var f = Fixture();
        await CyberpunkUpstreamInstaller.InstallAsync(Game, f.Package, f.Download, default);
        Directory.CreateDirectory(Path.GetDirectoryName(At(Stereo))!);
        File.Copy(CyberpunkUpstreamSession.PayloadPath(Game, Stereo), At(Stereo));
        var journal = Path.Combine(CyberpunkUpstreamSession.Store(Game), "active.json");
        File.WriteAllText(journal, System.Text.Json.JsonSerializer.Serialize(new[] { Stereo }));
        Assert.False(await CyberpunkUpstreamInstaller.RepairAsync(Game, f.Package, new Download([1, 2, 3])));
        Assert.False(File.Exists(At(Stereo)));
        Assert.Equal("[]", File.ReadAllText(journal));
    }

    [Fact]
    public async Task AutomaticRepairCannotUpgradeAnUnreviewedRelease()
    {
        var f = Fixture();
        await CyberpunkUpstreamInstaller.InstallAsync(Game, f.Package, f.Download, default);
        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            CyberpunkUpstreamInstaller.RepairAsync(Game, f.Package with { Version = "future-version" }, f.Download));
        Assert.Equal(f.Package.Version, CyberpunkUpstreamSession.ReadState(Game)!.Version);
    }

    [Fact]
    public async Task SameVersionRepairRestoresMissingScriptAndChangedDllButKeepsCameraChoice()
    {
        var f = Fixture(withPreferences: true);
        await CyberpunkUpstreamInstaller.InstallAsync(Game, f.Package, f.Download, default);
        File.Delete(CyberpunkUpstreamSession.PayloadPath(Game, Script));
        File.WriteAllText(CyberpunkUpstreamSession.PayloadPath(Game, Stereo), "damaged renderer");
        var camera = CyberpunkUpstreamSession.PayloadPath(Game, Camera);
        File.WriteAllText(camera, "{\"component\":\"vrcam_1024\"}");
        var grip = CyberpunkUpstreamSession.PayloadPath(Game, "bin/x64/grip.ini");
        File.WriteAllText(grip, "personal grip offsets");
        var settings = CyberpunkUpstreamSession.PayloadPath(Game, "red4ext/plugins/CyberpunkVR_Stereo/UserSettings.json");
        File.WriteAllText(settings, "{\"snap_degrees\":45}");
        Assert.Equal(2, CyberpunkUpstreamInstaller.FindDamagedFiles(Game, f.Package).Count);

        Assert.True(await CyberpunkUpstreamInstaller.RepairAsync(Game, f.Package, f.Download));
        Assert.Empty(CyberpunkUpstreamInstaller.FindDamagedFiles(Game, f.Package));
        Assert.Contains("vrcam_1024", File.ReadAllText(camera));
        Assert.Equal("personal grip offsets", File.ReadAllText(grip));
        Assert.Equal("{\"snap_degrees\":45}", File.ReadAllText(settings));
        var backup = CyberpunkUpstreamSession.Resolve(Game,
            CyberpunkUpstreamSession.StoreDirectory + "/repair-backups/" + Hash("damaged renderer") + "/" + Stereo);
        Assert.Equal("damaged renderer", File.ReadAllText(backup));
        Assert.False(File.Exists(At(Stereo)));
        Assert.False(await CyberpunkUpstreamInstaller.RepairAsync(Game, f.Package, new Download([1, 2, 3])));
    }

    [Fact]
    public async Task FailedRepairPreservesStoredFilesAndManagerDeployment()
    {
        var f = Fixture();
        await CyberpunkUpstreamInstaller.InstallAsync(Game, f.Package, f.Download, default);
        var stored = CyberpunkUpstreamSession.PayloadPath(Game, Stereo);
        File.WriteAllText(stored, "damaged renderer");
        await Assert.ThrowsAsync<HashMismatchException>(() =>
            CyberpunkUpstreamInstaller.RepairAsync(Game, f.Package, new Download([1, 2, 3])));
        Assert.Equal("damaged renderer", File.ReadAllText(stored));
        Put(Stereo, "Vortex deployment");
        await Assert.ThrowsAsync<IOException>(() => CyberpunkUpstreamInstaller.RepairAsync(Game, f.Package, f.Download));
        Assert.Equal("Vortex deployment", File.ReadAllText(At(Stereo)));
        Assert.Equal("damaged renderer", File.ReadAllText(stored));
    }

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
    private (CyberpunkBackendPackage Package, Download Download) Fixture(bool withPreferences = false)
    {
        Directory.CreateDirectory(Game);
        using var memory = new MemoryStream();
        using (var zip = new ZipArchive(memory, ZipArchiveMode.Create, true))
        {
            foreach (var (path, content) in new[] { (Stereo, "new renderer"), (Script, "new comfort script"),
                         (Camera, "{\"component\":\"vrcam_1400\"}") })
            {
                using var writer = new StreamWriter(zip.CreateEntry(path).Open());
                writer.Write(content);
            }
            if (withPreferences)
            {
                using (var writer = new StreamWriter(zip.CreateEntry("bin/x64/grip.ini").Open()))
                    writer.Write("default grip offsets");
                using (var writer = new StreamWriter(zip.CreateEntry("red4ext/plugins/CyberpunkVR_Stereo/UserSettings.json").Open()))
                    writer.Write("{}");
            }
        }
        var bytes = memory.ToArray();
        return (new CyberpunkBackendPackage("0.1.7",
            [new(Stereo, Hash("new renderer")), new(Script, Hash("new comfort script")),
             new(Camera, Hash("{\"component\":\"vrcam_1400\"}")),
             .. (withPreferences ? new CyberpunkBackendFile[] { new("bin/x64/grip.ini", Hash("default grip offsets")),
                 new("red4ext/plugins/CyberpunkVR_Stereo/UserSettings.json", Hash("{}")) } : [])], [], [],
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
