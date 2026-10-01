namespace VrClient.Core.Platform;

public enum HostPlatform
{
    Windows,
    Linux,
    Unknown
}

public enum PlatformCompatibilityState
{
    Compatible,
    CompatibleViaProton,
    Unsupported
}

public sealed record HostPlatformInfo(HostPlatform Platform, bool IsSteamOs, bool OpenXrRuntimeAvailable);

public sealed record PackagePlatformSupport(bool Windows, bool NativeLinux, bool Proton);

public sealed record PlatformCompatibility(PlatformCompatibilityState State, string ReasonCode)
{
    public bool CanUse => State is PlatformCompatibilityState.Compatible or PlatformCompatibilityState.CompatibleViaProton;
}

/// <summary>
/// Platform policy is intentionally independent from process launch. A caller must still satisfy
/// title safety, package integrity, and runtime readiness before using a compatible result.
/// </summary>
public static class ReleasePlatformCompatibility
{
    public static PlatformCompatibility Evaluate(HostPlatformInfo host, PackagePlatformSupport package)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(package);

        if (host.Platform == HostPlatform.Windows)
            return package.Windows
                ? new(PlatformCompatibilityState.Compatible, "windows_supported")
                : new(PlatformCompatibilityState.Unsupported, "windows_not_supported");

        if (host.Platform == HostPlatform.Linux)
        {
            if (package.NativeLinux)
                return new(PlatformCompatibilityState.Compatible, "linux_native_supported");
            if (host.IsSteamOs && package.Proton)
                return new(PlatformCompatibilityState.CompatibleViaProton, "steamos_proton_supported");
            return new(PlatformCompatibilityState.Unsupported,
                host.IsSteamOs ? "steamos_proton_not_supported" : "linux_not_supported");
        }

        return new(PlatformCompatibilityState.Unsupported, "host_platform_unknown");
    }

    public static string NormalizeRelativePackagePath(string value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value);
        var normalized = value.Replace('\\', '/');
        if (Path.IsPathRooted(value) || normalized.Split('/').Any(segment => segment is "" or "." or ".."))
            throw new ArgumentException("Package path must be a non-empty relative path without traversal.", nameof(value));
        return string.Join(Path.DirectorySeparatorChar, normalized.Split('/'));
    }
}
