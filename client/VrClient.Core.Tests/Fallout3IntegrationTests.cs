using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Fallout3;

public sealed class Fallout3IntegrationTests
{
    [Fact]
    public void Direct_fo3_launch_opts_into_vr_only_for_runtime_launches()
    {
        var desktop = AppController.CreateFallout3DirectLaunchInfo(@"C:\Games\Fallout 3", null);
        Assert.EndsWith("fose_loader.exe", desktop.FileName);
        Assert.Equal("0", desktop.Environment["VRCLIENT_FALLOUT3_NATIVE_VR"]);
        Assert.False(desktop.Environment.ContainsKey("XR_RUNTIME_JSON"));

        var vr = AppController.CreateFallout3DirectLaunchInfo(@"C:\Games\Fallout 3", @"C:\OpenXR\runtime.json");
        Assert.Equal("1", vr.Environment["VRCLIENT_FALLOUT3_NATIVE_VR"]);
        Assert.Equal(@"C:\OpenXR\runtime.json", vr.Environment["XR_RUNTIME_JSON"]);
    }

    [Fact]
    public void Direct_native_headset_test_requires_exact_profile_but_not_mo2_or_depth_stack()
    {
        using var temp = new TempDirectory("fo3-direct-native");
        var game = Path.Combine(temp.Path, "game");
        CreateGame(game, allDlc: true);
        File.WriteAllText(Path.Combine(game, "fose_loader.exe"), "fake");
        File.WriteAllText(Path.Combine(game, "fose_1_7.dll"), "fake");
        var plugin = Path.Combine(temp.Path, "vrclient_fallout3_native.dll");
        var profile = Path.Combine(temp.Path, "fallout3-native-profile.ini");
        var runtime = Path.Combine(temp.Path, "runtime-32.json");
        File.WriteAllText(plugin, "fake");
        File.WriteAllText(runtime, "{}");
        var install = new Fallout3Discovery().Inspect(game, Fallout3Storefront.Steam);
        var options = new Fallout3DependencyOptions(null, null, null, null, null,
            plugin, profile, null, new[] { "VirtualDesktop.Streamer" }, runtime);
        var validator = new Fallout3DependencyValidator();
        Assert.False(validator.ValidateNativeDirect(install, options).Ready);

        File.WriteAllText(profile, $"""
            [HookProfile]
            ExecutableSha256={install.ExecutableSha256}
            RenderHookRva=0x1000
            RenderContextPointerRva=0x2000
            SceneGraphPointerRva=0x3000
            RendererPointerRva=0x4000
            CameraOffset=0xAC
            DeviceOffset=0x288
            RenderEntryBytes=55 8B EC 83 EC
            """);
        var ready = validator.ValidateNativeDirect(install, options);
        Assert.True(ready.Ready);
        Assert.Equal(Fallout3BackendLabels.NativeExperimental, ready.ActiveLabel);
        Assert.DoesNotContain(ready.Checks, check => check.Component is "mod_organizer_2" or "reshade" or "osiris");
    }

    [Fact]
    public void Steam_goty_appid_is_discovered_from_secondary_library()
    {
        using var temp = new TempDirectory("fo3-steam-goty-library");
        var steam = Path.Combine(temp.Path, "Steam Root");
        var secondary = Path.Combine(temp.Path, "Games Drive", "SteamLibrary");
        var game = Path.Combine(secondary, "steamapps", "common", "Fallout 3 goty");
        CreateGame(game, allDlc: true);
        Directory.CreateDirectory(Path.Combine(steam, "steamapps"));
        File.WriteAllText(Path.Combine(steam, "steamapps", "libraryfolders.vdf"),
            $"\"libraryfolders\" {{ \"1\" {{ \"path\" \"{secondary.Replace("\\", "\\\\")}\" " +
            "\"apps\" { \"22370\" \"1\" } } }");
        File.WriteAllText(Path.Combine(secondary, "steamapps", "appmanifest_22370.acf"),
            "\"AppState\" { \"appid\" \"22370\" \"installdir\" \"Fallout 3 goty\" }");

        var install = Assert.Single(new Fallout3Discovery()
            .Discover(new[] { steam }, Array.Empty<string>()).Installs);

        Assert.Equal(Path.GetFullPath(game), install.RootDirectory);
        Assert.Equal(Fallout3Storefront.Steam, install.Storefront);
        Assert.DoesNotContain(install.Diagnostics, note => note.StartsWith("launcher_missing:"));
    }

    [Fact]
    public void Steam_gog_manual_discovery_and_dlc_validation_are_asset_free()
    {
        using var temp = new TempDirectory("fo3-discovery-ü");
        var steam = Path.Combine(temp.Path, "Steam Root");
        var game = Path.Combine(steam, "steamapps", "common", "Fallout 3 goty");
        CreateGame(game, allDlc: true);
        Directory.CreateDirectory(Path.Combine(steam, "steamapps"));
        File.WriteAllText(Path.Combine(steam, "steamapps", "appmanifest_22300.acf"),
            "\"AppState\" { \"appid\" \"22300\" \"installdir\" \"Fallout 3 goty\" }");
        var discovery = new Fallout3Discovery();
        var result = discovery.Discover(new[] { steam }, Array.Empty<string>());
        var install = Assert.Single(result.Installs);
        Assert.Equal(Fallout3Storefront.Steam, install.Storefront);
        Assert.All(install.Dlc, pair => Assert.True(pair.Value, pair.Key));

        var manual = Path.Combine(temp.Path, "manual");
        CreateGame(manual, allDlc: false);
        var inspected = discovery.Inspect(manual, Fallout3Storefront.Manual);
        Assert.Equal(Fallout3InstallSupport.ManualUnverified, inspected.Support);
        Assert.Contains(inspected.Diagnostics, note => note.StartsWith("goty_dlc_missing:"));
    }

    [Fact]
    public void Conversion_is_dry_run_acknowledged_idempotent_recoverable_and_source_preserving()
    {
        using var temp = new TempDirectory("fo3-mo2");
        var instance = Path.Combine(temp.Path, "MO2");
        var source = Path.Combine(instance, "profiles", "Normal Modded");
        Directory.CreateDirectory(source);
        File.WriteAllText(Path.Combine(instance, "ModOrganizer.exe"), "fake");
        File.WriteAllText(Path.Combine(source, "modlist.txt"), "+Weather Mod\n+HUD Mod\n");
        File.WriteAllText(Path.Combine(source, "plugins.txt"), "*Fallout3.esm\n*BrokenSteel.esm\n");
        var beforeMods = File.ReadAllBytes(Path.Combine(source, "modlist.txt"));
        var beforePlugins = File.ReadAllBytes(Path.Combine(source, "plugins.txt"));
        var service = new Fallout3Mo2ProfileService();
        var request = new Fallout3ConversionRequest(instance, "Normal Modded",
            Fallout3Mo2ProfileService.DefaultProfileName, temp.Path, Fallout3Backend.DepthVr,
            Fallout3ConversionMode.Convert, true, false);
        var dryPlan = service.Plan(request);
        Assert.False(Directory.Exists(dryPlan.VrProfileDirectory));
        Assert.False(service.Apply(dryPlan).Changed);

        var unsafePlan = service.Plan(request with { DryRun = false });
        Assert.Throws<InvalidOperationException>(() => service.Apply(unsafePlan));
        var plan = service.Plan(request with { DryRun = false, AcknowledgeMutation = true });
        var first = service.Apply(plan);
        Assert.True(first.Changed);
        Assert.Equal(beforeMods, File.ReadAllBytes(Path.Combine(source, "modlist.txt")));
        Assert.Equal(beforePlugins, File.ReadAllBytes(Path.Combine(source, "plugins.txt")));
        Assert.Equal("+Weather Mod\n+HUD Mod\n", File.ReadAllText(Path.Combine(plan.VrProfileDirectory, "modlist.txt")));
        Assert.False(service.Apply(service.Plan(request with { DryRun = false, AcknowledgeMutation = true })).Changed);

        File.WriteAllText(Path.Combine(plan.VrProfileDirectory, ".vrclient-fallout3-transaction.json"), "interrupted");
        var repaired = service.Apply(service.Plan(request with
        {
            Mode = Fallout3ConversionMode.Repair, DryRun = false, AcknowledgeMutation = true
        }));
        Assert.True(repaired.RecoveredInterruptedTransaction);
        Assert.False(File.Exists(Path.Combine(plan.VrProfileDirectory, ".vrclient-fallout3-transaction.json")));
    }

    [Fact]
    public void Restore_removes_only_managed_files_and_uninstall_refuses_dirty_profile()
    {
        using var temp = new TempDirectory("fo3-restore");
        var instance = Path.Combine(temp.Path, "MO2");
        var source = Path.Combine(instance, "profiles", "Desktop");
        Directory.CreateDirectory(source);
        File.WriteAllText(Path.Combine(source, "modlist.txt"), "+A\n");
        var service = new Fallout3Mo2ProfileService();
        var request = new Fallout3ConversionRequest(instance, "Desktop", "VRClient Fallout 3 VR",
            temp.Path, Fallout3Backend.DepthVr, Fallout3ConversionMode.Convert, false, true);
        var applied = service.Plan(request);
        service.Apply(applied);
        File.WriteAllText(Path.Combine(applied.VrProfileDirectory, "user-note.txt"), "mine");
        var restore = service.Plan(request with { Mode = Fallout3ConversionMode.Restore });
        service.Apply(restore);
        Assert.True(File.Exists(Path.Combine(applied.VrProfileDirectory, "modlist.txt")));
        Assert.True(File.Exists(Path.Combine(applied.VrProfileDirectory, "user-note.txt")));

        service.Apply(service.Plan(request));
        var uninstall = service.Plan(request with { Mode = Fallout3ConversionMode.Uninstall });
        Assert.Throws<InvalidOperationException>(() => service.Apply(uninstall));
        Assert.True(Directory.Exists(applied.VrProfileDirectory));
    }

    [Fact]
    public void Native_backend_allows_exact_build_experimental_and_reserves_verified_for_headset_proof()
    {
        using var temp = new TempDirectory("fo3-fallback");
        var game = Path.Combine(temp.Path, "game");
        CreateGame(game, allDlc: true);
        foreach (var name in new[] { "d3d9.dll", "fose_1_7.dll" }) File.WriteAllText(Path.Combine(game, name), "x");
        Directory.CreateDirectory(Path.Combine(game, "Shaders"));
        File.WriteAllText(Path.Combine(game, "Shaders", "SuperDepth3D.fx"), "user supplied");
        var mo2 = Path.Combine(temp.Path, "ModOrganizer.exe");
        var osiris = Path.Combine(temp.Path, "osiris-vr-viewer.exe");
        var adapter = Path.Combine(temp.Path, "vrclient_fallout3_native.dll");
        var hookProfile = Path.Combine(temp.Path, "fallout3-native-profile.ini");
        var runtime = Path.Combine(temp.Path, "steamxr_win32.json");
        foreach (var file in new[] { mo2, osiris, adapter, runtime }) File.WriteAllText(file, "x");
        var install = new Fallout3Discovery().Inspect(game, Fallout3Storefront.Gog);
        var options = new Fallout3DependencyOptions(mo2, null, game, game, osiris, adapter, null, null,
            new[] { "vrserver" }, runtime);
        var report = new Fallout3DependencyValidator().Validate(install, options, Fallout3Backend.NativeVr);
        Assert.Equal(Fallout3Backend.DepthVr, report.EffectiveBackend);
        Assert.Equal(Fallout3BackendLabels.DepthVr, report.ActiveLabel);
        Assert.NotEmpty(report.FallbackReasons);
        Assert.DoesNotContain(report.Checks, check =>
            check.Component.StartsWith("native_", StringComparison.Ordinal) &&
            check.Severity == Fallout3Severity.Failure);

        File.WriteAllText(hookProfile, $"""
            [HookProfile]
            ExecutableSha256={install.ExecutableSha256}
            RenderHookRva=0x1000
            RenderContextPointerRva=0x2000
            SceneGraphPointerRva=0x3000
            RendererPointerRva=0x4000
            CameraOffset=0xAC
            DeviceOffset=0x288
            RenderEntryBytes=55 8B EC 83 EC
            """);
        var experimentalOptions = options with { NativeHookProfilePath = hookProfile };
        var experimental = new Fallout3DependencyValidator().Validate(install, experimentalOptions, Fallout3Backend.NativeVr);
        Assert.Equal(Fallout3Backend.NativeVr, experimental.EffectiveBackend);
        Assert.Equal(Fallout3BackendStatus.Experimental, experimental.NativeStatus);
        Assert.Equal(Fallout3BackendLabels.NativeExperimental, experimental.ActiveLabel);

        var proof = Path.Combine(temp.Path, "proof.json");
        File.WriteAllText(proof, "{\"exact_build_match\":true,\"independent_per_eye\":true,\"headset_verified\":true,\"executable_sha256\":\"wrong-build\"}");
        var mismatched = new Fallout3DependencyValidator().Validate(install, experimentalOptions with { NativeStereoProofPath = proof }, Fallout3Backend.NativeVr);
        Assert.Equal(Fallout3Backend.NativeVr, mismatched.EffectiveBackend);
        Assert.Equal(Fallout3BackendStatus.Experimental, mismatched.NativeStatus);

        File.WriteAllText(proof, JsonSerializer.Serialize(new
        {
            exact_build_match = true,
            independent_per_eye = true,
            headset_verified = true,
            executable_sha256 = install.ExecutableSha256
        }));
        var verified = new Fallout3DependencyValidator().Validate(install, experimentalOptions with { NativeStereoProofPath = proof }, Fallout3Backend.NativeVr);
        Assert.Equal(Fallout3Backend.NativeVr, verified.EffectiveBackend);
        Assert.Equal(Fallout3BackendLabels.NativeVerified, verified.ActiveLabel);
    }

    [Fact]
    public void Compatibility_report_preserves_load_order_and_classifies_conflict_categories()
    {
        using var temp = new TempDirectory("fo3-compat");
        File.WriteAllText(Path.Combine(temp.Path, "modlist.txt"), "+ENB Lighting\n+Weapon Animation\n+Texture Pack\n");
        File.WriteAllText(Path.Combine(temp.Path, "plugins.txt"), "*Fallout3.esm\n*MyQuest.esp\n");
        var report = new Fallout3CompatibilityAnalyzer().Analyze(temp.Path, temp.Path, Fallout3Backend.DepthVr);
        Assert.Contains(report.Entries, entry => entry.Name == "ENB Lighting" && entry.Status == Fallout3CompatibilityStatus.ConflictsWithDepthVr);
        Assert.Contains(report.Entries, entry => entry.Name == "Weapon Animation" && entry.Status == Fallout3CompatibilityStatus.RequiresVrConfiguration);
        Assert.Contains(report.Entries, entry => entry.Name == "Texture Pack" && entry.Status == Fallout3CompatibilityStatus.LikelyCompatible);
        Assert.Contains(report.Entries, entry => entry.Name == "MyQuest.esp");
    }

    [Fact]
    public void Launch_order_is_visible_and_cleanup_only_stops_vrclient_started_helpers()
    {
        using var temp = new TempDirectory("fo3-launch");
        var game = Path.Combine(temp.Path, "game"); CreateGame(game, true);
        var mo2 = Path.Combine(temp.Path, "ModOrganizer.exe"); var osiris = Path.Combine(temp.Path, "osiris-vr-viewer.exe");
        var runtime = Path.Combine(temp.Path, "virtualdesktop-openxr-32.json");
        foreach (var file in new[] { mo2, osiris, runtime }) File.WriteAllText(file, "x");
        var profile = Path.Combine(temp.Path, "profiles", "VRClient Fallout 3 VR"); Directory.CreateDirectory(profile);
        File.WriteAllText(Path.Combine(profile, "vrclient-fallout3-vr.json"), "{}");
        var install = new Fallout3Discovery().Inspect(game, Fallout3Storefront.Gog);
        var readiness = new Fallout3Readiness(Array.Empty<Fallout3Check>(), Fallout3Backend.DepthVr,
            Fallout3Backend.DepthVr, Fallout3BackendStatus.Unavailable, Fallout3BackendLabels.DepthVr, Array.Empty<string>());
        var plan = new Fallout3LaunchOrchestrator().BuildPlan(install, readiness, mo2, profile,
            Path.Combine(temp.Path, "logs"), osiris, runtime);
        Assert.Equal("0", plan.Components.Last().Environment!["VRCLIENT_FALLOUT3_NATIVE_VR"]);
        Assert.Equal(new[] { Fallout3LaunchComponentKind.Osiris, Fallout3LaunchComponentKind.ModOrganizer },
            plan.Components.Select(item => item.Kind));
        var nativeReadiness = readiness with
        {
            RequestedBackend = Fallout3Backend.NativeVr,
            EffectiveBackend = Fallout3Backend.NativeVr,
            ActiveLabel = Fallout3BackendLabels.NativeExperimental
        };
        var nativePlan = new Fallout3LaunchOrchestrator().BuildPlan(install, nativeReadiness, mo2, profile,
            Path.Combine(temp.Path, "native-logs"), null, runtime);
        Assert.Equal("1", nativePlan.Components.Last().Environment!["VRCLIENT_FALLOUT3_NATIVE_VR"]);
        var fake = new FakeProcessController(Fallout3LaunchComponentKind.Osiris);
        using (var session = new Fallout3LaunchOrchestrator().Start(plan, fake, false))
            Assert.All(session.Processes, process => Assert.True(process.ProcessId == -1 || process.StartedByVrClient));
        Assert.Empty(fake.Stopped); // Osiris was already running; MO2/game is never killed by cleanup.
    }

    private static void CreateGame(string root, bool allDlc)
    {
        Directory.CreateDirectory(Path.Combine(root, "Data"));
        File.WriteAllBytes(Path.Combine(root, "Fallout3.exe"), new byte[] { (byte)'M', (byte)'Z', 1, 2, 3 });
        File.WriteAllText(Path.Combine(root, "Fallout3Launcher.exe"), "x");
        if (allDlc) foreach (var file in Fallout3Discovery.RequiredDlc.Values)
            File.WriteAllText(Path.Combine(root, "Data", file), "fixture");
    }

    private sealed class TempDirectory : IDisposable
    {
        public TempDirectory(string prefix)
        {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), prefix + "-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path);
        }
        public string Path { get; }
        public void Dispose() => Directory.Delete(Path, true);
    }

    private sealed class FakeProcessController : IFallout3ProcessController
    {
        private int _pid = 100;
        private readonly HashSet<Fallout3LaunchComponentKind> _running;
        private readonly Dictionary<int, Fallout3LaunchComponent> _started = new();
        public FakeProcessController(params Fallout3LaunchComponentKind[] running) => _running = new(running);
        public List<int> Stopped { get; } = new();
        public bool IsRunning(Fallout3LaunchComponent component) => _running.Contains(component.Kind);
        public Fallout3StartedProcess Start(Fallout3LaunchComponent component, string logPath)
        {
            var pid = _pid++; _started[pid] = component;
            return new Fallout3StartedProcess(component, pid, true, logPath);
        }
        public bool WaitUntilReady(Fallout3LaunchComponent component, TimeSpan timeout) => true;
        public int? TryGetExitCode(int processId) => null;
        public void Stop(int processId) => Stopped.Add(processId);
    }
}
