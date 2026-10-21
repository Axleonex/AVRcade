using System.Diagnostics;
using VrClient.Core.Launch;

namespace VrClient.Core.Tests;

public sealed class GameStartupMonitorTests
{
    [Theory]
    [InlineData(unchecked((int)0xC0000139), "entry point")]
    [InlineData(unchecked((int)0xC0000135), "DLL is missing")]
    [InlineData(unchecked((int)0xC000007B), "architecture")]
    [InlineData(unchecked((int)0xC0000142), "initialize")]
    public void LoaderFailuresGiveSpecificDiagnosisAndRepairLocation(int code, string cause)
    {
        var message = GameStartupMonitor.DescribeExitCode(code);
        Assert.Contains(cause, message);
        Assert.Contains("About tab at the bottom", message);
        Assert.Contains("Recheck setup", message);
    }

    [Theory]
    [InlineData("Cyberpunk2077.exe - Entry Point Not Found")]
    [InlineData("Game.exe - Bad Image")]
    [InlineData("Game.exe - System Error")]
    [InlineData("Game.exe - Application Error")]
    public void WaitingLoaderErrorDialogIsNotReportedAsSuccessfulStartup(string title)
    {
        Assert.Contains("has not completed startup", GameStartupMonitor.DescribeLoaderWindow(title));
        Assert.Null(GameStartupMonitor.DescribeLoaderWindow("Cyberpunk 2077"));
    }

    [Fact]
    public async Task EarlyExitDuringObservationIsReportedWithoutClaimingSuccess()
    {
        var executable = OperatingSystem.IsWindows() ? Path.Combine(Environment.SystemDirectory, "WindowsPowerShell", "v1.0", "powershell.exe") : "/bin/sh";
        var start = new ProcessStartInfo(executable) { UseShellExecute = false, CreateNoWindow = true };
        if (OperatingSystem.IsWindows())
        {
            start.ArgumentList.Add("-NoProfile"); start.ArgumentList.Add("-Command");
            start.ArgumentList.Add("Start-Sleep -Milliseconds 250; exit 23");
        }
        else { start.ArgumentList.Add("-c"); start.ArgumentList.Add("sleep 0.25; exit 23"); }
        using var process = Process.Start(start)!;
        var result = await GameStartupMonitor.ObserveAsync(process, executable, TimeSpan.FromSeconds(5));
        Assert.False(result.Running);
        Assert.Equal(23, result.ExitCode);
    }

    [Fact]
    public async Task ASpawnedProcessThatAlreadyExitedIsNotReportedAsRunning()
    {
        var executable = OperatingSystem.IsWindows() ? Path.Combine(Environment.SystemDirectory, "cmd.exe") : "/bin/sh";
        var start = new ProcessStartInfo(executable) { UseShellExecute = false, CreateNoWindow = true };
        if (OperatingSystem.IsWindows()) { start.ArgumentList.Add("/c"); start.ArgumentList.Add("exit 23"); }
        else { start.ArgumentList.Add("-c"); start.ArgumentList.Add("exit 23"); }
        using var process = Process.Start(start)!;
        await process.WaitForExitAsync();
        var result = await GameStartupMonitor.ObserveAsync(process, executable, TimeSpan.Zero);
        Assert.False(result.Running);
        Assert.Equal(23, result.ExitCode);
        Assert.Contains("0x00000017", result.Message);
    }

    [Fact]
    public async Task RunningProcessDoesNotClaimWindowOrVrValidation()
    {
        using var process = Process.GetCurrentProcess();
        var result = await GameStartupMonitor.ObserveAsync(process, Environment.ProcessPath!, TimeSpan.Zero);
        Assert.True(result.Running);
        Assert.Contains("process is running", result.Message);
        Assert.DoesNotContain("VR ready", result.Message);
    }

    [Fact]
    public async Task AnotherInstallationCannotStandInForTheSelectedExecutable()
    {
        using var process = Process.GetCurrentProcess();
        var result = await GameStartupMonitor.ObserveAsync(process, Environment.ProcessPath! + ".other-copy", TimeSpan.Zero);
        Assert.False(result.Running);
        Assert.Contains("selected installation", result.Message);
        Assert.False(process.HasExited);
    }

    [Fact]
    public async Task DryRunDoesNotObserveOrStartAProcess()
    {
        var result = await GameStartupMonitor.ObserveAsync(-1, "missing.exe", TimeSpan.Zero);
        Assert.True(result.Running);
        Assert.Contains("dry run", result.Message);
    }

    [Fact]
    public async Task CancellationDoesNotCloseAnExistingProcess()
    {
        using var process = Process.GetCurrentProcess();
        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            GameStartupMonitor.ObserveAsync(process, Environment.ProcessPath!, TimeSpan.FromSeconds(1), cancelled.Token));
        Assert.False(process.HasExited);
    }

    [Fact]
    public void GuiFormatsCliStatusWithoutExposingProtocolOrClaimingVrWasVerified()
    {
        var text = GameStartupMonitor.FormatCliResult("RESULT: cyberpunk-launch mode=vr pid=123 started=true mods=True vortex=False", true);
        Assert.Contains("Cyberpunk 2077", text);
        Assert.Contains("REDmod", text);
        Assert.DoesNotContain("RESULT:", text);
        Assert.DoesNotContain("pid=", text);
        Assert.DoesNotContain("VR ready", text);
        Assert.Equal("Game closed during startup (exit code 0xC0000409).", GameStartupMonitor.FormatCliResult(
            "RESULT: error=Game closed during startup (exit code 0xC0000409).", false));
    }
    [Theory]
    [InlineData(-1)]
    [InlineData(120001)]
    public async Task PidOverloadPreservesInvalidObservationErrors(int milliseconds)
    {
        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(() => GameStartupMonitor.ObserveAsync(
            Environment.ProcessId, Environment.ProcessPath!, TimeSpan.FromMilliseconds(milliseconds)));
    }

    [Fact]
    public async Task PidOverloadVerifiesTheSelectedRunningExecutable()
    {
        var result = await GameStartupMonitor.ObserveAsync(
            Environment.ProcessId, Environment.ProcessPath!, TimeSpan.Zero);
        Assert.True(result.Running);
        Assert.Contains("display readiness has not been verified", result.Message);
    }

    [Fact]
    public async Task MissingPidDoesNotBecomeAReportedSuccessfulLaunch()
    {
        var result = await GameStartupMonitor.ObserveAsync(int.MaxValue, "missing.exe", TimeSpan.Zero);
        Assert.False(result.Running);
        Assert.Contains("closed before startup", result.Message);
    }

    [Fact]
    public async Task CancellationDuringObservationLeavesTheSelectedProcessRunning()
    {
        using var process = Process.GetCurrentProcess();
        using var cancelled = new CancellationTokenSource(TimeSpan.FromMilliseconds(100));
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => GameStartupMonitor.ObserveAsync(
            process, Environment.ProcessPath!, TimeSpan.FromSeconds(5), cancelled.Token));
        Assert.False(process.HasExited);
    }
}
