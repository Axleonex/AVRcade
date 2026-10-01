using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace VrClient.Core.Redengine;

public enum CyberpunkTurnMode { Smooth, Snap }

public sealed record CyberpunkTurnPreference(CyberpunkTurnMode Mode, int SnapDegrees);

public static class CyberpunkTurnSettings
{
    public static CyberpunkTurnPreference Default => new(CyberpunkTurnMode.Smooth, 30);

    public static string SettingsPath(string gameRoot) =>
        Path.Combine(gameRoot, "bin", "x64", "vrport.ini");

    public static CyberpunkTurnPreference Read(string gameRoot)
    {
        var path = SettingsPath(gameRoot);
        if (!File.Exists(path)) return Default;

        var preference = Default;
        foreach (var line in File.ReadLines(path))
        {
            var parts = line.Split('=', 2, StringSplitOptions.TrimEntries);
            if (parts.Length != 2) continue;
            if (parts[0].Equals("xr_snap_turn", StringComparison.OrdinalIgnoreCase))
                preference = preference with
                {
                    Mode = parts[1] == "1" ? CyberpunkTurnMode.Snap : CyberpunkTurnMode.Smooth
                };
            else if (parts[0].Equals("xr_snap_turn_angle_deg", StringComparison.OrdinalIgnoreCase) &&
                     double.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var angle) &&
                     angle >= 10 && angle <= 90)
                preference = preference with { SnapDegrees = (int)Math.Round(angle) };
        }
        return preference;
    }

    public static void Write(string gameRoot, CyberpunkTurnPreference preference)
    {
        if (preference.SnapDegrees is < 10 or > 90)
            throw new ArgumentOutOfRangeException(nameof(preference), "Snap angle must be between 10 and 90 degrees.");
        var path = SettingsPath(gameRoot);
        if (!File.Exists(path))
            throw new FileNotFoundException("Cyberpunk VR Port settings were not found; install the VR conversion first.", path);

        var content = File.ReadAllText(path);
        var newline = content.Contains("\r\n", StringComparison.Ordinal) ? "\r\n" : "\n";
        content = ReplaceSetting(content, "xr_snap_turn", preference.Mode == CyberpunkTurnMode.Snap ? "1" : "0", newline);
        content = ReplaceSetting(content, "xr_snap_turn_angle_deg",
            preference.SnapDegrees.ToString("0.00", CultureInfo.InvariantCulture), newline);
        var temporary = path + ".tmp";
        File.WriteAllText(temporary, content, new UTF8Encoding(false));
        File.Move(temporary, path, overwrite: true);
    }

    private static string ReplaceSetting(string content, string key, string value, string newline)
    {
        var pattern = $@"(?im)^[ \t]*{Regex.Escape(key)}[ \t]*=[^\r\n]*";
        if (Regex.IsMatch(content, pattern))
            return Regex.Replace(content, pattern, $"{key}={value}");
        return content.TrimEnd('\r', '\n') + newline + $"{key}={value}" + newline;
    }
}
