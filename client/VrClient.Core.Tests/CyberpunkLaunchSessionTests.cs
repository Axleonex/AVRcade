using VrClient.Core.Launch;
using VrClient.Core.Model;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkLaunchSessionTests
{
    [Theory]
    [InlineData("bin")]
    [InlineData(".avrcade/cyberpunk-2077/launcher-config-backups")]
    public void AutomaticLauncherCannotWriteThroughAnotherInstallationLink(string linked)
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-launcher-link-" + Guid.NewGuid().ToString("N"));
        var game = Path.Combine(root, "game");
        var outside = Path.Combine(root, "other-game");
        var link = Path.Combine(game, linked.Replace('/', Path.DirectorySeparatorChar));
        try
        {
            Directory.CreateDirectory(Path.Combine(game, "bin", "x64"));
            File.WriteAllText(Path.Combine(game, "bin", "x64", "vrport-launcher.ini"), "show_launcher=1\n");
            Directory.CreateDirectory(outside);
            if (linked == "bin") Directory.Delete(Path.Combine(game, "bin"), true);
            Directory.CreateDirectory(Path.GetDirectoryName(link)!);
            if (OperatingSystem.IsWindows())
            {
                var start = new System.Diagnostics.ProcessStartInfo("powershell.exe") { UseShellExecute = false, CreateNoWindow = true };
                start.ArgumentList.Add("-NoProfile"); start.ArgumentList.Add("-Command");
                start.ArgumentList.Add("New-Item -ItemType Junction -Path '" + link.Replace("'", "''") + "' -Target '" + outside.Replace("'", "''") + "' | Out-Null");
                using var process = System.Diagnostics.Process.Start(start)!;
                process.WaitForExit(); Assert.Equal(0, process.ExitCode);
            }
            else Directory.CreateSymbolicLink(link, outside);
            Assert.Throws<InvalidDataException>(() => new CyberpunkLaunchSession().PrepareManagedLauncher(game, CyberpunkLaunchMode.Vr));
            Assert.Empty(Directory.EnumerateFileSystemEntries(outside));
        }
        finally
        {
            if (Directory.Exists(link) && (File.GetAttributes(link) & FileAttributes.ReparsePoint) != 0) Directory.Delete(link);
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
    }

    [Fact]
    public void AutomaticLauncherPreservesSettingsAndOriginalBackupAndIsIdempotent()
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-launcher-" + Guid.NewGuid().ToString("N"));
        try
        {
            var path = Path.Combine(root, "bin", "x64", "vrport-launcher.ini");
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            const string original = "width=2864\r\nheight=3184\r\nhmd_type=2\r\ndebug=0\r\nshow_launcher=1\r\nlauncher_delay_ms=1500\r\ncustom=value\r\n";
            File.WriteAllText(path, original);
            var session = new CyberpunkLaunchSession();
            var backup = session.PrepareManagedLauncher(root, CyberpunkLaunchMode.Vr);
            Assert.NotNull(backup);
            Assert.Equal(original, File.ReadAllText(backup));
            Assert.Equal(original.Replace("show_launcher=1", "show_launcher=0"), File.ReadAllText(path));
            var changedAt = File.GetLastWriteTimeUtc(path);
            Assert.Null(session.PrepareManagedLauncher(root, CyberpunkLaunchMode.Vr));
            Assert.Equal(changedAt, File.GetLastWriteTimeUtc(path));
            Assert.Single(Directory.GetFiles(Path.GetDirectoryName(backup)!));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
    }

    [Fact]
    public void AutomaticLauncherCreatesOnlySupportedDefaultsWhenConfigIsAbsent()
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-launcher-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(root);
            Assert.Null(new CyberpunkLaunchSession().PrepareManagedLauncher(root, CyberpunkLaunchMode.Vr));
            var text = File.ReadAllText(Path.Combine(root, "bin", "x64", "vrport-launcher.ini"));
            Assert.Contains("show_launcher=0", text);
            Assert.Contains("launcher_delay_ms=1000", text);
            Assert.DoesNotContain("width=", text);
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
    }

    [Theory]
    [InlineData(CyberpunkLaunchMode.Flat, false)]
    [InlineData(CyberpunkLaunchMode.Vr, true)]
    public void FlatAndDryRunDoNotChangeLauncherFiles(CyberpunkLaunchMode mode, bool dryRun)
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-launcher-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(root);
            Assert.Null(new CyberpunkLaunchSession().PrepareManagedLauncher(root, mode, dryRun));
            Assert.Empty(Directory.EnumerateFileSystemEntries(root));
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void DiagnosticsSeparateRedmodFromVortexAndRecordStartupFailure()
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-startup-" + Guid.NewGuid().ToString("N"));
        try
        {
            var session = new CyberpunkLaunchSession();
            var path = session.WriteDiagnostics(root, CyberpunkLaunchMode.Vr, "test", null, 42,
                withVortex: false, withMods: true);
            session.WriteStartupResult(path, new GameStartupResult(false, unchecked((int)0xC0000409), "Game closed during startup."));
            using var document = System.Text.Json.JsonDocument.Parse(File.ReadAllText(path));
            Assert.False(document.RootElement.GetProperty("vortex_launch").GetBoolean());
            Assert.True(document.RootElement.GetProperty("redmod_enabled").GetBoolean());
            Assert.Equal("startup_failed", document.RootElement.GetProperty("startup_status").GetString());
            Assert.Equal(unchecked((int)0xC0000409), document.RootElement.GetProperty("startup_exit_code").GetInt32());
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
    }

    [Fact]
    public void Vortex_launch_enables_redmod_without_changing_normal_vr_arguments()
    {
        var session = new CyberpunkLaunchSession();
        var plan = new LaunchPlan(
            @"C:\Games\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe",
            ModPresent: true,
            new SafetyVerdict(Verdict.Allow, "test", "", "test"));

        var normal = new GameLauncher().BuildStartInfo(
            plan, childEnvironment: session.BuildEnvironment(CyberpunkLaunchMode.Vr),
            launchArguments: session.BuildLaunchArguments(withVortex: false));
        var vortex = new GameLauncher().BuildStartInfo(
            plan, childEnvironment: session.BuildEnvironment(CyberpunkLaunchMode.Vr),
            launchArguments: session.BuildLaunchArguments(withVortex: true));
        var flatVortex = new GameLauncher().BuildStartInfo(
            plan, childEnvironment: session.BuildEnvironment(CyberpunkLaunchMode.Flat),
            launchArguments: session.BuildLaunchArguments(withVortex: true));

        Assert.Empty(normal.ArgumentList);
        Assert.Equal(["-modded"], vortex.ArgumentList);
        Assert.Equal("1", normal.Environment[CyberpunkLaunchSession.ActivationVariable]);
        Assert.Equal("1", vortex.Environment[CyberpunkLaunchSession.ActivationVariable]);
        Assert.Equal(new[] { "-modded" }, flatVortex.ArgumentList);
        Assert.False(flatVortex.Environment.ContainsKey(CyberpunkLaunchSession.ActivationVariable));
    }

    [Theory]
    [InlineData(null, false)]
    [InlineData("0", false)]
    [InlineData("true", false)]
    [InlineData("1", true)]
    public void Activation_requires_the_exact_per_process_opt_in(string? value, bool expected)
    {
        var environment = value is null
            ? new Dictionary<string, string>()
            : new Dictionary<string, string> { [CyberpunkLaunchSession.ActivationVariable] = value };

        Assert.Equal(expected, CyberpunkLaunchSession.IsVrActive(environment));
    }

    [Fact]
    public void Launch_modes_build_child_only_environment_without_mutating_the_parent()
    {
        const string parentSentinel = "VRCLIENT_PARENT_SENTINEL";
        var previous = Environment.GetEnvironmentVariable(parentSentinel);
        Environment.SetEnvironmentVariable(parentSentinel, "parent");
        try
        {
            var session = new CyberpunkLaunchSession();
            var xr = new XrRuntimeChoice("Test runtime", @"C:\runtime\openxr.json");
            var plan = new LaunchPlan(
                @"C:\Games\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe",
                ModPresent: true,
                new SafetyVerdict(Verdict.Allow, "test", "", "test"));

            var vrInfo = new GameLauncher().BuildStartInfo(
                plan, xr, session.BuildEnvironment(CyberpunkLaunchMode.Vr));
            var flatInfo = new GameLauncher().BuildStartInfo(
                plan, xrRuntime: null, session.BuildEnvironment(CyberpunkLaunchMode.Flat));

            Assert.Equal("1", vrInfo.Environment[CyberpunkLaunchSession.ActivationVariable]);
            Assert.Equal(xr.JsonPath, vrInfo.Environment["XR_RUNTIME_JSON"]);
            Assert.False(flatInfo.Environment.ContainsKey(CyberpunkLaunchSession.ActivationVariable));
            Assert.False(flatInfo.Environment.ContainsKey("XR_RUNTIME_JSON"));
            Assert.Equal("parent", Environment.GetEnvironmentVariable(parentSentinel));
            Assert.Null(Environment.GetEnvironmentVariable(CyberpunkLaunchSession.ActivationVariable));
        }
        finally
        {
            Environment.SetEnvironmentVariable(parentSentinel, previous);
        }
    }

    [Fact]
    public void Launch_without_runtime_choice_does_not_inherit_a_parent_runtime_pin()
    {
        const string previousRuntime = "VRCLIENT_TEST_PARENT_RUNTIME";
        var previous = Environment.GetEnvironmentVariable("XR_RUNTIME_JSON");
        Environment.SetEnvironmentVariable("XR_RUNTIME_JSON", previousRuntime);
        try
        {
            var plan = new LaunchPlan(
                @"C:\Games\Test\game.exe",
                ModPresent: true,
                new SafetyVerdict(Verdict.Allow, "test", "", "test"));

            var startInfo = new GameLauncher().BuildStartInfo(plan);

            Assert.False(startInfo.Environment.ContainsKey("XR_RUNTIME_JSON"));
        }
        finally
        {
            Environment.SetEnvironmentVariable("XR_RUNTIME_JSON", previous);
        }
    }
}
