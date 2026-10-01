namespace VrClient.Core.Launch;
using System.Diagnostics;
using System.Text.Json;
using System.Text.RegularExpressions;

/// A concrete OpenXR runtime the game process can be pinned to via the standard
/// XR_RUNTIME_JSON environment variable (per-process override defined by the
/// OpenXR loader spec; takes precedence over the system's active runtime).
public sealed record XrRuntimeChoice(string Name, string JsonPath);

public enum UevrRuntimeState
{
    Healthy,
    NoLiveSession,
    RuntimeManifestMissing,
    RequestedRuntimeConflict
}

public sealed record UevrRuntimeStatus(
    UevrRuntimeState State, string? SelectedRuntime, string Detail)
{
    public bool Ready => State is UevrRuntimeState.Healthy;
}

public static class OpenXrRuntimeSelector
{
    public const string SteamVrName = "SteamVR";
    public const string VirtualDesktopName = "VirtualDesktopXR";

    private static readonly string[] SteamVrProcesses = ["vrserver", "vrmonitor", "vrcompositor"];
    private static readonly string[] VirtualDesktopProcesses = ["VirtualDesktop.Streamer", "VirtualDesktop.Server"];

    /// Deterministic priority rule (pure; pinned by unit tests):
    ///   1. SteamVR is running AND its runtime manifest is available -> SteamVR
    ///   2. Virtual Desktop is running AND its runtime manifest is available -> VirtualDesktopXR
    ///   3. otherwise -> null (leave the system's active OpenXR runtime untouched)
    /// SteamVR outranks Virtual Desktop when BOTH are running because a running
    /// SteamVR compositor owns the headset display, so frames must go to it.
    public static XrRuntimeChoice? Select(
        IReadOnlyCollection<string> runningProcessNames,
        IReadOnlyList<XrRuntimeChoice> availableRuntimes)
    {
        bool AnyRunning(string[] names) => names.Any(n =>
            runningProcessNames.Contains(n, StringComparer.OrdinalIgnoreCase));
        XrRuntimeChoice? Available(string name) => availableRuntimes.FirstOrDefault(r =>
            r.Name.Equals(name, StringComparison.OrdinalIgnoreCase));

        if (AnyRunning(SteamVrProcesses) && Available(SteamVrName) is { } steamVr)
            return steamVr;
        if (AnyRunning(VirtualDesktopProcesses) && Available(VirtualDesktopName) is { } virtualDesktop)
            return virtualDesktop;
        return null;
    }

    public static UevrRuntimeStatus EvaluateForUevr(
        IReadOnlyCollection<string> runningProcessNames,
        IReadOnlyList<XrRuntimeChoice> availableRuntimes,
        string? requestedRuntime = null)
    {
        var steamRunning = IsSteamVrRunning(runningProcessNames);
        var vdRunning = IsVirtualDesktopRunning(runningProcessNames);
        if (!steamRunning && !vdRunning)
            return new UevrRuntimeStatus(UevrRuntimeState.NoLiveSession, null,
                "Start Virtual Desktop/VDXR or SteamVR and connect the headset before injection.");

        var selected = Select(runningProcessNames, availableRuntimes);
        if (selected is null)
            return new UevrRuntimeStatus(UevrRuntimeState.RuntimeManifestMissing, null,
                "A VR process is running, but its OpenXR runtime manifest is missing or stale. Re-select the runtime in Virtual Desktop Streamer or SteamVR settings.");

        if (!string.IsNullOrWhiteSpace(requestedRuntime) &&
            !selected.Name.Equals(requestedRuntime, StringComparison.OrdinalIgnoreCase))
            return new UevrRuntimeStatus(UevrRuntimeState.RequestedRuntimeConflict, selected.Name,
                $"Requested {requestedRuntime}, but {selected.Name} owns the live headset session. Stop the conflicting runtime or choose {selected.Name}.");

        return new UevrRuntimeStatus(UevrRuntimeState.Healthy, selected.Name,
            $"{selected.Name} owns the live OpenXR headset session.");
    }

    /// Select a runtime using the same priority rule as Select(), but only
    /// return a manifest that can be used by GTA San Andreas's PE32 process.
    /// If a higher-priority runtime is present but has no x86 manifest, continue
    /// to the next running runtime instead of silently losing a usable fallback.
    public static XrRuntimeChoice? SelectFor32BitProcess(
        IReadOnlyCollection<string> runningProcessNames,
        IReadOnlyList<XrRuntimeChoice> availableRuntimes)
    {
        var candidates = new[]
        {
            (Name: SteamVrName, Processes: SteamVrProcesses),
            (Name: VirtualDesktopName, Processes: VirtualDesktopProcesses)
        };

        foreach (var candidate in candidates)
        {
            if (!candidate.Processes.Any(n =>
                    runningProcessNames.Contains(n, StringComparer.OrdinalIgnoreCase)))
                continue;

            var runtime = availableRuntimes.FirstOrDefault(r =>
                r.Name.Equals(candidate.Name, StringComparison.OrdinalIgnoreCase));
            var x86Runtime = For32BitProcess(runtime);
            if (x86Runtime is not null)
                return x86Runtime;
        }

        return null;
    }

    /// Adjust a selected runtime for a PE32 game. Virtual Desktop publishes
    /// separate runtime manifests for x64 and x86 clients, and newer SteamVR
    /// builds may publish separate win32/win64 manifests. Passing an x64
    /// manifest to GTA SA's x86 OpenXR loader makes the runtime unavailable.
    /// If an x86 manifest is not present, leave selection to the system's
    /// 32-bit OpenXR registry entry instead of pinning the wrong architecture.
    public static XrRuntimeChoice? For32BitProcess(XrRuntimeChoice? runtime)
    {
        if (runtime is null)
            return runtime;

        if (runtime.Name.Equals(SteamVrName, StringComparison.OrdinalIgnoreCase))
            return For32BitSteamVrProcess(runtime);
        if (!runtime.Name.Equals(VirtualDesktopName, StringComparison.OrdinalIgnoreCase))
            return runtime.Name.Equals("Custom", StringComparison.OrdinalIgnoreCase)
                ? IsPe32RuntimeManifest(runtime.JsonPath) ? runtime : null
                : runtime;

        var directory = Path.GetDirectoryName(runtime.JsonPath);
        if (string.IsNullOrWhiteSpace(directory)) return null;
        var x86Manifest = Path.Combine(directory, "virtualdesktop-openxr-32.json");
        return File.Exists(x86Manifest)
            ? runtime with { JsonPath = x86Manifest }
            : null;
    }

    private static bool IsPe32RuntimeManifest(string manifestPath)
    {
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(manifestPath));
            var libraryPath = document.RootElement
                .GetProperty("runtime")
                .GetProperty("library_path")
                .GetString();
            if (string.IsNullOrWhiteSpace(libraryPath)) return false;

            libraryPath = Environment.ExpandEnvironmentVariables(libraryPath);
            var manifestDirectory = Path.GetDirectoryName(manifestPath) ?? string.Empty;
            var resolvedLibrary = Path.IsPathRooted(libraryPath)
                ? libraryPath
                : Path.Combine(manifestDirectory, libraryPath);
            using var stream = File.OpenRead(resolvedLibrary);
            using var reader = new BinaryReader(stream);
            if (stream.Length < 0x40 || reader.ReadUInt16() != 0x5a4d)
                return false;
            stream.Position = 0x3c;
            var peOffset = reader.ReadInt32();
            if (peOffset < 0 || peOffset > stream.Length - 6)
                return false;
            stream.Position = peOffset;
            return reader.ReadUInt32() == 0x00004550 && reader.ReadUInt16() == 0x014c;
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException or
                                    ArgumentException or NotSupportedException)
        {
            return false;
        }
    }

    private static XrRuntimeChoice? For32BitSteamVrProcess(XrRuntimeChoice runtime)
    {
        var directory = Path.GetDirectoryName(runtime.JsonPath);
        if (string.IsNullOrWhiteSpace(directory)) return null;

        var x86Manifest = Path.Combine(directory, "steamxr_win32.json");
        if (File.Exists(x86Manifest))
            return runtime with { JsonPath = x86Manifest };

        // Current SteamVR installations commonly expose only steamxr_win64.json,
        // whose library_path points at vrclient_x64.dll. Passing that manifest to
        // the x86 GTA process makes the x86 OpenXR loader attempt to load a PE64
        // runtime. Refuse that override and let the loader consult the WOW6432Node
        // 32-bit OpenXR ActiveRuntime entry instead.
        if (IsKnown64BitManifest(runtime.JsonPath))
            return null;
        return runtime;
    }

    private static bool IsKnown64BitManifest(string manifestPath)
    {
        var fileName = Path.GetFileName(manifestPath);
        if (fileName.Contains("win64", StringComparison.OrdinalIgnoreCase) ||
            fileName.Contains("x64", StringComparison.OrdinalIgnoreCase))
            return true;

        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(manifestPath));
            var libraryPath = document.RootElement
                .GetProperty("runtime")
                .GetProperty("library_path")
                .GetString();
            return libraryPath is not null &&
                (libraryPath.Contains("win64", StringComparison.OrdinalIgnoreCase) ||
                 libraryPath.Contains("x64", StringComparison.OrdinalIgnoreCase));
        }
        catch (JsonException)
        {
            return false;
        }
        catch (IOException)
        {
            return false;
        }
    }

    public static bool IsSteamVrRunning(IReadOnlyCollection<string> runningProcessNames)
        => SteamVrProcesses.Any(n => runningProcessNames.Contains(n, StringComparer.OrdinalIgnoreCase));

    public static bool IsVirtualDesktopRunning(IReadOnlyCollection<string> runningProcessNames)
        => VirtualDesktopProcesses.Any(n => runningProcessNames.Contains(n, StringComparer.OrdinalIgnoreCase));

    /// The runtime a 64-bit game should be pinned to right now, from the live process list.
    public static XrRuntimeChoice? SelectLive(string? steamRoot = null) =>
        Select(GetRunningVrProcessNames(), DiscoverAvailableRuntimes(steamRoot));

    /// The same for a 32-bit game such as classic GTA San Andreas.
    public static XrRuntimeChoice? SelectLiveFor32BitProcess() =>
        SelectFor32BitProcess(GetRunningVrProcessNames(), DiscoverAvailableRuntimes());

    /// Live process detection for the names Select() cares about, from one process snapshot.
    public static IReadOnlyCollection<string> GetRunningVrProcessNames()
    {
        Process[] processes = [];
        try { processes = Process.GetProcesses(); }
        catch { /* process enumeration denied: treat as not running */ }
        var running = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var process in processes)
        {
            try { running.Add(process.ProcessName); }
            catch (InvalidOperationException) { /* exited between the snapshot and the read */ }
            process.Dispose();
        }
        return SteamVrProcesses.Concat(VirtualDesktopProcesses).Where(running.Contains).ToList();
    }

    /// Probe well-known install locations for OpenXR runtime manifests.
    /// SteamVR: <steam library>\steamapps\common\SteamVR\steamxr_win64.json
    /// Virtual Desktop: any *openxr*.json under the Virtual Desktop Streamer folder.
    public static IReadOnlyList<XrRuntimeChoice> DiscoverAvailableRuntimes(string? steamRoot = null)
    {
        var runtimes = new List<XrRuntimeChoice>();

        foreach (var root in CandidateSteamRoots(steamRoot))
        {
            var manifest = Path.Combine(root, "steamapps", "common", "SteamVR", "steamxr_win64.json");
            if (File.Exists(manifest))
            {
                runtimes.Add(new XrRuntimeChoice(SteamVrName, manifest));
                break;
            }
        }

        var vdDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Virtual Desktop Streamer");
        if (Directory.Exists(vdDir))
        {
            // Prefer the canonical runtime manifest by exact name: the VD folder also
            // ships sibling manifests (openxr-oculus-compatibility.json, 32-bit variants)
            // that are NOT the runtime — pinning one makes the mod's override attempt
            // fail and fall back (observed 2026-07-10).
            var manifest = Directory
                .EnumerateFiles(vdDir, "*.json", SearchOption.AllDirectories)
                .Where(f => Path.GetFileName(f).Contains("openxr", StringComparison.OrdinalIgnoreCase))
                .OrderByDescending(f => Path.GetFileName(f).Equals(
                    "virtualdesktop-openxr.json", StringComparison.OrdinalIgnoreCase))
                .FirstOrDefault();
            if (manifest is not null)
                runtimes.Add(new XrRuntimeChoice(VirtualDesktopName, manifest));
        }

        return runtimes;
    }

    private static IEnumerable<string> CandidateSteamRoots(string? steamRoot)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var primaries = new List<string>();
        if (!string.IsNullOrEmpty(steamRoot))
            primaries.Add(steamRoot);
        primaries.Add(@"C:\Program Files (x86)\Steam");
        primaries.Add(@"C:\Program Files\Steam");

        foreach (var primary in primaries)
        {
            if (seen.Add(primary))
                yield return primary;
            // SteamVR may live in a secondary library; follow libraryfolders.vdf.
            var vdf = Path.Combine(primary, "steamapps", "libraryfolders.vdf");
            if (!File.Exists(vdf))
                continue;
            foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\""))
            {
                var library = m.Groups[1].Value.Replace(@"\\", @"\");
                if (seen.Add(library))
                    yield return library;
            }
        }
    }
}
