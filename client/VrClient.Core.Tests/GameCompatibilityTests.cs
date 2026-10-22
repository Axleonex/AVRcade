using System.Text.Json;
using System.IO.Compression;
using System.Text;
using VrClient.Core.App;
using VrClient.Core;
using VrClient.Core.Compatibility;
using VrClient.Core.Launch;
using VrClient.Core.Modpack;
using VrClient.Core.Model;

public sealed class GameCompatibilityTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "avrcade-compat-tests-" + Guid.NewGuid().ToString("N"));
    private string Repo => Path.Combine(_root, "app");
    private string GameRoot => Path.Combine(_root, "games", "older");
    private readonly GameInstallationStore _store;
    private readonly GameCompatibilityService _service;
    private GameView Game => new("sample", "Sample", "123", GameRoot, true, "Allow", "test", GameEngine.Unity, GameReadiness.ProfileUnverified, "Experimental");
    public GameCompatibilityTests()
    {
        Directory.CreateDirectory(Path.Combine(Repo, "config", "games"));
        Directory.CreateDirectory(GameRoot);
        File.WriteAllText(Path.Combine(Repo, "config", "games", "sample.json"), """{"executable_names":["Sample.exe"]}""");
        File.WriteAllText(Path.Combine(GameRoot, "Sample.exe"), "old-game-build");
        _store = new(Path.Combine(_root, "preferences", "installations.json"));
        _service = new(Repo, _store);
    }
    [Fact]
    public void First_observation_is_experimental_and_does_not_claim_headset_verification()
    {
        var state = _service.Inspect(Game);
        Assert.True(state.CanTryVr);
        Assert.False(state.ReviewedBuild);
        Assert.Contains("experimental", state.Message);
        Assert.Null(state.Profile!.HeadsetTested);
    }
    [Fact]
    public void Game_change_remains_detected_across_repeated_refreshes_and_restart()
    {
        _service.Inspect(Game);
        File.WriteAllText(Path.Combine(GameRoot, "Sample.exe"), "new-game-build");
        Assert.True(_service.Inspect(Game).BuildChanged);
        Assert.False(_service.Inspect(Game).CanTryVr);
        var restarted = new GameCompatibilityService(Repo, new GameInstallationStore(_store.PathOnDisk));
        Assert.False(restarted.Inspect(Game).CanTryVr);
    }
    [Fact]
    public void Human_confirmation_is_bound_to_game_hash_and_package_revision()
    {
        _service.Inspect(Game);
        File.WriteAllText(Path.Combine(GameRoot, "Sample.exe"), "new-game-build");
        Assert.True(_service.ConfirmHeadsetTest(Game).Ok);
        Assert.True(_service.Inspect(Game).CanTryVr);
        Directory.CreateDirectory(Path.Combine(Repo, "config", "modpacks"));
        File.WriteAllText(Path.Combine(Repo, "config", "modpacks", "sample.lock.json"), """{"game_slug":"sample","community":"sample","resolved_at_utc":"now","packages":[]}""");
        Assert.DoesNotContain("You confirmed VR", _service.Inspect(Game).Message);
    }
    [Fact]
    public void Profiles_and_automatic_update_preferences_are_separate_for_each_installation()
    {
        _service.SetAutomaticUpdates(Game, false);
        var newer = Game with { InstallDir = Path.Combine(_root, "newer") };
        Directory.CreateDirectory(newer.InstallDir!);
        File.WriteAllText(Path.Combine(newer.InstallDir!, "Sample.exe"), "newer-copy");
        Assert.True(_service.Inspect(newer).Profile!.AutomaticUpdates);
        Assert.False(_service.Inspect(Game).Profile!.AutomaticUpdates);
        Assert.True(_service.SelectInstallation(Game, newer.InstallDir!).Ok);
        Assert.Equal(newer.InstallDir, _store.SelectedForApp("123"));
        _store.UseDiscovered("sample");
        Assert.Null(_store.SelectedForApp("123"));
        Assert.False(_store.Profile("sample", GameRoot).AutomaticUpdates);
    }
    [Fact]
    public void Selecting_wrong_game_folder_preserves_previous_selection()
    {
        _service.SelectInstallation(Game, GameRoot);
        var wrong = Path.Combine(_root, "wrong"); Directory.CreateDirectory(wrong);
        Assert.Throws<FileNotFoundException>(() => _service.SelectInstallation(Game, wrong));
        Assert.Equal(GameRoot, _store.SelectedRoot("sample"));
    }
    [Fact]
    public void Unsupported_native_build_cannot_be_approved_by_user_confirmation()
    {
        Directory.CreateDirectory(Path.Combine(Repo, "config", "legacy"));
        File.WriteAllText(Path.Combine(Repo, "config", "legacy", "sample.json"), """{"game":{"executable_name":"Sample.exe","executable_sha256_observed":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}}""");
        var native = Game with { Engine = GameEngine.LegacyD3D9 };
        Assert.False(_service.Inspect(native).CanTryVr);
        Assert.False(_service.ConfirmHeadsetTest(native).Ok);
    }
    [Fact]
    public void Explicit_package_version_policy_accepts_supported_older_build_and_rejects_unknown_version()
    {
        var config = Path.Combine(Repo, "config", "games", "sample.json");
        File.WriteAllText(config, """{"executable_names":["Sample.exe"],"game_compatibility":{"supported_versions":["2.31"],"package_version":"0.1.7","source_url":"https://example.com/release"}}""");
        // The fake executable has no ProductVersion. Unknown is never treated as supported.
        Assert.False(_service.Inspect(Game).CanTryVr);
        Assert.False(_service.ConfirmHeadsetTest(Game).Ok);
        var supported = new GameCompatibilityService(Repo, _store, _ => "2.31");
        Assert.True(supported.Inspect(Game).CanTryVr);
        Assert.True(supported.Inspect(Game).ReviewedBuild);
        var updated = new GameCompatibilityService(Repo, _store, _ => "2.5");
        Assert.False(updated.Inspect(Game).CanTryVr);
        Assert.False(updated.ConfirmHeadsetTest(Game).Ok);
    }
    [Fact]
    public async Task Changed_unknown_build_is_blocked_before_any_update_callback_or_file_write()
    {
        _service.Inspect(Game);
        File.WriteAllText(Path.Combine(GameRoot, "Sample.exe"), "new-game-build");
        var calls = 0;
        var result = await _service.PrepareVrAsync(Game, _ => { calls++; return Task.FromResult(new ActionOutcome(true, "updated")); });
        Assert.False(result.Ok); Assert.Equal(0, calls);
        Assert.False(PackageRecovery.Exists(GameRoot));
    }
    [Fact]
    public async Task Automatic_updates_opt_out_is_honored_for_selected_installation()
    {
        _service.SetAutomaticUpdates(Game, false);
        var calls = 0;
        var result = await _service.PrepareVrAsync(Game with { ModInstalled = false }, _ => { calls++; return Task.FromResult(new ActionOutcome(true, "updated")); });
        Assert.False(result.Ok); Assert.Equal(0, calls);
        Assert.Null(_store.Profile("sample", GameRoot).PreparedPackage);
    }
    [Fact]
    public async Task Approved_update_failure_rolls_back_binary_but_preserves_user_configuration()
    {
        File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "old-loader");
        File.WriteAllText(Path.Combine(GameRoot, "doorstop_config.ini"), "user-settings");
        var result = await _service.PrepareVrAsync(Game with { ModInstalled = false }, _ =>
        {
            File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "new-loader");
            return Task.FromResult(new ActionOutcome(false, "failed"));
        });
        Assert.False(result.Ok);
        Assert.Equal("old-loader", File.ReadAllText(Path.Combine(GameRoot, "winhttp.dll")));
        Assert.Equal("user-settings", File.ReadAllText(Path.Combine(GameRoot, "doorstop_config.ini")));
    }
    [Fact]
    public async Task Game_update_during_package_install_restores_mods_and_refuses_launch()
    {
        File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "old-loader");
        var result = await _service.PrepareVrAsync(Game with { ModInstalled = false }, _ =>
        {
            File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "new-loader");
            File.WriteAllText(Path.Combine(GameRoot, "Sample.exe"), "steam-updated-game");
            return Task.FromResult(new ActionOutcome(true, "updated"));
        });
        Assert.False(result.Ok);
        Assert.Equal("old-loader", File.ReadAllText(Path.Combine(GameRoot, "winhttp.dll")));
        Assert.Equal("steam-updated-game", File.ReadAllText(Path.Combine(GameRoot, "Sample.exe")));
    }
    [Fact]
    public void Monitor_parking_and_restoration_survive_restart()
    {
        var path = Path.Combine(GameRoot, "winhttp.dll"); File.WriteAllText(path, "loader");
        Assert.Equal(1, MonitorModSession.Park(GameRoot, ["winhttp.dll"], () => { }));
        Assert.False(File.Exists(path));
        Assert.True(MonitorModSession.IsParked(GameRoot));
        Assert.Equal(1, MonitorModSession.Restore(GameRoot, () => { }));
        Assert.Equal("loader", File.ReadAllText(path));
    }
    [Fact]
    public void Monitor_restore_preflights_all_conflicts_and_preserves_foreign_edits()
    {
        File.WriteAllText(Path.Combine(GameRoot, "one.dll"), "one");
        File.WriteAllText(Path.Combine(GameRoot, "two.dll"), "two");
        MonitorModSession.Park(GameRoot, ["one.dll", "two.dll"], () => { });
        File.WriteAllText(Path.Combine(GameRoot, "two.dll"), "foreign");
        Assert.Throws<IOException>(() => MonitorModSession.Restore(GameRoot, () => { }));
        Assert.False(File.Exists(Path.Combine(GameRoot, "one.dll")));
        Assert.Equal("foreign", File.ReadAllText(Path.Combine(GameRoot, "two.dll")));
        Assert.True(MonitorModSession.IsParked(GameRoot));
    }
    [Theory]
    [InlineData("../outside.dll")]
    [InlineData("nested/../../outside.dll")]
    [InlineData(".avrcade/../outside.dll")]
    public void Monitor_parking_rejects_path_escape_before_moving_files(string path)
    { Assert.Throws<InvalidDataException>(() => MonitorModSession.Park(GameRoot, [path], () => { })); }
    [Fact]
    public void Running_game_refusal_never_parks_files()
    {
        File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "loader");
        Assert.Throws<IOException>(() => MonitorModSession.Park(GameRoot, ["winhttp.dll"], () => throw new IOException("running")));
        Assert.True(File.Exists(Path.Combine(GameRoot, "winhttp.dll")));
    }
    [Fact]
    public void Recovery_preserves_foreign_changes_and_does_not_touch_unrelated_files()
    {
        File.WriteAllText(Path.Combine(GameRoot, "plugin.dll"), "old");
        File.WriteAllText(Path.Combine(GameRoot, "save.dat"), "save");
        var backup = PackageRecovery.Capture(GameRoot, ["plugin.dll"]);
        File.WriteAllText(Path.Combine(GameRoot, "plugin.dll"), "new"); backup.Seal(["plugin.dll"]);
        File.WriteAllText(Path.Combine(GameRoot, "plugin.dll"), "third-party-edit");
        backup.Seal(["plugin.dll"]); // A retry cannot reclassify a foreign edit as owned output.
        Assert.Throws<IOException>(() => backup.Restore(() => { }));
        Assert.Equal("third-party-edit", File.ReadAllText(Path.Combine(GameRoot, "plugin.dll")));
        Assert.Equal("save", File.ReadAllText(Path.Combine(GameRoot, "save.dat")));
    }
    [Fact]
    public void Recovery_removes_only_new_owned_files_and_can_be_reopened()
    {
        var backup = PackageRecovery.Capture(GameRoot, []);
        File.WriteAllText(Path.Combine(GameRoot, "new-plugin.dll"), "new");
        File.WriteAllText(Path.Combine(GameRoot, "unrelated.txt"), "keep");
        backup.Seal(["new-plugin.dll"]);
        Assert.True(PackageRecovery.Open(GameRoot).PendingValidation);
        PackageRecovery.Open(GameRoot).Restore(() => { });
        Assert.False(File.Exists(Path.Combine(GameRoot, "new-plugin.dll")));
        Assert.True(File.Exists(Path.Combine(GameRoot, "unrelated.txt")));
    }
    [Fact]
    public void Startup_success_does_not_create_headset_verification()
    {
        _service.Inspect(Game);
        var backup = PackageRecovery.Capture(GameRoot, []); backup.Seal([]);
        _service.CompleteStartup(Game, true, false);
        Assert.False(PackageRecovery.Open(GameRoot).PendingValidation);
        Assert.Null(_service.Inspect(Game).Profile!.HeadsetTested);
    }
    [Fact]
    public void Confirmed_crash_rolls_back_provisional_update_and_prevents_retry_loop()
    {
        File.WriteAllText(Path.Combine(GameRoot, "plugin.dll"), "old");
        var backup = PackageRecovery.Capture(GameRoot, ["plugin.dll"]);
        File.WriteAllText(Path.Combine(GameRoot, "plugin.dll"), "bad-new"); backup.Seal(["plugin.dll"]);
        Assert.Contains("rolled back", _service.CompleteStartup(Game, false, true));
        Assert.Equal("old", File.ReadAllText(Path.Combine(GameRoot, "plugin.dll")));
        Assert.False(_store.Profile("sample", GameRoot).AutomaticUpdates);
    }
    [Fact]
    public void Startup_timeout_does_not_roll_back_unconfirmed_failures_or_authorization_waits()
    {
        var backup = PackageRecovery.Capture(GameRoot, []); backup.Seal([]);
        Assert.Equal("", _service.CompleteStartup(Game, false, false));
        Assert.True(PackageRecovery.Exists(GameRoot));
    }
    [Fact]
    public void Read_only_clean_launch_plan_does_not_park_or_change_any_files()
    {
        File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "loader");
        Assert.True(_service.LaunchCleanMonitor(Game, dryRun: true).Ok);
        Assert.True(File.Exists(Path.Combine(GameRoot, "winhttp.dll")));
        Assert.False(MonitorModSession.IsParked(GameRoot));
    }
    [Fact]
    public void Detached_older_copy_does_not_borrow_another_installations_manifest()
    {
        Assert.Null(GameLaunchBinding.OptionalManifestFor(GameRoot, "123"));
        var start = new System.Diagnostics.ProcessStartInfo("cli.exe");
        GameLaunchBinding.AddGame(start, GameRoot, "123");
        Assert.Equal(new[] { "--game-dir", GameRoot }, start.ArgumentList);
    }
    [Fact]
    public void Corrupt_preferences_are_reported_as_unknown_not_as_compatible()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_store.PathOnDisk)!);
        File.WriteAllText(_store.PathOnDisk, "{broken");
        Assert.False(_service.Inspect(Game).CanTryVr);
        Assert.Contains("could not complete", _service.Inspect(Game).Message);
        Assert.False(_service.Inspect(Game with { InstallDir = null }).CanTryVr);
    }
    [Fact]
    public void Manual_update_and_monitor_parking_share_one_exclusive_game_file_lease()
    {
        File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "loader");
        using var lease = MonitorModSession.AcquireUpdateLease(GameRoot);
        Assert.Throws<IOException>(() => MonitorModSession.Park(GameRoot, ["winhttp.dll"], () => { }));
        Assert.True(File.Exists(Path.Combine(GameRoot, "winhttp.dll")));
    }
    [Fact]
    public async Task Automatic_download_rejects_wrong_hash_without_querying_latest_upstream()
    {
        WriteModSpec();
        File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "existing-loader");
        var approved = new Lockfile("sample", "sample", "now", [new("Owner", "VR", "1.0.0",
            "https://thunderstore.io/package/download/Owner/VR/1.0.0/", new string('a', 64), 0)]);
        await Assert.ThrowsAsync<InvalidDataException>(() => ThunderstoreModUpdater.UpdateAsync(Repo, Game,
            new ForbiddenLatestIndex(), new BytesDownloader([1, 2, 3]), () => { }, reviewedLock: approved));
        Assert.Equal("existing-loader", File.ReadAllText(Path.Combine(GameRoot, "winhttp.dll")));
        Assert.False(File.Exists(Path.Combine(GameRoot, "BepInEx", "vrclient-install-manifest.json")));
    }
    [Fact]
    public async Task Approved_hash_pinned_install_preserves_existing_configuration()
    {
        WriteModSpec();
        var config = Path.Combine(GameRoot, "BepInEx", "config", "VR.cfg");
        Directory.CreateDirectory(Path.GetDirectoryName(config)!); File.WriteAllText(config, "custom-inputs");
        byte[] bytes;
        using (var memory = new MemoryStream())
        {
            using (var zip = new ZipArchive(memory, ZipArchiveMode.Create, true))
                foreach (var pair in new Dictionary<string, string> { ["BepInEx/plugins/VR/VR.dll"] = "approved-plugin",
                    ["BepInEx/core/BepInEx.dll"] = "loader", ["winhttp.dll"] = "proxy",
                    ["BepInEx/config/VR.cfg"] = "defaults" })
                { using var writer = new StreamWriter(zip.CreateEntry(pair.Key).Open()); writer.Write(pair.Value); }
            bytes = memory.ToArray();
        }
        var approved = new Lockfile("sample", "sample", "now", [new("Owner", "VR", "1.0.0",
            "https://thunderstore.io/package/download/Owner/VR/1.0.0/", Hashing.Sha256OfBytes(bytes), bytes.Length)]);
        var result = await ThunderstoreModUpdater.UpdateAsync(Repo, Game, new ForbiddenLatestIndex(),
            new BytesDownloader(bytes), () => { }, reviewedLock: approved);
        Assert.True(result.Ok, result.Message);
        Assert.Equal("custom-inputs", File.ReadAllText(config));
        Assert.Equal("approved-plugin", File.ReadAllText(Path.Combine(GameRoot, "BepInEx", "plugins", "VR", "VR.dll")));
    }
    private void WriteModSpec()
    {
        Directory.CreateDirectory(Path.Combine(Repo, "config", "modpacks"));
        File.WriteAllText(Path.Combine(Repo, "config", "modpacks", "sample.modpack.json"),
            """{"game_slug":"sample","steam_app_id":"123","community":"sample","mods":[{"namespace":"Owner","name":"VR"}]}""");
    }
    [Fact]
    public async Task Explicit_preparation_can_install_approved_package_without_enabling_automatic_updates()
    {
        _service.SetAutomaticUpdates(Game, false);
        var calls = 0;
        var result = await _service.PrepareVrAsync(Game with { ModInstalled = false }, _ =>
        { calls++; return Task.FromResult(new ActionOutcome(true, "updated")); }, forceUpdate: true);
        Assert.True(result.Ok); Assert.Equal(1, calls);
        Assert.False(_store.Profile("sample", GameRoot).AutomaticUpdates);
    }
    [Fact]
    public void Controller_assignments_are_isolated_by_installation_and_keep_runtime_routing_location()
    {
        var older = _service.ControllersFor(Game);
        var newer = _service.ControllersFor(Game with { InstallDir = Path.Combine(_root, "newer-copy") });
        var swapped = VrClient.Core.Config.ControllerCustomization.Swap(
            VrClient.Core.Config.ControllerCustomization.Identity(), "left_primary", "left_secondary");
        older.Save("sample", "quest-touch", swapped);
        Assert.NotEqual(older.Root, newer.Root);
        Assert.Equal("left_secondary", older.Load("sample", "quest-touch")["left_primary"]);
        Assert.Equal("left_primary", newer.Load("sample", "quest-touch")["left_primary"]);
        Assert.Equal(new VrClient.Core.Config.ControllerCustomization().RuntimeRoot, older.RuntimeRoot);
    }
    [Fact]
    public void New_mod_deployment_during_parked_session_is_not_misreported_as_clean()
    {
        File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "loader");
        MonitorModSession.Park(GameRoot, ["winhttp.dll"], () => { });
        File.WriteAllText(Path.Combine(GameRoot, "winhttp.dll"), "foreign-new-loader");
        Assert.Throws<IOException>(() => MonitorModSession.Park(GameRoot, ["winhttp.dll"], () => { }));
        Assert.Equal("foreign-new-loader", File.ReadAllText(Path.Combine(GameRoot, "winhttp.dll")));
    }
    private sealed class ForbiddenLatestIndex : IThunderstoreApi
    { public Task<string> GetPackageIndexJsonAsync(string community, CancellationToken ct = default) => throw new Exception("An automatic update must not resolve an unreviewed latest release."); }
    private sealed class BytesDownloader(byte[] data) : IHttpDownloader
    { public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default) => Task.FromResult(data); }
    [Fact]
    public void Separate_copy_cannot_be_handed_to_default_Steam_and_direct_plan_keeps_its_executable()
    {
        Assert.False(_service.CanUseStoreHandoff(Game));
        var outcome = _service.LaunchSelectedExecutable(Game, dryRun: true);
        Assert.True(outcome.Ok);
        Assert.Contains(Path.Combine(GameRoot, "Sample.exe"), outcome.Message);
        Assert.False(Directory.Exists(Path.Combine(GameRoot, ".avrcade")));
        File.Delete(Path.Combine(GameRoot, "Sample.exe"));
        Assert.False(_service.LaunchSelectedExecutable(Game, dryRun: true).Ok);
    }
    [Fact]
    public void Every_current_catalogue_game_has_a_compatibility_descriptor_and_clean_monitor_plan()
    {
        var source = FindSourceRoot();
        var controller = new AppController(source, locator: (_, _) => null, headsetConnected: () => false,
            gtaSanAndreasLocator: () => null, installationStore: _store);
        var games = controller.ListGames(false);
        Assert.Equal(10, games.Count);
        var service = new GameCompatibilityService(source, _store);
        foreach (var game in games)
        {
            var descriptor = service.Describe(game);
            Assert.False(string.IsNullOrWhiteSpace(descriptor.Executable));
            var root = Path.Combine(_root, "catalogue", game.Slug);
            var executable = Path.Combine(root, descriptor.Executable.Replace('\\', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(executable)!); File.WriteAllText(executable, "fake-game");
            Assert.True(service.LaunchCleanMonitor(game with { InstallDir = root }, dryRun: true).Ok);
            Assert.NotEmpty(service.CleanMonitorPaths(game with { InstallDir = root }));
        }
    }
    private static string FindSourceRoot()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory); directory is not null; directory = directory.Parent)
            if (File.Exists(Path.Combine(directory.FullName, "client", "VrClient.sln"))) return directory.FullName;
        throw new DirectoryNotFoundException("Source checkout not found.");
    }
    public void Dispose() { if (Directory.Exists(_root)) Directory.Delete(_root, true); }
}
