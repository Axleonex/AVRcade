namespace VrClient.Core.FalloutNewVegas;

using System.Text.Json;
using System.Text.RegularExpressions;

public sealed record FnvCompatibilityRule(
    string Pattern,
    FnvCompatibilityStatus Status,
    string Category,
    string Explanation,
    string SuggestedAction);

public sealed class FalloutNewVegasCompatibilityAnalyzer
{
    public static IReadOnlyList<FnvCompatibilityRule> LoadRules(string jsonPath)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(jsonPath));
        return document.RootElement.GetProperty("rules").EnumerateArray().Select(element =>
            new FnvCompatibilityRule(
                element.GetProperty("pattern").GetString()!,
                Enum.Parse<FnvCompatibilityStatus>(element.GetProperty("status").GetString()!, ignoreCase: true),
                element.GetProperty("category").GetString()!,
                element.GetProperty("explanation").GetString()!,
                element.GetProperty("suggested_action").GetString()!)).ToArray();
    }

    public FnvCompatibilityReport Analyze(
        string profileDirectory,
        string gameDirectory,
        IReadOnlyList<FnvCompatibilityRule> rules,
        string generatedAtUtc)
    {
        var modListPath = Path.Combine(profileDirectory, "modlist.txt");
        if (!File.Exists(modListPath))
            throw new InvalidOperationException($"MO2 modlist.txt not found: {modListPath}");
        var entries = new List<FnvCompatibilityEntry>();
        foreach (var rawLine in File.ReadLines(modListPath))
        {
            var line = rawLine.Trim();
            if (line.Length < 2 || line[0] != '+')
                continue;
            var name = line[1..].Trim();
            var matched = rules
                .Where(rule => Regex.IsMatch(name, rule.Pattern,
                    RegexOptions.IgnoreCase | RegexOptions.CultureInvariant,
                    TimeSpan.FromMilliseconds(250)))
                .OrderByDescending(rule => rule.Status switch
                {
                    FnvCompatibilityStatus.Incompatible => 3,
                    FnvCompatibilityStatus.Verified => 2,
                    FnvCompatibilityStatus.Warned => 1,
                    _ => 0
                })
                .FirstOrDefault();
            entries.Add(matched is null
                ? new FnvCompatibilityEntry(name, FnvCompatibilityStatus.Unknown, "unknown",
                    "No New Vegas VR compatibility evidence is recorded for this enabled mod.",
                    "Test in the isolated VR profile and retain the non-VR profile as the rollback path.")
                : new FnvCompatibilityEntry(name, matched.Status, matched.Category,
                    matched.Explanation, matched.SuggestedAction));
        }

        var rootDlls = AnalyzeRootDlls(gameDirectory);
        return new FnvCompatibilityReport(generatedAtUtc, Path.GetFullPath(profileDirectory), entries, rootDlls);
    }

    public void WriteReport(string path, FnvCompatibilityReport report)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, JsonSerializer.Serialize(new
        {
            schema_version = 1,
            generated_at_utc = report.GeneratedAtUtc,
            profile_directory = report.ProfileDirectory,
            summary = new
            {
                verified = report.Mods.Count(item => item.Status == FnvCompatibilityStatus.Verified),
                warned = report.Mods.Count(item => item.Status == FnvCompatibilityStatus.Warned),
                incompatible = report.Mods.Count(item => item.Status == FnvCompatibilityStatus.Incompatible),
                unknown = report.Mods.Count(item => item.Status == FnvCompatibilityStatus.Unknown)
            },
            mods = report.Mods.Select(ToJson),
            root_dlls = report.RootDlls.Select(ToJson),
            policy = new
            {
                no_automatic_disable = true,
                no_source_profile_reorder = true,
                compatibility_is_not_guaranteed = true
            }
        }, new JsonSerializerOptions { WriteIndented = true }));
    }

    private static IReadOnlyList<FnvCompatibilityEntry> AnalyzeRootDlls(string gameDirectory)
    {
        if (!Directory.Exists(gameDirectory))
            return Array.Empty<FnvCompatibilityEntry>();
        var hostileNames = new HashSet<string>(new[] { "d3d9.dll", "dxgi.dll", "dinput8.dll", "opengl32.dll" },
            StringComparer.OrdinalIgnoreCase);
        return Directory.EnumerateFiles(gameDirectory, "*.dll", SearchOption.TopDirectoryOnly)
            .Where(path => hostileNames.Contains(Path.GetFileName(path)))
            .Select(path => new FnvCompatibilityEntry(Path.GetFileName(path), FnvCompatibilityStatus.Warned,
                "renderer_or_input_hook",
                "A root-level graphics/input hook can conflict with AVRcade's native renderer or another wrapper even when it is valid for non-VR play.",
                "Identify the owning mod and test a VR-specific alternative in the isolated profile; do not delete it automatically."))
            .ToArray();
    }

    private static object ToJson(FnvCompatibilityEntry item) => new
    {
        name = item.Name,
        status = item.Status.ToString().ToLowerInvariant(),
        category = item.Category,
        explanation = item.Explanation,
        suggested_action = item.SuggestedAction
    };
}
