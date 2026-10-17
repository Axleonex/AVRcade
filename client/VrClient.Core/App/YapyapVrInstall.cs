using System.Security.Cryptography;
using System.Text.Json;

namespace VrClient.Core.App;

/// Checks the local, reversible YAPYAP test install before advertising a VR launch.
public sealed record YapyapVrInstall(bool Installed, int OtherPluginFiles, string Detail)
{
    public static YapyapVrInstall Inspect(string? gameDir)
    {
        if (gameDir is null || !File.Exists(Path.Combine(gameDir, "yapyap.exe")))
            return new(false, 0, "YAPYAP is not installed.");

        var recordPath = Path.Combine(gameDir, "BepInEx", "avrcade-yapyap-vr-install.json");
        if (!File.Exists(recordPath))
            return new(false, 0, "Install the AVRcade YAPYAP VR test package first.");

        try
        {
            using var record = JsonDocument.Parse(File.ReadAllText(recordPath));
            if (record.RootElement.GetProperty("schema").GetString() != "avrcade-yapyap-install/1")
                return new(false, 0, "YAPYAP VR install manifest is not recognized.");
            if (record.RootElement.GetProperty("uuvr_revision").GetString() !=
                "de8e9218f8a9310778f2f57c189bef9ccc8c4dc8")
                return new(false, 0, "YAPYAP UUVR build does not match the reviewed revision.");
            var entries = record.RootElement.GetProperty("installed").EnumerateArray().ToArray();
            if (entries.Length == 0)
                return new(false, 0, "YAPYAP VR install manifest is empty.");

            var owned = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var root = Path.GetFullPath(gameDir).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            foreach (var entry in entries)
            {
                // A damaged record must read as "needs repair", never crash the game list.
                var relative = entry.GetProperty("path").GetString();
                var expected = entry.GetProperty("sha256").GetString();
                if (string.IsNullOrEmpty(relative) || expected is null || expected.Length != 64 ||
                    !expected.All(char.IsAsciiHexDigit))
                    return new(false, 0, "YAPYAP VR install manifest has an invalid entry.");
                var parts = relative.Replace('\\', '/').Split('/');
                if (Path.IsPathRooted(relative) || parts.Any(p => p is "" or "." or ".."))
                    return new(false, 0, "YAPYAP VR install manifest has an invalid entry.");
                var target = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
                if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase) || !File.Exists(target))
                    return new(false, 0, $"YAPYAP VR file is missing: {relative}");
                var hash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(target)));
                // BepInEx rewrites this file with defaults on first launch.
                if (relative != "BepInEx/config/BepInEx.cfg" &&
                    !hash.Equals(expected, StringComparison.OrdinalIgnoreCase))
                    return new(false, 0, $"YAPYAP VR file changed: {relative}");
                owned.Add(target);
            }
            string[] essentials = [
                "winhttp.dll", "doorstop_config.ini", "BepInEx/core/BepInEx.dll",
                "BepInEx/plugins/AVRcade-UUVR/Uuvr.dll",
                "BepInEx/plugins/AVRcade-UUVR/Uuvr.XR.OpenXR.dll",
                "BepInEx/plugins/AVRcade-YAPYAP-VR/YapyapVrControls.dll",
                "yapyap_Data/Plugins/UnityOpenXR.dll"
            ];
            if (essentials.Any(path => !owned.Contains(Path.GetFullPath(
                    Path.Combine(root, path.Replace('/', Path.DirectorySeparatorChar))))))
                return new(false, 0, "YAPYAP VR install manifest is missing a required component.");

            var plugins = Path.Combine(gameDir, "BepInEx", "plugins");
            var extras = Directory.Exists(plugins)
                ? Directory.EnumerateFiles(plugins, "*.dll", SearchOption.AllDirectories)
                    .Count(path => !owned.Contains(Path.GetFullPath(path)))
                : 0;
            return new(true, extras, $"VR package files verified; {extras} other plugin DLLs found.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or
                                   KeyNotFoundException or InvalidOperationException or ArgumentException or FormatException)
        {
            return new(false, 0, "YAPYAP VR install manifest could not be verified.");
        }
    }
}
