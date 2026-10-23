using VrClient.Core.App;
using VrClient.Core.Config;
using VrClient.Core.Launch;

namespace VrClient.Core.Compatibility;

/// Describes recovery choices. Creating this plan never changes a game or its store branch.
public sealed record GameVersionRecovery(string GameName, string InstalledBuild, string RequiredBuild,
    string Requirement, string? SteamPropertiesUri, bool SeparateInstallation)
{
    public string Title => "Use a compatible version of " + GameName;
    public bool HasSteamOptions => SteamPropertiesUri is not null;
    public string SaveWarning => GameCompatibilityService.SaveWarning;
    public string StoreInstructions =>
        "In Steam, open Game Versions & Betas (called Betas in older clients). " +
        "Select a publisher-provided branch only if it matches the required game version above. " +
        "Steam manages the download and replaces its installed branch. Wait until it finishes, then return and recheck. " +
        "The required version may not be offered; if it is absent, choose an existing compatible copy or use clean monitor play.";
    public string InstallationNote => SeparateInstallation
        ? "You selected a separate game copy. Steam changes its registered installation, which may be a different folder. Choose that folder in AVRcade afterwards if needed."
        : "Steam properties apply to Steam's registered installation. AVRcade rechecks your selected folder before enabling VR.";

    public static GameVersionRecovery? Create(GameView game, GameCompatibilityState state,
        CompatibilityDescriptor descriptor, GameVersionCompatibility? policy)
    {
        if (state.CanTryVr || state.Identity is not { } identity || game.InstallDir is null) return null;
        var mismatch = policy is { Supported: false } ||
            descriptor.ExpectedHash is { } hash && !hash.Equals(identity.ExecutableSha256, StringComparison.OrdinalIgnoreCase) ||
            descriptor.ExpectedVersion is { } version && version != identity.Version ||
            state.BuildChanged && policy is null && descriptor.ExpectedHash is null && descriptor.ExpectedVersion is null;
        if (!mismatch) return null; // A missing runtime or another setup problem is not a reason to downgrade.
        var required = policy is not null ? string.Join(", ", policy.SupportedVersions)
            : descriptor.ExpectedVersion ?? "the reviewed game build for this VR integration";
        var requirement = policy is not null ? $"VR package {policy.PackageVersion} supports game version(s): {required}."
            : descriptor.ExpectedVersion is not null ? $"This VR integration requires game version {required}."
            : "Select the integration's reviewed build or a previously headset-tested copy. An arbitrary older version is not guaranteed to work.";
        var appId = game.SteamAppId;
        var uri = appId.Length > 0 && appId.All(char.IsAsciiDigit) && uint.TryParse(appId, out var id) && id > 0
            ? "steam://gameproperties/" + appId : null;
        var separate = uri is null || GameLaunchBinding.OptionalManifestFor(game.InstallDir, appId) is null;
        return new(game.DisplayName, identity.Version ?? identity.SteamBuild ?? "version not reported",
            required, requirement, uri, separate);
    }
}
