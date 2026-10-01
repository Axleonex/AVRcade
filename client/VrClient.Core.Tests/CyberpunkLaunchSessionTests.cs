using VrClient.Core.Launch;
using VrClient.Core.Model;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkLaunchSessionTests
{
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
