using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using VrClient.Core.ModManagers;

namespace VrClient.App.Views;

public partial class Fallout3VortexDialog : Window
{
    private readonly VortexWindowHost _host;
    private readonly TextBlock _status;
    private CancellationTokenSource? _detectCancellation;

    public Fallout3VortexDialog() : this("Fallout 3") { }

    public Fallout3VortexDialog(string gameName)
    {
        AvaloniaXamlLoader.Load(this);
        if (gameName != "Fallout 3")
        {
            Title = $"{gameName} mods — Vortex in AVRcade";
            this.FindControl<TextBlock>("ManagerTitle")!.Text = $"{gameName} mods in Vortex";
            this.FindControl<TextBlock>("ManagerIntro")!.Text =
                $"Manage and deploy your Vortex mods here, then launch {gameName} in VR from AVRcade. Vortex still owns its profiles and deployment.";
            this.FindControl<TextBlock>("ManagerInstructions")!.Text =
                $"Choose {gameName} and your desired profile in Vortex, then deploy the mods. Close this panel and choose VR with Vortex mods in AVRcade. Vortex's own Play action does not enable AVRcade VR.";
            this.FindControl<TextBlock>("ManagerStaging")!.Text =
                $"Resolve any Vortex deployment warning first. With hardlinks, the mod staging folder and {gameName} must share a drive; use Vortex's own move-staging flow.";
        }
        _host = this.FindControl<VortexWindowHost>("VortexHost")!;
        _status = this.FindControl<TextBlock>("Status")!;
        Opened += async (_, _) => await DetectAndOpenAsync();
        Closing += (_, _) => { _detectCancellation?.Cancel(); _host.Detach(); };
    }

    private async void Detect_Click(object? sender, RoutedEventArgs e) => await DetectAndOpenAsync();
    private void Focus_Click(object? sender, RoutedEventArgs e)
    {
        _status.Text = _host.TryFocusManager(out var error)
            ? "Keyboard focus moved to Vortex. Try typing in its text field; close this panel for the normal window if typing still fails."
            : error;
    }
    private void Close_Click(object? sender, RoutedEventArgs e) => Close();

    private async void Browse_Click(object? sender, RoutedEventArgs e)
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Select Vortex.exe",
            AllowMultiple = false,
            FileTypeFilter = [new FilePickerFileType("Vortex executable") { Patterns = ["Vortex.exe"] }]
        });
        var path = files.Count == 1 ? files[0].TryGetLocalPath() : null;
        if (!string.IsNullOrWhiteSpace(path))
        {
            try { ModManagerDiscovery.RememberVortexExecutable(path); }
            catch (Exception ex) when (ex is ArgumentException or IOException or UnauthorizedAccessException)
            {
                _status.Text = $"Could not remember Vortex location: {ex.Message}";
                return;
            }
            await OpenAsync(path);
        }
    }

    private async Task DetectAndOpenAsync()
    {
        if (_host.IsDocked)
        {
            _status.Text = "Vortex is already embedded. Close this panel to return it to a normal window.";
            return;
        }
        var install = new ModManagerDiscovery().FindInstalled()
            .FirstOrDefault(manager => manager.Kind == ModManagerKind.Vortex);
        if (install is null)
        {
            _status.Text = "Vortex is not available. AVRcade checks Windows installed-app records and standard install folders on every attempt. Reconnect its drive, then choose Detect again; portable installs can use Find Vortex.exe.";
            return;
        }
        await OpenAsync(install.LaunchPath);
    }

    private async Task OpenAsync(string executable)
    {
        if (!File.Exists(executable) ||
            !string.Equals(Path.GetFileName(executable), "Vortex.exe", StringComparison.OrdinalIgnoreCase))
        {
            _status.Text = "Select an accessible Vortex.exe. No mods or profiles were changed.";
            return;
        }
        _detectCancellation?.Cancel();
        _detectCancellation?.Dispose();
        _detectCancellation = new CancellationTokenSource();
        var cancel = _detectCancellation.Token;
        _status.Text = $"Opening Vortex from {executable}…";
        try
        {
            // Vortex is a single-instance app; this may signal an existing instance.
            // Do not own or terminate either process.
            Process.Start(new ProcessStartInfo(executable)
            {
                UseShellExecute = true,
                WorkingDirectory = Path.GetDirectoryName(executable)!
            });
            for (var attempt = 0; attempt < 75; attempt++)
            {
                await Task.Delay(200, cancel);
                var window = FindVortexWindow(executable);
                if (window == IntPtr.Zero) continue;
                if (_host.TryDock(window, out var error))
                    _status.Text = "Vortex is embedded. Manage/deploy mods here; close this panel to return Vortex to a normal window.";
                else
                    _status.Text = $"Vortex opened but could not be embedded: {error}";
                return;
            }
            _status.Text = "Vortex started, but no matching main window appeared within 15 seconds. Its own window may still open; try Detect again.";
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or IOException or UnauthorizedAccessException)
        {
            _status.Text = $"Could not open Vortex: {ex.Message}";
        }
    }

    private static IntPtr FindVortexWindow(string executable)
    {
        var wanted = Path.GetFullPath(executable);
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, _) =>
        {
            if (!IsWindowVisible(window) || GetWindow(window, 4) != IntPtr.Zero) return true;
            var title = new StringBuilder(256);
            GetWindowTextW(window, title, title.Capacity);
            if (!title.ToString().Contains("Vortex", StringComparison.OrdinalIgnoreCase)) return true;
            GetWindowThreadProcessId(window, out var processId);
            try
            {
                using var process = Process.GetProcessById((int)processId);
                if (string.Equals(process.MainModule?.FileName, wanted, StringComparison.OrdinalIgnoreCase))
                {
                    found = window;
                    return false;
                }
            }
            catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or
                System.ComponentModel.Win32Exception or UnauthorizedAccessException) { }
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
