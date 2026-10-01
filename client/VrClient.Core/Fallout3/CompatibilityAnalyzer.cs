namespace VrClient.Core.Fallout3;

using System.Text.Json;
using System.Text.RegularExpressions;

public sealed class Fallout3CompatibilityAnalyzer
{
    private sealed record Rule(string Pattern, string Category, Fallout3CompatibilityStatus Status, string Explanation, string Action);

    public Fallout3CompatibilityReport Analyze(string profileDirectory, string gameDirectory,
        Fallout3Backend backend, string? rulesPath = null)
    {
        var entries = new List<Fallout3CompatibilityEntry>();
        var rules = LoadRules(rulesPath);
        var modList = Path.Combine(profileDirectory, "modlist.txt");
        if (File.Exists(modList))
        {
            foreach (var line in File.ReadLines(modList).Select(line => line.Trim()))
            {
                if (line.Length < 2 || line[0] == '#') continue;
                var enabled = line[0] == '+';
                var name = line.TrimStart('+', '-').Trim();
                if (name.Length == 0) continue;
                entries.Add(Classify(name, enabled, backend, rules));
            }
        }

        var pluginList = Path.Combine(profileDirectory, "plugins.txt");
        if (File.Exists(pluginList))
        {
            foreach (var plugin in File.ReadLines(pluginList).Select(line => line.Trim().TrimStart('*'))
                         .Where(line => line.Length > 0 && !line.StartsWith('#')))
                if (!entries.Any(entry => entry.Name.Equals(plugin, StringComparison.OrdinalIgnoreCase)))
                    entries.Add(new Fallout3CompatibilityEntry(plugin, Fallout3CompatibilityStatus.LikelyCompatible,
                        "esm-esp", "Plugin remains enabled in the isolated profile; no VR-specific rule matched.",
                        "Keep the source load order and test this plugin in the representative-mod headset pass."));
        }

        foreach (var rootFile in EnumerateRootConflicts(gameDirectory))
            entries.Add(Classify(Path.GetFileName(rootFile), true, backend, rules) with { Evidence = rootFile });

        return new Fallout3CompatibilityReport(DateTimeOffset.UtcNow.ToString("O"), Path.GetFullPath(profileDirectory),
            backend == Fallout3Backend.NativeVr ? Fallout3BackendLabels.NativeExperimental : Fallout3BackendLabels.DepthVr,
            entries.OrderBy(entry => entry.Category).ThenBy(entry => entry.Name, StringComparer.OrdinalIgnoreCase).ToArray());
    }

    private static Fallout3CompatibilityEntry Classify(string name, bool enabled, Fallout3Backend backend, IReadOnlyList<Rule> rules)
    {
        if (!enabled)
            return new Fallout3CompatibilityEntry(name, Fallout3CompatibilityStatus.Unknown, "disabled",
                "The source profile lists this mod as disabled; AVRcade preserved that state.", "No automatic action.");
        foreach (var rule in rules)
        {
            if (!Regex.IsMatch(name, rule.Pattern, RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)) continue;
            var status = rule.Status;
            if (status == Fallout3CompatibilityStatus.ConflictsWithDepthVr && backend == Fallout3Backend.NativeVr)
                status = Fallout3CompatibilityStatus.RequiresVrConfiguration;
            if (status == Fallout3CompatibilityStatus.ConflictsWithNativeVr && backend == Fallout3Backend.DepthVr)
                status = Fallout3CompatibilityStatus.RequiresVrConfiguration;
            return new Fallout3CompatibilityEntry(name, status, rule.Category, rule.Explanation, rule.Action);
        }
        return new Fallout3CompatibilityEntry(name, Fallout3CompatibilityStatus.Unknown, "uncategorized",
            "No verified compatibility rule matched. AVRcade does not promise universal mod compatibility.",
            "Test in the isolated VR profile and record the result before promoting it to verified compatible.");
    }

    private static IReadOnlyList<Rule> LoadRules(string? path)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path)) return DefaultRules();
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        return document.RootElement.GetProperty("rules").EnumerateArray().Select(item => new Rule(
            item.GetProperty("pattern").GetString()!, item.GetProperty("category").GetString()!,
            Enum.Parse<Fallout3CompatibilityStatus>(item.GetProperty("status").GetString()!, true),
            item.GetProperty("explanation").GetString()!, item.GetProperty("suggested_action").GetString()!)).ToArray();
    }

    private static IReadOnlyList<Rule> DefaultRules() => new[]
    {
        new Rule("enb|reshade|sweetfx", "post-processing", Fallout3CompatibilityStatus.ConflictsWithDepthVr,
            "Graphics proxy or post-processing injection can compete with the Depth VR ReShade hook.", "Use one proxy chain only; preserve the original files and test a VR-specific configuration."),
        new Rule("camera|first.?person|animation|weapon|skeleton|body", "camera-animation", Fallout3CompatibilityStatus.RequiresVrConfiguration,
            "Camera, skeleton, body, or weapon placement can conflict with head-driven view and stereo convergence.", "Test head rotation, VATS, dialogue, terminals, Pip-Boy, holster and weapon transitions."),
        new Rule("hud|ui|menu|pip.?boy", "hud-menu", Fallout3CompatibilityStatus.RequiresVrConfiguration,
            "HUD/menu replacements may require scale or stereo-depth adjustment.", "Check both eyes, Pip-Boy readability, terminals, lockpicking and dialogue."),
        new Rule("fose|script extender|command extender|dll", "fose-plugin", Fallout3CompatibilityStatus.RequiresVrConfiguration,
            "Native plugins commonly assume an exact executable build and may share hook sites.", "Require Fallout3.exe 1.7.0.3 and compare plugin hook/version requirements."),
        new Rule("weather|lighting|shadow|water", "rendering", Fallout3CompatibilityStatus.RequiresVrConfiguration,
            "Rendering changes can alter depth availability, stereo artifacts, or performance.", "Verify depth visualization, shadows, water and representative interiors/exteriors."),
        new Rule("texture|lod|landscape", "performance", Fallout3CompatibilityStatus.LikelyCompatible,
            "Content replacement is usually compatible but can exceed VR frame-time or memory budgets.", "Measure frame pacing and reduce texture/LOD settings if required."),
        new Rule("controller|gamepad|input|hotkey", "input", Fallout3CompatibilityStatus.RequiresVrConfiguration,
            "Input mods can overlap head-to-mouse and controller mappings.", "Check recenter, turning, menus, VATS and dominant-hand mappings."),
        new Rule("launcher|4gb|anniversary|patcher", "launcher-build", Fallout3CompatibilityStatus.RequiresVrConfiguration,
            "Alternate launchers and executable patchers affect build identity and FOSE startup.", "Re-run build identification after any executable change; never stack patchers blindly.")
    };

    private static IEnumerable<string> EnumerateRootConflicts(string gameDirectory)
    {
        if (!Directory.Exists(gameDirectory)) yield break;
        foreach (var name in new[] { "d3d9.dll", "dxgi.dll", "d3d11.dll", "enbseries.dll", "fose_1_7.dll" })
        {
            var path = Path.Combine(gameDirectory, name);
            if (File.Exists(path)) yield return path;
        }
    }
}
