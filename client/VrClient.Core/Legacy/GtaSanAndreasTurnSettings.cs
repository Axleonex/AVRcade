using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace VrClient.Core.Legacy;

public enum GtaTurnMode { Snap, Smooth }

public sealed record GtaTurnPreference(GtaTurnMode Mode, int SmoothDegreesPerSecond)
{
    // An older two-field settings file still deserializes to the original 30° snap.
    public int SnapDegrees { get; init; } = 30;
}

/// Per-user comfort choice; never writes to the GTA installation or mod profiles.
public sealed class GtaSanAndreasTurnSettings(string? path = null)
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) }
    };

    public static GtaTurnPreference Default { get; } = new(GtaTurnMode.Snap, 90);

    public string PathOnDisk { get; } = path ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "gta-san-andreas-turn.json");

    public GtaTurnPreference Load()
    {
        if (!File.Exists(PathOnDisk)) return Default;
        try
        {
            var preference = JsonSerializer.Deserialize<GtaTurnPreference>(
                File.ReadAllText(PathOnDisk), JsonOptions);
            return IsValid(preference) ? preference! : Default;
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException)
        {
            return Default;
        }
    }

    public void Save(GtaTurnPreference preference)
    {
        if (!IsValid(preference))
            throw new ArgumentOutOfRangeException(nameof(preference),
                "Smooth turn speed must be 30–180°/s and snap angle must be 15–90°.");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(PathOnDisk))!);
        var temporary = PathOnDisk + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(preference, JsonOptions));
        File.Move(temporary, PathOnDisk, overwrite: true);
    }

    public static IReadOnlyDictionary<string, string> LaunchEnvironment(GtaTurnPreference preference)
    {
        if (!IsValid(preference))
            throw new ArgumentOutOfRangeException(nameof(preference));
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["VRCLIENT_GTASA_TURN_MODE"] = preference.Mode is GtaTurnMode.Smooth ? "smooth" : "snap",
            ["VRCLIENT_GTASA_SMOOTH_TURN_DPS"] =
                preference.SmoothDegreesPerSecond.ToString(CultureInfo.InvariantCulture),
            ["VRCLIENT_GTASA_SNAP_TURN_DEGREES"] =
                preference.SnapDegrees.ToString(CultureInfo.InvariantCulture)
        };
    }

    private static bool IsValid(GtaTurnPreference? preference) =>
        preference is not null && Enum.IsDefined(preference.Mode) &&
        preference.SmoothDegreesPerSecond is >= 30 and <= 180 &&
        preference.SnapDegrees is >= 15 and <= 90;
}
