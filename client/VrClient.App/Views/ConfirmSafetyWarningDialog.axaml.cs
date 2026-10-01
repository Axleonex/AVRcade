using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;

namespace VrClient.App.Views;

public partial class ConfirmSafetyWarningDialog : Window
{
    public ConfirmSafetyWarningDialog() => AvaloniaXamlLoader.Load(this);

    public ConfirmSafetyWarningDialog(string displayName, string warning) : this()
    {
        this.FindControl<TextBlock>("GameNameText")!.Text = displayName;
        this.FindControl<TextBlock>("WarningText")!.Text = warning;
    }

    private void Cancel_Click(object? sender, RoutedEventArgs e) => Close(false);
    private void Confirm_Click(object? sender, RoutedEventArgs e) => Close(true);
}
