using System.Text.Json;

namespace VrClient.Core.ModManagers;

/// <summary>
/// AVRcade-owned profile locations. External mod-manager profiles are never
/// returned by this store and must not be used as its root.
/// </summary>
public sealed class ManagedModProfileStore(string? root = null)
{
    private const string MetadataFile = "profile.json";
    private readonly string _root = Path.GetFullPath(root ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "managed-mod-profiles"));

    public string Root => _root;

    public ManagedModProfile Create(string gameSlug, string name)
    {
        if (!CommunityVrRoutes.All.ContainsKey(gameSlug))
            throw new ArgumentException("This game has no AVRcade-managed mod route.", nameof(gameSlug));
        if (string.IsNullOrWhiteSpace(name) || name.Length > 80 ||
            name.Any(char.IsControl))
            throw new ArgumentException("Profile name must be 1–80 printable characters.", nameof(name));

        var canonicalSlug = CommunityVrRoutes.All[gameSlug].Slug;
        var id = Guid.NewGuid().ToString("N");
        var directory = Path.Combine(_root, canonicalSlug, id);
        Directory.CreateDirectory(directory);
        var profile = new ManagedModProfile(id, canonicalSlug, name.Trim(), directory);
        using var stream = new FileStream(Path.Combine(directory, MetadataFile),
            FileMode.CreateNew, FileAccess.Write, FileShare.None);
        JsonSerializer.Serialize(stream, new StoredProfile(profile.Id, profile.GameSlug,
            profile.Name), new JsonSerializerOptions { WriteIndented = true });
        // If writing fails, the incomplete directory is ignored by List; do
        // not recursively delete anything after a filesystem failure.
        return profile;
    }

    public IReadOnlyList<ManagedModProfile> List(string gameSlug)
    {
        if (!CommunityVrRoutes.All.TryGetValue(gameSlug, out var route)) return [];
        var gameRoot = Path.Combine(_root, route.Slug);
        if (!Directory.Exists(gameRoot)) return [];
        var result = new List<ManagedModProfile>();
        foreach (var directory in Directory.EnumerateDirectories(gameRoot))
        {
            var id = Path.GetFileName(directory);
            if (id.Length != 32 || !Guid.TryParseExact(id, "N", out _)) continue;
            try
            {
                var metadata = JsonSerializer.Deserialize<StoredProfile>(
                    File.ReadAllText(Path.Combine(directory, MetadataFile)));
                if (metadata is not null && metadata.Id == id &&
                    metadata.GameSlug == route.Slug && !string.IsNullOrWhiteSpace(metadata.Name))
                    result.Add(new(id, route.Slug, metadata.Name, directory));
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
            {
                // An interrupted or unreadable profile is not a launch target.
            }
        }
        return result.OrderBy(p => p.Name, StringComparer.OrdinalIgnoreCase).ToArray();
    }

    private sealed record StoredProfile(string Id, string GameSlug, string Name);
}

public sealed record ManagedModProfile(string Id, string GameSlug, string Name,
    string Directory);
