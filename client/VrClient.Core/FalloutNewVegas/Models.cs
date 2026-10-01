namespace VrClient.Core.FalloutNewVegas;

public enum FnvStorefront
{
    Steam,
    Gog,
    Manual
}

public enum FnvInstallSupport
{
    Supported,
    ManualUnverified,
    WrongDirectory,
    UnsupportedEdition
}

public sealed record FnvGameInstall(
    string RootDirectory,
    string ExecutablePath,
    FnvStorefront Storefront,
    FnvInstallSupport Support,
    string BuildIdentity,
    string ExecutableSha256,
    string? FileVersion,
    IReadOnlyList<string> Diagnostics);

public sealed record FnvDiscoveryResult(
    IReadOnlyList<FnvGameInstall> Installs,
    IReadOnlyList<string> Diagnostics);

public enum FnvCheckSeverity
{
    Pass,
    Warning,
    Failure
}

public sealed record FnvCheckResult(
    string Component,
    FnvCheckSeverity Severity,
    string Code,
    string Message,
    string? EvidencePath = null,
    string? DetectedVersion = null);

public sealed record FnvPrerequisiteOptions(
    string? ModOrganizerExecutablePath,
    string? ModOrganizerProfileDirectory,
    string? TrackerExecutablePath,
    string? NativeAdapterExecutablePath,
    string? SteamVrDirectory,
    bool UseVirtualDesktop,
    IReadOnlyCollection<string>? RunningProcessNames = null,
    string? ActiveOpenXrRuntimePath = null);

public sealed record FnvReadinessReport(IReadOnlyList<FnvCheckResult> Checks)
{
    public bool Ready => Checks.All(check => check.Severity != FnvCheckSeverity.Failure);
}

public enum FnvCompatibilityStatus
{
    Verified,
    Warned,
    Incompatible,
    Unknown
}

public sealed record FnvCompatibilityEntry(
    string Name,
    FnvCompatibilityStatus Status,
    string Category,
    string Explanation,
    string SuggestedAction);

public sealed record FnvCompatibilityReport(
    string GeneratedAtUtc,
    string ProfileDirectory,
    IReadOnlyList<FnvCompatibilityEntry> Mods,
    IReadOnlyList<FnvCompatibilityEntry> RootDlls);

public enum FnvConversionMode
{
    Convert,
    Repair,
    Restore
}

public sealed record FnvConversionRequest(
    string ModOrganizerInstanceDirectory,
    string SourceProfileName,
    string VrProfileName,
    string GameDirectory,
    FnvConversionMode Mode,
    bool DryRun,
    bool AcknowledgeMutation);

public enum FnvFileOperationKind
{
    CreateDirectory,
    CopyIfMissing,
    WriteManagedFile,
    DeleteManagedFile,
    WriteIfMissing
}

public sealed record FnvFileOperation(
    FnvFileOperationKind Kind,
    string Source,
    string Destination,
    string Description);

public sealed record FnvConversionPlan(
    FnvConversionRequest Request,
    string SourceProfileDirectory,
    string VrProfileDirectory,
    IReadOnlyList<FnvFileOperation> Operations,
    IReadOnlyList<string> Warnings);

public sealed record FnvConversionResult(
    bool Changed,
    bool RecoveredInterruptedTransaction,
    IReadOnlyList<string> AppliedOperations,
    IReadOnlyList<string> Warnings);

public enum FnvLaunchComponentKind
{
    VirtualDesktop,
    SteamVr,
    FnvrTracker,
    NewVegas,
    NativeOpenXrAdapter
}

public sealed record FnvLaunchComponent(
    FnvLaunchComponentKind Kind,
    string ExecutablePath,
    string Arguments,
    string WorkingDirectory,
    bool StartOnlyWhenNotRunning,
    bool StopOnCleanup,
    IReadOnlyDictionary<string, string>? EnvironmentVariables = null);

public sealed record FnvLaunchPlan(
    IReadOnlyList<FnvLaunchComponent> Components,
    string LogDirectory,
    bool UsesVirtualDesktop,
    IReadOnlyList<string> Warnings);

public sealed record FnvStartedProcess(
    FnvLaunchComponent Component,
    int ProcessId,
    bool StartedByVrClient,
    string LogPath);
