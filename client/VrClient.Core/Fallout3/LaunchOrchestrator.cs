namespace VrClient.Core.Fallout3;

using System.Diagnostics;

public interface IFallout3ProcessController
{
    bool IsRunning(Fallout3LaunchComponent component);
    Fallout3StartedProcess Start(Fallout3LaunchComponent component, string logPath);
    bool WaitUntilReady(Fallout3LaunchComponent component, TimeSpan timeout);
    int? TryGetExitCode(int processId);
    void Stop(int processId);
}

public sealed class Fallout3LaunchOrchestrator
{
    public Fallout3LaunchPlan BuildPlan(
        Fallout3Install install,
        Fallout3Readiness readiness,
        string modOrganizerExecutable,
        string vrProfileDirectory,
        string logDirectory,
        string? osirisExecutable,
        string? openXrRuntimeManifest,
        string? steamVrMonitor = null)
    {
        if (install.Support is Fallout3InstallSupport.WrongDirectory or Fallout3InstallSupport.UnsupportedBuild)
            throw new InvalidOperationException("launch refused: unsupported Fallout 3 installation");
        if (!File.Exists(modOrganizerExecutable))
            throw new InvalidOperationException("launch refused: ModOrganizer.exe is missing");
        if (!Directory.Exists(vrProfileDirectory) || !File.Exists(Path.Combine(vrProfileDirectory, "vrclient-fallout3-vr.json")))
            throw new InvalidOperationException("launch refused: the isolated AVRcade Fallout 3 profile is missing or unmanaged");
        if (readiness.EffectiveBackend != Fallout3Backend.Desktop &&
            (string.IsNullOrWhiteSpace(openXrRuntimeManifest) || !File.Exists(openXrRuntimeManifest)))
            throw new InvalidOperationException("launch refused: select an active SteamVR or VirtualDesktopXR runtime manifest");
        if (readiness.EffectiveBackend == Fallout3Backend.DepthVr &&
            (string.IsNullOrWhiteSpace(osirisExecutable) || !File.Exists(osirisExecutable)))
            throw new InvalidOperationException("launch refused: Osiris is required for Depth VR full-SBS presentation");

        var environment = new Dictionary<string, string>
        {
            ["VRCLIENT_FALLOUT3_NATIVE_VR"] = readiness.EffectiveBackend == Fallout3Backend.NativeVr ? "1" : "0"
        };
        if (!string.IsNullOrWhiteSpace(openXrRuntimeManifest))
            environment["XR_RUNTIME_JSON"] = Path.GetFullPath(openXrRuntimeManifest);
        var components = new List<Fallout3LaunchComponent>();
        if (!string.IsNullOrWhiteSpace(steamVrMonitor) && File.Exists(steamVrMonitor) &&
            openXrRuntimeManifest!.Contains("steam", StringComparison.OrdinalIgnoreCase))
            components.Add(new Fallout3LaunchComponent(Fallout3LaunchComponentKind.SteamVr,
                steamVrMonitor, string.Empty, Path.GetDirectoryName(steamVrMonitor)!, true, false));
        if (readiness.EffectiveBackend == Fallout3Backend.DepthVr)
            components.Add(new Fallout3LaunchComponent(Fallout3LaunchComponentKind.Osiris,
                osirisExecutable!, string.Empty, Path.GetDirectoryName(osirisExecutable!)!, true, true, environment));

        var profileName = Path.GetFileName(Path.TrimEndingDirectorySeparator(vrProfileDirectory));
        var gameExecutable = install.FileVersion == "1.7.0.3" && File.Exists(Path.Combine(install.RootDirectory, "fose_loader.exe"))
            ? Path.Combine(install.RootDirectory, "fose_loader.exe") : install.ExecutablePath;
        components.Add(new Fallout3LaunchComponent(Fallout3LaunchComponentKind.ModOrganizer,
            modOrganizerExecutable, $"-p {Quote(profileName)} {Quote(gameExecutable)}",
            Path.GetDirectoryName(modOrganizerExecutable)!, false, false, environment));

        return new Fallout3LaunchPlan(readiness.EffectiveBackend, readiness.ActiveLabel, components,
            Path.GetFullPath(logDirectory), readiness.FallbackReasons.Concat(new[]
            {
                $"Active backend: {readiness.ActiveLabel}.",
                "All helper windows remain visible and user-controlled.",
                "Shutdown only stops helper processes that AVRcade started; SteamVR, Virtual Desktop, MO2 and the game remain under user control.",
                readiness.EffectiveBackend == Fallout3Backend.DepthVr
                    ? "Head rotation may be mouse/gamepad emulation; positional tracking and tracked weapons are not implied."
                    : "Native VR remains experimental unless an accepted independent-per-eye headset proof is active."
            }).ToArray());
    }

    public Fallout3LaunchSession Start(Fallout3LaunchPlan plan, IFallout3ProcessController controller, bool dryRun)
    {
        if (dryRun) return new Fallout3LaunchSession(controller, Array.Empty<Fallout3StartedProcess>());
        Directory.CreateDirectory(plan.LogDirectory);
        var started = new List<Fallout3StartedProcess>();
        try
        {
            foreach (var component in plan.Components)
            {
                var log = Path.Combine(plan.LogDirectory, component.Kind.ToString().ToLowerInvariant() + ".log");
                if (component.StartOnlyWhenNotRunning && controller.IsRunning(component))
                {
                    started.Add(new Fallout3StartedProcess(component, -1, false, log));
                    if (!controller.WaitUntilReady(component, TimeSpan.FromSeconds(20)))
                        throw new InvalidOperationException($"{component.Kind} is running but not ready");
                    continue;
                }
                var process = controller.Start(component, log);
                started.Add(process);
                var timeout = component.Kind == Fallout3LaunchComponentKind.ModOrganizer
                    ? TimeSpan.FromSeconds(60) : TimeSpan.FromSeconds(20);
                if (!controller.WaitUntilReady(component, timeout))
                    throw new InvalidOperationException($"{component.Kind} did not become ready; inspect {log}");
            }
            return new Fallout3LaunchSession(controller, started);
        }
        catch
        {
            Cleanup(controller, started);
            throw;
        }
    }

    internal static void Cleanup(IFallout3ProcessController controller, IEnumerable<Fallout3StartedProcess> started)
    {
        foreach (var process in started.Reverse())
            if (process.StartedByVrClient && process.Component.StopOnCleanup && process.ProcessId > 0)
                try { controller.Stop(process.ProcessId); } catch { }
    }

    private static string Quote(string value) => $"\"{value.Replace("\"", "\\\"")}\"";
}

public sealed class Fallout3LaunchSession : IDisposable
{
    private readonly IFallout3ProcessController _controller;
    private bool _cleaned;
    internal Fallout3LaunchSession(IFallout3ProcessController controller, IReadOnlyList<Fallout3StartedProcess> processes)
    {
        _controller = controller;
        Processes = processes;
    }
    public IReadOnlyList<Fallout3StartedProcess> Processes { get; }
    public IReadOnlyDictionary<Fallout3LaunchComponentKind, int?> ExitCodes() =>
        Processes.ToDictionary(item => item.Component.Kind,
            item => item.ProcessId > 0 ? _controller.TryGetExitCode(item.ProcessId) : null);
    public void Cleanup()
    {
        if (_cleaned) return;
        Fallout3LaunchOrchestrator.Cleanup(_controller, Processes);
        _cleaned = true;
    }
    public void Dispose() => Cleanup();
}

public sealed class SystemFallout3ProcessController : IFallout3ProcessController
{
    private readonly Dictionary<int, Process> _started = new();

    public bool IsRunning(Fallout3LaunchComponent component) => component.Kind switch
    {
        Fallout3LaunchComponentKind.SteamVr => Process.GetProcessesByName("vrserver").Length > 0 || Process.GetProcessesByName("vrmonitor").Length > 0,
        Fallout3LaunchComponentKind.Osiris => Process.GetProcessesByName("osiris-vr-viewer").Length > 0,
        _ => Process.GetProcessesByName(Path.GetFileNameWithoutExtension(component.ExecutablePath)).Length > 0
    };

    public Fallout3StartedProcess Start(Fallout3LaunchComponent component, string logPath)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
        File.AppendAllText(logPath, $"[{DateTimeOffset.UtcNow:O}] backend_component={component.Kind} start={component.ExecutablePath}{Environment.NewLine}");
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
        foreach (var pair in component.Environment ?? new Dictionary<string, string>())
            info.Environment[pair.Key] = pair.Value;
        var process = new Process { StartInfo = info, EnableRaisingEvents = true };
        var gate = new object();
        void Log(string text) { lock (gate) File.AppendAllText(logPath, text + Environment.NewLine); }
        process.OutputDataReceived += (_, e) => { if (e.Data is not null) Log("stdout: " + e.Data); };
        process.ErrorDataReceived += (_, e) => { if (e.Data is not null) Log("stderr: " + e.Data); };
        if (!process.Start()) throw new InvalidOperationException("process_start_failed: " + component.ExecutablePath);
        process.BeginOutputReadLine(); process.BeginErrorReadLine();
        _started[process.Id] = process;
        return new Fallout3StartedProcess(component, process.Id, true, logPath);
    }

    public bool WaitUntilReady(Fallout3LaunchComponent component, TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        do
        {
            if (component.Kind == Fallout3LaunchComponentKind.ModOrganizer)
            {
                if (Process.GetProcessesByName("Fallout3").Length > 0) return true;
            }
            else if (IsRunning(component)) return true;
            Thread.Sleep(100);
        } while (DateTimeOffset.UtcNow < deadline);
        return false;
    }

    public int? TryGetExitCode(int processId) =>
        _started.TryGetValue(processId, out var process) && process.HasExited ? process.ExitCode : null;

    public void Stop(int processId)
    {
        if (!_started.TryGetValue(processId, out var process) || process.HasExited) return;
        process.Kill(entireProcessTree: false);
        process.WaitForExit(5000);
    }
}
