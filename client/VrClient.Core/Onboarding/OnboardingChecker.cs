namespace VrClient.Core.Onboarding;
using System.Text.Json;

public sealed record CheckItem(string Id, bool Pass, string Detail);
public sealed record CheckReport(string Slug, bool AllPass, IReadOnlyList<CheckItem> Items);

public sealed class OnboardingChecker
{
    /// Run these checks for <slug> (each a CheckItem): modpack-present, comfort-map-present,
    /// game-config-present, profile-present, game-config-anti-cheat-set (anti_cheat_risk != "unknown"),
    /// game-config-online-set (online_risk != "unknown"), safety-rule-present (a rules[] row with
    /// game_id == slug in the given rules file). AllPass = all items pass. Reads files only; never writes.
    public CheckReport Check(string slug, string modpacksDir, string gameConfigDir, string profileDir, string safetyRulesPath)
    {
        var items = new List<CheckItem>();

        var modpackPath = Path.Combine(modpacksDir, $"{slug}.modpack.json");
        var comfortMapPath = Path.Combine(modpacksDir, $"{slug}.comfort-map.json");
        var gameConfigPath = Path.Combine(gameConfigDir, $"{slug}.json");
        var profilePath = Path.Combine(profileDir, $"{slug}-game-profile.json");

        var hasModpack = File.Exists(modpackPath);
        var hasGameConfig = File.Exists(gameConfigPath);
        items.Add(new CheckItem("modpack-present", hasModpack, hasModpack ? modpackPath : $"missing {modpackPath}"));
        items.Add(new CheckItem("comfort-map-present", File.Exists(comfortMapPath),
            File.Exists(comfortMapPath) ? comfortMapPath : $"missing {comfortMapPath}"));
        items.Add(new CheckItem("game-config-present", hasGameConfig, hasGameConfig ? gameConfigPath : $"missing {gameConfigPath}"));
        items.Add(new CheckItem("profile-present", File.Exists(profilePath),
            File.Exists(profilePath) ? profilePath : $"missing {profilePath}"));

        var antiCheat = ReadSupportPolicyValue(gameConfigPath, "anti_cheat_risk");
        var online = ReadSupportPolicyValue(gameConfigPath, "online_risk");
        items.Add(new CheckItem("game-config-anti-cheat-set", antiCheat is not null && antiCheat != "unknown",
            $"anti_cheat_risk={antiCheat ?? "<none>"}"));
        items.Add(new CheckItem("game-config-online-set", online is not null && online != "unknown",
            $"online_risk={online ?? "<none>"}"));

        var hasRule = SafetyRuleExists(safetyRulesPath, slug);
        items.Add(new CheckItem("safety-rule-present", hasRule,
            hasRule ? $"rule for game_id={slug} in {safetyRulesPath}" : $"no rules[] row with game_id={slug}"));

        return new CheckReport(slug, items.All(i => i.Pass), items);
    }

    private static string? ReadSupportPolicyValue(string gameConfigPath, string key)
    {
        if (!File.Exists(gameConfigPath))
            return null;
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(gameConfigPath));
            if (doc.RootElement.TryGetProperty("support_policy", out var policy) &&
                policy.TryGetProperty(key, out var value) &&
                value.ValueKind == JsonValueKind.String)
                return value.GetString();
        }
        catch (JsonException) { /* malformed config -> treated as unset */ }
        return null;
    }

    private static bool SafetyRuleExists(string safetyRulesPath, string slug)
    {
        if (!File.Exists(safetyRulesPath))
            return false;
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(safetyRulesPath));
            if (!doc.RootElement.TryGetProperty("rules", out var rules) || rules.ValueKind != JsonValueKind.Array)
                return false;
            foreach (var rule in rules.EnumerateArray())
                if (rule.TryGetProperty("game_id", out var gameId) &&
                    gameId.ValueKind == JsonValueKind.String &&
                    gameId.GetString() == slug)
                    return true;
        }
        catch (JsonException) { /* malformed rules -> no rule */ }
        return false;
    }
}
