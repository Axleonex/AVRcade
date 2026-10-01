using System.Text.Json;
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
}
