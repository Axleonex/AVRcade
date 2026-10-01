namespace VrClient.Core.Redengine;

using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

public enum CyberpunkNativePreflightStatus
{
    GameNotFound,
    ShippingBinaryNotFound,
    ConflictingHooks,
    DependenciesRequired,
    FingerprintUnpinned,
    SteamBuildUnknown,
    BuildMismatch,
    HashMismatch,
    ReadyForHeadsetValidation
}

public sealed record CyberpunkNativePreflightResult(
    CyberpunkNativePreflightStatus Status,
    int ExitCode,
    string? GameRoot,
    string? ExecutablePath,
    string? ManifestPath,
    string? ActualBuildId,
    string? ExpectedBuildId,
    string? ActualSha256,
    string? ExpectedSha256,
    IReadOnlyList<string> MissingDependencies,
    IReadOnlyList<string> Conflicts)
{
    public bool Ready => Status == CyberpunkNativePreflightStatus.ReadyForHeadsetValidation;

    /// The game on disk is a different build than the one the VR backend is pinned
    /// to. Status reports missing files first, so this is checked separately before
    /// offering to install them.
    public bool IdentityMismatch =>
        Differs(ActualSha256, ExpectedSha256, StringComparison.OrdinalIgnoreCase) ||
        Differs(ActualBuildId, ExpectedBuildId, StringComparison.Ordinal);

    private static bool Differs(string? actual, string? expected, StringComparison comparison) =>
        !string.IsNullOrWhiteSpace(actual) && !string.IsNullOrWhiteSpace(expected) &&
        !string.Equals(actual, expected, comparison);
}

/// Read-only, fail-closed validation for VRClient's Cyberpunk REDengine backend.
public sealed class CyberpunkNativePreflight
{
    public const string SteamAppId = "1091500";

    public CyberpunkNativePreflightResult Evaluate(
        string profilePath,
        string? gameRoot,
        string? steamManifestPath)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(profilePath));
        var root = document.RootElement;
        var game = root.GetProperty("game");
        var expectedBuild = game.GetProperty("steam_buildid_observed").GetString();
        var expectedHash = game.GetProperty("executable_sha256_observed").GetString();

        if (string.IsNullOrWhiteSpace(gameRoot) || !Directory.Exists(gameRoot))
            return Result(CyberpunkNativePreflightStatus.GameNotFound, 2);

        gameRoot = Path.GetFullPath(gameRoot);
        var executable = Resolve(gameRoot, game.GetProperty("shipping_binary").GetString()!);
        if (!File.Exists(executable))
            return Result(CyberpunkNativePreflightStatus.ShippingBinaryNotFound, 3, gameRoot, executable);

        var missing = root.GetProperty("dependencies").EnumerateArray()
            .Select(item => item.GetProperty("path").GetString()!)
            .Where(relative => !Path.Exists(Resolve(gameRoot, relative)))
            .ToArray();
        var conflicts = root.GetProperty("conflicts").EnumerateArray()
            .Select(item => item.GetProperty("path").GetString()!)
            .Where(relative => Path.Exists(Resolve(gameRoot, relative)))
            .ToArray();
        var actualHash = Sha256(executable);
        var manifest = !string.IsNullOrWhiteSpace(steamManifestPath)
            ? Path.GetFullPath(steamManifestPath)
            : null;
        var actualBuild = ReadBuildId(manifest);

        CyberpunkNativePreflightResult Complete(CyberpunkNativePreflightStatus status, int exitCode) =>
            new(status, exitCode, gameRoot, executable, manifest, actualBuild, expectedBuild,
                actualHash, expectedHash, missing, conflicts);

        if (conflicts.Length > 0)
            return Complete(CyberpunkNativePreflightStatus.ConflictingHooks, 4);
        if (missing.Length > 0)
            return Complete(CyberpunkNativePreflightStatus.DependenciesRequired, 5);
        if (string.IsNullOrWhiteSpace(expectedBuild) || string.IsNullOrWhiteSpace(expectedHash))
            return Complete(CyberpunkNativePreflightStatus.FingerprintUnpinned, 6);
        if (actualBuild is null)
            return Complete(CyberpunkNativePreflightStatus.SteamBuildUnknown, 7);
        if (!string.Equals(actualBuild, expectedBuild, StringComparison.Ordinal))
            return Complete(CyberpunkNativePreflightStatus.BuildMismatch, 8);
        if (!string.Equals(actualHash, expectedHash, StringComparison.OrdinalIgnoreCase))
            return Complete(CyberpunkNativePreflightStatus.HashMismatch, 9);
        return Complete(CyberpunkNativePreflightStatus.ReadyForHeadsetValidation, 0);

        CyberpunkNativePreflightResult Result(
            CyberpunkNativePreflightStatus status,
            int exitCode,
            string? resolvedRoot = null,
            string? resolvedExecutable = null) =>
            new(status, exitCode, resolvedRoot, resolvedExecutable, null, null, expectedBuild,
                null, expectedHash, [], []);
    }

    private static string Resolve(string root, string relative) =>
        Path.Combine(root, relative.Replace('\\', Path.DirectorySeparatorChar));

    private static string? ReadBuildId(string? manifestPath)
    {
        if (manifestPath is null || !File.Exists(manifestPath))
            return null;
        var match = Regex.Match(File.ReadAllText(manifestPath), "\"buildid\"\\s+\"([0-9]+)\"");
        return match.Success ? match.Groups[1].Value : null;
    }

    private static string Sha256(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }
}
