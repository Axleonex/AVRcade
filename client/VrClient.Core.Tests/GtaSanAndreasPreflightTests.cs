using System.Text.Json;
using VrClient.Core.Legacy;
using VrClient.Core.App;
using VrClient.Core.Launch;
using VrClient.Core.Model;

namespace VrClient.Core.Tests;

public sealed class GtaSanAndreasPreflightTests
{
    [Fact]
    public void Turn_preference_defaults_to_snap_and_persists_smooth_speed_without_touching_the_game()
    {
        using var root = new TemporaryDirectory();
        var path = Path.Combine(root.Path, "gta-turn.json");
        var settings = new GtaSanAndreasTurnSettings(path);
        Assert.Equal(new GtaTurnPreference(GtaTurnMode.Snap, 90), settings.Load());

        File.WriteAllText(path, "{\"Mode\":\"snap\",\"SmoothDegreesPerSecond\":90}");
        Assert.Equal(30, settings.Load().SnapDegrees);

        var smooth = new GtaTurnPreference(GtaTurnMode.Smooth, 135) { SnapDegrees = 45 };
        settings.Save(smooth);
        Assert.Equal(smooth, new GtaSanAndreasTurnSettings(path).Load());
        Assert.Equal("smooth", GtaSanAndreasTurnSettings.LaunchEnvironment(smooth)["VRCLIENT_GTASA_TURN_MODE"]);
        Assert.Equal("135", GtaSanAndreasTurnSettings.LaunchEnvironment(smooth)["VRCLIENT_GTASA_SMOOTH_TURN_DPS"]);
        Assert.Equal("45", GtaSanAndreasTurnSettings.LaunchEnvironment(smooth)["VRCLIENT_GTASA_SNAP_TURN_DEGREES"]);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            settings.Save(smooth with { SnapDegrees = 10 }));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            settings.Save(new GtaTurnPreference(GtaTurnMode.Smooth, 500)));
        Assert.Equal(smooth, settings.Load());
    }

    [Fact]
    public void Exact_x86_identity_reports_bridge_required_and_preserves_mod_surface()
    {
        using var fixture = Fixture.Create(x86: true);
        File.WriteAllText(Path.Combine(fixture.GameRoot, "GInputSA.asi"), "fixture");
        File.WriteAllText(Path.Combine(fixture.GameRoot, "GTASA_widescreen_fix.ini"), "fixture");
        Directory.CreateDirectory(Path.Combine(fixture.GameRoot, "scripts"));

        var result = fixture.Evaluate();

        Assert.Equal(GtaSanAndreasPreflightStatus.BridgeRequired, result.Status);
        Assert.True(result.GameIdentityReady);
        Assert.False(result.Ready);
        Assert.Contains("GInputSA.asi", result.ModArtifacts);
        Assert.Contains("GTASA_widescreen_fix.ini", result.ModArtifacts);
        Assert.Contains($"scripts{Path.DirectorySeparatorChar}", result.ModArtifacts);
    }

    [Fact]
    public void Wrong_architecture_fails_before_bridge_evaluation()
    {
        using var fixture = Fixture.Create(x86: false);

        var result = fixture.Evaluate();

        Assert.Equal(GtaSanAndreasPreflightStatus.ArchitectureMismatch, result.Status);
        Assert.Equal("x86", result.ExpectedArchitecture);
        Assert.Equal("x64", result.ActualArchitecture);
        Assert.False(result.GameIdentityReady);
    }

    [Fact]
    public void Pinned_x86_bridge_can_be_identity_ready_without_claiming_headset_playability()
    {
        using var fixture = Fixture.Create(x86: true);
        var bridge = Path.Combine(fixture.GameRoot, "VRClient.GTASA.asi");
        Fixture.WritePe(bridge, x86: true);
        fixture.PinBridge("VRClient.GTASA.asi", VrClient.Core.Hashing.Sha256OfFile(bridge));

        var result = fixture.Evaluate();

        Assert.Equal(GtaSanAndreasPreflightStatus.ReadyForExternalBridge, result.Status);
        Assert.True(result.Ready);
        Assert.Equal("x86", result.ActualArchitecture);
        Assert.DoesNotContain("VRClient.GTASA.asi", result.ModArtifacts);
    }

    [Fact]
    public void Theater_launch_dry_run_requires_bridge_and_loader_without_starting_game()
    {
        using var fixture = Fixture.Create(x86: true);
        var legacyDirectory = Path.Combine(fixture.Root, "config", "legacy");
        Directory.CreateDirectory(legacyDirectory);
        var bridge = Path.Combine(fixture.GameRoot, "vrclient_gtasa_theater.asi");
        Fixture.WritePe(bridge, x86: true);
        fixture.PinBridge("vrclient_gtasa_theater.asi", VrClient.Core.Hashing.Sha256OfFile(bridge));
        File.Copy(fixture.ProfilePath, Path.Combine(legacyDirectory, "gta-san-andreas.json"));
        var nativeConfigSource = Path.Combine(legacyDirectory, "gta-san-andreas-native.json");
        File.WriteAllText(nativeConfigSource, "{\"schema\":\"fixture\"}");
        File.Copy(nativeConfigSource, Path.Combine(fixture.GameRoot, "vrclient_gtasa_theater.json"));
        Fixture.WritePe(Path.Combine(fixture.GameRoot, "openxr_loader.dll"), x86: true);

        var controller = new AppController(
            fixture.Root,
            gtaSanAndreasLocator: () => fixture.GameRoot);
        var outcome = controller.LaunchGtaSanAndreasTheater(
            fixture.GameRoot, dryRun: true, xrRuntime: null);

        Assert.True(outcome.Ok, outcome.Message);
        Assert.Contains("dry run", outcome.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Theater_launch_refuses_an_unpinned_bridge()
    {
        using var fixture = Fixture.Create(x86: true);
        var legacyDirectory = Path.Combine(fixture.Root, "config", "legacy");
        Directory.CreateDirectory(legacyDirectory);
        File.Copy(fixture.ProfilePath, Path.Combine(legacyDirectory, "gta-san-andreas.json"));
        var nativeConfigSource = Path.Combine(legacyDirectory, "gta-san-andreas-native.json");
        File.WriteAllText(nativeConfigSource, "{\"schema\":\"fixture\"}");
        File.Copy(nativeConfigSource, Path.Combine(fixture.GameRoot, "vrclient_gtasa_theater.json"));
        Fixture.WritePe(Path.Combine(fixture.GameRoot, "vrclient_gtasa_theater.asi"), x86: true);
        Fixture.WritePe(Path.Combine(fixture.GameRoot, "openxr_loader.dll"), x86: true);

        var outcome = new AppController(fixture.Root, gtaSanAndreasLocator: () => fixture.GameRoot)
            .LaunchGtaSanAndreasTheater(fixture.GameRoot, dryRun: true, xrRuntime: null);

        Assert.False(outcome.Ok);
        Assert.Contains(nameof(GtaSanAndreasPreflightStatus.BridgeUnverified), outcome.Message);
    }

    [Fact]
    public void Theater_launch_refuses_a_modified_native_hook_profile()
    {
        using var fixture = Fixture.Create(x86: true);
        var legacyDirectory = Path.Combine(fixture.Root, "config", "legacy");
        Directory.CreateDirectory(legacyDirectory);
        var bridge = Path.Combine(fixture.GameRoot, "vrclient_gtasa_theater.asi");
        Fixture.WritePe(bridge, x86: true);
        fixture.PinBridge("vrclient_gtasa_theater.asi", VrClient.Core.Hashing.Sha256OfFile(bridge));
        File.Copy(fixture.ProfilePath, Path.Combine(legacyDirectory, "gta-san-andreas.json"));
        File.WriteAllText(Path.Combine(legacyDirectory, "gta-san-andreas-native.json"),
            "{\"schema\":\"fixture\"}");
        File.WriteAllText(Path.Combine(fixture.GameRoot, "vrclient_gtasa_theater.json"),
            "{\"schema\":\"modified\"}");
        Fixture.WritePe(Path.Combine(fixture.GameRoot, "openxr_loader.dll"), x86: true);

        var outcome = new AppController(fixture.Root, gtaSanAndreasLocator: () => fixture.GameRoot)
            .LaunchGtaSanAndreasTheater(fixture.GameRoot, dryRun: true, xrRuntime: null);

        Assert.False(outcome.Ok);
        Assert.Contains("differs from the pinned", outcome.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Desktop_launch_dry_run_needs_only_classic_x86_game_and_disables_vr()
    {
        using var fixture = Fixture.Create(x86: true);
        File.WriteAllText(Path.Combine(fixture.GameRoot, "example.asi"), "installed mod");
        var controller = new AppController(fixture.Root);

        var outcome = controller.LaunchGtaSanAndreasDesktop(fixture.GameRoot, dryRun: true);
        var child = AppController.GtaSanAndreasDesktopEnvironment();

        Assert.True(outcome.Ok, outcome.Message);
        Assert.Equal("1", child["VRCLIENT_GTASA_DISABLE"]);
        Assert.Equal("0", child["VRCLIENT_GTASA_STEREO"]);
        Assert.Equal("0", child["VRCLIENT_GTASA_INPUT"]);
    }

    [Fact]
    public void Vanilla_launch_requires_a_separate_clean_folder_and_rechecks_it_before_launch()
    {
        using var fixture = Fixture.Create(x86: true);
        var cleanFolder = Path.Combine(fixture.Root, "Clean San Andreas");
        Directory.CreateDirectory(cleanFolder);
        Fixture.WritePe(Path.Combine(cleanFolder, "gta_sa.exe"), x86: true);
        var settings = new GtaSanAndreasVanillaSettings(Path.Combine(fixture.Root, "vanilla.json"));
        var controller = new AppController(fixture.Root, gtaVanillaSettings: settings);

        Assert.False(controller.LaunchGtaSanAndreasVanilla(fixture.GameRoot, dryRun: true).Ok);
        Assert.False(controller.SetGtaSanAndreasVanillaFolder(fixture.GameRoot, fixture.GameRoot).Ok);
        Assert.Null(settings.Load());

        File.WriteAllText(Path.Combine(cleanFolder, "test.asi"), "mod");
        Assert.False(controller.SetGtaSanAndreasVanillaFolder(fixture.GameRoot, cleanFolder).Ok);
        File.Delete(Path.Combine(cleanFolder, "test.asi"));

        Assert.True(controller.SetGtaSanAndreasVanillaFolder(fixture.GameRoot, cleanFolder).Ok);
        Assert.Equal(cleanFolder, settings.Load());
        Assert.True(controller.LaunchGtaSanAndreasVanilla(fixture.GameRoot, dryRun: true).Ok);

        Directory.CreateDirectory(Path.Combine(cleanFolder, "modloader"));
        Assert.False(controller.LaunchGtaSanAndreasVanilla(fixture.GameRoot, dryRun: true).Ok);
    }

    [Fact]
    public void Ggmm_launcher_reports_missing_user_supplied_files_without_starting_game()
    {
        using var fixture = Fixture.Create(x86: true);

        var outcome = new AppController(fixture.Root).OpenGtaSanAndreasGgmm(fixture.GameRoot);

        Assert.False(outcome.Ok);
        Assert.Contains("ggmm.exe", outcome.Message);
        Assert.Contains("gtainterface.dll", outcome.Message);
    }

    [Fact]
    public void User_selected_san_andreas_mod_manager_is_remembered_without_installing_or_starting_it()
    {
        using var root = new TemporaryDirectory();
        var executable = Path.Combine(root.Path, "my-manager.exe");
        File.WriteAllText(executable, "test");
        var sami = Path.Combine(root.Path, "sami.exe");
        File.WriteAllText(sami, "test");
        var selectionPath = Path.Combine(root.Path, "selection.json");
        var settings = new GtaSanAndreasModManagerSettings(selectionPath);

        settings.Save(sami, GtaSanAndreasModManagerKind.Sami);

        Assert.Equal(sami, settings.Load());
        Assert.Equal(GtaSanAndreasModManagerKind.Sami, settings.LoadSelection()?.Kind);
        Assert.Null(GtaSanAndreasModManagerSettings.Validate(executable));
        Assert.False(File.Exists(Path.Combine(root.Path, "gta_sa.exe")));
        Assert.NotNull(GtaSanAndreasModManagerSettings.Validate(
            Path.Combine(root.Path, "missing.exe")));
        File.WriteAllText(Path.Combine(root.Path, "gta_sa.exe"), "test");
        Assert.NotNull(GtaSanAndreasModManagerSettings.Validate(
            Path.Combine(root.Path, "gta_sa.exe")));

        Assert.NotNull(GtaSanAndreasModManagerSettings.Validate(
            executable, GtaSanAndreasModManagerKind.Ggmm));
        var ggmm = Path.Combine(root.Path, "ggmm.exe");
        File.WriteAllText(ggmm, "test");
        settings.Save(ggmm, GtaSanAndreasModManagerKind.Ggmm);
        Assert.Equal(GtaSanAndreasModManagerKind.Ggmm, settings.LoadSelection()?.Kind);

        // Existing installations stored only the path; keep that selection as Custom.
        File.WriteAllText(selectionPath, JsonSerializer.Serialize(new { Executable = executable }));
        Assert.Equal(executable, settings.Load());
        Assert.Equal(GtaSanAndreasModManagerKind.Custom, settings.LoadSelection()?.Kind);
    }

    [Fact]
    public void Sami_selection_rejects_the_downloaded_setup_program()
    {
        using var root = new TemporaryDirectory();
        var installer = Path.Combine(root.Path, "San Andreas Mod Installer v1.0.exe");
        File.WriteAllText(installer, "setup fixture");
        var settings = new GtaSanAndreasModManagerSettings(
            Path.Combine(root.Path, "selection.json"));

        Assert.Contains("sami.exe", GtaSanAndreasModManagerSettings.Validate(
            installer, GtaSanAndreasModManagerKind.Sami), StringComparison.OrdinalIgnoreCase);
        Assert.Throws<InvalidOperationException>(() =>
            settings.Save(installer, GtaSanAndreasModManagerKind.Sami));
    }

    [Theory]
    [InlineData("ggmm.exe", GtaSanAndreasModManagerKind.Ggmm)]
    [InlineData("sami.exe", GtaSanAndreasModManagerKind.Sami)]
    [InlineData("San Andreas Mod Installer v1.0.exe", GtaSanAndreasModManagerKind.Sami)]
    [InlineData("another-manager.exe", GtaSanAndreasModManagerKind.Custom)]
    public void Generic_manager_picker_recognizes_known_tools_and_sami_setup(
        string fileName, GtaSanAndreasModManagerKind expected)
    {
        Assert.Equal(expected, GtaSanAndreasModManagerSettings.InferKind(fileName));
    }

    [Fact]
    public void ModLoader_profiles_separate_clean_and_modded_launches_without_overwriting_user_config()
    {
        using var fixture = Fixture.Create(x86: true);
        File.WriteAllText(Path.Combine(fixture.GameRoot, "modloader.asi"), "fixture");
        var modRoot = Path.Combine(fixture.GameRoot, "modloader");
        Directory.CreateDirectory(modRoot);
        File.WriteAllText(Path.Combine(modRoot, "modloader.ini"), "[Profiles.Default.Config]\n");

        var cleanArgs = GtaSanAndreasModLoaderProfiles.PrepareArguments(
            fixture.GameRoot, GtaSanAndreasVrMods.Clean, dryRun: false);
        var modsArgs = GtaSanAndreasModLoaderProfiles.PrepareArguments(
            fixture.GameRoot, GtaSanAndreasVrMods.ModLoader, dryRun: false);

        Assert.Equal(["-modprof", "VRClientClean"], cleanArgs);
        Assert.Equal(["-modprof", "VRClientMods"], modsArgs);
        Assert.Contains("IgnoreAllMods = true", File.ReadAllText(Path.Combine(
            modRoot, ".profiles", "VRClientClean.ini")));
        Assert.Contains("IgnoreAllMods = false", File.ReadAllText(Path.Combine(
            modRoot, ".profiles", "VRClientMods.ini")));
        Assert.Equal("[Profiles.Default.Config]\n", File.ReadAllText(Path.Combine(modRoot, "modloader.ini")));

        var plan = new LaunchPlan(Path.Combine(fixture.GameRoot, "gta_sa.exe"), true,
            new SafetyVerdict(Verdict.Allow, "offline", "", "test"));
        var startInfo = new GameLauncher().BuildStartInfo(plan, launchArguments: modsArgs);
        Assert.Equal(modsArgs, startInfo.ArgumentList);

        File.WriteAllText(Path.Combine(modRoot, ".profiles", "VRClientClean.ini"), "user change");
        Assert.Throws<InvalidOperationException>(() => GtaSanAndreasModLoaderProfiles.PrepareArguments(
            fixture.GameRoot, GtaSanAndreasVrMods.Clean, dryRun: false));
        Assert.Equal("user change", File.ReadAllText(Path.Combine(modRoot, ".profiles", "VRClientClean.ini")));
    }

    [Fact]
    public void ModLoader_launch_requires_installed_loader_but_clean_vr_does_not()
    {
        using var fixture = Fixture.Create(x86: true);
        Assert.Empty(GtaSanAndreasModLoaderProfiles.PrepareArguments(
            fixture.GameRoot, GtaSanAndreasVrMods.Clean, dryRun: false));
        Assert.Throws<InvalidOperationException>(() => GtaSanAndreasModLoaderProfiles.PrepareArguments(
            fixture.GameRoot, GtaSanAndreasVrMods.ModLoader, dryRun: false));
    }

    [Fact]
    public void ModLoader_dry_run_does_not_create_a_profile()
    {
        using var fixture = Fixture.Create(x86: true);
        File.WriteAllText(Path.Combine(fixture.GameRoot, "modloader.asi"), "fixture");
        Directory.CreateDirectory(Path.Combine(fixture.GameRoot, "modloader"));

        var arguments = GtaSanAndreasModLoaderProfiles.PrepareArguments(
            fixture.GameRoot, GtaSanAndreasVrMods.ModLoader, dryRun: true);

        Assert.Equal(["-modprof", "VRClientMods"], arguments);
        Assert.False(Directory.Exists(Path.Combine(fixture.GameRoot, "modloader", ".profiles")));
    }

    [Fact]
    public void Default_discovery_finds_the_known_layout_after_an_external_drive_letter_changes()
    {
        using var root = new TemporaryDirectory();
        var gameRoot = Path.Combine(
            root.Path, "Grand Theft Auto San Andreas + Utilities", "GTA San Andreas");
        Directory.CreateDirectory(gameRoot);
        File.WriteAllBytes(Path.Combine(gameRoot, "gta_sa.exe"), [0x4d, 0x5a]);

        var discovered = GtaSanAndreasPreflight.FindDefaultInstall(null, [root.Path]);

        Assert.Equal(Path.GetFullPath(gameRoot), discovered);
    }

    [Fact]
    public void Library_card_uses_the_classic_steam_app_id_for_official_artwork()
    {
        using var fixture = Fixture.Create(x86: true);
        var legacyDirectory = Path.Combine(fixture.Root, "config", "legacy");
        Directory.CreateDirectory(legacyDirectory);
        File.Copy(fixture.ProfilePath, Path.Combine(legacyDirectory, "gta-san-andreas.json"));

        var game = Assert.Single(new AppController(
            fixture.Root,
            gtaSanAndreasLocator: () => fixture.GameRoot).ListGames());

        Assert.Equal("12120", game.SteamAppId);
    }

    [Fact]
    public void Library_card_identifies_a_bridge_hash_mismatch_without_blaming_the_game_executable()
    {
        using var fixture = Fixture.Create(x86: true);
        var legacyDirectory = Path.Combine(fixture.Root, "config", "legacy");
        Directory.CreateDirectory(legacyDirectory);
        var bridge = Path.Combine(fixture.GameRoot, "vrclient_gtasa_theater.asi");
        Fixture.WritePe(bridge, x86: true);
        fixture.PinBridge("vrclient_gtasa_theater.asi", new string('0', 64));
        File.Copy(fixture.ProfilePath, Path.Combine(legacyDirectory, "gta-san-andreas.json"));

        var game = Assert.Single(new AppController(
            fixture.Root,
            gtaSanAndreasLocator: () => fixture.GameRoot).ListGames());

        Assert.Equal(GtaSanAndreasPreflightStatus.BridgeHashMismatch.ToString(), game.SafetyReason);
        Assert.Contains("VR bridge", game.ReadinessDetail, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("executable did not match", game.ReadinessDetail,
            StringComparison.OrdinalIgnoreCase);
    }

    private sealed class Fixture : IDisposable
    {
        private Fixture(string root, string gameRoot, string profilePath)
            => (Root, GameRoot, ProfilePath) = (root, gameRoot, profilePath);

        public string Root { get; }
        public string GameRoot { get; }
        public string ProfilePath { get; }

        public static Fixture Create(bool x86)
        {
            var root = Path.Combine(Path.GetTempPath(), $"vrclient-gta-sa-{Guid.NewGuid():N}");
            var gameRoot = Path.Combine(root, "GTA San Andreas");
            Directory.CreateDirectory(gameRoot);
            var executable = Path.Combine(gameRoot, "gta_sa.exe");
            WritePe(executable, x86);
            var profilePath = Path.Combine(root, "gta-san-andreas.json");
            File.WriteAllText(profilePath, JsonSerializer.Serialize(new
            {
                schema = "legacy-d3d9-vr/1",
                game = new
                {
                    slug = "gta-san-andreas",
                    display_name = "Grand Theft Auto: San Andreas",
                    executable_name = "gta_sa.exe",
                    required_architecture = "x86",
                    renderer = "d3d9-renderware",
                    executable_sha256_observed = VrClient.Core.Hashing.Sha256OfFile(executable)
                },
                bridge = new { status = "required", path = (string?)null, sha256 = (string?)null, architecture = "x86" },
                mod_surface = new { file_globs = new[] { "*.asi", "*.ini" }, directories = new[] { "cleo", "modloader", "scripts" } },
                safety = new { source = "manual", anti_cheat_risk = "none", online_risk = "offline_only", direct_online_launch = false, game_files_owned_by_user = true },
                conversion = new { status = "bridge_required", mod_coexistence = "preserve_existing_asi_surface", game_files_mutated_by_vrclient = false, required_bridge_capabilities = Array.Empty<string>(), ownership = new { runtime = "vrclient", stereo = "vrclient", camera = "vrclient", input = "vrclient", submission = "vrclient" } },
                verification = new { status = "game_identity_observed_bridge_missing", headset_verified = false, evidence = Array.Empty<string>() }
            }, new JsonSerializerOptions { WriteIndented = true }));
            return new Fixture(root, gameRoot, profilePath);
        }

        public void PinBridge(string path, string sha256)
        {
            using var document = JsonDocument.Parse(File.ReadAllText(ProfilePath));
            var root = document.RootElement;
            var json = JsonSerializer.Deserialize<Dictionary<string, object>>(root.GetRawText())!;
            json["bridge"] = new { status = "pinned", path, sha256, architecture = "x86" };
            File.WriteAllText(ProfilePath, JsonSerializer.Serialize(json));
        }

        public GtaSanAndreasPreflightResult Evaluate() =>
            new GtaSanAndreasPreflight().Evaluate(ProfilePath, GameRoot);

        public void Dispose() => Directory.Delete(Root, recursive: true);

        public static void WritePe(string path, bool x86)
        {
            var bytes = new byte[128];
            bytes[0] = 0x4d;
            bytes[1] = 0x5a;
            BitConverter.GetBytes(0x40).CopyTo(bytes, 0x3c);
            bytes[0x40] = 0x50;
            bytes[0x41] = 0x45;
            var machine = x86 ? (ushort)0x014c : (ushort)0x8664;
            BitConverter.GetBytes(machine).CopyTo(bytes, 0x44);
            File.WriteAllBytes(path, bytes);
        }
    }

    private sealed class TemporaryDirectory : IDisposable
    {
        public TemporaryDirectory()
        {
            Path = System.IO.Path.Combine(
                System.IO.Path.GetTempPath(), $"vrclient-gta-discovery-{Guid.NewGuid():N}");
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public void Dispose()
        {
            if (Directory.Exists(Path)) Directory.Delete(Path, recursive: true);
        }
    }
}
