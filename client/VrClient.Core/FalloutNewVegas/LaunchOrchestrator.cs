namespace VrClient.Core.FalloutNewVegas;

using System.Diagnostics;

public interface IFnvProcessController
{
    bool IsRunning(FnvLaunchComponent component);
    FnvStartedProcess Start(FnvLaunchComponent component, string logPath);
    bool WaitUntilReady(FnvLaunchComponent component, TimeSpan timeout);
    int? TryGetExitCode(int processId);
    void Stop(int processId);
    void StopNativeAdapter(FnvLaunchComponent component, string logPath);
}

public sealed class FalloutNewVegasLaunchOrchestrator
{
    public FnvLaunchPlan BuildPlan(
        FnvGameInstall install,
        FnvPrerequisiteOptions prerequisites,
        string logDirectory,
        string? virtualDesktopExecutablePath = null)
    {
        if (install.Support is FnvInstallSupport.WrongDirectory or FnvInstallSupport.UnsupportedEdition)
            throw new InvalidOperationException("launch plan refused: unsupported or incorrect Fallout: New Vegas installation");
        if (string.IsNullOrWhiteSpace(prerequisites.ModOrganizerExecutablePath) ||
            !File.Exists(prerequisites.ModOrganizerExecutablePath))
            throw new InvalidOperationException("launch plan refused: ModOrganizer.exe is missing");
        if (string.IsNullOrWhiteSpace(prerequisites.ModOrganizerProfileDirectory) ||
            !Directory.Exists(prerequisites.ModOrganizerProfileDirectory))
            throw new InvalidOperationException("launch plan refused: selected MO2 profile is missing");
        if (string.IsNullOrWhiteSpace(prerequisites.SteamVrDirectory))
            throw new InvalidOperationException("launch plan refused: SteamVR directory is not selected");
        if (string.IsNullOrWhiteSpace(prerequisites.NativeAdapterExecutablePath) ||
            !File.Exists(prerequisites.NativeAdapterExecutablePath))
            throw new InvalidOperationException("launch plan refused: AVRcade native OpenXR launcher is missing");
        var nativeDirectory = Path.GetDirectoryName(prerequisites.NativeAdapterExecutablePath)!;
        var missingNativeFiles = new[] { "vrclient_fnv_stereo.dll", "openxr_loader.dll" }
            .Where(name => !File.Exists(Path.Combine(nativeDirectory, name))).ToArray();
        if (missingNativeFiles.Length > 0)
            throw new InvalidOperationException($"launch plan refused: native adapter is missing {string.Join(", ", missingNativeFiles)}");
        if (string.IsNullOrWhiteSpace(prerequisites.TrackerExecutablePath) ||
            !File.Exists(prerequisites.TrackerExecutablePath))
            throw new InvalidOperationException("launch plan refused: official FNVR tracker executable is missing");
        var nvseLoader = Path.Combine(install.RootDirectory, "nvse_loader.exe");
        if (!File.Exists(nvseLoader))
            throw new InvalidOperationException("launch plan refused: nvse_loader.exe is missing from the game root");

        var steamVrMonitor = new[]
        {
            Path.Combine(prerequisites.SteamVrDirectory, "bin", "win64", "vrmonitor.exe"),
            Path.Combine(prerequisites.SteamVrDirectory, "vrmonitor.exe")
        }.FirstOrDefault(File.Exists)
            ?? throw new InvalidOperationException("launch plan refused: vrmonitor.exe was not found under the selected SteamVR directory");

        var components = new List<FnvLaunchComponent>();
        if (prerequisites.UseVirtualDesktop)
        {
            if (string.IsNullOrWhiteSpace(virtualDesktopExecutablePath) || !File.Exists(virtualDesktopExecutablePath))
                throw new InvalidOperationException("launch plan refused: Virtual Desktop Streamer path is required for the Virtual Desktop to SteamVR route");
            components.Add(new FnvLaunchComponent(FnvLaunchComponentKind.VirtualDesktop,
                virtualDesktopExecutablePath, string.Empty, Path.GetDirectoryName(virtualDesktopExecutablePath)!, true, false));
        }
        components.Add(new FnvLaunchComponent(FnvLaunchComponentKind.SteamVr,
            steamVrMonitor, string.Empty, Path.GetDirectoryName(steamVrMonitor)!, true, false));
        components.Add(new FnvLaunchComponent(FnvLaunchComponentKind.FnvrTracker,
            prerequisites.TrackerExecutablePath, string.Empty,
            Path.GetDirectoryName(prerequisites.TrackerExecutablePath)!, true, true));

        var profileName = Path.GetFileName(Path.TrimEndingDirectorySeparator(prerequisites.ModOrganizerProfileDirectory));
        var mo2Arguments = $"-p {Quote(profileName)} {Quote(nvseLoader)}";
        var steamOpenXrRuntime = Path.Combine(prerequisites.SteamVrDirectory, "steamxr_win64.json");
        if (!File.Exists(steamOpenXrRuntime))
            throw new InvalidOperationException("launch plan refused: SteamVR's steamxr_win64.json runtime manifest is missing");
        components.Add(new FnvLaunchComponent(FnvLaunchComponentKind.NewVegas,
            prerequisites.ModOrganizerExecutablePath, mo2Arguments,
            Path.GetDirectoryName(prerequisites.ModOrganizerExecutablePath)!, false, false,
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["XR_RUNTIME_JSON"] = Path.GetFullPath(steamOpenXrRuntime)
            }));
        components.Add(new FnvLaunchComponent(FnvLaunchComponentKind.NativeOpenXrAdapter,
            prerequisites.NativeAdapterExecutablePath, "--quiet",
            Path.GetDirectoryName(prerequisites.NativeAdapterExecutablePath)!, false, false));

        return new FnvLaunchPlan(components, Path.GetFullPath(logDirectory), prerequisites.UseVirtualDesktop,
            new[]
            {
                "AVRcade uses its native OpenXR stereo adapter; vorpX is neither required nor launched.",
                "The game is launched by MO2 with the selected isolated profile, nvse_loader.exe, and a process-scoped SteamVR OpenXR override; the native adapter then attaches only to the verified retail executable.",
                "SteamVR and Virtual Desktop remain under user control; the FNVR tracker and native adapter are stopped only when AVRcade started them."
            });
    }

    public FnvLaunchSession Start(FnvLaunchPlan plan, IFnvProcessController controller, bool dryRun)
    {
        if (dryRun)
            return new FnvLaunchSession(controller, Array.Empty<FnvStartedProcess>());
        Directory.CreateDirectory(plan.LogDirectory);
        var started = new List<FnvStartedProcess>();
        FnvLaunchComponent? activeComponent = null;
        try
        {
            foreach (var component in plan.Components)
            {
                activeComponent = component;
                if (component.StartOnlyWhenNotRunning && controller.IsRunning(component))
                {
                    started.Add(new FnvStartedProcess(component, -1, false,
                        Path.Combine(plan.LogDirectory, LogName(component.Kind))));
                    if (!controller.WaitUntilReady(component, TimeSpan.FromSeconds(15)))
                        throw new InvalidOperationException($"{component.Kind} is running but did not become ready within 15 seconds");
                    continue;
                }
                var logPath = Path.Combine(plan.LogDirectory, LogName(component.Kind));
                started.Add(controller.Start(component, logPath));
                if (component.Kind != FnvLaunchComponentKind.NewVegas &&
                    !controller.WaitUntilReady(component, TimeSpan.FromSeconds(15)))
                    throw new InvalidOperationException($"{component.Kind} did not become ready within 15 seconds; inspect {logPath}");
                if (component.Kind == FnvLaunchComponentKind.NewVegas &&
                    !controller.WaitUntilReady(component, TimeSpan.FromSeconds(60)))
                    throw new InvalidOperationException($"FalloutNV.exe did not become ready within 60 seconds; inspect {logPath}");
            }
            return new FnvLaunchSession(controller, started);
        }
        catch (Exception error)
        {
            foreach (var process in started.AsEnumerable().Reverse())
            {
                if (!process.StartedByVrClient)
                    continue;
                if (process.Component.Kind == FnvLaunchComponentKind.NativeOpenXrAdapter)
                {
                    try { controller.StopNativeAdapter(process.Component, process.LogPath + ".stop"); } catch { }
                }
                else if (process.Component.StopOnCleanup && process.ProcessId > 0)
                {
                    try { controller.Stop(process.ProcessId); } catch { }
                }
            }
            var failedComponent = activeComponent?.Kind.ToString() ?? "unknown";
            throw new InvalidOperationException($"launch failed at {failedComponent}: {error.Message}", error);
        }
    }

    private static string Quote(string value) => $"\"{value.Replace("\"", "\\\"")}\"";
    private static string LogName(FnvLaunchComponentKind kind) => $"{kind.ToString().ToLowerInvariant()}.log";
}

public sealed class FnvLaunchSession : IDisposable
{
    private readonly IFnvProcessController _controller;
    private bool _cleaned;

    internal FnvLaunchSession(IFnvProcessController controller, IReadOnlyList<FnvStartedProcess> processes)
    {
        _controller = controller;
        Processes = processes;
    }

    public IReadOnlyList<FnvStartedProcess> Processes { get; }

    public IReadOnlyDictionary<FnvLaunchComponentKind, int?> CaptureExitCodes() =>
        Processes.ToDictionary(process => process.Component.Kind,
            process => process.ProcessId > 0 ? _controller.TryGetExitCode(process.ProcessId) : null);

    public void Cleanup()
    {
        if (_cleaned)
            return;
        foreach (var process in Processes.Reverse())
        {
            if (!process.StartedByVrClient)
                continue;
            if (process.Component.Kind == FnvLaunchComponentKind.NativeOpenXrAdapter)
                _controller.StopNativeAdapter(process.Component, process.LogPath + ".stop");
            else if (process.Component.StopOnCleanup && process.ProcessId > 0)
                _controller.Stop(process.ProcessId);
        }
        _cleaned = true;
    }

    public void Dispose() => Cleanup();
}

public sealed class SystemFnvProcessController : IFnvProcessController
{
    private readonly Dictionary<int, Process> _started = new();

    public bool IsRunning(FnvLaunchComponent component)
    {
        return component.Kind switch
        {
            FnvLaunchComponentKind.SteamVr => Process.GetProcessesByName("vrserver").Length > 0 || Process.GetProcessesByName("vrmonitor").Length > 0,
            FnvLaunchComponentKind.VirtualDesktop => Process.GetProcessesByName("VirtualDesktop.Streamer").Length > 0 || Process.GetProcessesByName("VirtualDesktopStreamer").Length > 0,
            FnvLaunchComponentKind.NewVegas => Process.GetProcessesByName("FalloutNV").Length > 0,
            _ => Process.GetProcessesByName(Path.GetFileNameWithoutExtension(component.ExecutablePath)).Length > 0
        };
    }

    public FnvStartedProcess Start(FnvLaunchComponent component, string logPath)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
        var logLock = new object();
        void AppendLog(string line)
        {
            lock (logLock)
                File.AppendAllText(logPath, line + Environment.NewLine);
        }
        AppendLog($"[{DateTimeOffset.UtcNow:O}] starting {component.Kind}: {component.ExecutablePath} {component.Arguments}");
        var info = new ProcessStartInfo
        {
            FileName = component.ExecutablePath,
            Arguments = component.Arguments,
            WorkingDirectory = component.WorkingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = false
        };
        if (component.EnvironmentVariables is not null)
        {
            foreach (var pair in component.EnvironmentVariables)
                info.Environment[pair.Key] = pair.Value;
        }
        var process = new Process { StartInfo = info, EnableRaisingEvents = true };
        process.OutputDataReceived += (_, eventArgs) => { if (eventArgs.Data is not null) AppendLog($"stdout: {eventArgs.Data}"); };
        process.ErrorDataReceived += (_, eventArgs) => { if (eventArgs.Data is not null) AppendLog($"stderr: {eventArgs.Data}"); };
        process.Exited += (_, _) => AppendLog($"[{DateTimeOffset.UtcNow:O}] exit_code={process.ExitCode}");
        if (!process.Start())
            throw new InvalidOperationException($"process_start_failed: {component.ExecutablePath}");
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        _started[process.Id] = process;
        return new FnvStartedProcess(component, process.Id, true, logPath);
    }

    public int? TryGetExitCode(int processId)
    {
        if (!_started.TryGetValue(processId, out var process) || !process.HasExited)
            return null;
        return process.ExitCode;
    }

    public bool WaitUntilReady(FnvLaunchComponent component, TimeSpan timeout)
    {
        if (component.Kind == FnvLaunchComponentKind.NativeOpenXrAdapter)
        {
            var adapter = _started.Values.LastOrDefault(process =>
                string.Equals(process.StartInfo.FileName, component.ExecutablePath, StringComparison.OrdinalIgnoreCase));
            return adapter is not null && adapter.WaitForExit((int)timeout.TotalMilliseconds) && adapter.ExitCode == 0;
        }
        var deadline = DateTimeOffset.UtcNow + timeout;
        do
        {
            var ready = component.Kind switch
            {
                FnvLaunchComponentKind.SteamVr => Process.GetProcessesByName("vrserver").Length > 0 || Process.GetProcessesByName("vrmonitor").Length > 0,
                FnvLaunchComponentKind.NewVegas => Process.GetProcessesByName("FalloutNV").Length > 0,
                _ => Process.GetProcessesByName(Path.GetFileNameWithoutExtension(component.ExecutablePath)).Length > 0
            };
            if (ready)
                return true;
            Thread.Sleep(100);
        } while (DateTimeOffset.UtcNow < deadline);
        return false;
    }

    public void Stop(int processId)
    {
        if (!_started.TryGetValue(processId, out var process) || process.HasExited)
            return;
        process.Kill(entireProcessTree: false);
        process.WaitForExit(5000);
    }

    public void StopNativeAdapter(FnvLaunchComponent component, string logPath)
    {
        var stop = component with { Arguments = "--stop --quiet", StartOnlyWhenNotRunning = false };
        Start(stop, logPath);
        if (!WaitUntilReady(stop, TimeSpan.FromSeconds(15)))
            throw new InvalidOperationException($"native adapter stop request failed; inspect {logPath}");
    }
}
