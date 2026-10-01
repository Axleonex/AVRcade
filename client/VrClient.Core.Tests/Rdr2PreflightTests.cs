using System.Text.Json;
using VrClient.Core;
using VrClient.Core.Discovery;
using VrClient.Core.Rage;

namespace VrClient.Core.Tests;

public sealed class Rdr2PreflightTests
{
    [Theory]
    [InlineData(Rdr2PreflightStatus.GameNotFound, 10)]
    [InlineData(Rdr2PreflightStatus.ExecutableNotFound, 11)]
    [InlineData(Rdr2PreflightStatus.ManifestNotFound, 12)]
    [InlineData(Rdr2PreflightStatus.SteamBuildUnknown, 13)]
    [InlineData(Rdr2PreflightStatus.FingerprintUnpinned, 14)]
    [InlineData(Rdr2PreflightStatus.BuildMismatch, 15)]
    [InlineData(Rdr2PreflightStatus.HashMismatch, 16)]
    [InlineData(Rdr2PreflightStatus.SafetyUnreviewed, 17)]
    [InlineData(Rdr2PreflightStatus.OnlineModeBlocked, 19)]
    public void Every_refusal_has_a_distinct_nonzero_exit_code(Rdr2PreflightStatus expected, int exitCode)
    {
        using var fixture = Rdr2Fixture.Create();
        var result = expected switch
        {
            Rdr2PreflightStatus.GameNotFound => fixture.EvaluateMissingGame(),
            Rdr2PreflightStatus.ExecutableNotFound => fixture.Evaluate(deleteExecutable: true),
            Rdr2PreflightStatus.ManifestNotFound => fixture.Evaluate(deleteManifest: true),
            Rdr2PreflightStatus.SteamBuildUnknown => fixture.Evaluate(manifestText: "\"AppState\" { }"),
            Rdr2PreflightStatus.FingerprintUnpinned => fixture.Evaluate(build: null, hash: null),
            Rdr2PreflightStatus.BuildMismatch => fixture.Evaluate(build: "999"),
            Rdr2PreflightStatus.HashMismatch => fixture.Evaluate(hash: new string('0', 64)),
            Rdr2PreflightStatus.SafetyUnreviewed => fixture.Evaluate(review: "unknown"),
            Rdr2PreflightStatus.OnlineModeBlocked => fixture.Evaluate(mode: "online"),
            _ => throw new ArgumentOutOfRangeException(nameof(expected))
        };
        Assert.Equal(expected, result.Status);
        Assert.Equal(exitCode, result.ExitCode);
        Assert.False(result.Ready);
    }

    [Fact]
    public void Only_a_pinned_reviewed_story_fixture_is_ready_and_hash_comparison_is_case_insensitive()
    {
        using var fixture = Rdr2Fixture.Create();
        var result = fixture.Evaluate(hash: fixture.Hash.ToUpperInvariant(), review: "reviewed_story_mode");
        Assert.Equal(Rdr2PreflightStatus.ReadyForRageEvidence, result.Status);
        Assert.Equal(0, result.ExitCode);
        Assert.True(result.Ready);
    }

    [Fact]
    public void Mode_is_checked_before_profile_or_executable_reads()
    {
        var result = new Rdr2Preflight().Evaluate("missing-profile.json", null, null);
        Assert.Equal(Rdr2PreflightStatus.OnlineModeBlocked, result.Status);
        Assert.Equal(19, result.ExitCode);
    }

    [Fact]
    public void App_id_mismatch_and_blocked_review_fail_closed()
    {
        using var fixture = Rdr2Fixture.Create();
        var wrongApp = new SteamGame
        {
            AppId = "999",
            InstallDir = fixture.Game.InstallDir,
            ExeName = fixture.Game.ExeName,
            ExecutablePath = fixture.Game.ExecutablePath,
            ManifestPath = fixture.Game.ManifestPath,
            BuildId = fixture.Game.BuildId
        };
        Assert.Equal(Rdr2PreflightStatus.GameNotFound, fixture.Evaluate(discovered: wrongApp).Status);
        Assert.Equal(Rdr2PreflightStatus.OnlineModeBlocked, fixture.Evaluate(review: "blocked").Status);
    }

    private sealed class Rdr2Fixture : IDisposable
    {
        private Rdr2Fixture(string root, string profile, SteamGame game, string hash) => (Root, Profile, Game, Hash) = (root, profile, game, hash);
        public string Root { get; }
        public string Profile { get; }
        public SteamGame Game { get; }
        public string Hash { get; }

        public static Rdr2Fixture Create()
        {
            var root = Path.Combine(Path.GetTempPath(), $"vrclient-rdr2-{Guid.NewGuid():N}");
            var gameRoot = Path.Combine(root, "Red Dead Redemption 2");
            Directory.CreateDirectory(gameRoot);
            var executable = Path.Combine(gameRoot, "RDR2.exe");
            File.WriteAllBytes(executable, [1, 2, 3]);
            var manifest = Path.Combine(root, "appmanifest_1174180.acf");
            File.WriteAllText(manifest, "\"AppState\" { \"buildid\" \"123\" }");
            var profile = Path.Combine(root, "profile.json");
            var game = new SteamGame { AppId = "1174180", InstallDir = gameRoot, ExeName = "RDR2.exe", ExecutablePath = executable, ManifestPath = manifest, BuildId = "123" };
            return new(root, profile, game, Hashing.Sha256OfFile(executable));
        }

        public Rdr2PreflightResult Evaluate(string? build = "123", string? hash = null, string review = "reviewed_story_mode", string? mode = "story", SteamGame? discovered = null, bool deleteExecutable = false, bool deleteManifest = false, string? manifestText = null)
        {
            if (deleteExecutable) File.Delete(Game.ExecutablePath);
            if (deleteManifest) File.Delete(Game.ManifestPath);
            if (manifestText is not null) File.WriteAllText(Game.ManifestPath, manifestText);
            File.WriteAllText(Profile, JsonSerializer.Serialize(new { game = new { steam_buildid_observed = build, executable_sha256_observed = hash ?? Hash }, safety = new { review_status = review } }));
            return new Rdr2Preflight().Evaluate(Profile, discovered ?? Game, mode);
        }

        public Rdr2PreflightResult EvaluateMissingGame()
        {
            File.WriteAllText(Profile, JsonSerializer.Serialize(new { game = new { steam_buildid_observed = "123", executable_sha256_observed = Hash }, safety = new { review_status = "reviewed_story_mode" } }));
            return new Rdr2Preflight().Evaluate(Profile, null, "story");
        }

        public void Dispose() => Directory.Delete(Root, recursive: true);
    }
}
