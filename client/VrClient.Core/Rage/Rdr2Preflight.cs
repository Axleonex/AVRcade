namespace VrClient.Core.Rage;

using System.Text.Json;
using System.Text.RegularExpressions;
using VrClient.Core.Discovery;

public enum Rdr2PreflightStatus
{
    GameNotFound,
    ExecutableNotFound,
    ManifestNotFound,
    SteamBuildUnknown,
    FingerprintUnpinned,
    BuildMismatch,
    HashMismatch,
    SafetyUnreviewed,
    OnlineModeBlocked,
    ReadyForRageEvidence
}

public sealed record Rdr2PreflightResult(
    Rdr2PreflightStatus Status,
    int ExitCode,
    string? InstallRoot,
    string? ExecutablePath,
    string? ManifestPath,
    string? ExpectedBuildId,
    string? ActualBuildId,
    string? ExpectedSha256,
    string? ActualSha256,
    string? RequestedMode)
{
    public bool Ready => Status == Rdr2PreflightStatus.ReadyForRageEvidence;
}

/// <summary>Read-only, fail-closed eligibility evaluation for RDR2 Story Mode evidence.</summary>
public sealed class Rdr2Preflight
{
    public Rdr2PreflightResult Evaluate(string profilePath, SteamGame? discovered, string? requestedMode)
    {
        if (!string.Equals(requestedMode, "story", StringComparison.OrdinalIgnoreCase))
            return Result(Rdr2PreflightStatus.OnlineModeBlocked, 19, requestedMode: requestedMode);

        using var document = JsonDocument.Parse(File.ReadAllText(profilePath));
        var root = document.RootElement;
        var game = root.GetProperty("game");
        var expectedBuild = GetOptionalString(game, "steam_buildid_observed");
        var expectedHash = GetOptionalString(game, "executable_sha256_observed");
        var review = root.GetProperty("safety").GetProperty("review_status").GetString();

        if (discovered is null || !string.Equals(discovered.AppId, "1174180", StringComparison.Ordinal))
            return Result(Rdr2PreflightStatus.GameNotFound, 10, expectedBuild: expectedBuild, expectedHash: expectedHash, requestedMode: requestedMode);

        var installRoot = Path.GetFullPath(discovered.InstallDir);
        var executable = Path.GetFullPath(discovered.ExecutablePath);
        var manifest = Path.GetFullPath(discovered.ManifestPath);
        if (!File.Exists(executable))
            return Result(Rdr2PreflightStatus.ExecutableNotFound, 11, installRoot, executable, manifest, expectedBuild, expectedHash, requestedMode);
        if (!File.Exists(manifest))
            return Result(Rdr2PreflightStatus.ManifestNotFound, 12, installRoot, executable, manifest, expectedBuild, expectedHash, requestedMode);

        var actualBuild = ReadBuildId(manifest);
        if (actualBuild is null)
            return Result(Rdr2PreflightStatus.SteamBuildUnknown, 13, installRoot, executable, manifest, expectedBuild, expectedHash, requestedMode);
        if (string.IsNullOrWhiteSpace(expectedBuild) || string.IsNullOrWhiteSpace(expectedHash))
            return Result(Rdr2PreflightStatus.FingerprintUnpinned, 14, installRoot, executable, manifest, expectedBuild, expectedHash, requestedMode, actualBuild: actualBuild);
        if (!string.Equals(actualBuild, expectedBuild, StringComparison.Ordinal))
            return Result(Rdr2PreflightStatus.BuildMismatch, 15, installRoot, executable, manifest, expectedBuild, expectedHash, requestedMode, actualBuild: actualBuild);

        var actualHash = Hashing.Sha256OfFile(executable);
        if (!string.Equals(actualHash, expectedHash, StringComparison.OrdinalIgnoreCase))
            return Result(Rdr2PreflightStatus.HashMismatch, 16, installRoot, executable, manifest, expectedBuild, expectedHash, requestedMode, actualBuild, actualHash);
        if (!string.Equals(review, "reviewed_story_mode", StringComparison.Ordinal))
            return Result(review == "blocked" ? Rdr2PreflightStatus.OnlineModeBlocked : Rdr2PreflightStatus.SafetyUnreviewed,
                review == "blocked" ? 19 : 17, installRoot, executable, manifest, expectedBuild, expectedHash, requestedMode, actualBuild, actualHash);
        return Result(Rdr2PreflightStatus.ReadyForRageEvidence, 0, installRoot, executable, manifest, expectedBuild, expectedHash, requestedMode, actualBuild, actualHash);
    }

    private static Rdr2PreflightResult Result(Rdr2PreflightStatus status, int exitCode,
        string? installRoot = null, string? executable = null, string? manifest = null,
        string? expectedBuild = null, string? expectedHash = null, string? requestedMode = null,
        string? actualBuild = null, string? actualHash = null) =>
        new(status, exitCode, installRoot, executable, manifest, expectedBuild, actualBuild, expectedHash, actualHash, requestedMode);

    private static string? GetOptionalString(JsonElement parent, string property) =>
        parent.TryGetProperty(property, out var value) && value.ValueKind != JsonValueKind.Null ? value.GetString() : null;

    private static string? ReadBuildId(string manifestPath)
    {
        var match = Regex.Match(File.ReadAllText(manifestPath), "\\\"buildid\\\"\\s+\\\"([0-9]+)\\\"");
        return match.Success ? match.Groups[1].Value : null;
    }
}
