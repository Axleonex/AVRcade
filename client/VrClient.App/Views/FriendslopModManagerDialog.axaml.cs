using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using VrClient.Core.ModManagers;

namespace VrClient.App.Views;

/// <summary>Hosts an installed manager's existing window; the manager retains its process and data.</summary>
public partial class FriendslopModManagerDialog : Window
{
    private ModManagerInstall? _manager;
    private readonly VortexWindowHost _host;
    private readonly TextBlock _status;
    private string? _vrModPageUrl;
    private CancellationTokenSource? _detectCancellation;

    public FriendslopModManagerDialog()
    {
        AvaloniaXamlLoader.Load(this);
        _host = this.FindControl<VortexWindowHost>("ManagerHost")!;
        _status = this.FindControl<TextBlock>("Status")!;
        Opened += async (_, _) => await OpenAndDockAsync();
        Closing += (_, _) => { _detectCancellation?.Cancel(); _host.Detach(); };
    }

    public FriendslopModManagerDialog(ModManagerInstall manager, CommunityVrRoute route)
        : this()
    {
        _manager = manager;
        _vrModPageUrl = route.PageUrl;
        Title = $"{route.GameFolder} mods — {manager.DisplayName} in AVRcade";
        this.FindControl<TextBlock>("Heading")!.Text = $"{route.GameFolder} mods in {manager.DisplayName}";
        this.FindControl<TextBlock>("Instructions")!.Text = ModManagerVrSetupGuide.Instructions(route, manager.Kind);
    }

    private async void Detect_Click(object? sender, RoutedEventArgs e) => await OpenAndDockAsync();
    private void OpenVrModPage_Click(object? sender, RoutedEventArgs e)
    {
        if (_vrModPageUrl is null) return;
        try { Process.Start(new ProcessStartInfo(_vrModPageUrl) { UseShellExecute = true }); }
        catch (Exception ex) when (ex is Win32Exception or IOException)
        {
            _status.Text = $"Could not open the VR mod page: {ex.Message}";
        }
    }
    private void Focus_Click(object? sender, RoutedEventArgs e)
    {
        _status.Text = _host.TryFocusManager(out var error)
            ? "Keyboard focus moved to the embedded manager. Try typing in its text field; close this panel for the normal window if typing still fails."
            : error;
    }
    private void Close_Click(object? sender, RoutedEventArgs e) => Close();

    private async Task OpenAndDockAsync()
    {
        if (_manager is not { } manager) return;
        if (_host.IsDocked)
        {
            _status.Text = $"{manager.DisplayName} is already inside AVRcade. Close this panel to restore its normal window.";
            return;
        }
        if (!File.Exists(manager.LaunchPath))
        {
            _status.Text = "The mod manager is no longer at its discovered path. Reconnect its drive and refresh AVRcade.";
            return;
        }
        _detectCancellation?.Cancel();
        _detectCancellation?.Dispose();
        _detectCancellation = new CancellationTokenSource();
        var cancel = _detectCancellation.Token;
        _status.Text = $"Opening {manager.DisplayName}…";
        try
        {
            Process.Start(new ProcessStartInfo(manager.LaunchPath)
            {
                UseShellExecute = true,
                WorkingDirectory = Path.GetDirectoryName(manager.LaunchPath)!
            });
            // Thunderstore's shortcut starts an Overwolf child. Its process identity cannot be
            // safely inferred from the shortcut, so never dock an unrelated Overwolf window.
            if (manager.Kind == ModManagerKind.Thunderstore)
            {
                _status.Text = "Thunderstore Mod Manager opened separately. AVRcade cannot safely identify its Overwolf child window for embedding. Manage the profile there, then refresh AVRcade.";
                return;
            }
            for (var attempt = 0; attempt < 75; attempt++)
            {
                await Task.Delay(200, cancel);
                var window = FindManagerWindow(manager);
                if (window == IntPtr.Zero) continue;
                _status.Text = _host.TryDock(window, out var error)
                    ? $"{manager.DisplayName} is inside AVRcade. Close this panel to restore its normal window; then refresh profiles."
                    : $"{manager.DisplayName} opened separately because embedding failed: {error}";
                return;
            }
            _status.Text = $"{manager.DisplayName} started, but no matching window appeared within 15 seconds. Its own window may still open; try Detect again.";
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) when (ex is Win32Exception or IOException or UnauthorizedAccessException)
        {
            _status.Text = $"Could not open {manager.DisplayName}: {ex.Message}";
        }
    }

    private static IntPtr FindManagerWindow(ModManagerInstall manager)
    {
        var wanted = Path.GetFullPath(manager.LaunchPath);
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, _) =>
        {
            if (!IsWindowVisible(window) || GetWindow(window, 4) != IntPtr.Zero) return true;
            var title = new StringBuilder(256);
            GetWindowTextW(window, title, title.Capacity);
            if (title.Length == 0) return true;
            if (manager.Kind == ModManagerKind.Vortex &&
                !title.ToString().Contains("Vortex", StringComparison.OrdinalIgnoreCase)) return true;
            GetWindowThreadProcessId(window, out var processId);
            try
            {
                using var process = Process.GetProcessById((int)processId);
                if (string.Equals(process.MainModule?.FileName, wanted,
                    StringComparison.OrdinalIgnoreCase))
                {
                    found = window;
                    return false;
                }
            }
            catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or
                Win32Exception or UnauthorizedAccessException) { }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    private delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(IntPtr window, StringBuilder text, int maxCount);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
}
