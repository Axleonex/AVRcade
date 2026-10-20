using System.Text.Json;

namespace VrClient.Core.Config;

public sealed record GameModCredit(string Project, string Creator, string Url, string Role = "VR mod")
{
    public string DisplayText => $"{Project} by {Creator}";
    public string CreatorInitial => string.IsNullOrWhiteSpace(Creator) ? "?" : Creator[..1].ToUpperInvariant();
}

/// Attribution is catalogue metadata, independent of controller guides and install state.
public static class GameModCredits
{
    public static IReadOnlyList<GameModCredit> Load(string repoRoot, string slug)
    {
        var path = Path.Combine(repoRoot, "config", "mod-credits.json");
        if (!File.Exists(path))
        {
            var fallback = ControllerReferenceCatalog.Load(repoRoot, slug)?.ConversionCredit;
            return fallback is null ? [] : [new(fallback.Project, fallback.Creator, fallback.ProjectUrl)];
        }
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        if (document.RootElement.GetProperty("schema").GetString() != "mod-credits/1")
            throw new InvalidDataException("Unsupported mod-credit catalogue.");
        if (!document.RootElement.GetProperty("games").TryGetProperty(slug, out var entries)) return [];
        var credits = entries.Deserialize<GameModCredit[]>(new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? [];
        if (credits.Any(credit => string.IsNullOrWhiteSpace(credit.Project) || string.IsNullOrWhiteSpace(credit.Creator) ||
            !Uri.TryCreate(credit.Url, UriKind.Absolute, out var uri) || uri.Scheme != Uri.UriSchemeHttps))
            throw new InvalidDataException($"Invalid mod credit for {slug}.");
        return credits;
    }
}
