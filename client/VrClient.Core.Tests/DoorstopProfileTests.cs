using VrClient.Core.Launch;
using Xunit;

public class DoorstopProfileTests
{
    private const string Profile = @"C:\Users\u\AppData\Roaming\r2modmanPlus-local\REPO\profiles\Go to";
    private const string Preloader = Profile + @"\BepInEx\core\BepInEx.Preloader.dll";

    [Fact]
    public void Doorstop4_args_yield_profile_root()
    {
        var root = DoorstopProfile.ProfileRootFromCommand(
            [@"C:\g\REPO.exe", "--doorstop-enabled", "true", "--doorstop-target-assembly", Preloader]);
        Assert.Equal(Profile, root);
    }

    [Fact]
    public void Doorstop3_args_yield_profile_root()
    {
        var root = DoorstopProfile.ProfileRootFromCommand(
            [@"C:\g\REPO.exe", "--doorstop-enable", "true", "--doorstop-target", Preloader]);
        Assert.Equal(Profile, root);
    }

    [Fact]
    public void Explicit_disable_means_unmodded_launch()
    {
        string[] command = [@"C:\g\REPO.exe", "--doorstop-enabled", "false",
            "--doorstop-target-assembly", Preloader];
        Assert.True(DoorstopProfile.IsExplicitlyDisabled(command));
        Assert.Null(DoorstopProfile.ProfileRootFromCommand(command));
        Assert.False(DoorstopProfile.IsExplicitlyDisabled([@"C:\g\REPO.exe"]));
    }

    [Fact]
    public void No_doorstop_args_yield_null()
    {
        Assert.Null(DoorstopProfile.ProfileRootFromCommand([@"C:\g\REPO.exe", "-novid"]));
    }

    [Fact]
    public void First_target_wins_when_both_styles_present()
    {
        var root = DoorstopProfile.ProfileRootFromCommand(
        [
            @"C:\g\REPO.exe",
            "--doorstop-enable", "true", "--doorstop-target", Preloader,
            "--doorstop-enabled", "true", "--doorstop-target-assembly", Preloader,
        ]);
        Assert.Equal(Profile, root);
    }

    [Fact]
    public void HasVrMod_requires_the_mod_folder_to_exist_in_the_profile()
    {
        var profile = Directory.CreateTempSubdirectory("vrclient-test-profile").FullName;
        try
        {
            string[] hints = ["BepInEx-BepInExPack", "DaXcess-RepoXR"];
            // no plugins dir at all
            Assert.False(DoorstopProfile.HasVrMod(profile, hints));
            // plugins with unrelated mods only
            var plugins = Directory.CreateDirectory(Path.Combine(profile, "BepInEx", "plugins")).FullName;
            Directory.CreateDirectory(Path.Combine(plugins, "SomeTeam-SomeMod"));
            Assert.False(DoorstopProfile.HasVrMod(profile, hints));
            // VR mod installed by the mod manager -> opt-in
            Directory.CreateDirectory(Path.Combine(plugins, "DaXcess-RepoXR"));
            Assert.True(DoorstopProfile.HasVrMod(profile, hints));
        }
        finally
        {
            Directory.Delete(profile, recursive: true);
        }
    }

    [Fact]
    public void MinimalPinCfg_carries_the_internal_section_and_value()
    {
        var cfg = DoorstopProfile.MinimalPinCfg(@"C:\steam\steamxr_win64.json");
        Assert.Contains("[Internal]", cfg);
        Assert.Contains(@"OpenXRRuntimeFile = C:\steam\steamxr_win64.json", cfg);
    }
}
