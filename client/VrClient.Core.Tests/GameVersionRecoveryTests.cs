using System.Diagnostics;
using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Compatibility;

public sealed class GameVersionRecoveryTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "avrcade-version-recovery-" + Guid.NewGuid().ToString("N"));
    private string Repo => Path.Combine(_root, "app");
    private string GameRoot => Path.Combine(_root, "separate-copy");
    private string Preferences => Path.Combine(_root, "preferences", "installations.json");
    private GameView Game => new("sample", "Sample", "1091500", GameRoot, true, "Allow", "test",
        GameEngine.Unity, GameReadiness.ProfileUnverified, "Experimental");
    private GameCompatibilityService Service(string? version = "2.5") => new(Repo, new GameInstallationStore(Preferences), _ => version);
    public GameVersionRecoveryTests()
    {
        Directory.CreateDirectory(Path.Combine(Repo, "config", "games"));
        Directory.CreateDirectory(GameRoot);
        File.WriteAllText(Path.Combine(GameRoot, "Sample.exe"), "unchanged-game-build");
        File.WriteAllText(Path.Combine(Repo, "config", "games", "sample.json"),
            """{"executable_names":["Sample.exe"],"game_compatibility":{"supported_versions":["2.31"],"package_version":"0.1.7","source_url":"https://example.com/reviewed"}}""");
    }
    [Fact]
    public void Mismatch_offers_exact_reviewed_version_without_claiming_store_availability()
    {
        var plan = Service().VersionRecoveryFor(Game);
        Assert.NotNull(plan);
        Assert.Equal("2.5", plan.InstalledBuild);
        Assert.Equal("2.31", plan.RequiredBuild);
        Assert.Contains("0.1.7", plan.Requirement);
        Assert.Contains("may not be offered", plan.StoreInstructions);
        Assert.True(plan.SeparateInstallation);
        Assert.Contains("different folder", plan.InstallationNote);
        Assert.Contains("Back up saves", plan.SaveWarning);
        Assert.False(File.Exists(Preferences));
    }
    [Theory]
    [InlineData("2.31")]
    [InlineData("2.31.0.0")]
    public void Supported_build_does_not_offer_an_unnecessary_downgrade(string version) =>
        Assert.Null(Service(version).VersionRecoveryFor(Game));
    [Fact]
    public void Unrelated_setup_block_does_not_offer_a_game_downgrade() =>
        Assert.Null(Service("2.31").VersionRecoveryFor(Game with { Readiness = GameReadiness.Blocked, ReadinessDetail = "Runtime missing" }));
    [Fact]
    public void Newly_changed_but_supported_build_does_not_offer_rollback_for_a_runtime_problem()
    {
        Service().Inspect(Game);
        File.WriteAllText(Path.Combine(GameRoot, "Sample.exe"), "supported-store-build");
        var game = Game with { Readiness = GameReadiness.Blocked, ReadinessDetail = "Runtime missing" };
        var service = Service("2.31");
        Assert.True(service.Inspect(game).BuildChanged);
        Assert.Null(service.VersionRecoveryFor(game));
    }
    [Fact]
    public void Unknown_installed_version_still_shows_the_package_requirement()
    {
        var plan = Service(null).VersionRecoveryFor(Game);
        Assert.NotNull(plan);
        Assert.Equal("version not reported", plan.InstalledBuild);
        Assert.Equal("2.31", plan.RequiredBuild);
    }
    [Fact]
    public void Missing_installation_has_no_rollback_action() =>
        Assert.Null(Service().VersionRecoveryFor(Game with { InstallDir = null }));
    [Theory]
    [InlineData("1091500/../../quit")]
    [InlineData("0")]
    [InlineData("")]
    [InlineData("１２３")]
    public void Invalid_store_identity_cannot_form_a_protocol_request(string appId)
    {
        var game = Game with { SteamAppId = appId };
        var calls = 0;
        Assert.Null(Service().VersionRecoveryFor(game)!.SteamPropertiesUri);
        Assert.False(Service().OpenStoreVersionOptions(game, _ => calls++).Ok);
        Assert.Equal(0, calls);
    }
    [Fact]
    public void Store_handoff_is_only_a_properties_request_and_preserves_game_and_preference_state()
    {
        var service = Service();
        service.SetAutomaticUpdates(Game, false);
        var preferences = File.ReadAllText(Preferences);
        ProcessStartInfo? start = null;
        var result = service.OpenStoreVersionOptions(Game, info => start = info);
        Assert.True(result.Ok);
        Assert.NotNull(start);
        Assert.Equal("steam://gameproperties/1091500", start.FileName);
        Assert.Empty(start.ArgumentList);
        Assert.True(start.UseShellExecute);
        Assert.Contains("has not changed the game version", result.Message);
        Assert.Contains("availability has not been checked", result.Message);
        Assert.Equal(preferences, File.ReadAllText(Preferences));
        Assert.Equal("unchanged-game-build", File.ReadAllText(Path.Combine(GameRoot, "Sample.exe")));
        Assert.False(service.Inspect(Game).CanTryVr);
        Assert.False(Directory.Exists(Path.Combine(GameRoot, ".avrcade")));
    }
    [Fact]
    public void Exact_native_build_mismatch_has_recovery_but_no_invented_version_number()
    {
        File.Delete(Path.Combine(Repo, "config", "games", "sample.json"));
        Directory.CreateDirectory(Path.Combine(Repo, "config", "legacy"));
        File.WriteAllText(Path.Combine(Repo, "config", "legacy", "sample.json"),
            """{"game":{"executable_name":"Sample.exe","executable_sha256_observed":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}}""");
        var plan = Service().VersionRecoveryFor(Game with { Engine = GameEngine.LegacyD3D9 });
        Assert.NotNull(plan);
        Assert.Contains("arbitrary older version is not guaranteed", plan.Requirement);
        Assert.DoesNotContain("2.31", plan.Requirement);
    }
    [Fact]
    public void Previously_observed_changed_build_offers_recovery_without_fabricating_a_supported_version()
    {
        File.WriteAllText(Path.Combine(Repo, "config", "games", "sample.json"), """{"executable_names":["Sample.exe"]}""");
        var service = Service();
        service.Inspect(Game);
        File.WriteAllText(Path.Combine(GameRoot, "Sample.exe"), "updated-game-build");
        var plan = service.VersionRecoveryFor(Game);
        Assert.NotNull(plan);
        Assert.Contains("previously headset-tested copy", plan.Requirement);
        Assert.False(service.Inspect(Game).CanTryVr);
    }
    [Fact]
    public void Returning_after_a_real_supported_build_change_clears_only_the_version_block()
    {
        Assert.NotNull(Service().VersionRecoveryFor(Game));
        var serviceAfterStoreChange = Service("2.31");
        Assert.Null(serviceAfterStoreChange.VersionRecoveryFor(Game));
        Assert.True(serviceAfterStoreChange.Inspect(Game).CanTryVr);
        Assert.Null(new GameInstallationStore(Preferences).Profile("sample", GameRoot).HeadsetTested);
    }
    public void Dispose() => Directory.Delete(_root, true);
}
