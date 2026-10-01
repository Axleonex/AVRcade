namespace VrClient.Core.Rage;

using System.Text.Json;

public enum Rdr2AdapterContractStatus
{
    ManifestMissing,
    BuildUnsupported,
    BridgeServicesRequired,
    ReadyForSmoke
}

public sealed record Rdr2NativeAdapterContractResult(
    Rdr2AdapterContractStatus Status,
    string? AdapterId,
    string? AdapterVersion,
    string? RequiredBuild,
    string? AdapterBinary,
    string? Detail)
{
    public bool Ready => Status == Rdr2AdapterContractStatus.ReadyForSmoke;
}

/// Describes the native RAGE adapter boundary without attempting injection or
/// inspecting a running game's memory. A bridge artifact is only smoke-ready
/// when the built bridge, ScriptHookRDR2, its ASI loader, and the ASI payload
/// are all present in the selected game root; otherwise the result remains
/// fail-closed.
public static class Rdr2NativeAdapterContract
{
    public static Rdr2NativeAdapterContractResult Evaluate(
        string repoRoot,
        string buildId,
        string? gameRoot = null)
    {
        var manifestPath = Path.Combine(repoRoot, "adapters", "rdr2", "adapter.json");
        if (!File.Exists(manifestPath))
            return new(Rdr2AdapterContractStatus.ManifestMissing, null, null, null, null,
                "RDR2 adapter manifest is missing.");

        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(manifestPath));
            var root = document.RootElement;
            var adapterId = root.GetProperty("adapter_id").GetString();
            var adapterVersion = root.GetProperty("adapter_version").GetString();
            var builds = root.GetProperty("supported_builds").EnumerateArray()
                .Select(item => item.GetProperty("build_id").GetString())
                .Where(value => value is not null)
                .ToHashSet(StringComparer.Ordinal);
            var requiredBuild = $"steam-1174180-build-{buildId}";
            if (!builds.Contains(requiredBuild))
                return new(Rdr2AdapterContractStatus.BuildUnsupported, adapterId,
                    adapterVersion, requiredBuild, null,
                    "The installed Steam build is not in the adapter compatibility matrix.");

            var binary = FindBridgeBinary(repoRoot);
            if (binary is null)
                return new(Rdr2AdapterContractStatus.BridgeServicesRequired, adapterId,
                    adapterVersion, requiredBuild, null,
                    "Build the optional vrclient_rdr2_bridge target; runtime bridge services are still required.");

            var bridgeArchitecture = VrClient.Core.Legacy.GtaSanAndreasPreflight.ReadPeArchitecture(binary);
            if (!string.Equals(bridgeArchitecture, "x64", StringComparison.OrdinalIgnoreCase))
                return new(Rdr2AdapterContractStatus.BridgeServicesRequired, adapterId,
                    adapterVersion, requiredBuild, binary,
                    $"The RDR2 bridge must be x64 to load into RDR2.exe; detected {bridgeArchitecture ?? "unknown"}.");

            if (string.IsNullOrWhiteSpace(gameRoot))
                return new(Rdr2AdapterContractStatus.BridgeServicesRequired, adapterId,
                    adapterVersion, requiredBuild, binary,
                    "The bridge artifact exists, but a game root was not supplied for installation checks.");

            var installedRoot = Path.GetFullPath(gameRoot);
            var requiredFiles = new[]
            {
                "ScriptHookRDR2.dll",
                "dinput8.dll",
                "vrclient_rdr2_bridge.asi",
                "vulkan-1.dll",
                "runtime-profile.json"
            };
            var missingFiles = requiredFiles
                .Where(name => !File.Exists(Path.Combine(installedRoot, name)))
                .ToArray();
            if (missingFiles.Length > 0)
                return new(Rdr2AdapterContractStatus.BridgeServicesRequired, adapterId,
                    adapterVersion, requiredBuild, binary,
                    $"Install the Story Mode bridge prerequisites in the game root before launch: {string.Join(", ", missingFiles)}.");

            var installedBridge = Path.Combine(installedRoot, "vrclient_rdr2_bridge.asi");
            if (!string.Equals(
                    Hashing.Sha256OfFile(installedBridge),
                    Hashing.Sha256OfFile(binary),
                    StringComparison.OrdinalIgnoreCase))
                return new(Rdr2AdapterContractStatus.BridgeServicesRequired, adapterId,
                    adapterVersion, requiredBuild, binary,
                    "The installed RDR2 bridge does not match the AVRcade artifact. Re-stage and apply the profile before launch.");

            return new(Rdr2AdapterContractStatus.ReadyForSmoke, adapterId,
                adapterVersion, requiredBuild, binary,
                "The pinned bridge artifact and Story Mode loader prerequisites are present; live OpenXR/camera evidence remains a smoke-test gate.");
        }
        catch (JsonException ex)
        {
            return new(Rdr2AdapterContractStatus.ManifestMissing, null, null, null, null,
                $"RDR2 adapter manifest is invalid: {ex.Message}");
        }
    }

    public static string? FindBridgeBinary(string repoRoot)
    {
        var names = OperatingSystem.IsWindows()
            ? new[] { "vrclient_rdr2_bridge.dll", "vrclient_rdr2_bridge.asi" }
            : new[] { "libvrclient_rdr2_bridge.so", "vrclient_rdr2_bridge.asi" };
        var roots = new[] { Path.Combine(repoRoot, "artifacts", "adapters", "rdr2"),
            Path.Combine(repoRoot, "build") };
        foreach (var root in roots.Where(Directory.Exists))
            foreach (var name in names)
            {
                var direct = Path.Combine(root, name);
                if (File.Exists(direct)) return direct;
                var nested = Directory.EnumerateFiles(root, name, SearchOption.AllDirectories).FirstOrDefault();
                if (nested is not null) return nested;
            }
        return null;
    }
}
