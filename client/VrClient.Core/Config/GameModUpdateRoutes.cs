using System.Text.Json;
using System.Text.Json.Serialization;

namespace VrClient.Core.Config;

public sealed record ModReleaseSource(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("repository")] string Repository,
    [property: JsonPropertyName("kind")] string Kind = "release",
    [property: JsonPropertyName("reviewed_reference")] string? ReviewedReference = null,
    [property: JsonPropertyName("branch")] string Branch = "main");

public sealed record GameModUpdateRoute(
    [property: JsonPropertyName("adapter")] string Adapter,
    [property: JsonPropertyName("process_names")] string[] ProcessNames,
    [property: JsonPropertyName("sources")] ModReleaseSource[] Sources);

/// Metadata selects an adapter; creators and UI do not contain installation code.
public static class GameModUpdateRoutes
{
    public static IReadOnlyDictionary<string, GameModUpdateRoute> Load(string repoRoot)
    {
        var path = Path.Combine(repoRoot, "config", "mod-update-routes.json");
        if (!File.Exists(path)) return new Dictionary<string, GameModUpdateRoute>();
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        if (doc.RootElement.GetProperty("schema").GetString() != "mod-update-routes/1")
            throw new InvalidDataException("Unsupported mod update catalogue.");
        var routes = doc.RootElement.GetProperty("games").Deserialize<Dictionary<string, GameModUpdateRoute>>()
            ?? throw new InvalidDataException("Mod update catalogue is empty.");
        foreach (var (slug, route) in routes)
        {
            if (!System.Text.RegularExpressions.Regex.IsMatch(slug, "^[a-z0-9]+(?:-[a-z0-9]+)*$") ||
                route.Adapter is not ("thunderstore" or "cyberpunk" or "uevr" or "paired") ||
                route.ProcessNames is not { Length: > 0 } || route.Sources is null ||
                route.ProcessNames.Any(name => string.IsNullOrWhiteSpace(name) || name.IndexOfAny(['/', '\\', ':']) >= 0))
                throw new InvalidDataException($"Invalid mod update route: {slug}");
            foreach (var source in route.Sources)
                if (source.Kind is not ("release" or "commits") ||
                    !System.Text.RegularExpressions.Regex.IsMatch(source.Repository, "^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$") ||
                    string.IsNullOrWhiteSpace(source.Name))
                    throw new InvalidDataException($"Invalid upstream release source: {slug}");
        }
        return routes;
    }
}
