using System.Text.Json;
using System.Text.Json.Serialization;

namespace VrClient.Core.Config;

public sealed record GameSetupGuide(
    [property: JsonPropertyName("steps")] IReadOnlyList<string> Steps,
    [property: JsonPropertyName("source_url")] string? SourceUrl = null);

/// Recovery copy is metadata; install commands and readiness stay with the game adapter.
public static class GameSetupGuides
{
    private sealed record Document(
        [property: JsonPropertyName("schema")] string Schema,
        [property: JsonPropertyName("games")] Dictionary<string, GameSetupGuide> Games);

    public static IReadOnlyDictionary<string, GameSetupGuide> Load(string root)
    {
        var path = Path.Combine(root, "config", "game-setup-guides.json");
        if (!File.Exists(path)) return new Dictionary<string, GameSetupGuide>();
        var document = JsonSerializer.Deserialize<Document>(File.ReadAllText(path));
        if (document?.Schema != "game-setup-guides/1" || document.Games is null ||
            document.Games.Values.Any(guide => guide?.Steps is null || guide.Steps.Any(string.IsNullOrWhiteSpace)))
            throw new InvalidDataException("Invalid game setup guidance configuration.");
        foreach (var guide in document.Games.Values)
            if (guide.SourceUrl is { } url && (!Uri.TryCreate(url, UriKind.Absolute, out var uri) || uri.Scheme != Uri.UriSchemeHttps))
                throw new InvalidDataException("Game setup source links must use HTTPS.");
        return document.Games;
    }
}
