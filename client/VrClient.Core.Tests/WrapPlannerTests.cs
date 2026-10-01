using VrClient.Core.Launch;
using Xunit;

public class WrapPlannerTests
{
    private static readonly XrRuntimeChoice SteamVr =
        new(OpenXrRuntimeSelector.SteamVrName, @"C:\steam\steamxr_win64.json");
    private static readonly XrRuntimeChoice Vdxr =
        new(OpenXrRuntimeSelector.VirtualDesktopName, @"C:\vd\virtualdesktop-openxr.json");

    private static readonly string[] GameCommand = [@"C:\games\REPO\REPO.exe"];

    [Fact]
    public void Auto_with_no_vr_runtime_running_is_flat_with_disable_vr_appended()
    {
        var plan = WrapPlanner.Plan(GameCommand, WrapVrMode.Auto, [], [SteamVr, Vdxr]);
        Assert.True(plan.FlatMode);
        Assert.Null(plan.PinRuntimeJsonPath);
        Assert.Equal([GameCommand[0], WrapPlanner.DisableVrArg], plan.Command);
    }

    [Fact]
    public void Auto_with_steamvr_running_pins_steamvr_and_keeps_command_unchanged()
    {
        var plan = WrapPlanner.Plan(GameCommand, WrapVrMode.Auto, ["vrserver"], [SteamVr, Vdxr]);
        Assert.False(plan.FlatMode);
        Assert.Equal(SteamVr.JsonPath, plan.PinRuntimeJsonPath);
        Assert.Equal(GameCommand, plan.Command);
    }

    [Fact]
    public void Explicit_vr_marker_keeps_vr_without_a_detected_runtime_and_never_reaches_the_game()
    {
        string[] vrWithMods = [GameCommand[0], "--doorstop-enabled", "true", WrapPlanner.ForceVrArg];

        var undetected = WrapPlanner.Plan(vrWithMods, WrapVrMode.Auto, [], [SteamVr, Vdxr]);
        Assert.False(undetected.FlatMode);
        Assert.Equal([GameCommand[0], "--doorstop-enabled", "true"], undetected.Command);

        var detected = WrapPlanner.Plan(vrWithMods, WrapVrMode.Auto, ["vrserver"], [SteamVr, Vdxr]);
        Assert.Equal(SteamVr.JsonPath, detected.PinRuntimeJsonPath);
        Assert.DoesNotContain(WrapPlanner.ForceVrArg, detected.Command);

        // A flat launch forced in the Steam launch option still wins over the marker.
        var forcedFlat = WrapPlanner.Plan(vrWithMods, WrapVrMode.Off, ["vrserver"], [SteamVr, Vdxr]);
        Assert.True(forcedFlat.FlatMode);
        Assert.DoesNotContain(WrapPlanner.ForceVrArg, forcedFlat.Command);
    }

    [Fact]
    public void Explicit_flat_launch_stays_flat_with_a_running_headset_and_one_switch()
    {
        string[] modsNoVr = [GameCommand[0], "--doorstop-enabled", "true", WrapPlanner.DisableVrArg];

        foreach (var mode in new[] { WrapVrMode.Auto, WrapVrMode.On })
        {
            var plan = WrapPlanner.Plan(modsNoVr, mode, ["vrserver"], [SteamVr, Vdxr]);
            Assert.True(plan.FlatMode);
            Assert.Null(plan.PinRuntimeJsonPath);
            Assert.Equal(modsNoVr, plan.Command);
        }
    }

    [Fact]
    public void Off_forces_flat_even_with_steamvr_running()
    {
        var plan = WrapPlanner.Plan(GameCommand, WrapVrMode.Off, ["vrserver"], [SteamVr, Vdxr]);
        Assert.True(plan.FlatMode);
        Assert.Null(plan.PinRuntimeJsonPath);
        Assert.Contains(WrapPlanner.DisableVrArg, plan.Command);
    }

    [Fact]
    public void Explicitly_disabled_doorstop_stays_flat_even_with_a_running_headset()
    {
        string[] vanilla = [GameCommand[0], "--doorstop-enable", "false", "--doorstop-enabled", "false"];
        var plan = WrapPlanner.Plan(vanilla, WrapVrMode.Auto, ["vrserver"], [SteamVr, Vdxr]);
        Assert.True(plan.FlatMode);
        Assert.Null(plan.PinRuntimeJsonPath);
        Assert.Contains(WrapPlanner.DisableVrArg, plan.Command);
    }

    [Fact]
    public void On_with_nothing_running_stays_vr_without_pin()
    {
        var plan = WrapPlanner.Plan(GameCommand, WrapVrMode.On, [], [SteamVr, Vdxr]);
        Assert.False(plan.FlatMode);
        Assert.Null(plan.PinRuntimeJsonPath);
        Assert.DoesNotContain(WrapPlanner.DisableVrArg, plan.Command);
    }

    [Fact]
    public void Mod_manager_doorstop_args_pass_through_verbatim_in_order()
    {
        string[] doorstop =
        [
            @"C:\games\REPO\REPO.exe",
            "--doorstop-enable", "true",
            "--doorstop-target", @"C:\r2modman\profiles\Default\BepInEx\core\BepInEx.Preloader.dll",
        ];
        var vr = WrapPlanner.Plan(doorstop, WrapVrMode.Auto, ["VirtualDesktop.Streamer"], [SteamVr, Vdxr]);
        Assert.Equal(doorstop, vr.Command);

        var flat = WrapPlanner.Plan(doorstop, WrapVrMode.Auto, [], [SteamVr, Vdxr]);
        Assert.Equal([.. doorstop, WrapPlanner.DisableVrArg], flat.Command);
    }

    [Fact]
    public void Empty_command_throws()
    {
        Assert.Throws<ArgumentException>(() => WrapPlanner.Plan([], WrapVrMode.Auto, [], []));
    }

    [Theory]
    [InlineData("on", WrapVrMode.On)]
    [InlineData("OFF", WrapVrMode.Off)]
    [InlineData("auto", WrapVrMode.Auto)]
    [InlineData(null, WrapVrMode.Auto)]
    [InlineData("garbage", WrapVrMode.Auto)]
    public void ParseMode_is_forgiving_and_defaults_to_auto(string? text, WrapVrMode expected)
    {
        Assert.Equal(expected, WrapPlanner.ParseMode(text));
    }
}
