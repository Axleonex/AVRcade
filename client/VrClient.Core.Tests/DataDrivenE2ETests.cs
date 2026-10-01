using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using VrClient.Core;
using VrClient.Core.Config;
using VrClient.Core.Launch;
using VrClient.Core.Model;
using VrClient.Core.Modpack;
using VrClient.Core.Safety;
using Xunit;

/// Proves the "any game is data" claim: a brand-new synthetic game (demo-coop)
/// flows through the identical M1 Core code paths as R.E.P.O., driven ONLY by
/// fixture data. No new non-test .cs is created by this phase. If any step needed
/// a new Core method, the pipeline would not be data-driven — that is the thing
/// this test exists to catch.
public class DataDrivenE2ETests
{
    private static string Fx(params string[] parts)
        => Path.Combine(new[] { AppContext.BaseDirectory, "Fixtures", "demo-coop" }.Concat(parts).ToArray());

    private static byte[] MakeZip(params (string Name, byte[] Content)[] entries)
    {
        using var ms = new MemoryStream();
        using (var zip = new ZipArchive(ms, ZipArchiveMode.Create, leaveOpen: true))
            foreach (var (name, content) in entries)
            {
                var e = zip.CreateEntry(name);
                using var s = e.Open();
                s.Write(content, 0, content.Length);
            }
        return ms.ToArray();
    }

    private sealed class FixtureApi : IThunderstoreApi
    {
        private readonly string _json;
        public FixtureApi(string json) => _json = json;
        public Task<string> GetPackageIndexJsonAsync(string community, CancellationToken ct = default)
            => Task.FromResult(_json);
    }

    private sealed class StubDownloader : IHttpDownloader
    {
        private readonly Dictionary<string, byte[]> _byUrl;
        public StubDownloader(Dictionary<string, byte[]> byUrl) => _byUrl = byUrl;
        public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default)
            => Task.FromResult(_byUrl[url]);
    }

    [Fact]
    public async Task Demo_coop_runs_end_to_end_on_M1_core_with_no_new_code()
    {
        // Step 1: LoadSpec from the demo-coop modpack fixture -> 2 mods.
        var spec = ModpackResolver.LoadSpec(Fx("config", "modpacks", "demo-coop.modpack.json"));
        Assert.Equal("demo-coop", spec.GameSlug);
        Assert.Equal(2, spec.Mods.Count);

        // Step 2: Resolve against the fixture Thunderstore index -> dep-first lockfile.
        var indexJson = File.ReadAllText(Fx("thunderstore-index.json"));
        var resolver = new ModpackResolver(new FixtureApi(indexJson));
        var lockfile = await resolver.ResolveAsync(spec, "2026-07-08T00:00:00Z");
        Assert.Equal(2, lockfile.Packages.Count);
        Assert.Equal("BepInExPack", lockfile.Packages[0].Name);
        Assert.Equal("DemoVR", lockfile.Packages[1].Name);

        // Step 3: Build in-memory zips matching the M1 installer layout, fill lockfile
        // sha256 from their bytes, and download+verify them through the stub.
        var packZip = MakeZip(
            ("BepInExPack/winhttp.dll", new byte[] { 1, 2, 3 }),
            ("BepInExPack/BepInEx/core/BepInEx.dll", new byte[] { 4, 5 }));
        var modZip = MakeZip(
            ("BepInEx/plugins/DemoVR/Demo.dll", new byte[] { 6, 7, 8 }));
        var filled = lockfile with
        {
            Packages = new List<LockedPackage>
            {
                lockfile.Packages[0] with { Sha256 = Hashing.Sha256OfBytes(packZip), SizeBytes = packZip.Length },
                lockfile.Packages[1] with { Sha256 = Hashing.Sha256OfBytes(modZip), SizeBytes = modZip.Length }
            }
        };
        var http = new StubDownloader(new Dictionary<string, byte[]>
        {
            [filled.Packages[0].DownloadUrl] = packZip,
            [filled.Packages[1].DownloadUrl] = modZip
        });

        var cache = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        var gameDir = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        Directory.CreateDirectory(cache);
        Directory.CreateDirectory(gameDir);
        try
        {
            var zips = await new PackageDownloader(http).DownloadAllAsync(filled, cache);
            Assert.Equal(2, zips.Count);

            // Step 4: Install into a dir containing the demo game's exe (data-driven exe name).
            File.WriteAllBytes(Path.Combine(gameDir, "DemoCoop.exe"), Array.Empty<byte>());
            var install = new ModInstaller().Install(zips, gameDir, new[] { "DemoCoop.exe" });
            Assert.True(install.Installed);
            Assert.True(File.Exists(Path.Combine(gameDir, "winhttp.dll")));
            Assert.True(new ModInstaller().IsInstalled(gameDir));

            // Step 5: Safety verdict from the demo game config + demo rules -> Warn
            // (known_safe + private_modded_coop in allowed modes — same table as R.E.P.O.).
            var verdict = new SafetyGate().Evaluate(
                Fx("config", "games", "demo-coop.json"),
                Fx("config", "safety", "demo-rules.json"));
            Assert.Equal(Verdict.Warn, verdict.Verdict);

            // Step 6: Launch plan gate passes with acknowledge; dryRun returns -1, starts nothing.
            var launcher = new GameLauncher();
            var plan = launcher.Plan(gameDir, verdict, modInstalled: true);
            var pid = launcher.Launch(plan, acknowledgeWarn: true, dryRun: true);
            Assert.Equal(-1, pid);
        }
        finally
        {
            if (Directory.Exists(cache)) Directory.Delete(cache, recursive: true);
            if (Directory.Exists(gameDir)) Directory.Delete(gameDir, recursive: true);
        }
    }
}
