using System.ComponentModel;
using VrClient.Core.Launch;

namespace VrClient.Core.Tests;

public sealed class SteamVrStartupTests : TempTree
{
    private string InstallRuntime()
    {
        var exe = Touch("steamapps", "common", "SteamVR", "bin", "win64", "vrstartup.exe");
        File.WriteAllText(Touch("steamapps", "appmanifest_250820.acf"),
            "\"AppState\" { \"installdir\" \"SteamVR\" }");
        return exe;
    }

    [Fact]
    public async Task StartsInstalledRuntimeAndWaitsForServer()
    {
        var expected = InstallRuntime();
        var ready = false;
        var starts = 0;
        var waits = 0;
        var startup = new SteamVrStartup(() => ready, info =>
        {
            Assert.Equal(expected, info.FileName);
            Assert.False(info.UseShellExecute);
            Assert.True(info.CreateNoWindow);
            starts++;
            return true;
        }, (_, _) => { waits++; ready = true; return Task.CompletedTask; }, _ => true);
        Assert.True((await startup.EnsureRunningAsync(Root)).Success);
        Assert.Equal(1, starts);
        Assert.Equal(1, waits);
    }

    [Fact]
    public async Task RunningServerStillRequiresAUsableHeadset()
    {
        InstallRuntime();
        var waits = 0;
        var startup = new SteamVrStartup(() => true,
            _ => throw new InvalidOperationException("must not start"),
            (_, _) => { waits++; return Task.CompletedTask; }, _ => false);
        var result = await startup.EnsureRunningAsync(Root);
        Assert.False(result.Success);
        Assert.Equal(40, waits);
        Assert.Contains("no usable headset", result.Detail);
        Assert.Contains("not launched", result.Detail);
    }

    [Fact]
    public async Task WaitsForHeadsetEvenWhenServerAlreadyExists()
    {
        InstallRuntime();
        var ready = false;
        var startup = new SteamVrStartup(() => true,
            _ => throw new InvalidOperationException("must not start"),
            (_, _) => { ready = true; return Task.CompletedTask; }, _ => ready);
        Assert.True((await startup.EnsureRunningAsync(Root)).Success);
    }

    [Fact]
    public async Task MissingRuntimeDoesNotStartAnything()
    {
        var startup = new SteamVrStartup(() => false,
            _ => throw new InvalidOperationException("must not start"));
        var result = await startup.EnsureRunningAsync(Root);
        Assert.False(result.Success);
        Assert.Contains("not installed", result.Detail);
    }

    [Fact]
    public async Task DryRunDoesNotStartOrWait()
    {
        InstallRuntime();
        var startup = new SteamVrStartup(() => false,
            _ => throw new InvalidOperationException("must not start"),
            (_, _) => throw new InvalidOperationException("must not wait"));
        var result = await startup.EnsureRunningAsync(Root, dryRun: true);
        Assert.True(result.Success);
        Assert.Contains("startup plan", result.Detail);
    }

    [Fact]
    public async Task TimeoutIsBoundedAndDoesNotMistakeStartupForServerReadiness()
    {
        InstallRuntime();
        var waits = 0;
        var startup = new SteamVrStartup(() => false, _ => true,
            (duration, _) =>
            {
                Assert.Equal(TimeSpan.FromMilliseconds(500), duration);
                waits++;
                return Task.CompletedTask;
            });
        var result = await startup.EnsureRunningAsync(Root);
        Assert.False(result.Success);
        Assert.Equal(40, waits);
        Assert.Contains("within 20 seconds", result.Detail);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task StartFailureIsReported(bool throws)
    {
        InstallRuntime();
        var startup = new SteamVrStartup(() => false,
            _ => throws ? throw new Win32Exception("fixture denied") : false);
        var result = await startup.EnsureRunningAsync(Root);
        Assert.False(result.Success);
        Assert.Contains("SteamVR", result.Detail);
    }

    [Fact]
    public async Task CancellationStopsWaitingWithoutReportingReady()
    {
        InstallRuntime();
        using var cancel = new CancellationTokenSource();
        var startup = new SteamVrStartup(() => false, _ => true,
            (_, _) => { cancel.Cancel(); return Task.CompletedTask; });
        var result = await startup.EnsureRunningAsync(Root, ct: cancel.Token);
        Assert.False(result.Success);
        Assert.Contains("cancelled", result.Detail);
    }
}
