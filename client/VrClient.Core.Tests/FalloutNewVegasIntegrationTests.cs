using System.Buffers.Binary;
using System.Text.Json;
using VrClient.Core.FalloutNewVegas;
using Xunit;

public sealed class FalloutNewVegasIntegrationTests
{
    [Fact]
    public void Fresh_profile_needs_no_source_and_preview_does_not_write()
    {
        using var temp = new TempDirectory("fnv fresh preparation");
        var instance = Path.Combine(temp.Path, "managed-instance");
        var request = new FnvConversionRequest(instance, "", "New Vegas VR", temp.Path,
            FnvConversionMode.Convert, true, false);
        var service = new FalloutNewVegasMo2ProfileService();
        var preview = service.Plan(request);
        Assert.False(Directory.Exists(instance));
        Assert.False(service.Apply(preview).Changed);
        var result = service.Apply(service.Plan(request with { DryRun = false, AcknowledgeMutation = true }));
        Assert.True(result.Changed);
        var profile = Path.Combine(instance, "profiles", "New Vegas VR");
        Assert.True(File.Exists(Path.Combine(profile, "modlist.txt")));
        Assert.True(File.Exists(Path.Combine(profile, "plugins.txt")));
        Assert.False(Directory.Exists(Path.Combine(profile, "saves")));
        File.WriteAllText(Path.Combine(profile, "modlist.txt"), "+User mod");
        Assert.False(service.Apply(service.Plan(request with { DryRun = false, AcknowledgeMutation = true })).Changed);
        Assert.Equal("+User mod", File.ReadAllText(Path.Combine(profile, "modlist.txt")));
    }

    [Fact]
    public void Fresh_profile_still_rejects_unsafe_target_names_and_explicit_missing_source()
    {
        using var temp = new TempDirectory("fnv fresh validation");
        var service = new FalloutNewVegasMo2ProfileService();
        var request = new FnvConversionRequest(temp.Path, "", "../escape", temp.Path,
            FnvConversionMode.Convert, true, false);
        Assert.Throws<ArgumentException>(() => service.Plan(request));
        Assert.Throws<InvalidOperationException>(() => service.Plan(request with
            { SourceProfileName = "Missing", VrProfileName = "VR" }));
    }

    [Fact]
    public void Discovery_finds_steam_gog_and_manual_paths_with_spaces_and_unicode()
    {
        using var temp = new TempDirectory("VR Client 测试 discovery");
        var steam = Path.Combine(temp.Path, "Steam Library");
        var steamApps = Path.Combine(steam, "steamapps");
        Directory.CreateDirectory(steamApps);
        File.WriteAllText(Path.Combine(steamApps, "libraryfolders.vdf"),
            $"\"libraryfolders\" {{ \"0\" {{ \"path\" \"{steam.Replace("\\", "\\\\")}\" }} }}");
        File.WriteAllText(Path.Combine(steamApps, "appmanifest_22380.acf"),
            "\"AppState\" { \"appid\" \"22380\" \"installdir\" \"Fallout New Vegas\" }");
        var steamGame = Path.Combine(steamApps, "common", "Fallout New Vegas");
        CreateGame(steamGame, largeAddressAware: true);

        var gogGame = Path.Combine(temp.Path, "GOG 游戏", "Fallout New Vegas");
        CreateGame(gogGame, largeAddressAware: true);
        var discovery = new FalloutNewVegasDiscovery().Discover(new[] { steam }, new[] { gogGame }, gogGame);

        Assert.Equal(2, discovery.Installs.Count);
        Assert.Contains(discovery.Installs, install => install.Storefront == FnvStorefront.Steam);
        Assert.Contains(discovery.Installs, install => install.Storefront == FnvStorefront.Gog);
        Assert.All(discovery.Installs, install => Assert.Equal(64, install.ExecutableSha256.Length));
    }

    [Fact]
    public void Discovery_reports_wrong_directory_and_unsupported_edition()
    {
        using var temp = new TempDirectory("fnv-discovery-errors");
        var wrong = Path.Combine(temp.Path, "Fallout 4");
        Directory.CreateDirectory(wrong);
        File.WriteAllBytes(Path.Combine(wrong, "Fallout4.exe"), Array.Empty<byte>());
        var wrongResult = new FalloutNewVegasDiscovery().Discover(Array.Empty<string>(), Array.Empty<string>(), wrong);
        Assert.Equal(FnvInstallSupport.WrongDirectory, Assert.Single(wrongResult.Installs).Support);
        Assert.Contains("wrong_game_executable_detected", string.Join(';', wrongResult.Installs[0].Diagnostics));

        var unsupported = Path.Combine(temp.Path, "GamePass");
        CreateGame(unsupported, largeAddressAware: true);
        File.WriteAllText(Path.Combine(unsupported, "MicrosoftGame.Config"), "fixture");
        Assert.Equal(FnvInstallSupport.UnsupportedEdition,
            new FalloutNewVegasDiscovery().Inspect(unsupported, FnvStorefront.Manual).Support);
    }

    [Fact]
    public void Prerequisites_cover_patcher_plugins_runtime_virtual_desktop_and_profile()
    {
        using var temp = new TempDirectory("fnv preflight");
        var game = Path.Combine(temp.Path, "Fallout New Vegas");
        CreateGame(game, largeAddressAware: true);
        Touch(Path.Combine(game, "nvse_loader.exe"));
        Touch(Path.Combine(game, "nvse_1_4.dll"));
        Touch(Path.Combine(game, "Data", "nvse", "plugins", "jip_nvse.dll"));
        Touch(Path.Combine(game, "Data", "nvse", "plugins", "ShowOffNVSE.dll"));
        Touch(Path.Combine(game, "Data", "FNVR.esp"));
        var tracker = Touch(Path.Combine(temp.Path, "Tracker", "Fallout - New Virtual Reality.exe"));
        var nativeAdapter = Touch(Path.Combine(temp.Path, "native", "fnv-test-launcher.exe"));
        Touch(Path.Combine(temp.Path, "native", "vrclient_fnv_stereo.dll"));
        Touch(Path.Combine(temp.Path, "native", "openxr_loader.dll"));
        var steamVr = Path.Combine(temp.Path, "SteamVR");
        Touch(Path.Combine(steamVr, "bin", "win64", "vrmonitor.exe"));
        var activeRuntime = Touch(Path.Combine(steamVr, "steamxr_win64.json"));
        var mo2 = Touch(Path.Combine(temp.Path, "MO2", "ModOrganizer.exe"));
        var profile = Path.Combine(temp.Path, "MO2", "profiles", "New Vegas VR");
        Directory.CreateDirectory(profile);
        File.WriteAllText(Path.Combine(profile, "modlist.txt"), "+Fallout - New Virtual Reality");
        File.WriteAllText(Path.Combine(profile, "plugins.txt"), "*FalloutNV.esm\n*FNVR.esp\n");

        var install = new FalloutNewVegasDiscovery().Inspect(game, FnvStorefront.Steam);
        var report = new FalloutNewVegasPrerequisiteValidator().Validate(install,
            new FnvPrerequisiteOptions(mo2, profile, tracker, nativeAdapter, steamVr, true,
                new[] { "vrserver.exe", "VirtualDesktop.Streamer.exe" }, activeRuntime));

        Assert.True(report.Ready, string.Join(Environment.NewLine, report.Checks.Select(check => check.Message)));
        Assert.Contains(report.Checks, check => check.Code == "large_address_aware");
        Assert.Contains(report.Checks, check => check.Code == "fnvr_esp_enabled");
        Assert.Contains(report.Checks, check => check.Code == "virtual_desktop_streamer_running");

        // Process.ProcessName omits .exe, including dotted executable basenames.
        var liveProcessReport = new FalloutNewVegasPrerequisiteValidator().Validate(install,
            new FnvPrerequisiteOptions(mo2, profile, tracker, nativeAdapter, steamVr, true,
                new[] { "vrserver", "VirtualDesktop.Streamer" }, activeRuntime));
        Assert.Contains(liveProcessReport.Checks, check => check.Code == "virtual_desktop_streamer_running");
    }

    [Fact]
    public void Prerequisites_fail_for_unpatched_missing_dependencies_and_vd_not_running()
    {
        using var temp = new TempDirectory("fnv-preflight-failures");
        CreateGame(temp.Path, largeAddressAware: false);
        var install = new FalloutNewVegasDiscovery().Inspect(temp.Path, FnvStorefront.Steam);
        var report = new FalloutNewVegasPrerequisiteValidator().Validate(install,
            new FnvPrerequisiteOptions(null, null, null, null, null, true, Array.Empty<string>()));
        Assert.False(report.Ready);
        Assert.Contains(report.Checks, check => check.Code == "large_address_aware_missing");
        Assert.Contains(report.Checks, check => check.Code == "xnvse_missing");
        Assert.Contains(report.Checks, check => check.Code == "virtual_desktop_streamer_missing");
    }

    [Fact]
    public void Version_comparison_rejects_old_xnvse_and_accepts_current_version_shapes()
    {
        Assert.False(FalloutNewVegasPrerequisiteValidator.IsAtLeastVersion("6.3.3", "6.3.4"));
        Assert.True(FalloutNewVegasPrerequisiteValidator.IsAtLeastVersion("6.4.8.0", "6.3.4"));
        Assert.True(FalloutNewVegasPrerequisiteValidator.IsAtLeastVersion("6.4.8 beta", "6.3.4"));
        Assert.True(FalloutNewVegasPrerequisiteValidator.IsAtLeastVersion("0, 6, 4, 9", "6.3.4"));
        Assert.False(FalloutNewVegasPrerequisiteValidator.IsAtLeastVersion("unknown", "6.3.4"));
    }

    [Fact]
    public void Direct_steamvr_preflight_does_not_require_virtual_desktop()
    {
        using var temp = new TempDirectory("fnv direct steamvr");
        CreateGame(temp.Path, true);
        Touch(Path.Combine(temp.Path, "nvse_loader.exe"));
        Touch(Path.Combine(temp.Path, "nvse_1_4.dll"));
        Touch(Path.Combine(temp.Path, "Data", "nvse", "plugins", "jip_nvse.dll"));
        Touch(Path.Combine(temp.Path, "Data", "nvse", "plugins", "ShowOffNVSE.dll"));
        Touch(Path.Combine(temp.Path, "Data", "FNVR.esp"));
        var profile = Path.Combine(temp.Path, "MO2", "profiles", "VR");
        Directory.CreateDirectory(profile);
        File.WriteAllText(Path.Combine(profile, "modlist.txt"), "+FNVR");
        File.WriteAllText(Path.Combine(profile, "plugins.txt"), "*FNVR.esp");
        var mo2 = Touch(Path.Combine(temp.Path, "MO2", "ModOrganizer.exe"));
        var tracker = Touch(Path.Combine(temp.Path, "tracker.exe"));
        var nativeAdapter = Touch(Path.Combine(temp.Path, "native", "fnv-test-launcher.exe"));
        Touch(Path.Combine(temp.Path, "native", "vrclient_fnv_stereo.dll"));
        Touch(Path.Combine(temp.Path, "native", "openxr_loader.dll"));
        var steamVr = Path.Combine(temp.Path, "SteamVR");
        Touch(Path.Combine(steamVr, "vrmonitor.exe"));
        var activeRuntime = Touch(Path.Combine(steamVr, "steamxr_win64.json"));
        var report = new FalloutNewVegasPrerequisiteValidator().Validate(
            new FalloutNewVegasDiscovery().Inspect(temp.Path, FnvStorefront.Steam),
            new FnvPrerequisiteOptions(mo2, profile, tracker, nativeAdapter, steamVr, false, new[] { "vrserver" }, activeRuntime));
        Assert.True(report.Ready);
        Assert.Contains(report.Checks, check => check.Code == "direct_steamvr");
        Assert.Contains(report.Checks, check => check.Code == "steamvr_openxr_active");
        Assert.DoesNotContain(report.Checks, check => check.Component == "virtual_desktop");
    }

    [Fact]
    public void Preflight_uses_a_process_scoped_steamvr_override_when_another_runtime_is_global()
    {
        using var temp = new TempDirectory("fnv split runtime");
        CreateGame(temp.Path, true);
        var steamVr = Path.Combine(temp.Path, "SteamVR");
        Touch(Path.Combine(steamVr, "vrmonitor.exe"));
        Touch(Path.Combine(steamVr, "steamxr_win64.json"));
        var otherRuntime = Touch(Path.Combine(temp.Path, "VDXR", "virtualdesktop-openxr.json"));
        var report = new FalloutNewVegasPrerequisiteValidator().Validate(
            new FalloutNewVegasDiscovery().Inspect(temp.Path, FnvStorefront.Steam),
            new FnvPrerequisiteOptions(null, null, null, null, steamVr, false, Array.Empty<string>(), otherRuntime));
        Assert.Contains(report.Checks, check => check.Code == "steamvr_openxr_process_override" &&
            check.Severity == FnvCheckSeverity.Warning && check.Message.Contains("leave it unchanged", StringComparison.Ordinal));
    }

    [Fact]
    public void Conversion_requires_dry_run_acknowledgement_is_idempotent_and_preserves_profiles()
    {
        using var temp = new TempDirectory("fnv conversion");
        var instance = Path.Combine(temp.Path, "MO2 Instance");
        var source = Path.Combine(instance, "profiles", "Normal Modded");
        Directory.CreateDirectory(source);
        File.WriteAllText(Path.Combine(source, "modlist.txt"), "+Existing Camera Mod\n+JIP PP LN\n");
        File.WriteAllText(Path.Combine(source, "plugins.txt"), "*FalloutNV.esm\n");
        var service = new FalloutNewVegasMo2ProfileService();
        var request = new FnvConversionRequest(instance, "Normal Modded", "VR Isolated", temp.Path,
            FnvConversionMode.Convert, DryRun: true, AcknowledgeMutation: false);
        var dryPlan = service.Plan(request);
        Assert.False(service.Apply(dryPlan).Changed);
        Assert.False(Directory.Exists(Path.Combine(instance, "profiles", "VR Isolated")));

        var unacknowledged = dryPlan with { Request = request with { DryRun = false } };
        Assert.Throws<InvalidOperationException>(() => service.Apply(unacknowledged));
        var acknowledged = service.Plan(request with { DryRun = false, AcknowledgeMutation = true });
        var first = service.Apply(acknowledged);
        Assert.True(first.Changed);
        var target = Path.Combine(instance, "profiles", "VR Isolated");
        Assert.Equal(File.ReadAllText(Path.Combine(source, "modlist.txt")), File.ReadAllText(Path.Combine(target, "modlist.txt")));
        Assert.True(File.Exists(Path.Combine(target, "vrclient-fallout-new-vegas.json")));

        var second = service.Apply(service.Plan(request with { DryRun = false, AcknowledgeMutation = true, Mode = FnvConversionMode.Repair }));
        Assert.False(second.Changed);
        Assert.Equal("+Existing Camera Mod\n+JIP PP LN\n", File.ReadAllText(Path.Combine(source, "modlist.txt")));
    }

    [Fact]
    public void Conversion_backs_up_only_managed_changes_recovers_journal_and_restore_keeps_profile()
    {
        using var temp = new TempDirectory("fnv repair restore");
        var instance = Path.Combine(temp.Path, "MO2");
        var source = Path.Combine(instance, "profiles", "Main");
        Directory.CreateDirectory(source);
        File.WriteAllText(Path.Combine(source, "modlist.txt"), "+My Mod");
        var service = new FalloutNewVegasMo2ProfileService();
        var baseRequest = new FnvConversionRequest(instance, "Main", "VR", Path.Combine(temp.Path, "game-a"),
            FnvConversionMode.Convert, false, true);
        service.Apply(service.Plan(baseRequest));
        var target = Path.Combine(instance, "profiles", "VR");
        File.WriteAllText(Path.Combine(target, ".vrclient-fnv-transaction.json"), "interrupted fixture");
        var repaired = service.Apply(service.Plan(baseRequest with
        {
            GameDirectory = Path.Combine(temp.Path, "game-b"),
            Mode = FnvConversionMode.Repair
        }));
        Assert.True(repaired.RecoveredInterruptedTransaction);
        Assert.Single(Directory.GetFiles(Path.Combine(target, ".vrclient-backups"), "*.bak"));

        var restored = service.Apply(service.Plan(baseRequest with { Mode = FnvConversionMode.Restore }));
        Assert.True(restored.Changed);
        Assert.True(File.Exists(Path.Combine(target, "modlist.txt")));
        Assert.False(File.Exists(Path.Combine(target, "vrclient-fallout-new-vegas.json")));
    }

    [Fact]
    public void Conversion_never_overwrites_preexisting_vr_profile_files()
    {
        using var temp = new TempDirectory("fnv existing dirty profile");
        var instance = Path.Combine(temp.Path, "MO2");
        var source = Path.Combine(instance, "profiles", "Main");
        var target = Path.Combine(instance, "profiles", "VR");
        Directory.CreateDirectory(source);
        Directory.CreateDirectory(target);
        File.WriteAllText(Path.Combine(source, "modlist.txt"), "+Source Mod");
        File.WriteAllText(Path.Combine(source, "plugins.txt"), "*Source.esp");
        File.WriteAllText(Path.Combine(target, "modlist.txt"), "+User VR Mod");
        File.WriteAllText(Path.Combine(target, "plugins.txt"), "*UserVR.esp");
        var request = new FnvConversionRequest(instance, "Main", "VR", temp.Path,
            FnvConversionMode.Convert, false, true);
        new FalloutNewVegasMo2ProfileService().Apply(new FalloutNewVegasMo2ProfileService().Plan(request));
        Assert.Equal("+User VR Mod", File.ReadAllText(Path.Combine(target, "modlist.txt")));
        Assert.Equal("*UserVR.esp", File.ReadAllText(Path.Combine(target, "plugins.txt")));
        Assert.Equal("+Source Mod", File.ReadAllText(Path.Combine(source, "modlist.txt")));
    }

    [Fact]
    public void Restore_of_absent_vr_profile_is_a_noop_and_creates_nothing()
    {
        using var temp = new TempDirectory("fnv absent restore");
        var instance = Path.Combine(temp.Path, "MO2");
        var source = Path.Combine(instance, "profiles", "Main");
        Directory.CreateDirectory(source);
        File.WriteAllText(Path.Combine(source, "modlist.txt"), "+Source Mod");
        var request = new FnvConversionRequest(instance, "Main", "Never Created", temp.Path,
            FnvConversionMode.Restore, false, true);
        var service = new FalloutNewVegasMo2ProfileService();
        var result = service.Apply(service.Plan(request));
        Assert.False(result.Changed);
        Assert.False(Directory.Exists(Path.Combine(instance, "profiles", "Never Created")));
    }

    [Fact]
    public void Compatibility_report_classifies_without_disabling_or_reordering()
    {
        using var temp = new TempDirectory("fnv compatibility");
        var profile = Path.Combine(temp.Path, "profile");
        var game = Path.Combine(temp.Path, "game");
        Directory.CreateDirectory(profile);
        Directory.CreateDirectory(game);
        File.WriteAllText(Path.Combine(profile, "modlist.txt"),
            "+JIP PP LN\n+Enhanced Camera\n+VATS Camera Tweaks\n+Unknown Quest Mod\n-Disabled ENB\n");
        Touch(Path.Combine(game, "d3d9.dll"));
        var rulesPath = FindRepoFile("config", "modpacks", "fallout-new-vegas.compatibility-rules.json");
        var analyzer = new FalloutNewVegasCompatibilityAnalyzer();
        var report = analyzer.Analyze(profile, game,
            FalloutNewVegasCompatibilityAnalyzer.LoadRules(rulesPath), "2026-08-29T00:00:00Z");
        Assert.Contains(report.Mods, mod => mod.Name == "JIP PP LN" && mod.Status == FnvCompatibilityStatus.Verified);
        Assert.Contains(report.Mods, mod => mod.Name == "Enhanced Camera" && mod.Status == FnvCompatibilityStatus.Warned);
        Assert.Contains(report.Mods, mod => mod.Name == "VATS Camera Tweaks" && mod.Status == FnvCompatibilityStatus.Incompatible);
        Assert.Contains(report.Mods, mod => mod.Name == "Unknown Quest Mod" && mod.Status == FnvCompatibilityStatus.Unknown);
        Assert.DoesNotContain(report.Mods, mod => mod.Name.Contains("Disabled ENB"));
        Assert.Single(report.RootDlls);

        var output = Path.Combine(temp.Path, "compatibility-report.json");
        analyzer.WriteReport(output, report);
        Assert.Contains("\"no_automatic_disable\": true", File.ReadAllText(output));
    }

    [Fact]
    public void Launch_orders_components_and_cleanup_stops_only_tracker_started_by_vrclient()
    {
        using var temp = new TempDirectory("fnv launch");
        var game = Path.Combine(temp.Path, "game");
        CreateGame(game, true);
        Touch(Path.Combine(game, "nvse_loader.exe"));
        var mo2 = Touch(Path.Combine(temp.Path, "MO2", "ModOrganizer.exe"));
        var profile = Path.Combine(temp.Path, "MO2", "profiles", "VR Profile");
        Directory.CreateDirectory(profile);
        var tracker = Touch(Path.Combine(temp.Path, "tracker", "FNVR_Tracker.exe"));
        var nativeAdapter = Touch(Path.Combine(temp.Path, "native", "fnv-test-launcher.exe"));
        Touch(Path.Combine(temp.Path, "native", "vrclient_fnv_stereo.dll"));
        Touch(Path.Combine(temp.Path, "native", "openxr_loader.dll"));
        var steamVr = Path.Combine(temp.Path, "steamvr");
        Touch(Path.Combine(steamVr, "bin", "win64", "vrmonitor.exe"));
        var steamOpenXr = Touch(Path.Combine(steamVr, "steamxr_win64.json"));
        var vd = Touch(Path.Combine(temp.Path, "Virtual Desktop", "VirtualDesktop.Streamer.exe"));
        var install = new FalloutNewVegasDiscovery().Inspect(game, FnvStorefront.Steam);
        var plan = new FalloutNewVegasLaunchOrchestrator().BuildPlan(install,
            new FnvPrerequisiteOptions(mo2, profile, tracker, nativeAdapter, steamVr, true),
            Path.Combine(temp.Path, "logs"), vd);
        Assert.Equal(new[]
        {
            FnvLaunchComponentKind.VirtualDesktop,
            FnvLaunchComponentKind.SteamVr,
            FnvLaunchComponentKind.FnvrTracker,
            FnvLaunchComponentKind.NewVegas,
            FnvLaunchComponentKind.NativeOpenXrAdapter
        }, plan.Components.Select(component => component.Kind));
        var gameComponent = Assert.Single(plan.Components, component => component.Kind == FnvLaunchComponentKind.NewVegas);
        Assert.Contains("-p \"VR Profile\"", gameComponent.Arguments);
        Assert.Contains("nvse_loader.exe", gameComponent.Arguments);
        Assert.Equal(Path.GetFullPath(steamOpenXr), gameComponent.EnvironmentVariables!["XR_RUNTIME_JSON"]);

        var fake = new FakeProcessController(FnvLaunchComponentKind.SteamVr);
        using (var session = new FalloutNewVegasLaunchOrchestrator().Start(plan, fake, dryRun: false))
        {
            Assert.Equal(4, fake.StartOrder.Count);
            Assert.DoesNotContain(FnvLaunchComponentKind.SteamVr, fake.StartOrder);
            Assert.Null(session.CaptureExitCodes()[FnvLaunchComponentKind.NewVegas]);
        }
        Assert.Single(fake.Stopped);
        Assert.Equal(FnvLaunchComponentKind.FnvrTracker, fake.Stopped[0]);
        Assert.Single(fake.NativeStopRequests);
    }

    [Fact]
    public void Launch_failure_reports_component_and_cleans_only_started_owned_tracker()
    {
        using var temp = new TempDirectory("fnv launch failure");
        var components = new[]
        {
            new FnvLaunchComponent(FnvLaunchComponentKind.SteamVr, Touch(Path.Combine(temp.Path, "vrmonitor.exe")), "", temp.Path, true, false),
            new FnvLaunchComponent(FnvLaunchComponentKind.FnvrTracker, Touch(Path.Combine(temp.Path, "tracker.exe")), "", temp.Path, false, true),
            new FnvLaunchComponent(FnvLaunchComponentKind.NewVegas, Touch(Path.Combine(temp.Path, "ModOrganizer.exe")), "", temp.Path, false, false)
        };
        var plan = new FnvLaunchPlan(components, Path.Combine(temp.Path, "logs"), false, Array.Empty<string>());
        var fake = new FakeProcessController(FnvLaunchComponentKind.SteamVr) { FailOn = FnvLaunchComponentKind.NewVegas };
        var error = Assert.Throws<InvalidOperationException>(() =>
            new FalloutNewVegasLaunchOrchestrator().Start(plan, fake, false));
        Assert.Contains("NewVegas", error.Message);
        Assert.Equal(new[] { FnvLaunchComponentKind.FnvrTracker }, fake.Stopped);
    }

    [Fact]
    public void Comfort_plan_never_invents_or_writes_dependency_keys()
    {
        var plan = new FalloutNewVegasComfortPlanner().Build(
            new FnvComfortPreferences("seated", "snap", "controller", "left", "balanced"));
        Assert.False(plan.WritesThirdPartyConfiguration);
        Assert.Contains(plan.Items, item => item.Feature == "turning" && item.EvidenceBoundary.Contains("does not write"));
        Assert.Contains(plan.Items, item => item.Feature == "weapon_alignment" && item.Status == "hardware_pending");
        Assert.Throws<ArgumentException>(() => new FalloutNewVegasComfortPlanner().Build(
            new FnvComfortPreferences("flying", "snap", "controller", "left", "balanced")));
    }

    [Fact]
    public void Adapter_and_dependency_manifests_are_machine_readable_and_restrict_bundling()
    {
        using var adapter = JsonDocument.Parse(File.ReadAllText(FindRepoFile("adapters", "fallout_new_vegas", "adapter.json")));
        Assert.True(adapter.RootElement.GetProperty("native_renderer").GetBoolean());
        Assert.False(adapter.RootElement.GetProperty("restrictions").GetProperty("bundles_vorpx").GetBoolean());
        using var manifest = JsonDocument.Parse(File.ReadAllText(FindRepoFile("config", "modpacks", "fallout-new-vegas.dependencies.json")));
        Assert.Equal("2026-09-21", manifest.RootElement.GetProperty("retrieved_at").GetString());
        var dependencies = manifest.RootElement.GetProperty("dependencies").EnumerateArray().ToArray();
        Assert.True(dependencies.Length >= 8);
        Assert.All(dependencies, dependency =>
        {
            Assert.Equal("user-supplied-from-official-source", dependency.GetProperty("acquisition").GetString());
            Assert.StartsWith("https://", dependency.GetProperty("source_url").GetString());
            Assert.False(string.IsNullOrWhiteSpace(dependency.GetProperty("license").GetString()));
        });
    }

    private static void CreateGame(string directory, bool largeAddressAware)
    {
        Directory.CreateDirectory(directory);
        var bytes = new byte[512];
        BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(0x3c, 4), 0x80);
        bytes[0x80] = (byte)'P';
        bytes[0x81] = (byte)'E';
        if (largeAddressAware)
            BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(0x80 + 22, 2), 0x20);
        File.WriteAllBytes(Path.Combine(directory, "FalloutNV.exe"), bytes);
        Touch(Path.Combine(directory, "FalloutNVLauncher.exe"));
    }

    private static string Touch(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, new byte[] { 0 });
        return path;
    }

    private static string FindRepoFile(params string[] segments)
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            var candidate = Path.Combine(new[] { directory.FullName }.Concat(segments).ToArray());
            if (File.Exists(candidate))
                return candidate;
            directory = directory.Parent;
        }
        throw new FileNotFoundException($"repository fixture not found: {string.Join('/', segments)}");
    }

    private sealed class TempDirectory : IDisposable
    {
        public TempDirectory(string prefix)
        {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"{prefix}-{Guid.NewGuid():N}");
            Directory.CreateDirectory(Path);
        }
        public string Path { get; }
        public void Dispose() => Directory.Delete(Path, recursive: true);
    }

    private sealed class FakeProcessController : IFnvProcessController
    {
        private int _nextPid = 100;
        private readonly HashSet<FnvLaunchComponentKind> _alreadyRunning;
        private readonly Dictionary<int, FnvLaunchComponentKind> _processes = new();

        public FakeProcessController(params FnvLaunchComponentKind[] alreadyRunning) =>
            _alreadyRunning = new HashSet<FnvLaunchComponentKind>(alreadyRunning);

        public List<FnvLaunchComponentKind> StartOrder { get; } = new();
        public List<FnvLaunchComponentKind> Stopped { get; } = new();
        public List<string> NativeStopRequests { get; } = new();
        public FnvLaunchComponentKind? FailOn { get; init; }
        public bool IsRunning(FnvLaunchComponent component) => _alreadyRunning.Contains(component.Kind);
        public FnvStartedProcess Start(FnvLaunchComponent component, string logPath)
        {
            if (FailOn == component.Kind)
                throw new InvalidOperationException("synthetic component failure");
            StartOrder.Add(component.Kind);
            var pid = _nextPid++;
            _processes[pid] = component.Kind;
            return new FnvStartedProcess(component, pid, true, logPath);
        }
        public int? TryGetExitCode(int processId) => null;
        public bool WaitUntilReady(FnvLaunchComponent component, TimeSpan timeout) => true;
        public void Stop(int processId) => Stopped.Add(_processes[processId]);
        public void StopNativeAdapter(FnvLaunchComponent component, string logPath) => NativeStopRequests.Add(logPath);
    }
}
