using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Launch;
using VrClient.Core.Model;
using Xunit;

public class AppControllerTests
{
    [Theory]
    [InlineData(Verdict.Allow, false)]
    [InlineData(Verdict.Warn, false)]
    [InlineData(Verdict.Block, true)]
    [InlineData(Verdict.UnknownBlocked, true)]
    public void Both_blocking_verdicts_count_as_blocked(Verdict verdict, bool blocked)
        => Assert.Equal(blocked, verdict.IsBlocked());

    [Theory]
    [InlineData(false, false, false, false, false, false, false, false, GameReadiness.NeedsGame)]
    [InlineData(true, false, false, true, true, false, false, false, GameReadiness.NeedsUevr)]
    [InlineData(true, false, true, true, false, false, false, false, GameReadiness.NeedsHeadset)]
    [InlineData(true, false, true, true, true, false, false, false, GameReadiness.ProfileUnverified)]
    [InlineData(true, false, true, true, true, true, true, true, GameReadiness.Ready)]
    [InlineData(true, true, true, true, true, true, true, true, GameReadiness.Blocked)]
    public void Unreal_readiness_is_driven_by_independent_prerequisite_facts(
        bool installed, bool blocked, bool uevr, bool dotnet, bool headset,
        bool injection, bool headsetT1, bool profile, GameReadiness expected)
        => Assert.Equal(expected, AppController.ComputeUnrealReadiness(
            installed, blocked, uevr, dotnet, headset, injection, headsetT1, profile));

    [Fact]
    public void ListGames_merges_unreal_and_surfaces_missing_uevr()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        var unreal = Path.Combine(root, "config", "unreal");
        var safety = Path.Combine(root, "config", "safety");
        Directory.CreateDirectory(unreal);
        Directory.CreateDirectory(safety);
        try
        {
            File.WriteAllText(Path.Combine(unreal, "uevr-release.json"), """
                { "pinned_tag":"1", "download_url":"https://example/uevr.zip", "sha256":"aa",
                  "injector":{"exe":"UEVRInjector.exe"},
                  "nightly_channel":{"pinned_tag":"n1", "download_url":"https://example/n.zip", "sha256":"bb"} }
                """);
            File.WriteAllText(Path.Combine(unreal, "dotnet-desktop-runtime.json"), """
                { "version":"8.0.29",
                  "runtime_download_url":"https://example/runtime.zip", "runtime_sha512":"cc",
                  "desktop_download_url":"https://example/desktop.zip", "desktop_sha512":"dd" }
                """);
            File.WriteAllText(Path.Combine(unreal, "probe.uevr.json"), """
                { "game_slug":"probe", "game_id":"probe", "display_name":"Probe Unreal", "steam_app_id":"42",
                  "uevr_channel":"nightly",
                  "executable_roles":{"launcher_stub":"Probe.exe", "shipping_binary":"Binaries/Probe-Win64-Shipping.exe"},
                  "support_policy":{"anti_cheat_risk":"none", "online_risk":"private_modded_coop"},
                  "verification":{"injection_stable":false,"headset_t1":false,"profile_status":"unverified"} }
                """);
            File.WriteAllText(Path.Combine(safety, "default-rules.json"), """
                { "rules":[{"game_id":"probe","allowed_launch_modes":["private_modded_coop"]}] }
                """);

            var controller = new AppController(
                root, (_, _) => @"C:\Games\Probe", _ => false, _ => true, () => true);
            var game = Assert.Single(controller.ListGames());

            Assert.Equal(GameEngine.Unreal, game.Engine);
            Assert.Equal(GameReadiness.NeedsUevr, game.Readiness);
            Assert.Equal("Probe Unreal", game.DisplayName);
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void ListGames_reads_demo_coop_fixture_not_installed_when_locator_returns_null()
    {
        var repoRoot = Path.Combine(System.AppContext.BaseDirectory, "Fixtures", "demo-coop");
        var controller = new AppController(repoRoot, (_, _) => null);
        var games = controller.ListGames();

        var demo = Assert.Single(games);
        Assert.Equal("demo-coop", demo.Slug);
        Assert.Equal("Demo Co-op", demo.DisplayName);
        Assert.Null(demo.InstallDir);
        Assert.False(demo.ModInstalled);
        Assert.Contains(demo.Readiness, new[] { GameReadiness.NeedsGame, GameReadiness.Blocked });
    }

    [Fact]
    public void GetRuntimeStatus_reports_injected_process_state()
    {
        var repoRoot = Path.Combine(System.AppContext.BaseDirectory, "Fixtures", "demo-coop");
        var controller = new AppController(repoRoot, (_, _) => null);
        var steamVr = new XrRuntimeChoice(OpenXrRuntimeSelector.SteamVrName, @"C:\steam\steamxr_win64.json");

        var status = controller.GetRuntimeStatus(
            new[] { "vrserver" }, new List<XrRuntimeChoice> { steamVr });

        Assert.True(status.SteamVrRunning);
        Assert.False(status.VirtualDesktopRunning);
        Assert.Equal(OpenXrRuntimeSelector.SteamVrName, status.SelectedRuntime);
    }

    [Fact]
    public void Rv_there_yet_readiness_reports_capabilities_without_overclaiming_motion_or_coop()
    {
        var root = AppContext.BaseDirectory;
        while (root is not null && !File.Exists(Path.Combine(root, "config", "unreal", "rv-there-yet.uevr.json")))
            root = Path.GetDirectoryName(root);
        Assert.NotNull(root);
        var controller = new AppController(
            root!, (app, _) => app == "3949040" ? @"C:\Games\RV There Yet" : null,
            _ => true, _ => true, () => true);

        var game = Assert.Single(controller.ListGames().Where(item => item.Slug == "rv-there-yet"));

        Assert.Equal(GameReadiness.ProfileUnverified, game.Readiness);
        Assert.Contains("VDXR has displayed the main menu", game.ReadinessDetail);
        Assert.Contains("Tracked hands are not implemented", game.ReadinessDetail);
        Assert.Contains("Private co-op is untested", game.ReadinessDetail);
    }

    [Fact]
    public void ListGames_surfaces_installed_redengine_backend_as_headset_validation()
    {
        using var fixture = AppRedengineFixture.Create();
        var controller = new AppController(
            fixture.Root,
            (app, _) => app == "1091500" ? fixture.GameRoot : null,
            headsetConnected: () => true);

        var game = Assert.Single(controller.ListGames());

        Assert.Equal(GameEngine.Redengine, game.Engine);
        Assert.Equal(GameReadiness.ProfileUnverified, game.Readiness);
        Assert.True(game.ModInstalled);
        Assert.Equal("Cyberpunk 2077", game.DisplayName);
    }

    [Fact]
    public void ListGames_hides_rdr2_while_preserving_its_profile()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-app-rdr2-{Guid.NewGuid():N}");
        var rage = Path.Combine(root, "config", "rage");
        Directory.CreateDirectory(rage);
        try
        {
            File.WriteAllText(Path.Combine(rage, "rdr2.json"), """
                { "game":{"slug":"red-dead-redemption-2","display_name":"Red Dead Redemption 2","steam_app_id":"1174180","launcher_executable":"RDR2.exe","steam_buildid_observed":null,"executable_sha256_observed":null},
                  "safety":{"review_status":"unknown"} }
                """);
            Assert.Empty(new AppController(root, (_, _) => null, headsetConnected: () => false).ListGames());
            Assert.True(File.Exists(Path.Combine(rage, "rdr2.json")));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void ListGames_hides_future_rdr1_modpack()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-app-rdr1-{Guid.NewGuid():N}");
        var modpacks = Path.Combine(root, "config", "modpacks");
        Directory.CreateDirectory(modpacks);
        try
        {
            File.WriteAllText(Path.Combine(modpacks, "red-dead-redemption.modpack.json"), """
                { "schema":"modpack-spec/1", "game_slug":"red-dead-redemption",
                  "steam_app_id":"2668510", "community":"red-dead-redemption", "mods":[] }
                """);
            Assert.Empty(new AppController(root, (_, _) => null, headsetConnected: () => false).ListGames());
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void ListGames_hides_unpublished_fallout_adapters()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-app-fnv-{Guid.NewGuid():N}");
        var newVegasAdapterDirectory = Path.Combine(root, "adapters", "fallout_new_vegas");
        var fallout3AdapterDirectory = Path.Combine(root, "adapters", "fallout_3");
        Directory.CreateDirectory(newVegasAdapterDirectory);
        Directory.CreateDirectory(fallout3AdapterDirectory);
        try
        {
            File.WriteAllText(Path.Combine(newVegasAdapterDirectory, "adapter.json"), "{}");
            File.WriteAllText(Path.Combine(fallout3AdapterDirectory, "adapter.json"), "{}");
            var controller = new AppController(
                root, (_, _) => null, headsetConnected: () => false,
                falloutNewVegasDiscovery: () => throw new InvalidOperationException("New Vegas discovery should be hidden."),
                fallout3Discovery: () => throw new InvalidOperationException("Fallout 3 discovery should be hidden."));

            Assert.Empty(controller.ListGames());
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private sealed class AppRedengineFixture : IDisposable
    {
        private AppRedengineFixture(string root, string gameRoot)
        {
            Root = root;
            GameRoot = gameRoot;
        }

        public string Root { get; }
        public string GameRoot { get; }

        public static AppRedengineFixture Create()
        {
            var root = Path.Combine(Path.GetTempPath(), $"vrclient-app-redengine-{Guid.NewGuid():N}");
            var gameRoot = Path.Combine(root, "steamapps", "common", "Cyberpunk 2077");
            var executable = Path.Combine(gameRoot, "bin", "x64", "Cyberpunk2077.exe");
            Directory.CreateDirectory(Path.GetDirectoryName(executable)!);
            File.WriteAllBytes(executable, [1, 2, 3]);
            var manifest = Path.Combine(root, "steamapps", "appmanifest_1091500.acf");
            File.WriteAllText(manifest, "\"AppState\" { \"buildid\" \"123456\" }");
            var profileDir = Path.Combine(root, "config", "redengine", "native");
            Directory.CreateDirectory(profileDir);
            var dependencies = new[]
            {
                "red4ext\\RED4ext.dll",
                "red4ext\\plugins\\CyberpunkVR_Stereo\\CyberpunkVR_Stereo.dll"
            };
            foreach (var relative in dependencies)
            {
                var path = Path.Combine(gameRoot, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllText(path, "fixture");
            }
            File.WriteAllText(Path.Combine(profileDir, "cyberpunk-2077.json"), JsonSerializer.Serialize(new
            {
                game = new
                {
                    slug = "cyberpunk-2077",
                    display_name = "Cyberpunk 2077",
                    steam_app_id = "1091500",
                    steam_buildid_observed = "123456",
                    shipping_binary = "bin\\x64\\Cyberpunk2077.exe",
                    executable_sha256_observed = VrClient.Core.Hashing.Sha256OfFile(executable)
                },
                dependencies = dependencies.Select(path => new { path, reason = "fixture" }),
                conflicts = new[] { new { path = "bin\\x64\\dxgi.dll", reason = "proxy" } }
            }));
            return new AppRedengineFixture(root, gameRoot);
        }

        public void Dispose() => Directory.Delete(Root, recursive: true);
    }
}
