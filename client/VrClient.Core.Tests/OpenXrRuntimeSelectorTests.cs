using VrClient.Core.Launch;
using Xunit;

public class OpenXrRuntimeSelectorTests
{
    [Fact]
    public void Uevr_runtime_status_distinguishes_vdxr_steam_absent_missing_and_conflict()
    {
        var steam = new XrRuntimeChoice(OpenXrRuntimeSelector.SteamVrName, @"C:\steamxr.json");
        var vd = new XrRuntimeChoice(OpenXrRuntimeSelector.VirtualDesktopName, @"C:\vdxr.json");
        var vdxr = OpenXrRuntimeSelector.EvaluateForUevr(
            ["VirtualDesktop.Streamer"], [steam, vd]);
        Assert.True(vdxr.Ready);
        Assert.Equal(OpenXrRuntimeSelector.VirtualDesktopName, vdxr.SelectedRuntime);
        var steamVr = OpenXrRuntimeSelector.EvaluateForUevr(["vrserver"], [steam, vd]);
        Assert.True(steamVr.Ready);
        Assert.Equal(OpenXrRuntimeSelector.SteamVrName, steamVr.SelectedRuntime);
        Assert.Equal(UevrRuntimeState.NoLiveSession,
            OpenXrRuntimeSelector.EvaluateForUevr([], [steam, vd]).State);
        Assert.Equal(UevrRuntimeState.RuntimeManifestMissing,
            OpenXrRuntimeSelector.EvaluateForUevr(["VirtualDesktop.Streamer"], []).State);
        Assert.Equal(UevrRuntimeState.RequestedRuntimeConflict,
            OpenXrRuntimeSelector.EvaluateForUevr(
                ["vrserver", "VirtualDesktop.Streamer"], [steam, vd],
                OpenXrRuntimeSelector.VirtualDesktopName).State);
    }

    private static readonly XrRuntimeChoice SteamVr =
        new(OpenXrRuntimeSelector.SteamVrName, @"C:\steam\steamxr_win64.json");
    private static readonly XrRuntimeChoice Vdxr =
        new(OpenXrRuntimeSelector.VirtualDesktopName, @"C:\vd\virtualdesktop-openxr.json");

    [Fact]
    public void Both_running_prefers_steamvr()
    {
        var choice = OpenXrRuntimeSelector.Select(
            ["vrserver", "VirtualDesktop.Streamer"], [SteamVr, Vdxr]);
        Assert.Equal(SteamVr, choice);
    }

    [Fact]
    public void Only_virtual_desktop_running_picks_vdxr()
    {
        var choice = OpenXrRuntimeSelector.Select(
            ["VirtualDesktop.Streamer"], [SteamVr, Vdxr]);
        Assert.Equal(Vdxr, choice);
    }

    [Fact]
    public void Steamvr_running_but_manifest_missing_falls_through_to_vd()
    {
        var choice = OpenXrRuntimeSelector.Select(
            ["vrserver", "VirtualDesktop.Streamer"], [Vdxr]);
        Assert.Equal(Vdxr, choice);
    }

    [Fact]
    public void Nothing_running_returns_null_system_default()
    {
        var choice = OpenXrRuntimeSelector.Select([], [SteamVr, Vdxr]);
        Assert.Null(choice);
    }

    [Fact]
    public void Process_name_matching_is_case_insensitive()
    {
        var choice = OpenXrRuntimeSelector.Select(
            ["VRSERVER"], [SteamVr, Vdxr]);
        Assert.Equal(SteamVr, choice);
    }

    [Fact]
    public void Steamvr_running_with_no_manifests_at_all_returns_null()
    {
        var choice = OpenXrRuntimeSelector.Select(["vrserver"], []);
        Assert.Null(choice);
    }

    [Fact]
    public void Virtual_desktop_selection_uses_its_32bit_manifest_for_gta_sa()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"vrclient-xr-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            File.WriteAllText(Path.Combine(directory, "virtualdesktop-openxr.json"), "{}");
            File.WriteAllText(Path.Combine(directory, "virtualdesktop-openxr-32.json"), "{}");

            var choice = OpenXrRuntimeSelector.For32BitProcess(
                new XrRuntimeChoice(OpenXrRuntimeSelector.VirtualDesktopName,
                    Path.Combine(directory, "virtualdesktop-openxr.json")));

            Assert.NotNull(choice);
            Assert.Equal(Path.Combine(directory, "virtualdesktop-openxr-32.json"), choice.JsonPath);
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Steamvr_prefers_a_32bit_manifest_for_gta_sa()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"vrclient-steamvr-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            File.WriteAllText(Path.Combine(directory, "steamxr_win64.json"),
                "{\"runtime\":{\"library_path\":\"bin\\\\vrclient_x64.dll\"}}");
            File.WriteAllText(Path.Combine(directory, "steamxr_win32.json"),
                "{\"runtime\":{\"library_path\":\"bin\\\\vrclient.dll\"}}");

            var choice = OpenXrRuntimeSelector.For32BitProcess(
                new XrRuntimeChoice(OpenXrRuntimeSelector.SteamVrName,
                    Path.Combine(directory, "steamxr_win64.json")));

            Assert.NotNull(choice);
            Assert.Equal(Path.Combine(directory, "steamxr_win32.json"), choice.JsonPath);
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Steamvr_x64_only_manifest_is_not_pinned_for_gta_sa()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"vrclient-steamvr-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            var manifest = Path.Combine(directory, "steamxr_win64.json");
            File.WriteAllText(manifest,
                "{\"runtime\":{\"library_path\":\"bin\\\\vrclient_x64.dll\"}}");

            var choice = OpenXrRuntimeSelector.For32BitProcess(
                new XrRuntimeChoice(OpenXrRuntimeSelector.SteamVrName, manifest));

            Assert.Null(choice);
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void X86_auto_selection_falls_back_to_virtual_desktop_when_steamvr_is_x64_only()
    {
        var steamDirectory = Path.Combine(Path.GetTempPath(), $"vrclient-steamvr-{Guid.NewGuid():N}");
        var virtualDesktopDirectory = Path.Combine(Path.GetTempPath(), $"vrclient-vd-{Guid.NewGuid():N}");
        Directory.CreateDirectory(steamDirectory);
        Directory.CreateDirectory(virtualDesktopDirectory);
        try
        {
            var steamManifest = Path.Combine(steamDirectory, "steamxr_win64.json");
            var virtualDesktopManifest = Path.Combine(virtualDesktopDirectory, "virtualdesktop-openxr.json");
            var virtualDesktopX86Manifest = Path.Combine(virtualDesktopDirectory, "virtualdesktop-openxr-32.json");
            File.WriteAllText(steamManifest,
                "{\"runtime\":{\"library_path\":\"bin\\\\vrclient_x64.dll\"}}");
            File.WriteAllText(virtualDesktopManifest, "{}");
            File.WriteAllText(virtualDesktopX86Manifest, "{}");

            var choice = OpenXrRuntimeSelector.SelectFor32BitProcess(
                ["vrserver", "VirtualDesktop.Streamer"],
                [
                    new XrRuntimeChoice(OpenXrRuntimeSelector.SteamVrName, steamManifest),
                    new XrRuntimeChoice(OpenXrRuntimeSelector.VirtualDesktopName, virtualDesktopManifest)
                ]);

            Assert.NotNull(choice);
            Assert.Equal(virtualDesktopX86Manifest, choice.JsonPath);
        }
        finally
        {
            if (Directory.Exists(steamDirectory)) Directory.Delete(steamDirectory, recursive: true);
            if (Directory.Exists(virtualDesktopDirectory)) Directory.Delete(virtualDesktopDirectory, recursive: true);
        }
    }

    [Theory]
    [InlineData((ushort)0x014c, true)]
    [InlineData((ushort)0x8664, false)]
    public void Custom_runtime_manifest_is_checked_against_the_native_library_architecture(
        ushort machine, bool expectedCompatible)
    {
        var directory = Path.Combine(Path.GetTempPath(), $"vrclient-custom-xr-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            WritePe(Path.Combine(directory, "runtime.dll"), machine);
            var manifest = Path.Combine(directory, "custom.json");
            File.WriteAllText(manifest,
                "{\"runtime\":{\"library_path\":\"runtime.dll\"}}");

            var choice = OpenXrRuntimeSelector.For32BitProcess(
                new XrRuntimeChoice("Custom", manifest));

            Assert.Equal(expectedCompatible, choice is not null);
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    private static void WritePe(string path, ushort machine)
    {
        var bytes = new byte[128];
        bytes[0] = 0x4d;
        bytes[1] = 0x5a;
        BitConverter.GetBytes(0x40).CopyTo(bytes, 0x3c);
        bytes[0x40] = 0x50;
        bytes[0x41] = 0x45;
        BitConverter.GetBytes(machine).CopyTo(bytes, 0x44);
        File.WriteAllBytes(path, bytes);
    }
}
