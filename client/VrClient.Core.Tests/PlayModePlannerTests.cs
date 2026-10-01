using VrClient.Core.App;

namespace VrClient.Core.Tests;

public sealed class PlayModePlannerTests
{
    private static readonly PlayMode[] FourModes =
        [PlayMode.VrOnly, PlayMode.VrWithMods, PlayMode.ModsNoVr, PlayMode.Vanilla];

    private static UnityPlayFacts Unity(
        bool installed = true, bool allowed = true, bool conversion = true, bool vrOnly = true,
        PlayModeProfile? vr = null, PlayModeProfile? flat = null, bool managers = true,
        bool runtime = true, bool needsSteamVr = false, bool steamVr = false) =>
        new(installed, allowed, conversion, vrOnly, vr, flat, managers, runtime, needsSteamVr, steamVr);

    private static PlayModeProfile Profile(bool? vrMod, bool? loader = true, bool otherMods = true,
        bool direct = true, bool linked = true) =>
        new("r2modman · Friends", "r2modman", vrMod, loader, otherMods, direct, linked);

    private static PlayModeState Mode(IReadOnlyList<PlayModeState> modes, PlayMode mode) =>
        modes.Single(state => state.Mode == mode);

    [Fact]
    public void EveryEngineAlwaysOffersTheSameFourModesInOrder()
    {
        foreach (var installed in new[] { false, true })
        foreach (var ready in new[] { false, true })
        {
            IReadOnlyList<PlayModeState>[] plans =
            [
                PlayModePlanner.Unity(Unity(installed: installed, conversion: ready)),
                PlayModePlanner.Unreal(new UnrealPlayFacts(installed, true, ready, ready, ready ? 2 : 0, ready)),
                PlayModePlanner.Cyberpunk(new CyberpunkPlayFacts(installed, null, ready, ready, true)),
                PlayModePlanner.GtaSanAndreas(new GtaPlayFacts(installed, null, ready, ready, ready))
            ];
            foreach (var plan in plans)
                Assert.Equal(FourModes, plan.Select(state => state.Mode));
        }
    }

    [Fact]
    public void NothingLaunchesBeforeTheGameIsInstalledOrWhenSafetyBlocks()
    {
        Assert.All(PlayModePlanner.Unity(Unity(installed: false)), state => Assert.False(state.CanAct));
        Assert.All(PlayModePlanner.Unity(Unity(allowed: false)), state => Assert.False(state.CanAct));
        Assert.All(PlayModePlanner.Unreal(new UnrealPlayFacts(false, true, true, true, 0, true)),
            state => Assert.False(state.CanAct));
    }

    [Fact]
    public void Unity_FreshGame_InstallsTheVrModAndStillPlaysVanilla()
    {
        var modes = PlayModePlanner.Unity(Unity(conversion: false, vrOnly: false, managers: false));

        Assert.Equal(PlayModeSetup.InstallVr, Mode(modes, PlayMode.VrOnly).Setup);
        Assert.Equal(PlayModeSetup.OpenMods, Mode(modes, PlayMode.VrWithMods).Setup);
        Assert.Equal(PlayModeSetup.OpenMods, Mode(modes, PlayMode.ModsNoVr).Setup);
        Assert.True(Mode(modes, PlayMode.Vanilla).CanLaunch);
    }

    [Fact]
    public void Unity_AllFourModesCanBeReadyAtOnce()
    {
        var modes = PlayModePlanner.Unity(Unity(vr: Profile(vrMod: true), flat: Profile(vrMod: false)));

        Assert.All(modes, state => Assert.True(state.CanLaunch));
        Assert.All(modes, state => Assert.False(state.Caution));
    }

    [Fact]
    public void Unity_VrOnlyRefusesAGameFolderThatHoldsOtherMods()
    {
        var modes = PlayModePlanner.Unity(Unity(vrOnly: false));

        Assert.Equal(PlayModeAvailability.Unavailable, Mode(modes, PlayMode.VrOnly).Availability);
        // The same folder is exactly what "VR + mods" plays when no profile is chosen.
        Assert.True(Mode(modes, PlayMode.VrWithMods).CanLaunch);
    }

    [Fact]
    public void Unity_ProfileWithoutVrModOrLoaderNeedsSetupNotALaunch()
    {
        Assert.Equal(PlayModeSetup.OpenMods, Mode(PlayModePlanner.Unity(
            Unity(vr: Profile(vrMod: false))), PlayMode.VrWithMods).Setup);
        Assert.Equal(PlayModeSetup.OpenMods, Mode(PlayModePlanner.Unity(
            Unity(vr: Profile(vrMod: true, loader: false))), PlayMode.VrWithMods).Setup);
        Assert.Equal(PlayModeSetup.OpenMods, Mode(PlayModePlanner.Unity(
            Unity(flat: Profile(vrMod: true))), PlayMode.ModsNoVr).Setup);
        Assert.Equal(PlayModeSetup.OpenMods, Mode(PlayModePlanner.Unity(
            Unity(flat: Profile(vrMod: false, otherMods: false))), PlayMode.ModsNoVr).Setup);
    }

    [Fact]
    public void Unity_ProfileLaunchNeedsTheManagersLoaderInTheGameFolder()
    {
        var modes = PlayModePlanner.Unity(Unity(conversion: false, vrOnly: false,
            vr: Profile(vrMod: true, linked: false), flat: Profile(vrMod: false, linked: false)));

        Assert.False(Mode(modes, PlayMode.VrWithMods).CanLaunch);
        Assert.Contains("Start modded", Mode(modes, PlayMode.VrWithMods).Status);
        Assert.False(Mode(modes, PlayMode.ModsNoVr).CanLaunch);
    }

    [Fact]
    public void Unity_ManagerThatHidesItsModListHandsOverToTheManager()
    {
        var vortex = new PlayModeProfile("Vortex · Default", "Vortex", null, null, false, false, true);
        var modes = PlayModePlanner.Unity(Unity(vr: vortex, flat: vortex));

        Assert.Equal("Open Vortex", Mode(modes, PlayMode.VrWithMods).Action);
        Assert.Equal("Open Vortex", Mode(modes, PlayMode.ModsNoVr).Action);
    }

    [Fact]
    public void MissingHeadsetSoftwareCautionsVrModesButNeverBlocksThem()
    {
        var modes = PlayModePlanner.Unity(Unity(runtime: false));

        Assert.True(Mode(modes, PlayMode.VrOnly).CanLaunch);
        Assert.True(Mode(modes, PlayMode.VrOnly).Caution);
        Assert.False(Mode(modes, PlayMode.Vanilla).Caution);
    }

    [Fact]
    public void Unity_SteamVrOnlyModSaysSoWhenSteamVrIsOff()
    {
        var modes = PlayModePlanner.Unity(Unity(needsSteamVr: true, steamVr: false));

        Assert.Equal("Start SteamVR first", Mode(modes, PlayMode.VrOnly).Status);
    }

    [Theory]
    [InlineData(0, true, false, false, true)]
    [InlineData(3, false, true, true, false)]
    public void Unreal_ModsInTheGameFolderDecideWhichModesAreHonest(
        int modFiles, bool vrOnly, bool vrMods, bool flatMods, bool vanilla)
    {
        var modes = PlayModePlanner.Unreal(new UnrealPlayFacts(true, true, true, true, modFiles, true));

        Assert.Equal(vrOnly, Mode(modes, PlayMode.VrOnly).CanLaunch);
        Assert.Equal(vrMods, Mode(modes, PlayMode.VrWithMods).CanLaunch);
        Assert.Equal(flatMods, Mode(modes, PlayMode.ModsNoVr).CanLaunch);
        Assert.Equal(vanilla, Mode(modes, PlayMode.Vanilla).CanLaunch);
    }

    [Fact]
    public void Unreal_VrModesFirstDownloadTheVrSetup()
    {
        var modes = PlayModePlanner.Unreal(new UnrealPlayFacts(true, true, false, false, 0, true));

        Assert.Equal(PlayModeSetup.InstallVr, Mode(modes, PlayMode.VrOnly).Setup);
        Assert.Equal(PlayModeSetup.InstallVr, Mode(modes, PlayMode.VrWithMods).Setup);
        Assert.True(Mode(modes, PlayMode.Vanilla).CanLaunch);
    }

    [Fact]
    public void Cyberpunk_MonitorModesWorkWithoutTheVrBackend()
    {
        var modes = PlayModePlanner.Cyberpunk(new CyberpunkPlayFacts(true, null, false, true, true));

        Assert.Equal(PlayModeSetup.InstallVr, Mode(modes, PlayMode.VrOnly).Setup);
        Assert.Equal(PlayModeSetup.InstallVr, Mode(modes, PlayMode.VrWithMods).Setup);
        Assert.True(Mode(modes, PlayMode.ModsNoVr).CanLaunch);
        Assert.True(Mode(modes, PlayMode.Vanilla).CanLaunch);
    }

    [Fact]
    public void Cyberpunk_WithoutTheBackendFilesTheInstallIsNotOffered()
    {
        var modes = PlayModePlanner.Cyberpunk(new CyberpunkPlayFacts(true, null, false, true, false));

        Assert.Equal(PlayModeSetup.OpenVrSetup, Mode(modes, PlayMode.VrOnly).Setup);
        Assert.Contains("does not include", Mode(modes, PlayMode.VrOnly).Status);
        Assert.True(Mode(modes, PlayMode.Vanilla).CanLaunch);
    }

    [Fact]
    public void Cyberpunk_UnsupportedBuildRulesOutVrOnly()
    {
        var modes = PlayModePlanner.Cyberpunk(new CyberpunkPlayFacts(true, "build mismatch", true, true, true));

        Assert.Equal(PlayModeAvailability.Unavailable, Mode(modes, PlayMode.VrOnly).Availability);
        Assert.Equal("build mismatch", Mode(modes, PlayMode.VrWithMods).Status);
        Assert.True(Mode(modes, PlayMode.Vanilla).CanLaunch);
    }

    [Fact]
    public void GtaSanAndreas_VanillaNeedsItsSeparateCleanFolder()
    {
        var without = PlayModePlanner.GtaSanAndreas(new GtaPlayFacts(true, null, true, false, true));
        var with = PlayModePlanner.GtaSanAndreas(new GtaPlayFacts(true, null, true, true, true));

        Assert.Equal(PlayModeSetup.ChooseCleanFolder, Mode(without, PlayMode.Vanilla).Setup);
        Assert.All(with, state => Assert.True(state.CanLaunch));
    }

    [Fact]
    public void GtaSanAndreas_VrModesInstallTheBridgeFirst()
    {
        var modes = PlayModePlanner.GtaSanAndreas(new GtaPlayFacts(true, null, false, false, true));

        Assert.Equal(PlayModeSetup.InstallVr, Mode(modes, PlayMode.VrOnly).Setup);
        Assert.Equal(PlayModeSetup.InstallVr, Mode(modes, PlayMode.VrWithMods).Setup);
        Assert.True(Mode(modes, PlayMode.ModsNoVr).CanLaunch);
    }
}
