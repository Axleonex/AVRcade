namespace VrClient.Core.Fallout3;

public static class Fallout3BackendLabels
{
    public const string DepthVr = "Depth VR - Free";
    public const string NativeExperimental = "Native VR - Experimental";
    public const string NativeVerified = "Native VR - Verified";
    public const string Desktop = "Desktop";
}

public enum Fallout3Storefront { Steam, Gog, Manual }
public enum Fallout3InstallSupport { Supported, ManualUnverified, WrongDirectory, UnsupportedBuild }
public enum Fallout3Backend { DepthVr, NativeVr, Desktop }
public enum Fallout3BackendStatus { Unavailable, Experimental, Verified }
public enum Fallout3Severity { Pass, Warning, Failure }
public enum Fallout3ConversionMode { Convert, Repair, Restore, Uninstall }

public sealed record Fallout3Install(
    string RootDirectory,
    string ExecutablePath,
    Fallout3Storefront Storefront,
    Fallout3InstallSupport Support,
    string BuildIdentity,
    string ExecutableSha256,
    string? FileVersion,
    IReadOnlyDictionary<string, bool> Dlc,
    IReadOnlyList<string> Diagnostics);

public sealed record Fallout3DiscoveryResult(
    IReadOnlyList<Fallout3Install> Installs,
    IReadOnlyList<string> Diagnostics);

public sealed record Fallout3Check(
    string Component,
    Fallout3Severity Severity,
    string Code,
    string Message,
    string? EvidencePath = null,
    string? DetectedVersion = null);

public sealed record Fallout3DependencyOptions(
    string? ModOrganizerExecutablePath,
    string? ModOrganizerProfileDirectory,
    string? ReShadeDirectory,
    string? Depth3DDirectory,
    string? OsirisExecutablePath,
    string? NativeAdapterPath,
    string? NativeHookProfilePath,
    string? NativeStereoProofPath,
    IReadOnlyCollection<string>? RunningProcessNames = null,
    string? ActiveOpenXrRuntimePath = null);

public sealed record Fallout3Readiness(
    IReadOnlyList<Fallout3Check> Checks,
    Fallout3Backend RequestedBackend,
    Fallout3Backend EffectiveBackend,
    Fallout3BackendStatus NativeStatus,
    string ActiveLabel,
    IReadOnlyList<string> FallbackReasons)
{
    public bool Ready => Checks.All(check => check.Severity != Fallout3Severity.Failure);
}

public sealed record Fallout3ConversionRequest(
    string ModOrganizerInstanceDirectory,
    string SourceProfileName,
    string VrProfileName,
    string GameDirectory,
    Fallout3Backend Backend,
    Fallout3ConversionMode Mode,
    bool DryRun,
    bool AcknowledgeMutation);

public enum Fallout3FileOperationKind
{
    CreateDirectory,
    CopyIfMissing,
    WriteIfMissing,
    WriteManagedFile,
    DeleteManagedFile,
    DeleteOwnedProfile
}

public sealed record Fallout3FileOperation(
    Fallout3FileOperationKind Kind,
    string Source,
    string Destination,
    string Description);

public sealed record Fallout3ConversionPlan(
    Fallout3ConversionRequest Request,
    string SourceProfileDirectory,
    string VrProfileDirectory,
    string BackendLabel,
    IReadOnlyList<Fallout3FileOperation> Operations,
    IReadOnlyList<string> Warnings);

public sealed record Fallout3ConversionResult(
    bool Changed,
    bool RecoveredInterruptedTransaction,
    IReadOnlyList<string> AppliedOperations,
    IReadOnlyList<string> Warnings);

public enum Fallout3CompatibilityStatus
{
    VerifiedCompatible,
    LikelyCompatible,
    RequiresVrConfiguration,
    ConflictsWithDepthVr,
    ConflictsWithNativeVr,
    Incompatible,
    Unknown
}

public sealed record Fallout3CompatibilityEntry(
    string Name,
    Fallout3CompatibilityStatus Status,
    string Category,
    string Evidence,
    string SuggestedAction);

public sealed record Fallout3CompatibilityReport(
    string GeneratedAtUtc,
    string ProfileDirectory,
    string BackendLabel,
    IReadOnlyList<Fallout3CompatibilityEntry> Entries);

public enum Fallout3LaunchComponentKind { SteamVr, Osiris, ModOrganizer }

public sealed record Fallout3LaunchComponent(
    Fallout3LaunchComponentKind Kind,
    string ExecutablePath,
    string Arguments,
    string WorkingDirectory,
    bool StartOnlyWhenNotRunning,
    bool StopOnCleanup,
    IReadOnlyDictionary<string, string>? Environment = null);

public sealed record Fallout3LaunchPlan(
    Fallout3Backend EffectiveBackend,
    string BackendLabel,
    IReadOnlyList<Fallout3LaunchComponent> Components,
    string LogDirectory,
    IReadOnlyList<string> Diagnostics);

public sealed record Fallout3StartedProcess(
    Fallout3LaunchComponent Component,
    int ProcessId,
    bool StartedByVrClient,
    string LogPath);
