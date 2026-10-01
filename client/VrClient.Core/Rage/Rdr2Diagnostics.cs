namespace VrClient.Core.Rage;

using System.Text.Json;

/// <summary>
/// Redacted, title-scoped RDR2 compatibility evidence. This type deliberately
/// carries no process memory, command line, account data, or installation path.
/// </summary>
public sealed record Rdr2DiagnosticEvidence(
    string Schema,
    string Title,
    string RunId,
    string SteamBuildId,
    string ExecutableSha256,
    string Compatibility,
    string RendererObservation,
    string CameraObservation,
    DateTimeOffset RecordedAtUtc)
{
    public const string SchemaName = "vrclient-rdr2-diagnostic/1";
}

public sealed record Rdr2ReadinessEvidenceResult(
    bool Ready,
    string Status,
    Rdr2DiagnosticEvidence? Evidence = null,
    string? Detail = null);

public static class Rdr2Diagnostics
{
    public static bool ProcessMatchesExpectedExecutable(
        string expectedExecutablePath,
        string? liveExecutablePath)
    {
        if (string.IsNullOrWhiteSpace(liveExecutablePath)) return false;
        return string.Equals(
            Path.GetFullPath(expectedExecutablePath),
            Path.GetFullPath(liveExecutablePath),
            StringComparison.OrdinalIgnoreCase);
    }

    public static string BridgeLogPath(string installRoot, int processId)
    {
        if (processId <= 0) throw new ArgumentOutOfRangeException(nameof(processId));
        return Path.Combine(Path.GetFullPath(installRoot), $"rdr2-bridge-{processId}.log");
    }

    /// <summary>
    /// Validates that a live diagnostic belongs to the exact build and
    /// executable currently admitted by preflight. This keeps readiness from
    /// accepting a successful record left by an older RDR2 installation.
    /// </summary>
    public static Rdr2ReadinessEvidenceResult EvaluateReadinessEvidence(
        string evidencePath,
        string expectedSteamBuildId,
        string expectedExecutableSha256)
    {
        if (!File.Exists(evidencePath))
            return new(false, "evidence_missing");

        try
        {
            var evidence = JsonSerializer.Deserialize<Rdr2DiagnosticEvidence>(
                File.ReadAllText(evidencePath));
            if (evidence is null ||
                !string.Equals(evidence.Schema, Rdr2DiagnosticEvidence.SchemaName, StringComparison.Ordinal) ||
                !string.Equals(evidence.Title, "red-dead-redemption-2", StringComparison.Ordinal))
                return new(false, "evidence_schema_mismatch");

            if (!string.Equals(evidence.SteamBuildId, expectedSteamBuildId, StringComparison.Ordinal) ||
                !string.Equals(evidence.ExecutableSha256, expectedExecutableSha256, StringComparison.OrdinalIgnoreCase))
                return new(false, "evidence_identity_mismatch", evidence,
                    "The diagnostic was captured from a different RDR2 build or executable.");

            if (!string.Equals(evidence.Compatibility, "candidate", StringComparison.Ordinal))
                return new(false, "evidence_compatibility_invalid", evidence);

            var liveD3d12 = string.Equals(
                    evidence.RendererObservation, "d3d12_live_candidate",
                    StringComparison.Ordinal) ||
                string.Equals(
                    evidence.RendererObservation, "vulkan_and_d3d12_live_candidate",
                    StringComparison.Ordinal);
            if (!liveD3d12)
                return new(false, "renderer_evidence_missing", evidence);

            if (!string.Equals(
                    evidence.CameraObservation, "native_bridge_verified",
                    StringComparison.Ordinal))
                return new(false, "camera_bridge_evidence_missing", evidence);

            return new(true, "ready_for_smoke", evidence);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return new(false, "evidence_invalid", Detail: ex.Message);
        }
    }

    /// <summary>
    /// Classifies only static import-string evidence. It is intentionally a
    /// candidate observation: it does not establish a live render path.
    /// </summary>
    public static string ObserveRendererImports(string executablePath)
    {
        var text = System.Text.Encoding.ASCII.GetString(File.ReadAllBytes(executablePath));
        var hasVulkan = text.IndexOf("vulkan-1.dll", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        text.IndexOf("vkGetInstanceProcAddr", StringComparison.OrdinalIgnoreCase) >= 0;
        var hasD3d12 = text.IndexOf("d3d12.dll", StringComparison.OrdinalIgnoreCase) >= 0 ||
                       text.IndexOf("D3D12CreateDevice", StringComparison.OrdinalIgnoreCase) >= 0;
        return (hasVulkan, hasD3d12) switch
        {
            (true, true) => "vulkan_and_d3d12_candidate",
            (true, false) => "vulkan_candidate",
            (false, true) => "d3d12_candidate",
            _ => "unknown"
        };
    }

    /// <summary>Classifies module names observed by an explicitly user-started game.</summary>
    public static string ClassifyRendererModules(IEnumerable<string> moduleNames)
    {
        var names = new HashSet<string>(
            moduleNames.Select(Path.GetFileName).OfType<string>(),
            StringComparer.OrdinalIgnoreCase);
        var hasVulkan = names.Contains("vulkan-1.dll");
        var hasD3d12 = names.Contains("d3d12.dll") || names.Contains("dxgi.dll");
        return (hasVulkan, hasD3d12) switch
        {
            (true, true) => "vulkan_and_d3d12_live_candidate",
            (true, false) => "vulkan_live_candidate",
            (false, true) => "d3d12_live_candidate",
            _ => "unknown"
        };
    }

    public static Rdr2DiagnosticEvidence CreateCandidate(
        string runId,
        string steamBuildId,
        string executableSha256,
        string rendererObservation = "unknown",
        string cameraObservation = "unknown")
    {
        SanitizeRunId(runId);
        ArgumentNullException.ThrowIfNull(steamBuildId);
        ArgumentNullException.ThrowIfNull(executableSha256);
        if (!System.Text.RegularExpressions.Regex.IsMatch(steamBuildId, "^[0-9]+$"))
            throw new ArgumentException("Steam build ID must contain only digits.", nameof(steamBuildId));
        if (!System.Text.RegularExpressions.Regex.IsMatch(executableSha256, "^[a-fA-F0-9]{64}$"))
            throw new ArgumentException("Executable hash must be SHA-256.", nameof(executableSha256));

        return new Rdr2DiagnosticEvidence(
            Rdr2DiagnosticEvidence.SchemaName,
            "red-dead-redemption-2",
            runId,
            steamBuildId,
            executableSha256.ToLowerInvariant(),
            "candidate",
            rendererObservation,
            cameraObservation,
            DateTimeOffset.UtcNow);
    }

    public static string Write(string evidenceRoot, Rdr2DiagnosticEvidence evidence)
    {
        ArgumentNullException.ThrowIfNull(evidence);
        var root = Path.GetFullPath(evidenceRoot);
        Directory.CreateDirectory(root);
        var fileName = $"rdr2-{SanitizeRunId(evidence.RunId)}.json";
        var destination = Path.Combine(root, fileName);
        var temporary = destination + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(evidence, new JsonSerializerOptions { WriteIndented = true }));
        File.Move(temporary, destination, overwrite: true);
        return destination;
    }

    private static string SanitizeRunId(string runId)
    {
        if (!System.Text.RegularExpressions.Regex.IsMatch(runId, "^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"))
            throw new ArgumentException("Run ID contains unsupported characters.", nameof(runId));
        return runId;
    }
}
