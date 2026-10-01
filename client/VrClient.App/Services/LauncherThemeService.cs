using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace VrClient.App.Services;

public sealed record LauncherPalette(string Id, string Name, string Description);

public sealed class LauncherThemeService
{
    private sealed record ThemeSettings(bool IsDark, string PaletteId);
    private sealed record PaletteColors(
        string Ground, string Surface, string Ink, string InkSecondary, string Accent,
        string AccentInk, string AccentHover, string Good, string Warning, string Danger, string Line);

    private static readonly IReadOnlyDictionary<string, (LauncherPalette Option, PaletteColors Light, PaletteColors Dark)> Palettes =
        new Dictionary<string, (LauncherPalette, PaletteColors, PaletteColors)>(StringComparer.OrdinalIgnoreCase)
        {
            ["city-rain"] = (new("city-rain", "City rain", "Cool blue-gray, like a wet Night City street."),
                new("#F3F7FA", "#FFFFFF", "#14212B", "#526471", "#176B87", "#FFFFFF", "#125C75", "#1E7654", "#A86200", "#B42318", "#D7E0E6"),
                new("#10181E", "#18242C", "#E8F1F5", "#A7BBC5", "#5CC8E8", "#08202A", "#80D8F0", "#59C99B", "#F0B75F", "#F28B82", "#2C3D47")),
            ["neon-alley"] = (new("neon-alley", "Neon alley", "Electric violet against midnight blue."),
                new("#F7F4FB", "#FFFFFF", "#21172A", "#665875", "#7C3FC4", "#FFFFFF", "#62309D", "#187A5D", "#A86200", "#B42318", "#E2DCE8"),
                new("#16121D", "#211A2B", "#F2ECF8", "#C4B4D2", "#C798FF", "#2A0E48", "#D8B5FF", "#65D6A8", "#F2C26B", "#FF9A94", "#3B3048")),
            ["afterglow"] = (new("afterglow", "Afterglow", "Warm copper for late-night play."),
                new("#FBF6F2", "#FFFFFF", "#2A1B15", "#725B50", "#B85F31", "#FFFFFF", "#914822", "#1E7654", "#8C5100", "#B42318", "#E9DDD5"),
                new("#1C1512", "#291D18", "#F7ECE4", "#D1B6A5", "#F3A36F", "#321307", "#FFC09A", "#6DD1A5", "#F3C46A", "#FF9A94", "#443128")),
            ["signal-green"] = (new("signal-green", "Signal green", "A restrained terminal-green interface."),
                new("#F2F8F5", "#FFFFFF", "#11241D", "#526B61", "#167255", "#FFFFFF", "#105B43", "#167255", "#9B6200", "#B42318", "#D8E6DF"),
                new("#0F1B16", "#172720", "#E5F5EC", "#A9C6B7", "#63D6A6", "#092117", "#8BE8BD", "#63D6A6", "#F0C269", "#FF9A94", "#294237")),
            ["cobalt-drive"] = (new("cobalt-drive", "Cobalt drive", "Clean cobalt with high legibility."),
                new("#F3F6FC", "#FFFFFF", "#152238", "#586B86", "#2D63C8", "#FFFFFF", "#234E9F", "#1D7859", "#995C00", "#B42318", "#D8E1F0"),
                new("#101725", "#18243A", "#E9F0FF", "#AFBFDD", "#75A7FF", "#0B1C3C", "#A1C4FF", "#62D4A4", "#F2C46C", "#FF9A94", "#2D405E")),
            ["rosewire"] = (new("rosewire", "Rosewire", "Muted magenta with a technical edge."),
                new("#FBF5F8", "#FFFFFF", "#2B1721", "#735B67", "#A53D6A", "#FFFFFF", "#843052", "#1D7859", "#8C5600", "#B42318", "#E9DCE3"),
                new("#1D1218", "#2A1922", "#F9EAF1", "#D3B3C2", "#F08AB4", "#350916", "#FFB2CD", "#63D6A4", "#F2C46C", "#FF9A94", "#49303C"))
        };

    private readonly string _settingsPath;

    public static LauncherThemeService Current { get; } = new();
    public IReadOnlyList<LauncherPalette> AvailablePalettes => Palettes.Values.Select(entry => entry.Option).ToList();
    // Dark is the first-run look; a saved choice always wins.
    public bool IsDark { get; private set; } = true;
    public string PaletteId { get; private set; } = "city-rain";
    public LauncherPalette SelectedPalette => Palettes[PaletteId].Option;

    private LauncherThemeService()
    {
        var directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "VRClient");
        _settingsPath = Path.Combine(directory, "launcher-theme.json");
        Load();
    }

    public void ToggleMode() => SetMode(!IsDark);

    /// persist: false shows a mode without changing the saved choice (screenshot tooling).
    public void SetMode(bool isDark, bool persist = true)
    {
        IsDark = isDark;
        if (persist) ApplyAndSave();
        else Apply();
    }

    /// A palette brush by resource key. The same brush object is recoloured when
    /// the theme changes, so a caller may hold on to it.
    public static IBrush Brush(string key) =>
        Application.Current is not null &&
        Application.Current.TryFindResource(key, null, out var value) && value is IBrush brush
            ? brush : Brushes.Gray;

    public void SetPalette(string? paletteId)
    {
        if (paletteId is null || !Palettes.ContainsKey(paletteId))
            return;
        PaletteId = paletteId;
        ApplyAndSave();
    }

    public void Apply()
    {
        if (Application.Current is null)
            return;
        var colors = IsDark ? Palettes[PaletteId].Dark : Palettes[PaletteId].Light;
        Application.Current.RequestedThemeVariant = IsDark ? Avalonia.Styling.ThemeVariant.Dark : Avalonia.Styling.ThemeVariant.Light;
        SetBrush("GroundBrush", colors.Ground);
        SetBrush("SurfaceBrush", colors.Surface);
        SetBrush("InkBrush", colors.Ink);
        SetBrush("InkSecondaryBrush", colors.InkSecondary);
        SetBrush("AccentBrush", colors.Accent);
        SetBrush("AccentInkBrush", colors.AccentInk);
        SetBrush("AccentHoverBrush", colors.AccentHover);
        SetBrush("GoodBrush", colors.Good);
        SetBrush("WarningBrush", colors.Warning);
        SetBrush("DangerBrush", colors.Danger);
        SetBrush("LineBrush", colors.Line);

        // Fluent's own input and expander surfaces follow the palette too, so a
        // text box or expander inside a card does not fall back to plain black or white.
        foreach (var key in new[]
                 {
                     "ExpanderHeaderBackground", "ExpanderHeaderBackgroundPointerOver",
                     "ExpanderHeaderBackgroundPressed", "ExpanderContentBackground",
                     "ComboBoxBackground", "ComboBoxBackgroundPointerOver",
                     "TextControlBackground", "TextControlBackgroundPointerOver",
                     "TextControlBackgroundFocused"
                 })
            SetBrush(key, colors.Ground);
        foreach (var key in new[]
                 {
                     "ExpanderHeaderBorderBrush", "ExpanderContentBorderBrush",
                     "ComboBoxBorderBrush", "TextControlBorderBrush"
                 })
            SetBrush(key, colors.Line);
    }

    /// Recolour the brush in place: everything already painted with it, including
    /// brushes view models handed to bindings, follows without being rebuilt.
    private static void SetBrush(string key, string hex)
    {
        var color = Color.Parse(hex);
        if (Application.Current!.Resources.TryGetValue(key, out var existing) && existing is SolidColorBrush brush)
            brush.Color = color;
        else
            Application.Current.Resources[key] = new SolidColorBrush(color);
    }

    private void Load()
    {
        try
        {
            if (!File.Exists(_settingsPath)) return;
            var settings = JsonSerializer.Deserialize<ThemeSettings>(File.ReadAllText(_settingsPath));
            if (settings is not null && Palettes.ContainsKey(settings.PaletteId))
            {
                IsDark = settings.IsDark;
                PaletteId = settings.PaletteId;
            }
        }
        catch (JsonException)
        {
            // A malformed local preference must never prevent the launcher from opening.
        }
    }

    private void ApplyAndSave()
    {
        Apply();
        Directory.CreateDirectory(Path.GetDirectoryName(_settingsPath)!);
        File.WriteAllText(_settingsPath, JsonSerializer.Serialize(new ThemeSettings(IsDark, PaletteId)));
    }
}
