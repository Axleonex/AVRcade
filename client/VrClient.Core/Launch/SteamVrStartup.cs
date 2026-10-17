using System.ComponentModel;
using System.Diagnostics;
using VrClient.Core.Discovery;

namespace VrClient.Core.Launch;

public sealed record SteamVrStartResult(bool Success, string Detail);

/// Starts the installed runtime and waits for its headset before handing a game to Steam.
public sealed class SteamVrStartup(
    Func<bool>? serverRunning = null,
    Func<ProcessStartInfo, bool>? start = null,
    Func<TimeSpan, CancellationToken, Task>? delay = null,
    Func<string, bool>? headsetReady = null)
{
    private readonly Func<bool> _serverRunning = serverRunning ?? IsServerRunning;
    private readonly Func<ProcessStartInfo, bool> _start = start ?? Start;
    private readonly Func<TimeSpan, CancellationToken, Task> _delay = delay ?? Task.Delay;
    private readonly Func<string, bool> _headsetReady = headsetReady ?? OpenVrHeadsetProbe.IsReady;

    public static bool IsServerRunning()
    {
        var processes = Process.GetProcessesByName("vrserver");
        try { return processes.Length > 0; }
        finally { foreach (var process in processes) process.Dispose(); }
    }

    public async Task<SteamVrStartResult> EnsureRunningAsync(string? steamRoot = null,
        bool dryRun = false, CancellationToken ct = default)
    {
        try
        {
            ct.ThrowIfCancellationRequested();
            var runtime = new SteamLibraryScanner().FindGame("250820",
                Path.Combine("bin", "win64", "vrstartup.exe"), steamRoot);
            if (runtime is null)
                return new(false, "SteamVR is not installed or its startup executable is missing. Install or repair SteamVR in Steam, then retry.");
            if (dryRun)
                return new(true, $"SteamVR startup plan: {runtime.ExecutablePath}");
            if (!_serverRunning() && !_start(new ProcessStartInfo(runtime.ExecutablePath)
                {
                    WorkingDirectory = Path.GetDirectoryName(runtime.ExecutablePath)!,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WindowStyle = ProcessWindowStyle.Hidden
                }))
                return new(false, "SteamVR startup did not start. Open SteamVR and retry.");
            // A server can exist while drivers report HmdNotFound or OculusRuntimeBadInstall.
            for (var attempt = 0; attempt < 40; attempt++)
            {
                ct.ThrowIfCancellationRequested();
                if (_serverRunning() && _headsetReady(runtime.ExecutablePath))
                    return new(true, "SteamVR headset is ready.");
                await _delay(TimeSpan.FromMilliseconds(500), ct).ConfigureAwait(false);
            }
            ct.ThrowIfCancellationRequested();
            return new(false, _serverRunning()
                ? "SteamVR has no usable headset connection after 20 seconds. Connect your headset through Virtual Desktop, then retry. If SteamVR still shows an error, restart SteamVR after connecting. YAPYAP was not launched."
                : "SteamVR did not start its server within 20 seconds. Check SteamVR and the headset connection, then retry. YAPYAP was not launched.");
        }
        catch (OperationCanceledException)
        {
            return new(false, "SteamVR startup was cancelled. YAPYAP was not launched.");
        }
        catch (Exception ex) when (ex is Win32Exception or IOException or UnauthorizedAccessException or
                                   InvalidOperationException or ArgumentException)
        {
            return new(false, $"Could not start SteamVR: {ex.Message}");
        }
    }

    private static bool Start(ProcessStartInfo info)
    {
        using var process = Process.Start(info);
        return process is not null;
    }
}
