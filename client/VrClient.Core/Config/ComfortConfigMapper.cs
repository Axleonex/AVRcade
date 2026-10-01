namespace VrClient.Core.Config;
using System.Globalization;
using System.Text.Json;
using VrClient.Core.Model;

public sealed class ComfortConfigMapper
{
    public static ComfortMapping LoadMap(string comfortMapJsonPath)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(comfortMapJsonPath));
        var map = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var field in doc.RootElement.GetProperty("map").EnumerateObject())
            map[field.Name] = field.Value.GetString()!;
        return new ComfortMapping(map);
    }

    /// Given our comfort profile JSON (config/profiles/<slug>-game-profile.json) and the
    /// loaded mapping, produce the set of (Section, Key, Value) triples to apply to the
    /// mod cfg. Only fields present in BOTH the profile and the map are emitted.
    /// Only scalar profile values map to cfg lines; nested objects are left to the
    /// user-gated verify-comfort-map repair step.
    public IReadOnlyList<(string Section, string Key, string Value)> BuildCfgEntries(
        string profileJsonPath, ComfortMapping mapping)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(profileJsonPath));
        var entries = new List<(string Section, string Key, string Value)>();
        if (!doc.RootElement.TryGetProperty("comfort", out var comfort))
            return entries;
        foreach (var (fieldPath, target) in mapping.CfgKeyByProfileField)
        {
            // Map field names are dot-paths into the comfort object ("snap_turn.degrees");
            // a bare name ("world_scale") is the single-segment case of the same walk.
            var element = comfort;
            var found = true;
            foreach (var segment in fieldPath.Split('.'))
            {
                if (element.ValueKind != JsonValueKind.Object ||
                    !element.TryGetProperty(segment, out element))
                {
                    found = false;
                    break;
                }
            }
            if (!found)
                continue;
            var value = element.ValueKind switch
            {
                JsonValueKind.String => element.GetString()!,
                JsonValueKind.True => "true",
                JsonValueKind.False => "false",
                JsonValueKind.Number => element.GetDouble().ToString(CultureInfo.InvariantCulture),
                _ => null
            };
            if (value is null)
                continue;
            var separator = target.IndexOf('/');
            entries.Add((target[..separator], target[(separator + 1)..], value));
        }
        return entries;
    }

    /// Apply entries to an existing INI-style .cfg text, replacing the value under the
    /// matching [Section]/Key = line if present; entries whose Section/Key are NOT already
    /// present in the cfg are returned as "unmatched" (never invented into the file).
    public (string NewCfgText, IReadOnlyList<string> Unmatched) ApplyToCfg(
        string cfgText, IReadOnlyList<(string Section, string Key, string Value)> entries)
    {
        var lines = cfgText.Replace("\r\n", "\n").Split('\n');
        var unmatched = new List<string>();
        foreach (var (section, key, value) in entries)
        {
            var applied = false;
            string? currentSection = null;
            for (var i = 0; i < lines.Length; i++)
            {
                var trimmed = lines[i].Trim();
                if (trimmed.StartsWith("[", StringComparison.Ordinal) &&
                    trimmed.EndsWith("]", StringComparison.Ordinal))
                {
                    currentSection = trimmed[1..^1];
                    continue;
                }
                if (currentSection != section ||
                    trimmed.StartsWith("#", StringComparison.Ordinal) ||
                    trimmed.StartsWith(";", StringComparison.Ordinal))
                    continue;
                var equals = trimmed.IndexOf('=');
                if (equals <= 0)
                    continue;
                if (trimmed[..equals].Trim() == key)
                {
                    lines[i] = $"{key} = {value}";
                    applied = true;
                    break;
                }
            }
            if (!applied)
                unmatched.Add($"{section}/{key}");
        }
        return (string.Join("\n", lines), unmatched);
    }
}
