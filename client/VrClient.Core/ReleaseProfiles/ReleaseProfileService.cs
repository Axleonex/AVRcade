using System.Text.Json;

namespace VrClient.Core.ReleaseProfiles;

public enum ReleaseLaunchMode
{
    Flat,
    VrClient,
    ExternalVrLaunchOnly
}

public enum VariantAvailabilityState
{
    Available,
    MissingPackage,
    UnsupportedBuild,
    SafetyBlocked,
    LaunchOnly
}

public enum ControllerPreference
{
    Default,
    QuestXboxStyle,
    QuestPlayStationStyle,
    IndexXboxStyle,
    IndexPlayStationStyle,
    Custom
}

public enum TurnPreference
{
    GameDefault,
    Snap,
    Smooth
}

public sealed record PlayerVrPreferences(
    ControllerPreference Controller,
    bool HudVisible,
    double HudScale,
    TurnPreference Turn,
    bool ComfortVignette)
{
    public static PlayerVrPreferences Default { get; } = new(
        ControllerPreference.Default,
        HudVisible: true,
        HudScale: 1.0,
        TurnPreference.GameDefault,
        ComfortVignette: false);
}

public sealed record ReleaseVariant(
    string VariantId,
    string GameId,
    string DisplayName,
    ReleaseLaunchMode LaunchMode,
    bool SupportedBuild,
    bool SafetyAllowed,
    IReadOnlyList<string> RequiredPackageIds);

public sealed record VariantAvailability(
    VariantAvailabilityState State,
    string ReasonCode,
    bool CanLaunch,
    bool CanInject);

public sealed record SavedReleaseProfile(int SchemaVersion, PlayerVrPreferences Preferences);

public static class ReleaseVariantEvaluator
{
    public static VariantAvailability Evaluate(ReleaseVariant variant, ISet<string> installedPackageIds)
    {
        ArgumentNullException.ThrowIfNull(variant);
        ArgumentNullException.ThrowIfNull(installedPackageIds);

        if (!variant.SupportedBuild)
            return new(VariantAvailabilityState.UnsupportedBuild, "unsupported_build", false, false);
        if (!variant.SafetyAllowed)
            return new(VariantAvailabilityState.SafetyBlocked, "safety_blocked", false, false);
        if (variant.RequiredPackageIds.Any(package => !installedPackageIds.Contains(package)))
            return new(VariantAvailabilityState.MissingPackage, "missing_required_package", false, false);
        if (variant.LaunchMode == ReleaseLaunchMode.ExternalVrLaunchOnly)
            return new(VariantAvailabilityState.LaunchOnly, "external_vr_launch_only", true, false);

        return new(VariantAvailabilityState.Available,
            variant.LaunchMode == ReleaseLaunchMode.Flat ? "flat_launch" : "vrclient_ready",
            true,
            variant.LaunchMode == ReleaseLaunchMode.VrClient);
    }
}

/// <summary>Persists only user-owned, title-neutral presentation and control preferences.</summary>
public sealed class ReleaseProfileStore
{
    private const int CurrentSchemaVersion = 1;
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private readonly string _root;

    public ReleaseProfileStore(string root)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(root);
        _root = Path.GetFullPath(root);
    }

    public PlayerVrPreferences Load(string profileId)
    {
        var path = ProfilePath(profileId);
        if (!File.Exists(path))
            return PlayerVrPreferences.Default;

        var saved = JsonSerializer.Deserialize<SavedReleaseProfile>(File.ReadAllText(path), JsonOptions)
            ?? throw new InvalidDataException("Release profile is empty or invalid.");
        if (saved.SchemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported release profile schema {saved.SchemaVersion}.");
        ValidatePreferences(saved.Preferences);
        return saved.Preferences;
    }

    public void Save(string profileId, PlayerVrPreferences preferences)
    {
        ValidatePreferences(preferences);
        var path = ProfilePath(profileId);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + $".{Guid.NewGuid():N}.tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(new SavedReleaseProfile(CurrentSchemaVersion, preferences), JsonOptions));
        File.Move(temporary, path, overwrite: true);
    }

    private string ProfilePath(string profileId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(profileId);
        if (profileId.Any(character => !(char.IsLetterOrDigit(character) || character is '.' or '-' or '_')))
            throw new ArgumentException("Profile IDs may contain only letters, digits, '.', '-', and '_'.", nameof(profileId));
        return Path.Combine(_root, "profiles", profileId + ".json");
    }

    private static void ValidatePreferences(PlayerVrPreferences preferences)
    {
        ArgumentNullException.ThrowIfNull(preferences);
        if (preferences.HudScale is < 0.5 or > 2.0)
            throw new ArgumentOutOfRangeException(nameof(preferences), "HUD scale must be between 0.5 and 2.0.");
    }
}
