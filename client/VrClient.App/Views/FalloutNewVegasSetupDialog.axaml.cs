using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using System.Diagnostics;
using VrClient.App.ViewModels;

namespace VrClient.App.Views;

public partial class FalloutNewVegasSetupDialog : Window
{
    public FalloutNewVegasSetupDialog()
    {
        AvaloniaXamlLoader.Load(this);
    }

    private FalloutNewVegasSetupViewModel? ViewModel => DataContext as FalloutNewVegasSetupViewModel;

    private void Close_Click(object? sender, RoutedEventArgs e) => Close();

    private void OpenUrl_Click(object? sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string url })
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
    }

    private async void BrowseGame_Click(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is { } vm && await PickFolderAsync("Select the Fallout: New Vegas folder") is { } path)
            vm.GameDirectory = path;
    }

    private async void BrowseMo2_Click(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is { } vm && await PickExecutableAsync("Select ModOrganizer.exe") is { } path)
        {
            vm.ModOrganizerExecutablePath = path;
            vm.InferMo2InstanceFromExecutable();
        }
    }

    private async void BrowseSourceProfile_Click(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is not { } vm || await PickFolderAsync("Select your normal MO2 profile folder") is not { } path)
            return;
        vm.SourceProfileName = Path.GetFileName(Path.TrimEndingDirectorySeparator(path));
        var profiles = Directory.GetParent(path);
        if (profiles?.Name.Equals("profiles", StringComparison.OrdinalIgnoreCase) == true && profiles.Parent is not null)
            vm.ModOrganizerInstanceDirectory = profiles.Parent.FullName;
    }

    private async void BrowseTracker_Click(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is { } vm && await PickExecutableAsync("Select the FNVR Tracker executable") is { } path)
            vm.TrackerExecutablePath = path;
    }

    private async void BrowseNativeAdapter_Click(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is { } vm && await PickExecutableAsync("Select fnv-test-launcher.exe") is { } path)
            vm.NativeAdapterExecutablePath = path;
    }

    private async void BrowseSteamVr_Click(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is { } vm && await PickFolderAsync("Select the SteamVR folder") is { } path)
            vm.SteamVrDirectory = path;
    }

    private async void BrowseVirtualDesktop_Click(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is { } vm && await PickExecutableAsync("Select Virtual Desktop Streamer") is { } path)
            vm.VirtualDesktopExecutablePath = path;
    }

    private async Task<string?> PickExecutableAsync(string title)
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = title,
            AllowMultiple = false,
            FileTypeFilter =
            [
                new FilePickerFileType("Windows application") { Patterns = ["*.exe"] },
                FilePickerFileTypes.All
            ]
        });
        return files.Count == 1 ? files[0].TryGetLocalPath() : null;
    }

    private async Task<string?> PickFolderAsync(string title)
    {
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = title,
            AllowMultiple = false
        });
        return folders.Count == 1 ? folders[0].TryGetLocalPath() : null;
    }
}
