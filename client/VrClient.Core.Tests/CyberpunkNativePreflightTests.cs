using System.Text.Json;
using System.Text.Json.Nodes;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkNativePreflightTests
{
    [Fact]
    public void Evaluate_requires_every_backend_dependency_and_refuses_retired_bridge()
    {
        using var fixture = CyberpunkFixture.Create();
        var preflight = new CyberpunkNativePreflight();

        var missing = preflight.Evaluate(fixture.Profile, fixture.GameRoot, fixture.Manifest);
        Assert.Equal(CyberpunkNativePreflightStatus.DependenciesRequired, missing.Status);

        fixture.InstallDependencies();
        var ready = preflight.Evaluate(fixture.Profile, fixture.GameRoot, fixture.Manifest);
        Assert.Equal(CyberpunkNativePreflightStatus.ReadyForHeadsetValidation, ready.Status);

        Directory.CreateDirectory(Path.Combine(fixture.GameRoot, "red4ext", "plugins", "VRClient.REDengine"));
        var conflict = preflight.Evaluate(fixture.Profile, fixture.GameRoot, fixture.Manifest);
        Assert.Equal(CyberpunkNativePreflightStatus.ConflictingHooks, conflict.Status);
    }

    [Fact]
    public void Evaluate_fails_closed_when_the_game_build_changes()
    {
        using var fixture = CyberpunkFixture.Create();
        fixture.InstallDependencies();
        File.WriteAllText(fixture.Manifest, "\"AppState\" { \"buildid\" \"999\" }");

        var result = new CyberpunkNativePreflight().Evaluate(
            fixture.Profile, fixture.GameRoot, fixture.Manifest);

        Assert.Equal(CyberpunkNativePreflightStatus.BuildMismatch, result.Status);
        Assert.Equal(8, result.ExitCode);
    }

    private sealed class CyberpunkFixture : IDisposable
    {
        private readonly string[] dependencies =
        [
            "red4ext\\RED4ext.dll",
            "red4ext\\plugins\\CyberpunkVR_Stereo\\CyberpunkVR_Stereo.dll",
            "engine\\tools\\scc.exe"
        ];

        private CyberpunkFixture(string root, string gameRoot, string manifest, string profile)
        {
            Root = root;
            GameRoot = gameRoot;
            Manifest = manifest;
            Profile = profile;
        }

        public string Root { get; }
        public string GameRoot { get; }
        public string Manifest { get; }
        public string Profile { get; }

        public void UseVersionPolicy(string version = "2.31")
        {
            var profile = JsonNode.Parse(File.ReadAllText(Profile))!;
            profile["game_compatibility"] = JsonSerializer.SerializeToNode(new {
                supported_versions = new[] { version }, package_version = "0.1.7",
                source_url = "https://github.com/dariulone/cyberpunk-vr-port/blob/0.1.7/README.md#requirements" });
            File.WriteAllText(Profile, profile.ToJsonString());
        }

        public static CyberpunkFixture Create()
        {
            var root = Path.Combine(Path.GetTempPath(), $"vrclient-cyberpunk-{Guid.NewGuid():N}");
            var gameRoot = Path.Combine(root, "Cyberpunk 2077");
            var executable = Path.Combine(gameRoot, "bin", "x64", "Cyberpunk2077.exe");
            Directory.CreateDirectory(Path.GetDirectoryName(executable)!);
            File.WriteAllBytes(executable, [1, 2, 3]);
            var manifest = Path.Combine(root, "appmanifest_1091500.acf");
            File.WriteAllText(manifest, "\"AppState\" { \"buildid\" \"123456\" }");
            var profile = Path.Combine(root, "profile.json");
            File.WriteAllText(profile, JsonSerializer.Serialize(new
            {
                game = new
                {
                    steam_buildid_observed = "123456",
                    shipping_binary = "bin\\x64\\Cyberpunk2077.exe",
                    executable_sha256_observed = VrClient.Core.Hashing.Sha256OfFile(executable)
                },
                dependencies = new[]
                {
                    new { path = "red4ext\\RED4ext.dll", reason = "loader" },
                    new { path = "red4ext\\plugins\\CyberpunkVR_Stereo\\CyberpunkVR_Stereo.dll", reason = "backend" },
                    new { path = "engine\\tools\\scc.exe", reason = "redscript" }
                },
                conflicts = new[]
                {
                    new { path = "bin\\x64\\dxgi.dll", reason = "proxy" },
                    new { path = "red4ext\\plugins\\VRClient.REDengine", reason = "retired bridge" }
                }
            }));
            return new CyberpunkFixture(root, gameRoot, manifest, profile);
        }

        public void InstallDependencies()
        {
            foreach (var relative in dependencies)
            {
                var path = Path.Combine(GameRoot, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllText(path, "fixture");
            }
        }

        public void Dispose() => Directory.Delete(Root, recursive: true);
    }

    [Theory]
    [InlineData("2.31")]
    [InlineData("2.31.0.0")]
    [InlineData("2.31+store-build")]
    public void SupportedProductVersionDoesNotRequireTheDevelopersBuildOrHash(string version)
    {
        using var fixture = CyberpunkFixture.Create();
        fixture.InstallDependencies(); fixture.UseVersionPolicy();
        File.WriteAllText(fixture.Manifest, "\"AppState\" { \"buildid\" \"999\" }");
        File.WriteAllBytes(Path.Combine(fixture.GameRoot, "bin", "x64", "Cyberpunk2077.exe"), [9, 8, 7]);
        var result = new CyberpunkNativePreflight(_ => version).Evaluate(fixture.Profile, fixture.GameRoot, fixture.Manifest);
        Assert.True(result.Ready); Assert.False(result.IdentityMismatch);
        Assert.NotEqual(result.ActualSha256, result.ExpectedSha256);
        Assert.NotEqual(result.ActualBuildId, result.ExpectedBuildId);
    }

    [Theory]
    [InlineData("2.5")]
    [InlineData("2.32")]
    [InlineData(null)]
    public void UnsupportedOrUnknownVersionsDoNotBecomeVrCompatibleAfterSteamValidation(string? version)
    {
        using var fixture = CyberpunkFixture.Create();
        fixture.InstallDependencies(); fixture.UseVersionPolicy();
        var result = new CyberpunkNativePreflight(_ => version).Evaluate(fixture.Profile, fixture.GameRoot, fixture.Manifest);
        Assert.Equal(CyberpunkNativePreflightStatus.BuildMismatch, result.Status);
        Assert.True(result.IdentityMismatch);
        Assert.False(result.CanLaunch(CyberpunkLaunchMode.Vr));
        Assert.True(result.CanLaunch(CyberpunkLaunchMode.Flat));
        Assert.Contains(version ?? "unknown", result.CompatibilityDetail);
        Assert.Contains("2.31", result.CompatibilityDetail);
        Assert.Contains("Update mods", result.CompatibilityDetail);
    }

    [Fact]
    public void SupportedVersionsStillRequireVrDependenciesAndRejectConflictingHooks()
    {
        using var fixture = CyberpunkFixture.Create(); fixture.UseVersionPolicy();
        var checker = new CyberpunkNativePreflight(_ => "2.31");
        Assert.False(checker.Evaluate(fixture.Profile, fixture.GameRoot, fixture.Manifest).Ready);
        fixture.InstallDependencies();
        File.WriteAllText(Path.Combine(fixture.GameRoot, "bin", "x64", "dxgi.dll"), "unmanaged hook");
        Assert.Equal(CyberpunkNativePreflightStatus.ConflictingHooks,
            checker.Evaluate(fixture.Profile, fixture.GameRoot, fixture.Manifest).Status);
    }

    [Fact]
    public void InvalidCompatibilityMetadataFailsClosedAndMissingExecutableCannotLaunchFlat()
    {
        using var fixture = CyberpunkFixture.Create(); fixture.UseVersionPolicy("*");
        Assert.Throws<InvalidDataException>(() => new CyberpunkNativePreflight(_ => "2.31")
            .Evaluate(fixture.Profile, fixture.GameRoot, fixture.Manifest));
        File.Delete(Path.Combine(fixture.GameRoot, "bin", "x64", "Cyberpunk2077.exe"));
        Assert.False(new CyberpunkNativePreflight().Evaluate(fixture.Profile, fixture.GameRoot, fixture.Manifest)
            .CanLaunch(CyberpunkLaunchMode.Flat));
    }

    [Fact]
    public void PackagedCompatibilityMetadataNamesTheActualReviewedVrRelease()
    {
        var source = TestRepoRoot.Find();
        using var profile = JsonDocument.Parse(File.ReadAllText(Path.Combine(source, "config", "redengine", "native", "cyberpunk-2077.json")));
        var compatibility = VrClient.Core.Config.GameVersionCompatibility.Read(profile.RootElement, "2.5")!;
        var package = CyberpunkBackendInstaller.LoadPackage(Path.Combine(source, "config", "redengine", "native", "cyberpunk-2077-backend.json"));
        Assert.Equal(package.Version, compatibility.PackageVersion);
        Assert.True(compatibility.Supported);
        Assert.Contains("/" + package.Version + "/", compatibility.SourceUrl);
    }

    [Fact]
    public void OlderInstalledVrPackageRequiresAnUpdateBeforeTheNewCompatibilityPolicyCanLaunch()
    {
        using var fixture = CyberpunkFixture.Create(); fixture.InstallDependencies();
        var executable = Path.Combine(fixture.GameRoot, "bin", "x64", "Cyberpunk2077.exe");
        File.Copy(typeof(CyberpunkNativePreflight).Assembly.Location, executable, true);
        fixture.UseVersionPolicy(System.Diagnostics.FileVersionInfo.GetVersionInfo(executable).ProductVersion!.Split('+')[0]);
        var profile = JsonNode.Parse(File.ReadAllText(fixture.Profile))!;
        profile["game"]!["slug"] = "cyberpunk-2077";
        profile["game"]!["display_name"] = "Cyberpunk 2077";
        profile["game"]!["steam_app_id"] = "1091500";
        profile["game_compatibility"]!["package_version"] = CyberpunkBackendInstaller.LoadPackage(
            Path.Combine(TestRepoRoot.Find(), "config", "redengine", "native", "cyberpunk-2077-backend.json")).Version;
        var native = Path.Combine(fixture.Root, "config", "redengine", "native"); Directory.CreateDirectory(native);
        File.WriteAllText(Path.Combine(native, "cyberpunk-2077.json"), profile.ToJsonString());
        File.Copy(Path.Combine(TestRepoRoot.Find(), "config", "redengine", "native", "cyberpunk-2077-backend.json"),
            Path.Combine(native, "cyberpunk-2077-backend.json"));
        Directory.CreateDirectory(CyberpunkUpstreamSession.Store(fixture.GameRoot));
        File.WriteAllText(CyberpunkUpstreamSession.StatePath(fixture.GameRoot),
            JsonSerializer.Serialize(new CyberpunkUpstreamSession.InstallState("older", [], [])));
        var controller = new VrClient.Core.App.AppController(fixture.Root, (_, _) => fixture.GameRoot, headsetConnected: () => true);
        var game = Assert.Single(controller.ListGames());
        Assert.False(game.ModInstalled);
        Assert.Equal(VrClient.Core.App.GameReadiness.NeedsVrMod, game.Readiness);
        profile["game_compatibility"]!["supported_versions"] = JsonSerializer.SerializeToNode(new[] { "*" });
        File.WriteAllText(Path.Combine(native, "cyberpunk-2077.json"), profile.ToJsonString());
        var invalid = Assert.Single(controller.ListGames());
        Assert.Equal(VrClient.Core.App.GameReadiness.Blocked, invalid.Readiness);
        Assert.Equal("cyberpunk_configuration_invalid", invalid.SafetyReason);
    }
}
