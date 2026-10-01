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
    public IReadOnlyList<OnboardedGame> Discover(string modpacksDir, string gameConfigDir, string profileDir)
    {
        var games = new List<OnboardedGame>();
        if (!Directory.Exists(modpacksDir))
            return games;

        foreach (var modpackPath in Directory.EnumerateFiles(modpacksDir, "*.modpack.json"))
        {
            var fileName = Path.GetFileName(modpackPath);
            var slug = fileName[..^".modpack.json".Length];

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
