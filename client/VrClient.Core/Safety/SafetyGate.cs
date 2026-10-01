namespace VrClient.Core.Safety;
using System.Text.Json;
using VrClient.Core.Model;

/// Independent pure-.NET safety evaluator over the existing
/// config/safety/default-rules.json + config/games/<slug>.json (plan D9).
/// Applies the fixed Phase 4 decision table in order; fail-closed by default.
public sealed class SafetyGate : ISafetyGate
{
    public SafetyVerdict Evaluate(string gameConfigPath, string safetyRulesPath)
    {
        using var gameDoc = JsonDocument.Parse(File.ReadAllText(gameConfigPath));
        var gameRoot = gameDoc.RootElement;
        var gameId = gameRoot.GetProperty("game_id").GetString()!;
        var supportPolicy = gameRoot.GetProperty("support_policy"); // missing => throws (not a verdict)
        var antiCheatRisk = supportPolicy.GetProperty("anti_cheat_risk").GetString()!;
        var onlineRisk = supportPolicy.GetProperty("online_risk").GetString()!;

        using var rulesDoc = JsonDocument.Parse(File.ReadAllText(safetyRulesPath));
        JsonElement rule = default;
        foreach (var candidate in rulesDoc.RootElement.GetProperty("rules").EnumerateArray())
            if (candidate.GetProperty("game_id").GetString() == gameId)
            {
                rule = candidate;
                break;
            }

        // 1: no rule for this game_id -> fail closed.
        if (rule.ValueKind == JsonValueKind.Undefined)
            return new SafetyVerdict(Verdict.UnknownBlocked, "no_safety_rule", "safety.block.no_rule",
                $"no safety rule exists for game '{gameId}'; refusing fail-closed");

        // 2: any anti-cheat signal that is not known-safe blocks.
        if (antiCheatRisk is not ("known_safe" or "none"))
            return new SafetyVerdict(Verdict.Block, "anti_cheat_present", "safety.block.anti_cheat",
                $"anti-cheat risk '{antiCheatRisk}' is not known-safe for game '{gameId}'");

        // 3: offline-only is allowed outright.
        if (onlineRisk == "offline_only")
            return new SafetyVerdict(Verdict.Allow, "offline_only_ok", "safety.allow.offline",
                $"game '{gameId}' is offline-only");

        // 4: private modded (co-op) requires the exact mode in the rule's allowed_launch_modes.
        if (onlineRisk is "private_modded_coop" or "private_modded")
        {
            var modeAllowed = false;
            foreach (var mode in rule.GetProperty("allowed_launch_modes").EnumerateArray())
                if (mode.GetString() == onlineRisk)
                {
                    modeAllowed = true;
                    break;
                }
            if (modeAllowed)
                return new SafetyVerdict(Verdict.Warn,
                    "private_modded_coop_requires_acknowledgement",
                    "safety.warn.private_modded_coop",
                    $"game '{gameId}' allows '{onlineRisk}'; user acknowledgement required before modded play");
        }

        // 5: public / unknown online scope blocks.
        return new SafetyVerdict(Verdict.Block, "online_scope_unsafe", "safety.block.online_scope",
            $"online risk '{onlineRisk}' for game '{gameId}' is not a sanctioned modded scope");
    }
}
