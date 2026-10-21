namespace VrClient.Core.Catalog;
using VrClient.Core.Modpack;

public sealed record OnboardedGame(
    string Slug, string SteamAppId, string Community,
    bool HasModpack, bool HasComfortMap, bool HasGameConfig, bool HasProfile);

public sealed class GameCatalog
{
    /// Discover every game with a config/modpacks/<slug>.modpack.json under modpacksDir,
    /// and report which of its four convention files exist. gameConfigDir/profileDir are
    /// the roots for config/games and config/profiles.
    public IReadOnlyList<OnboardedGame> Discover(string modpacksDir, string gameConfigDir, string profileDir, string? gameSlug = null)
    {
        var games = new List<OnboardedGame>();
        if (!Directory.Exists(modpacksDir))
            return games;

        foreach (var modpackPath in Directory.EnumerateFiles(modpacksDir, "*.modpack.json"))
        {
            var fileName = Path.GetFileName(modpackPath);
            var slug = fileName[..^".modpack.json".Length];

            // Filter before parsing: another game's broken config cannot block a scoped refresh.
            if (gameSlug is not null && !slug.Equals(gameSlug, StringComparison.OrdinalIgnoreCase)) continue;
            var spec = ModpackResolver.LoadSpec(modpackPath);
            games.Add(new OnboardedGame(
                Slug: slug,
                SteamAppId: spec.SteamAppId,
                Community: spec.Community,
                HasModpack: true,
                HasComfortMap: File.Exists(Path.Combine(modpacksDir, $"{slug}.comfort-map.json")),
                HasGameConfig: File.Exists(Path.Combine(gameConfigDir, $"{slug}.json")),
                HasProfile: File.Exists(Path.Combine(profileDir, $"{slug}-game-profile.json"))));
        }

        return games.OrderBy(g => g.Slug, StringComparer.Ordinal).ToList();
    }
}
