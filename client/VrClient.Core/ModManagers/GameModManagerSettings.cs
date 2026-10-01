using System.Text.Json;

namespace VrClient.Core.ModManagers;

/// Remembers which mod-manager application the player uses for a game whose
/// mods are deployed into the game folder (Cyberpunk 2077, Unreal titles).
/// AVRcade only opens that application; it never installs mods or reads the
/// manager's profiles.
public sealed class GameModManagerSettings(string? path = null)
{
    public string PathOnDisk { get; } = path ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "game-mod-managers.json");

    public string? Load(string gameSlug) =>
        LoadAll().TryGetValue(gameSlug, out var executable) && File.Exists(executable) ? executable : null;

    public void Save(string gameSlug, string executable)
    {
        if (Validate(executable) is { } error)
            throw new InvalidOperationException(error);
        var entries = new Dictionary<string, string>(LoadAll(), StringComparer.OrdinalIgnoreCase)
        {
            [gameSlug] = Path.GetFullPath(executable)
        };
        AtomicFile.WriteAllText(PathOnDisk, JsonSerializer.Serialize(entries,
            new JsonSerializerOptions { WriteIndented = true }));
    }

    public static string? Validate(string? executable)
    {
        if (string.IsNullOrWhiteSpace(executable))
            return "Choose a mod-manager application first.";
        try
        {
            return Path.GetExtension(executable).Equals(".exe", StringComparison.OrdinalIgnoreCase) &&
                   File.Exists(executable)
                ? null : "Select an existing Windows mod-manager .exe file.";
        }
        catch (Exception ex) when (ex is ArgumentException or IOException or UnauthorizedAccessException or NotSupportedException)
        {
            return $"Could not check the mod manager: {ex.Message}";
        }
    }

    private IReadOnlyDictionary<string, string> LoadAll()
    {
        if (!File.Exists(PathOnDisk)) return new Dictionary<string, string>();
        try
        {
            return JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(PathOnDisk))
                ?? new Dictionary<string, string>();
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException)
        {
            return new Dictionary<string, string>();
        }
    }
}
