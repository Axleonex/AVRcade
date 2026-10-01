using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;

namespace VrClient.App.Views;

public partial class ConfirmUninstallDialog : Window
{
    public ConfirmUninstallDialog() => AvaloniaXamlLoader.Load(this);

    public ConfirmUninstallDialog(string displayName) : this() =>
        this.FindControl<TextBlock>("GameNameText")!.Text = displayName;

    private void Cancel_Click(object? sender, RoutedEventArgs e) => Close(false);
    private void Confirm_Click(object? sender, RoutedEventArgs e) => Close(true);
}
