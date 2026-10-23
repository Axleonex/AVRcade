using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using VrClient.Core.Compatibility;

namespace VrClient.App.Views;

public enum GameVersionRecoveryAction { Cancel, OpenSteam, ChooseInstallation, Recheck, Monitor }

public partial class GameVersionRecoveryDialog : Window
{
    public GameVersionRecoveryDialog() => AvaloniaXamlLoader.Load(this);
    public GameVersionRecoveryDialog(GameVersionRecovery plan) : this() => DataContext = plan;
    private void OpenSteam_Click(object? sender, RoutedEventArgs e) => Close(GameVersionRecoveryAction.OpenSteam);
    private void ChooseInstallation_Click(object? sender, RoutedEventArgs e) => Close(GameVersionRecoveryAction.ChooseInstallation);
    private void Recheck_Click(object? sender, RoutedEventArgs e) => Close(GameVersionRecoveryAction.Recheck);
    private void Monitor_Click(object? sender, RoutedEventArgs e) => Close(GameVersionRecoveryAction.Monitor);
    private void Cancel_Click(object? sender, RoutedEventArgs e) => Close(GameVersionRecoveryAction.Cancel);
}
