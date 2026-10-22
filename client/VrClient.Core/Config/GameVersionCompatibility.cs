using System.Text.Json;

namespace VrClient.Core.Config;

/// The VR package's supported game versions, independent of a developer's Steam build/hash.
/// Adapters opt into this policy; exact-binary native bridges retain their own checks.
public sealed record GameVersionCompatibility(string? InstalledVersion, IReadOnlyList<string> SupportedVersions,
    string PackageVersion, string SourceUrl)
{
    public bool Supported => Normalize(InstalledVersion) is { } actual &&
        SupportedVersions.Any(version => Normalize(version) == actual);
    public string Detail => $"Installed game version: {InstalledVersion ?? "unknown"}. " +
        $"VR package {PackageVersion} supports: {string.Join(", ", SupportedVersions)}. " +
        (Supported ? "Game version is compatible; Steam build IDs and executable hashes may differ between copies."
            : "Use Update mods to check for a compatible VR package. Steam verification repairs game files; it does not establish VR compatibility.");
    public const string RecoveryInstructions = "Use Update mods to check this game's VR releases. AVRcade installs only a supported package. " +
        "If none supports your version, use a mod-supported version offered by Steam or wait for a compatible VR release. Then Recheck setup. " +
        "Steam verification is for missing or damaged files and does not restore an older game version.";

    public static GameVersionCompatibility? Read(JsonElement profile, string? installedVersion)
    {
        if (!profile.TryGetProperty("game_compatibility", out var policy)) return null;
        var versions = policy.GetProperty("supported_versions").EnumerateArray().Select(item => item.GetString()!).ToArray();
        var package = policy.GetProperty("package_version").GetString();
        var source = policy.GetProperty("source_url").GetString();
        if (versions.Length == 0 || versions.Any(version => !Version.TryParse(version, out _)) ||
            string.IsNullOrWhiteSpace(package) || !Uri.TryCreate(source, UriKind.Absolute, out var uri) || uri.Scheme != "https")
            throw new InvalidDataException("The VR package has invalid game compatibility metadata.");
        return new(installedVersion, versions, package, source!);
    }

    private static string? Normalize(string? value)
    {
        if (value is null) return null;
        var parts = value.Trim().Split('+', 2);
        if (parts.Length == 2 && (parts[1].Length == 0 || parts[1].Any(c => !char.IsAsciiLetterOrDigit(c) && c is not ('.' or '-'))))
            return null;
        if (!Version.TryParse(parts[0], out var version)) return null;
        return $"{version.Major}.{version.Minor}.{Math.Max(0, version.Build)}.{Math.Max(0, version.Revision)}";
    }
}
