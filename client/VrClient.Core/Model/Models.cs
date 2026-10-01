namespace VrClient.Core.Model;

/// One declared mod dependency in a *.modpack.json (namespace/name on Thunderstore).
public sealed record ModDependency(string Namespace, string Name);

/// A game's desired modpack: the ordered set of mods to install for VR.
public sealed record ModpackSpec(
    string GameSlug,
    string SteamAppId,
    string Community,                 // Thunderstore community slug, e.g. "repo"
    IReadOnlyList<ModDependency> Mods // install order; deps first
);

/// One pinned package in a lockfile.
public sealed record LockedPackage(
    string Namespace,
    string Name,
    string Version,
    string DownloadUrl,
    string Sha256,                    // lowercase hex of the downloaded zip
    long SizeBytes
);

/// The resolved, hash-pinned install set for a game.
public sealed record Lockfile(
    string GameSlug,
    string Community,
    string ResolvedAtUtc,             // ISO-8601; supplied by caller, never DateTime.Now inside pure code
    IReadOnlyList<LockedPackage> Packages
);

public enum Verdict { Allow, Warn, Block, UnknownBlocked }

public static class VerdictExtensions
{
    /// Both blocking verdicts refuse every modded or VR action the same way.
    public static bool IsBlocked(this Verdict verdict) => verdict is Verdict.Block or Verdict.UnknownBlocked;
}

public sealed record SafetyVerdict(
    Verdict Verdict,
    string ReasonCode,
    string MessageKey,
    string Explanation
);

public sealed record InstallResult(
    bool Installed,
    IReadOnlyList<string> InstalledFiles,   // repo-dir-relative paths written
    string? RefusalReason                   // null when Installed==true
);

public sealed record ComfortMapping(
    IReadOnlyDictionary<string, string> CfgKeyByProfileField // profileField -> "Section/Key"
);

public sealed record LaunchPlan(
    string GameExePath,
    bool ModPresent,
    SafetyVerdict Safety
);

public sealed record SessionEvidence(
    string GameSlug,
    string ModpackLockSha256,
    string LaunchedAtUtc,
    bool UserConfirmedVrStereo,
    bool UserConfirmedHeadTracking,
    string Notes
);
