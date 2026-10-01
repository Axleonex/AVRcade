using System.Text;
using System.Text.Json;
using VrClient.Core.Distribution;

namespace VrClient.Core.ReleaseDiagnostics;

public enum DoctorSeverity
{
    Info,
    Warning,
    Blocker
}

public sealed record DoctorFinding(string Code, DoctorSeverity Severity, string Message);

public sealed record ReleaseDoctorInput(
    bool OpenXrRuntimeAvailable,
    ReleasePackageHealth PackageHealth,
    bool RollbackAvailable,
    bool WriteLocationAvailable);

public sealed record ReleaseDoctorReport(IReadOnlyList<DoctorFinding> Findings)
{
    public bool CanLaunch => Findings.All(finding => finding.Severity != DoctorSeverity.Blocker);
}

public static class ReleaseDoctor
{
    public static ReleaseDoctorReport Evaluate(ReleaseDoctorInput input)
    {
        ArgumentNullException.ThrowIfNull(input);
        var findings = new List<DoctorFinding>();
        if (!input.OpenXrRuntimeAvailable)
            findings.Add(new("openxr_runtime_missing", DoctorSeverity.Blocker, "No active OpenXR runtime was reported."));
        if (input.PackageHealth == ReleasePackageHealth.Missing)
            findings.Add(new("release_package_missing", DoctorSeverity.Blocker, "The active AVRcade package is missing."));
        if (input.PackageHealth == ReleasePackageHealth.Corrupt)
            findings.Add(new("release_package_corrupt", DoctorSeverity.Blocker, "The active AVRcade package failed its integrity check."));
        if (!input.RollbackAvailable)
            findings.Add(new("rollback_unavailable", DoctorSeverity.Warning, "No previous package version is available for rollback."));
        if (!input.WriteLocationAvailable)
            findings.Add(new("diagnostic_location_unavailable", DoctorSeverity.Warning, "Diagnostic export location is not writable."));
        if (findings.Count == 0)
            findings.Add(new("ready", DoctorSeverity.Info, "Shared release prerequisites are ready."));
        return new(findings);
    }
}

public sealed record DiagnosticEntry(string Code, string Message);

public sealed class DiagnosticBundleWriter
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public string Write(string outputDirectory, IEnumerable<DiagnosticEntry> entries, string? userProfileDirectory = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(outputDirectory);
        ArgumentNullException.ThrowIfNull(entries);
        Directory.CreateDirectory(outputDirectory);
        var bundlePath = Path.Combine(outputDirectory, $"vrclient-diagnostics-{DateTimeOffset.UtcNow:yyyyMMdd-HHmmss}.json");
        var redacted = entries.Select(entry => entry with { Message = Redact(entry.Message, userProfileDirectory) }).ToArray();
        File.WriteAllText(bundlePath, JsonSerializer.Serialize(redacted, JsonOptions), Encoding.UTF8);
        return bundlePath;
    }

    internal static string Redact(string value, string? userProfileDirectory)
    {
        ArgumentNullException.ThrowIfNull(value);
        var result = value;
        if (!string.IsNullOrWhiteSpace(userProfileDirectory))
            result = result.Replace(Path.GetFullPath(userProfileDirectory), "<user-profile>", StringComparison.OrdinalIgnoreCase);

        var usersMarker = "\\Users\\";
        var markerIndex = result.IndexOf(usersMarker, StringComparison.OrdinalIgnoreCase);
        if (markerIndex >= 0)
        {
            var nameStart = markerIndex + usersMarker.Length;
            var nameEnd = result.IndexOf('\\', nameStart);
            if (nameEnd > nameStart)
                result = result.Remove(nameStart, nameEnd - nameStart).Insert(nameStart, "<user>");
        }
        return result;
    }
}

public sealed record ReleaseQualification(
    bool FlatLaunch,
    bool VrLaunch,
    bool Stereo,
    bool Input,
    bool SafetyRollback,
    bool Diagnostics)
{
    public bool Passed => FlatLaunch && VrLaunch && Stereo && Input && SafetyRollback && Diagnostics;
}
