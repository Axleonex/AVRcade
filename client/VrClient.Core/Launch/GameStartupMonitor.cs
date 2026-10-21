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
        Process process;
        try
        {
            process = Process.GetProcessById(processId);
        }
        catch (ArgumentException)
        {
            return new(false, null, "The game closed before startup could be checked. Use Recheck setup, then try again.");
        }
        using (process)
            return await ObserveAsync(process, executable, observation, ct);
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
                    return new(false, code, DescribeExitCode(code));
                }
                var actual = GameLaunchBinding.ProcessExecutable(process);
                if (actual is null || !Path.GetFullPath(actual).Equals(Path.GetFullPath(executable), StringComparison.OrdinalIgnoreCase))
                    return new(false, null, "Could not verify that the process belongs to the selected installation. No process was stopped. Use Locate installed game and Recheck setup.");
                if (DescribeLoaderWindow(process.MainWindowTitle) is { } loaderFailure)
                    return new(false, null, loaderFailure);
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

    public static string DescribeExitCode(int code)
    {
        var cause = unchecked((uint)code) switch
        {
            0xC0000139 => " A required DLL entry point is missing; a game file or native mod may be incompatible.",
            0xC0000135 => " A required DLL is missing.",
            0xC000007B => " A native library has the wrong architecture or an invalid binary format.",
            0xC0000142 => " A native library failed to initialize.",
            _ => ""
        };
        return $"The game closed during startup (exit code 0x{unchecked((uint)code):X8}).{cause} Use Recheck setup" +
            (cause.Length == 0 ? ", then try again." : "; the Troubleshooting tab at the bottom of this page has Verify game files in Steam for original game files. Check incompatible mods in their manager.");
    }

    public static string? DescribeLoaderWindow(string title)
    {
        var failure = new[] { "Entry Point Not Found", "Bad Image", "System Error", "Application Error" }
            .FirstOrDefault(kind => title.EndsWith(" - " + kind, StringComparison.OrdinalIgnoreCase));
        return failure is null ? null : $"Windows reported {failure} during game startup. Close the error dialog, then use Recheck setup. Verify game files in Steam is in the Troubleshooting tab at the bottom of this page. The game has not completed startup.";
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
