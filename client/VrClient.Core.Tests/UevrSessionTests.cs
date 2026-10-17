using VrClient.Core.Unreal;

public sealed class UevrSessionTests
{
    private static readonly UevrGame Backrooms = new(
        "escape-the-backrooms", "escape-the-backrooms", "Escape the Backrooms", "1943950",
        "nightly", "Backrooms.exe", "EscapeTheBackrooms/Binaries/Win64/Backrooms-Win64-Shipping.exe",
        true, true, false);

    [Theory]
    [InlineData("Backrooms")]
    [InlineData("Backrooms-Win64-Shipping")]
    [InlineData("backrooms-win64-shipping")]
    public void A_running_game_blocks_a_new_launch_mode(string running)
    {
        var conflict = UevrSession.RunningConflict(Backrooms, [running]);
        Assert.Contains("already running", conflict);
    }

    [Fact]
    public void A_leftover_injector_blocks_a_new_launch()
    {
        Assert.Contains("injector", UevrSession.RunningConflict(Backrooms, ["UEVRInjector"]));
        Assert.Null(UevrSession.RunningConflict(Backrooms, ["vrserver", "steam"]));
    }

    [Fact]
    public void Watched_processes_cover_launcher_shipping_binary_and_injector()
    {
        Assert.Equal(new[] { "Backrooms", "Backrooms-Win64-Shipping", "UEVRInjector" },
            UevrSession.WatchedProcessNames(Backrooms));
    }

    [Fact]
    public void Missing_launch_arguments_are_found_by_whole_token()
    {
        Assert.Empty(UevrSession.MissingLaunchArguments(
            "\"C:\\Games\\Backrooms-Win64-Shipping.exe\" EscapeTheBackrooms -DX11 -nosplash", ["-dx11"]));
        Assert.Equal(new[] { "-dx11" }, UevrSession.MissingLaunchArguments(
            "\"C:\\Games\\Backrooms-Win64-Shipping.exe\" EscapeTheBackrooms -dx12", ["-dx11"]));
        Assert.Equal(new[] { "-dx11" }, UevrSession.MissingLaunchArguments("game.exe -dx11x", ["-dx11"]));
    }

    [Fact]
    public void This_process_command_line_can_be_read()
    {
        if (!OperatingSystem.IsWindows()) return;
        var commandLine = UevrSession.TryReadCommandLine(Environment.ProcessId);
        Assert.False(string.IsNullOrWhiteSpace(commandLine));
    }

}
