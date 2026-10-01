using System.Text.Json;
using VrClient.Core.Discovery;

namespace VrClient.Core.Unreal;

public enum NativeUnrealPreflightStatus
{
    ReadyForInProcessObservation,
    GameNotFound,
    BuildMismatch,
    FingerprintUnpinned,
    HashMismatch,
    InvalidProfile
}

public sealed record NativeUnrealPreflightResult(
    NativeUnrealPreflightStatus Status,
    int ExitCode,
    string Detail,
    string GameSlug,
    string SteamAppId,
    string ProfilePath,
    string? InstallDir,
    string? ExecutablePath,
    string? ManifestPath,
    string? ExpectedBuildId,
    string? ActualBuildId,
    string? ExpectedSha256,
    string? ActualSha256);

public sealed class NativeUnrealPreflight
{
    public NativeUnrealPreflightResult Evaluate(
        string profilePath,
        string? steamRoot = null)
    {
        if (!File.Exists(profilePath))
            return Invalid(profilePath, "native profile does not exist");

        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(profilePath));
            var root = document.RootElement;
            if (root.GetProperty("schema").GetString() != "unreal-native/1")
                return Invalid(profilePath, "unsupported native profile schema");

            var gameProfile = root.GetProperty("game");
            var slug = RequiredString(gameProfile, "slug");
            var appId = RequiredString(gameProfile, "steam_app_id");
            var expectedBuildId = RequiredString(
                gameProfile, "steam_buildid_observed");
            var shippingBinary = RequiredString(
                gameProfile, "shipping_binary");
            var expectedSha256 = OptionalString(
                gameProfile, "executable_sha256_observed");

            var game = new SteamLibraryScanner().FindGame(
                appId, shippingBinary, steamRoot);
            if (game is null)
            {
                return Result(
                    NativeUnrealPreflightStatus.GameNotFound,
                    2,
                    $"Steam app {appId} with {shippingBinary} was not found",
                    slug,
                    appId,
                    profilePath,
                    expectedBuildId,
                    expectedSha256);
            }

            if (!string.Equals(
                    game.BuildId,
                    expectedBuildId,
                    StringComparison.Ordinal))
            {
                return Result(
                    NativeUnrealPreflightStatus.BuildMismatch,
                    4,
                    $"Steam build changed: expected {expectedBuildId}, found {game.BuildId ?? "(missing)"}",
                    slug,
                    appId,
                    profilePath,
                    expectedBuildId,
                    expectedSha256,
                    game);
            }

            var actualSha256 = Hashing.Sha256OfFile(game.ExecutablePath);
            if (string.IsNullOrWhiteSpace(expectedSha256))
            {
                return Result(
                    NativeUnrealPreflightStatus.FingerprintUnpinned,
                    5,
                    "Steam build matches, but the executable fingerprint is not pinned",
                    slug,
                    appId,
                    profilePath,
                    expectedBuildId,
                    expectedSha256,
                    game,
                    actualSha256);
            }

            if (!string.Equals(
                    actualSha256,
                    expectedSha256,
                    StringComparison.OrdinalIgnoreCase))
            {
                return Result(
                    NativeUnrealPreflightStatus.HashMismatch,
                    6,
                    "executable fingerprint changed",
                    slug,
                    appId,
                    profilePath,
                    expectedBuildId,
                    expectedSha256,
                    game,
                    actualSha256);
            }

            return Result(
                NativeUnrealPreflightStatus.ReadyForInProcessObservation,
                0,
                "Steam build and executable fingerprint match the native profile",
                slug,
                appId,
                profilePath,
                expectedBuildId,
                expectedSha256,
                game,
                actualSha256);
        }
        catch (Exception ex) when (
            ex is IOException or
            UnauthorizedAccessException or
            JsonException or
            InvalidOperationException or
            KeyNotFoundException)
        {
            return Invalid(profilePath, ex.Message);
        }
    }

    private static string RequiredString(JsonElement parent, string propertyName)
        => parent.GetProperty(propertyName).GetString() is { Length: > 0 } value
            ? value
            : throw new InvalidOperationException(
                $"native profile property '{propertyName}' is required");

    private static string? OptionalString(
        JsonElement parent,
        string propertyName)
        => parent.TryGetProperty(propertyName, out var value) &&
           value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static NativeUnrealPreflightResult Invalid(
        string profilePath,
        string detail)
        => new(
            NativeUnrealPreflightStatus.InvalidProfile,
            2,
            detail,
            "",
            "",
            profilePath,
            null,
            null,
            null,
            null,
            null,
            null,
            null);

    private static NativeUnrealPreflightResult Result(
        NativeUnrealPreflightStatus status,
        int exitCode,
        string detail,
        string slug,
        string appId,
        string profilePath,
        string expectedBuildId,
        string? expectedSha256,
        SteamGame? game = null,
        string? actualSha256 = null)
        => new(
            status,
            exitCode,
            detail,
            slug,
            appId,
            profilePath,
            game?.InstallDir,
            game?.ExecutablePath,
            game?.ManifestPath,
            expectedBuildId,
            game?.BuildId,
            expectedSha256,
            actualSha256);
}
