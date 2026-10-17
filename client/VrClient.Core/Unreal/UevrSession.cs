namespace VrClient.Core.Unreal;
using System.Diagnostics;
using System.Runtime.InteropServices;

/// Keeps every UEVR launch tied to a fresh game session. Steam hands a launch
/// request to an already-running game instead of starting a new one, so a
/// mode switch while the game (or a previous injector) is still open would
/// silently reuse the old VR or mod state.
public static class UevrSession
{
    public const string InjectorProcessName = "UEVRInjector";

    public static IReadOnlyList<string> WatchedProcessNames(UevrGame game) =>
    [
        Path.GetFileNameWithoutExtension(game.LauncherStub),
        UevrTools.ShippingProcessName(game),
        InjectorProcessName
    ];

    /// Why a new launch must not start, or null when nothing from an earlier session is open.
    public static string? RunningConflict(UevrGame game, IReadOnlyCollection<string> runningProcessNames)
    {
        bool Running(string name) => runningProcessNames.Contains(name, StringComparer.OrdinalIgnoreCase);
        if (Running(Path.GetFileNameWithoutExtension(game.LauncherStub)) || Running(UevrTools.ShippingProcessName(game)))
            return $"{game.DisplayName} is already running. Close it before starting it in another mode.";
        if (Running(InjectorProcessName))
            return "A UEVR injector from an earlier session is still open. Close it before starting a new launch.";
        return null;
    }

    /// Live check before a new launch. AVRcade leaves its injector open after a
    /// successful attach, so a leftover injector with no game running is closed
    /// here instead of blocking the player; a running game always blocks.
    public static string? PrepareForNewLaunch(UevrGame game)
    {
        var running = RunningProcessNames(game);
        if (RunningConflict(game, running.Where(name => name != InjectorProcessName).ToArray()) is { } gameConflict)
            return gameConflict;
        if (running.Contains(InjectorProcessName))
            CloseLeftoverInjectors();
        return RunningConflict(game, RunningProcessNames(game));
    }

    private static void CloseLeftoverInjectors()
    {
        foreach (var injector in Process.GetProcessesByName(InjectorProcessName))
        {
            using (injector)
            {
                try
                {
                    if (!injector.CloseMainWindow() || !injector.WaitForExit(3000))
                        injector.Kill();
                    injector.WaitForExit(3000);
                }
                catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception)
                {
                    // Exited already, or not ours to close; RunningConflict reports what is left.
                }
            }
        }
    }

    public static IReadOnlyCollection<string> RunningProcessNames(UevrGame game)
    {
        var running = new List<string>();
        foreach (var name in WatchedProcessNames(game))
        {
            var found = Process.GetProcessesByName(name);
            if (found.Length > 0) running.Add(name);
            foreach (var process in found) process.Dispose();
        }
        return running;
    }

    /// Launch arguments the profile needs that the game's command line does not carry.
    public static IReadOnlyList<string> MissingLaunchArguments(string commandLine, IReadOnlyList<string> required)
    {
        var tokens = commandLine.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(token => token.Trim('"'));
        return required.Where(argument => !tokens.Contains(argument, StringComparer.OrdinalIgnoreCase)).ToArray();
    }

    /// The command line of a process owned by this user, or null when Windows does not expose it.
    public static string? TryReadCommandLine(int processId)
    {
        if (!OperatingSystem.IsWindows()) return null;
        const int processQueryLimitedInformation = 0x1000;
        const int processCommandLineInformation = 60;
        var handle = OpenProcess(processQueryLimitedInformation, false, processId);
        if (handle == IntPtr.Zero) return null;
        try
        {
            NtQueryInformationProcess(handle, processCommandLineInformation, IntPtr.Zero, 0, out var length);
            if (length <= 0 || length > 1 << 20) return null;
            var buffer = Marshal.AllocHGlobal(length);
            try
            {
                if (NtQueryInformationProcess(handle, processCommandLineInformation, buffer, length, out _) != 0)
                    return null;
                // The buffer starts with a UNICODE_STRING that points into the same buffer.
                var byteLength = (ushort)Marshal.ReadInt16(buffer);
                var text = Marshal.ReadIntPtr(buffer, IntPtr.Size);
                return text == IntPtr.Zero ? null : Marshal.PtrToStringUni(text, byteLength / 2);
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }
        finally { CloseHandle(handle); }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(int access, bool inheritHandle, int processId);

    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);

    [DllImport("ntdll.dll")]
    private static extern int NtQueryInformationProcess(
        IntPtr process, int informationClass, IntPtr information, int length, out int returnLength);
}
