using System.Text.Json;

namespace VrClient.Core.ModManagers;

public sealed record SavedModdedProfile(string ProfileKey, string? R2LaunchArguments = null);

/// Only VRClient preferences are written. Manager state and profiles remain owned by the manager.
public sealed class ModdedVrPreferences(string? path = null)
{
    public string PathOnDisk { get; } = path ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "modded-vr-preferences.json");

    public IReadOnlyDictionary<string, SavedModdedProfile> Load()
    {
        if (!File.Exists(PathOnDisk)) return new Dictionary<string, SavedModdedProfile>();
        var entries = JsonSerializer.Deserialize<Dictionary<string, SavedModdedProfile>>(
            File.ReadAllText(PathOnDisk));
        return entries ?? new Dictionary<string, SavedModdedProfile>();
    }

    public SavedModdedProfile? ForGame(string gameSlug) =>
        Load().TryGetValue(gameSlug, out var selected) ? selected : null;

    /// The profile used for "Mods, no VR". It is remembered apart from the VR
    /// profile so both modded modes can be ready at the same time.
    public string? FlatProfileKey(string gameSlug) => ForGame(FlatKey(gameSlug))?.ProfileKey;

    public void SelectFlat(string gameSlug, string? profileKey)
    {
        if (profileKey is null) UseManaged(FlatKey(gameSlug));
        else Select(FlatKey(gameSlug), profileKey);
    }

    private static string FlatKey(string gameSlug) => gameSlug + "#flat";

    public IReadOnlyDictionary<ModManagerKind, string> LoadDataRoots()
    {
        var path = PathOnDisk + ".roots.json";
        if (!File.Exists(path)) return new Dictionary<ModManagerKind, string>();
        var stored = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(path)) ?? [];
        var roots = new Dictionary<ModManagerKind, string>();
        foreach (var (key, value) in stored)
            if (Enum.TryParse<ModManagerKind>(key, out var kind) &&
                Path.IsPathFullyQualified(value))
                roots[kind] = value;
        return roots;
    }

    public void SetDataRoot(ModManagerKind manager, string directory)
    {
        if (!Directory.Exists(directory))
            throw new DirectoryNotFoundException("Choose the existing data folder shown in the mod manager's settings.");
        var roots = LoadDataRoots().ToDictionary(x => x.Key.ToString(), x => x.Value);
        roots[manager.ToString()] = Path.GetFullPath(directory);
        AtomicFile.WriteAllText(PathOnDisk + ".roots.json", JsonSerializer.Serialize(roots,
            new JsonSerializerOptions { WriteIndented = true }));
    }

    public void Select(string gameSlug, string profileKey)
    {
        var entries = new Dictionary<string, SavedModdedProfile>(Load(), StringComparer.OrdinalIgnoreCase);
        entries.TryGetValue(gameSlug, out var previous);
        entries[gameSlug] = new(profileKey,
            previous?.ProfileKey == profileKey ? previous.R2LaunchArguments : null);
        Save(entries);
    }

    public void UseManaged(string gameSlug)
    {
        var entries = new Dictionary<string, SavedModdedProfile>(Load(), StringComparer.OrdinalIgnoreCase);
        entries.Remove(gameSlug);
        Save(entries);
    }

    public void SetR2LaunchArguments(string gameSlug, string profileKey, string arguments)
    {
        var entries = new Dictionary<string, SavedModdedProfile>(Load(), StringComparer.OrdinalIgnoreCase);
        if (!entries.TryGetValue(gameSlug, out var selected) || selected.ProfileKey != profileKey)
            throw new InvalidOperationException("Select this profile before saving its launch arguments.");
        entries[gameSlug] = selected with { R2LaunchArguments = arguments };
        Save(entries);
    }

    private void Save(Dictionary<string, SavedModdedProfile> entries) =>
        AtomicFile.WriteAllText(PathOnDisk, JsonSerializer.Serialize(entries,
            new JsonSerializerOptions { WriteIndented = true }));
}
