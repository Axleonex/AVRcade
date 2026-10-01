using System.Globalization;
using System.Text;

namespace VrClient.Core.Redengine;

public enum CyberpunkHudMode
{
    Comfortable,
    Compact,
    Hidden
}

public static class CyberpunkHudSettings
{
    private const string RelativeSettingsPath =
        @"bin\x64\plugins\cyber_engine_tweaks\mods\CyberpunkVRPort_HUD\hud_user.ini";

    public static string SettingsPath(string gameRoot) =>
        Path.Combine(gameRoot, RelativeSettingsPath);

    public static CyberpunkHudMode ReadMode(string gameRoot)
    {
        var path = SettingsPath(gameRoot);
        if (!File.Exists(path))
            return CyberpunkHudMode.Compact;

        foreach (var line in File.ReadLines(path))
        {
            var parts = line.Split('=', 2, StringSplitOptions.TrimEntries);
            if (parts.Length != 2 || !parts[0].Equals("xr_hud_mode", StringComparison.OrdinalIgnoreCase))
                continue;
            return parts[1].ToLowerInvariant() switch
            {
                "comfortable" => CyberpunkHudMode.Comfortable,
                "hidden" => CyberpunkHudMode.Hidden,
                _ => CyberpunkHudMode.Compact
            };
        }
        return CyberpunkHudMode.Compact;
    }

    public static string DisplayName(CyberpunkHudMode mode) => mode switch
    {
        CyberpunkHudMode.Comfortable => "Comfortable",
        CyberpunkHudMode.Compact => "Compact",
        CyberpunkHudMode.Hidden => "Hidden (immersive)",
        _ => mode.ToString()
    };

    public static void WriteMode(string gameRoot, CyberpunkHudMode mode)
    {
        var path = SettingsPath(gameRoot);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".tmp";
        File.WriteAllText(temporary, BuildIni(mode), new UTF8Encoding(false));
        File.Move(temporary, path, overwrite: true);
    }

    internal static string BuildIni(CyberpunkHudMode mode)
    {
        var inset = mode is CyberpunkHudMode.Comfortable ? 220.0 : 360.0;
        var vertical = mode is CyberpunkHudMode.Comfortable ? 80.0 : 110.0;
        var regionScale = mode is CyberpunkHudMode.Comfortable ? 1.70 : 1.52;
        var visible = mode is CyberpunkHudMode.Hidden ? 0 : 1;
        var modeName = mode.ToString().ToLowerInvariant();
        string N(double value) => value.ToString("0.0000", CultureInfo.InvariantCulture);

        return $"""
# VRClient per-user Cyberpunk HUD preference. Safe to edit or delete.
xr_hud_mode={modeName}
xr_hud_visible={visible}
xr_hud_scale={N(-inset)}
xr_hud_scale_y={N(vertical - 20.0)}
xr_hud_scale_scale={N(regionScale)}
xr_hud_phone={N(inset)}
xr_hud_phone_y={N(vertical - 20.0)}
xr_hud_phone_scale={N(regionScale)}
xr_hud_top_left_alerts={N(inset)}
xr_hud_top_left_alerts_y={N(vertical)}
xr_hud_top_left_alerts_scale={N(regionScale)}
xr_hud_top_right={N(-inset)}
xr_hud_top_right_y={N(vertical)}
xr_hud_top_right_scale={N(regionScale)}
xr_hud_bottom_left={N(inset)}
xr_hud_bottom_left_y={N(-vertical)}
xr_hud_bottom_left_scale={N(regionScale)}
xr_hud_bottom_left_top={N(inset)}
xr_hud_bottom_left_top_y={N(-(vertical - 20.0))}
xr_hud_bottom_left_top_scale={N(regionScale)}
xr_hud_radio={N(inset)}
xr_hud_radio_y={N(-vertical)}
xr_hud_radio_scale={N(regionScale)}
xr_hud_bottom_right={N(-inset)}
xr_hud_bottom_right_y={N(-vertical)}
xr_hud_bottom_right_scale={N(regionScale)}
xr_hud_right_center={N(-inset)}
xr_hud_right_center_y=0.0000
xr_hud_right_center_scale={N(regionScale)}
xr_hud_johnny_hint={N(-inset)}
xr_hud_activity_log={N(inset)}
xr_hud_warning={N(vertical)}
xr_hud_boss_health={N(-(vertical - 20.0))}
xr_hud_vehicle_scan=0.0000
xr_hud_progress_bar={N(-(vertical - 20.0))}
xr_hud_oxygen_bar={N(-(vertical - 20.0))}
""";
    }
}
