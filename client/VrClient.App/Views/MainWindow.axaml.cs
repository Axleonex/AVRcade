using System.Diagnostics;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using VrClient.Core.App;
using VrClient.Core.ModManagers;
using VrClient.Core.Legacy;
using VrClient.App.ViewModels;

namespace VrClient.App.Views;

public partial class MainWindow : Window
{
    private const int VrSetupTab = 0;
    private const int ModsTab = 1;

    private DateTime _lastRedetect = DateTime.UtcNow;

    public MainWindow()
    {
        AvaloniaXamlLoader.Load(this);
        Closed += (_, _) => (DataContext as IDisposable)?.Dispose();
        Activated += (_, _) => RedetectAfterReturning();
        DataContextChanged += (_, _) =>
        {
            if (DataContext is MainWindowViewModel vm)
                vm.PropertyChanged += (_, change) =>
                {
                    // Every game page opens at its top, whatever the last one was scrolled to.
                    if (change.PropertyName == nameof(MainWindowViewModel.ShowGameDetail) && vm.ShowGameDetail)
                        this.FindControl<ScrollViewer>("GameScroll")?.ScrollToHome();
                };
        };
    }

    /// Coming back from Steam, a mod manager or the headset software usually means
    /// something was just installed or started. Open setup steps are rechecked then,
    /// so their prompts go away without the player pressing Refresh.
    private void RedetectAfterReturning()
    {
        if (DataContext is not MainWindowViewModel { HasPendingSetup: true } vm) return;
        // Not while one of our own dialogs is open, nor over text being typed.
        if (OwnedWindows.Count > 0 || FocusManager?.GetFocusedElement() is TextBox) return;
        if (DateTime.UtcNow - _lastRedetect < TimeSpan.FromSeconds(5)) return;
        _lastRedetect = DateTime.UtcNow;
        vm.RedetectQuietly();
    }

    /// One handler for all four play-mode buttons on every game: launch when the
    /// mode is ready, otherwise take the player to its single next setup step.
    private async void PlayMode_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel vm || vm.SelectedGame is not { } game ||
            sender is not Button { Tag: PlayModeViewModel mode }) return;

        if (!mode.IsLaunch)
        {
            switch (mode.State.Setup)
            {
                case PlayModeSetup.InstallVr:
                    vm.DetailTabIndex = VrSetupTab;
                    vm.PrepareCommand.Execute(null);
                    break;
                case PlayModeSetup.OpenVrSetup:
                    vm.DetailTabIndex = VrSetupTab;
                    vm.StatusText = $"{mode.Title}: {mode.Status}. See VR setup below.";
                    break;
                case PlayModeSetup.OpenMods:
                    vm.DetailTabIndex = ModsTab;
                    vm.StatusText = $"{mode.Title}: {mode.Status}. Set it up under Mods below.";
                    break;
                case PlayModeSetup.ChooseCleanFolder:
                    vm.DetailTabIndex = ModsTab;
                    await ChooseGtaVanillaFolderAsync(vm);
                    break;
            }
            return;
        }

        // Vanilla starts nothing modded, so it has no private-session warning to accept.
        var acknowledge = mode.Mode is PlayMode.Vanilla || !game.RequiresLaunchAcknowledgement;
        if (!acknowledge)
            acknowledge = await new ConfirmSafetyWarningDialog(game.DisplayName, game.SafetyText)
                .ShowDialog<bool>(this);
        if (acknowledge)
            await vm.RunPlayModeAsync(mode.Mode, acknowledge);
    }

    private async void OpenFalloutNewVegasSetup_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel vm ||
            vm.CreateFalloutNewVegasSetupViewModel() is not { } setup)
            return;
        await new FalloutNewVegasSetupDialog { DataContext = setup }.ShowDialog(this);
    }

    private async void OpenFallout3Vortex_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel { SelectedGame.IsFallout3: true })
            return;
        await new Fallout3VortexDialog().ShowDialog(this);
    }

    private async void OpenCyberpunkVortex_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel { SelectedGame.IsCyberpunk: true })
            return;
        await new Fallout3VortexDialog("Cyberpunk 2077").ShowDialog(this);
    }

    private async void Uninstall_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel vm || vm.SelectedGame is null)
            return;

        var confirmed = await new ConfirmUninstallDialog(vm.SelectedGame.DisplayName)
            .ShowDialog<bool>(this);
        if (confirmed)
            vm.UninstallCommand.Execute(null);
    }

    /// Shows the manager, not the game, inside AVRcade; profiles are reread when it closes.
    private async void OpenFriendslopManager_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel { SelectedGame.Modded.SelectedManager: ModManagerInstall manager } vm)
            return;
        await new FriendslopModManagerDialog(manager, vm.SelectedGame.Modded.Route).ShowDialog(this);
        vm.RefreshCommand.Execute(null);
    }

    private async void LocateFriendslopGame_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel { SelectedGame.CanLocateGame: true } vm) return;
        if (await PickFolderAsync(vm.SelectedGame.IsGtaSanAndreas
                ? "Choose the folder that contains gta_sa.exe"
                : "Choose the installed Steam game's folder under steamapps\\common") is { } path)
            vm.RememberGameFolder(path);
    }

    private async void LocateModManager_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel vm || vm.SelectedGame?.Modded is null) return;
        if (await PickFileAsync("Select an installed r2modman.exe or Vortex.exe",
                new FilePickerFileType("Mod manager executable") { Patterns = ["r2modman.exe", "Vortex.exe"] })
            is not { } path) return;
        try
        {
            if (Path.GetFileName(path).Equals("r2modman.exe", StringComparison.OrdinalIgnoreCase))
                ModManagerDiscovery.RememberR2ModmanExecutable(path);
            else
                ModManagerDiscovery.RememberVortexExecutable(path);
            vm.RefreshCommand.Execute(null);
        }
        catch (Exception ex) when (ex is ArgumentException or IOException or UnauthorizedAccessException)
        {
            vm.StatusText = $"Could not use that mod manager: {ex.Message}";
        }
    }

    private async void ImportUevrProfile_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel vm || vm.SelectedGame is null)
            return;
        if (await PickFileAsync($"Select the reviewed {vm.SelectedGame.DisplayName} profile ZIP",
                new FilePickerFileType("ZIP archive") { Patterns = ["*.zip"] }, FilePickerFileTypes.All)
            is { } path)
            vm.ImportSelectedUevrProfile(path);
    }

    private async void SelectGtaVanillaFolder_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is MainWindowViewModel vm)
            await ChooseGtaVanillaFolderAsync(vm);
    }

    private async Task ChooseGtaVanillaFolderAsync(MainWindowViewModel vm)
    {
        if (vm.SelectedGame?.IsGtaSanAndreas != true)
            return;
        if (await PickFolderAsync("Select a separate clean GTA San Andreas folder") is { } path)
            vm.SelectGtaSanAndreasVanillaFolder(path);
    }

    private async void SelectGtaModManager_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel { SelectedGame.IsGtaSanAndreas: true } vm)
            return;
        if (await PickManagerExecutableAsync("Choose your installed GTA San Andreas mod manager (.exe)") is { } path)
            vm.SelectGtaSanAndreasModManager(path, GtaSanAndreasModManagerSettings.InferKind(path));
    }

    private async void SelectGameModManager_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel { SelectedGame.UsesFolderMods: true } vm)
            return;
        if (await PickManagerExecutableAsync($"Choose the mod manager you use for {vm.SelectedGame.DisplayName} (.exe)") is { } path)
            vm.SelectGameModManager(path);
    }

    private Task<string?> PickManagerExecutableAsync(string title) =>
        PickFileAsync(title, new FilePickerFileType("Windows application") { Patterns = ["*.exe"] });

    private async Task<string?> PickFileAsync(string title, params FilePickerFileType[] types)
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = title,
            AllowMultiple = false,
            FileTypeFilter = types
        });
        var path = files.Count == 1 ? files[0].TryGetLocalPath() : null;
        return string.IsNullOrWhiteSpace(path) ? null : path;
    }

    private async Task<string?> PickFolderAsync(string title)
    {
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = title,
            AllowMultiple = false
        });
        var path = folders.Count == 1 ? folders[0].TryGetLocalPath() : null;
        return string.IsNullOrWhiteSpace(path) ? null : path;
    }

    private void OpenExternalUrl_Click(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: string value } ||
            !Uri.TryCreate(value, UriKind.Absolute, out var uri) ||
            uri.Scheme != Uri.UriSchemeHttps)
            return;

        Process.Start(new ProcessStartInfo(uri.AbsoluteUri) { UseShellExecute = true });
    }
}
