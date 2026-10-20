using System.Diagnostics;

namespace VrClient.Core.Launch;

public sealed record GameStartupResult(bool Running, int? ExitCode, string Message);

/// Checks startup without claiming that process survival verifies a window or VR.
/// Never closes, replaces or attaches to another game's process.
public static class GameStartupMonitor
{
    public static readonly TimeSpan StartupObservation = TimeSpan.FromSeconds(8);

    public static async Task<GameStartupResult> ObserveAsync(int processId, string executable,
        TimeSpan? observation = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        if (processId == -1) return new(true, null, "Launch plan checked (dry run); no game was started.");
        try
        {
            using var process = Process.GetProcessById(processId);
            return await ObserveAsync(process, executable, observation, ct);
        }
        catch (ArgumentException)
        {
            return new(false, null, "The game closed before startup could be checked. Use Recheck setup, then try again.");
        }
    }

    public static async Task<GameStartupResult> ObserveAsync(Process process, string executable,
        TimeSpan? observation = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var duration = observation ?? StartupObservation;
        if (duration < TimeSpan.Zero || duration > TimeSpan.FromMinutes(2))
            throw new ArgumentOutOfRangeException(nameof(observation));
        var elapsed = Stopwatch.StartNew();
        try
        {
            while (true)
            {
                ct.ThrowIfCancellationRequested();
                if (process.HasExited)
                {
                    var code = process.ExitCode;
                    return new(false, code, $"The game closed during startup (exit code 0x{unchecked((uint)code):X8}). Use Recheck setup, then try again.");
                }
                var actual = GameLaunchBinding.ProcessExecutable(process);
                if (actual is null || !Path.GetFullPath(actual).Equals(Path.GetFullPath(executable), StringComparison.OrdinalIgnoreCase))
                    return new(false, null, "Could not verify that the process belongs to the selected installation. No process was stopped. Use Locate installed game and Recheck setup.");
                var remaining = duration - elapsed.Elapsed;
                if (remaining <= TimeSpan.Zero)
                    return new(true, null, "The game process is running. Check the game window or headset; display readiness has not been verified.");
                await Task.Delay(remaining < TimeSpan.FromMilliseconds(100) ? remaining : TimeSpan.FromMilliseconds(100), ct);
            }
        }
        catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            return new(false, null, "The game stopped or its startup could not be inspected. Use Recheck setup, then try again.");
        }
    }

    public static string FormatCliResult(string result, bool ok)
    {
        var message = result.Trim();
        if (message.StartsWith("RESULT:", StringComparison.Ordinal)) message = message[7..].Trim();
        var error = message.IndexOf("error=", StringComparison.Ordinal);
        if (!ok && error >= 0)
            return message[(error + 6)..].Trim();
        if (ok && message.StartsWith("cyberpunk-launch ", StringComparison.Ordinal))
        {
            if (message.Contains("pid=-1 ", StringComparison.Ordinal)) return "Cyberpunk 2077 launch plan checked (dry run).";
            var mode = message.Contains("mode=vr ", StringComparison.Ordinal) ? "VR" : "monitor";
            var mods = message.Contains("mods=True", StringComparison.OrdinalIgnoreCase) ? " REDmod mods are enabled." : "";
            return $"Cyberpunk 2077 process is running in {mode} mode.{mods} Check the game window or headset; display readiness has not been verified.";
        }
        return message;
    }
}
