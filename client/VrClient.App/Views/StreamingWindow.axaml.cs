using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using VrClient.App.ViewModels;

namespace VrClient.App.Views;

public partial class StreamingWindow : Window
{
    public StreamingWindow() => AvaloniaXamlLoader.Load(this);

    private async void ChooseObs_Click(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not StreamingViewModel vm) return;
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Choose OBS Studio", AllowMultiple = false,
            FileTypeFilter = [new FilePickerFileType("OBS executable") { Patterns = ["obs64.exe", "obs32.exe"] }]
        });
        if (files.FirstOrDefault()?.TryGetLocalPath() is { } path) vm.ChooseObs(path);
    }
}
