using CommunityToolkit.Mvvm.Input;
using VrClient.Core.App;
using VrClient.Core.Compatibility;
using VrClient.Core.Launch;
using VrClient.Core.Modpack;

namespace VrClient.App.ViewModels;

public partial class MainWindowViewModel
{
    public GameVersionRecovery? CreateVersionRecovery(GameView captured) =>
        _controller?.Compatibility.VersionRecoveryFor(captured);

    private bool CanActOnRecovery(GameView captured)
    {
        if (_controller is null || Busy || IsInstallingCyberpunkRequirements) return false;
        try
        {
            var current = _controller.GetGame(captured.Slug);
            if (GameInstallationStore.SameRoot(current?.InstallDir, captured.InstallDir)) return true;
            StatusText = "The selected installation changed. Open the version options again for the intended copy.";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Text.Json.JsonException or ArgumentException)
        { StatusText = "Game version recovery could not recheck this installation: " + ex.Message; }
        return false;
    }
    public void OpenGameVersionStore(GameView captured)
    {
        if (CanActOnRecovery(captured)) StatusText = _controller!.Compatibility.OpenStoreVersionOptions(captured).Message;
    }
    public void RecheckCompatibleVersion(GameView captured)
    {
        if (!CanActOnRecovery(captured)) return;
        try
        {
            RefreshGame(captured.Slug);
            StatusText = Games.FirstOrDefault(g => g.Slug == captured.Slug)?.CompatibilityText
                ?? "The selected installation is unavailable. Reconnect its drive or choose another copy.";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Text.Json.JsonException or ArgumentException)
        { StatusText = "Game version recheck could not complete: " + ex.Message; }
    }
    public async Task LaunchRecoveryMonitorAsync(GameView captured)
    {
        if (CanActOnRecovery(captured)) await RunCleanMonitorAsync(captured);
    }
    public void SelectGameInstallation(GameView captured, string root)
    {
        if (_controller is null || Busy || IsInstallingCyberpunkRequirements) return;
        try
        {
            _controller.Compatibility.EnsureClosed(captured with { InstallDir = captured.InstallDir ?? root });
            StatusText = _controller.Compatibility.SelectInstallation(captured, root).Message;
            RefreshGame(captured.Slug);
        }
        catch (Exception ex) { StatusText = "Installation selection failed: " + ex.Message; }
    }
    private void SelectInstallationChoice(GameView captured, InstallationChoice choice)
    {
        if (_controller is null || Busy || IsInstallingCyberpunkRequirements) return;
        if (choice.Root is { } root) { SelectGameInstallation(captured, root); return; }
        try
        {
            if (captured.InstallDir is not null) _controller.Compatibility.EnsureClosed(captured);
            _controller.Compatibility.Installations.UseDiscovered(captured.Slug);
            RefreshGame(captured.Slug);
            StatusText = "Using the store / discovered installation. " + GameCompatibilityService.SaveWarning;
        }
        catch (Exception ex) { StatusText = "Could not switch installations: " + ex.Message; }
    }
    [RelayCommand]
    private async Task PrepareCompatibleVrAsync()
    {
        if (_controller is null || SelectedGame is not { } game || Busy || IsInstallingCyberpunkRequirements) return;
        Busy = true;
        var message = "VR preparation did not finish.";
        try { message = (await PrepareCompatibleVrCoreAsync(game.View, forceUpdate: true)).Message; }
        catch (Exception ex) { message = "VR preparation failed: " + ex.Message; }
        finally { Busy = false; RefreshGame(game.Slug); StatusText = message; }
    }
    private async Task<ActionOutcome> PrepareCompatibleVrCoreAsync(GameView game, bool forceUpdate = false)
    {
        if (_controller is null) return new(false, "Game configuration is unavailable.");
        var compatibility = _controller.Compatibility;
        using var http = NewHttp();
        StatusText = "Checking the selected game build and approved VR packages…";
        var acceptProgress = true;
        var progress = new Progress<string>(message => { if (acceptProgress) StatusText = message; });
        try { return await Task.Run(() => compatibility.PrepareVrAsync(game,
            ct => new GameModUpdater(_controller.RepoRoot, http).UpdateReviewedAsync(game, _controller, progress, ct, leaseHeld: true),
            refreshGame: () => _controller.GetGame(game.Slug) ?? game, forceUpdate: forceUpdate)); }
        finally { acceptProgress = false; }
    }
    [RelayCommand]
    private async Task CleanMonitorAsync()
    {
        if (_controller is null || SelectedGame is not { } game || Busy || IsInstallingCyberpunkRequirements) return;
        await RunCleanMonitorAsync(game.View);
    }
    private async Task RunCleanMonitorAsync(GameView game)
    {
        if (_controller is null) return;
        Busy = true;
        var message = "Monitor launch did not complete.";
        try
        {
            var outcome = await Task.Run(() => _controller.Compatibility.LaunchCleanMonitor(game));
            message = outcome.Message;
            if (outcome.Ok && outcome.LaunchedProcessId is { } pid && outcome.LaunchedExecutable is { } exe)
            {
                var startup = await GameStartupMonitor.ObserveAsync(pid, exe);
                message += " " + startup.Message;
            }
        }
        catch (Exception ex) { message = "Monitor launch failed: " + ex.Message; }
        finally { Busy = false; RefreshGame(game.Slug); StatusText = message; }
    }
    [RelayCommand]
    private void RestoreParkedMods()
    {
        if (_controller is null || SelectedGame?.View is not { InstallDir: { } root } game || Busy) return;
        try { var count = MonitorModSession.Restore(root, () => _controller.Compatibility.EnsureClosed(game)); StatusText = $"Restored {count} parked mod files. VR compatibility checks still apply."; }
        catch (Exception ex) { StatusText = "Mods were not restored: " + ex.Message; }
        RefreshGame(game.Slug);
    }
    [RelayCommand]
    private void RollBackVrPackage()
    {
        if (_controller is null || SelectedGame?.View is not { InstallDir: { } root } game || Busy) return;
        try
        {
            PackageRecovery.Open(root).Restore(() => _controller.Compatibility.EnsureClosed(game));
            _controller.Compatibility.SetAutomaticUpdates(game, false);
            StatusText = "Restored the previous mod package and paused automatic updates for this installation. The Steam game version and saves were not changed.";
        }
        catch (Exception ex) { StatusText = "Package rollback did not complete: " + ex.Message; }
        RefreshGame(game.Slug);
    }
    [RelayCommand]
    private void ConfirmCompatibleHeadsetTest()
    {
        if (_controller is null || SelectedGame?.View is not { } game || Busy) return;
        try { StatusText = _controller.Compatibility.ConfirmHeadsetTest(game).Message; }
        catch (Exception ex) { StatusText = "Could not record headset confirmation: " + ex.Message; }
        RefreshGame(game.Slug);
    }
}
