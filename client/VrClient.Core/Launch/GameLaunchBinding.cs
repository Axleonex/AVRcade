using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace VrClient.Core.Launch;

/// Carries the selected installation across the GUI/CLI process boundary.
public static class GameLaunchBinding
{
    public static ProcessStartInfo CreateCliStartInfo(string configurationRoot, string executable)
    {
        var root = Path.GetFullPath(configurationRoot);
        var start = new ProcessStartInfo(executable)
        {
            WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true
        };
        start.Environment["AVRCADE_REPO_ROOT"] = root;
        start.Environment["VRCLIENT_REPO_ROOT"] = root;
        return start;
    }

    public static string ManifestFor(string gameRoot, string appId)
    {
        if (string.IsNullOrWhiteSpace(appId) || !appId.All(char.IsAsciiDigit))
            throw new InvalidDataException("Invalid Steam game identity.");
        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(gameRoot));
        var common = Directory.GetParent(root);
        var apps = common?.Parent;
        if (common?.Name.Equals("common", StringComparison.OrdinalIgnoreCase) != true ||
            apps?.Name.Equals("steamapps", StringComparison.OrdinalIgnoreCase) != true)
            throw new InvalidDataException("Locate this game's folder inside its Steam library.");
        return Path.Combine(apps.FullName, "appmanifest_" + appId + ".acf");
    }

    public static void AddGame(ProcessStartInfo start, string gameRoot, string appId)
    {
        start.ArgumentList.Add("--game-dir");
        start.ArgumentList.Add(Path.GetFullPath(gameRoot));
        start.ArgumentList.Add("--steam-manifest");
        start.ArgumentList.Add(ManifestFor(gameRoot, appId));
    }

    public static bool MatchesExecutable(string? actual, string gameRoot, string relativeExecutable)
    {
        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(gameRoot)) + Path.DirectorySeparatorChar;
        var expected = Path.GetFullPath(Path.Combine(root, relativeExecutable.Replace('/', Path.DirectorySeparatorChar)));
        return actual is not null && expected.StartsWith(root, StringComparison.OrdinalIgnoreCase) &&
            Path.GetFullPath(actual).Equals(expected, StringComparison.OrdinalIgnoreCase);
    }

    public static string? ProcessExecutable(Process process)
    {
        try
        {
            if (!OperatingSystem.IsWindows()) return process.MainModule?.FileName;
            var handle = OpenProcess(0x1000, false, process.Id); // Query limited information, no module enumeration.
            if (handle == IntPtr.Zero) return null;
            try
            {
                var buffer = new StringBuilder(32768);
                var length = buffer.Capacity;
                return QueryFullProcessImageName(handle, 0, buffer, ref length) ? buffer.ToString() : null;
            }
            finally { CloseHandle(handle); }
        }
        catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception) { return null; }
    }
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool QueryFullProcessImageName(IntPtr process, int flags, StringBuilder name, ref int length);
    [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool CloseHandle(IntPtr handle);
}
