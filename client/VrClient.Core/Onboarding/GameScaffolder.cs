namespace VrClient.Core.Onboarding;
using System.Text.Json;
using System.Text.Json.Nodes;

public sealed record ScaffoldRequest(
    string Slug, string SteamAppId, string Community,
    IReadOnlyList<(string Namespace, string Name)> Mods);

public sealed record ScaffoldResult(bool Created, IReadOnlyList<string> WrittenFiles, string? RefusalReason);

public sealed class GameScaffolder
{
    private static readonly JsonSerializerOptions Indented = new() { WriteIndented = true };

    /// Write config/modpacks/<slug>.modpack.json and <slug>.comfort-map.json under modpacksDir,
    /// config/games/<slug>.json under gameConfigDir, config/profiles/<slug>-game-profile.json
    /// under profileDir — as schema-valid skeletons (comfort-map verified_against_cfg=false;
    /// game config anti_cheat_risk="unknown", online_risk="unknown" so the game is fail-closed
    /// until reviewed; empty builds[]; profile with an empty comfort object). REFUSES (Created=false)
    /// if ANY target file already exists, writing nothing.
    public ScaffoldResult Scaffold(
        ScaffoldRequest req,
        string modpacksDir,
        string gameConfigDir,
        string profileDir,
        string? controllerMapsDir = null)
    {
        var modpackPath = Path.Combine(modpacksDir, $"{req.Slug}.modpack.json");
        var comfortMapPath = Path.Combine(modpacksDir, $"{req.Slug}.comfort-map.json");
        var gameConfigPath = Path.Combine(gameConfigDir, $"{req.Slug}.json");
        var profilePath = Path.Combine(profileDir, $"{req.Slug}-game-profile.json");
        var controllerMapPath = controllerMapsDir is null
            ? null
            : Path.Combine(controllerMapsDir, $"{req.Slug}.json");
        var targets = new[] { modpackPath, comfortMapPath, gameConfigPath, profilePath, controllerMapPath }
            .Where(path => path is not null).Cast<string>().ToArray();

        // All-or-nothing: refuse if ANY target already exists; write nothing.
        var existing = targets.FirstOrDefault(File.Exists);
        if (existing is not null)
            return new ScaffoldResult(false, [], $"already_exists:{existing}");

        var modsArray = new JsonArray();
        foreach (var (ns, name) in req.Mods)
            modsArray.Add(new JsonObject { ["namespace"] = ns, ["name"] = name });

        var modpack = new JsonObject
        {
            ["schema"] = "modpack-spec/1",
            ["game_slug"] = req.Slug,
            ["steam_app_id"] = req.SteamAppId,
            ["community"] = req.Community,
            ["mods"] = modsArray
        };

        var comfortMap = new JsonObject
        {
            ["schema"] = "comfort-map/1",
            ["game_slug"] = req.Slug,
            ["cfg_file"] = $"BepInEx/config/{req.Slug}.cfg",
            ["verified_against_cfg"] = false,
            ["map"] = new JsonObject
            {
                ["snap_turn.degrees"] = "Comfort/SnapTurnSize",
                ["vignette.enabled"] = "Comfort/Vignette"
            }
        };

        var gameConfig = new JsonObject
        {
            ["version"] = 1,
            ["game_id"] = req.Slug,
            ["display_name"] = req.Slug,
            ["controlled_smoke_target"] = false,
            ["executable_names"] = new JsonArray("<GAME>.exe"),
            ["support_policy"] = new JsonObject
            {
                ["allowed_sources"] = new JsonArray("steam", "manual"),
                ["target_architecture"] = "x64",
                ["anti_cheat_risk"] = "unknown",
                // Default posture (user directive 2026-07-10): VR modding is assumed
                // private co-op with friends - Warn + explicit acknowledgement, not a
                // block. Public-lobby play is the exception and must be authored as
                // such. anti_cheat_risk stays "unknown" (fail-closed) - the author
                // must verify it; that is the axis that gets accounts banned.
                ["online_risk"] = "private_modded_coop"
            },
            ["builds"] = new JsonArray()
        };

        var profile = new JsonObject { ["comfort"] = new JsonObject() };

        JsonObject Binding(string slot, string control) => new()
        {
            ["slot"] = slot,
            ["control"] = control,
            ["action"] = "VERIFY IN-GAME ACTION"
        };
        var controllerMap = new JsonObject
        {
            ["schema"] = "controller-map/1",
            ["verification_state"] = "template",
            ["game_slug"] = req.Slug,
            ["title"] = $"{req.Slug} VR controls",
            ["devices"] = new JsonArray(new JsonObject
            {
                ["id"] = "quest-touch",
                ["display_name"] = "Meta Quest Touch",
                ["bindings"] = new JsonArray(
                    Binding("left_stick", "Left stick"),
                    Binding("right_stick", "Right stick"),
                    Binding("left_primary", "X"),
                    Binding("left_secondary", "Y"),
                    Binding("right_primary", "A"),
                    Binding("right_secondary", "B"),
                    Binding("left_trigger", "Left trigger"),
                    Binding("right_trigger", "Right trigger"),
                    Binding("left_grip", "Left grip"),
                    Binding("right_grip", "Right grip"),
                    Binding("menu", "Menu"),
                    Binding("both_sticks", "Both stick clicks"),
                    Binding("both_grips", "Both grips"))
            })
        };

        Directory.CreateDirectory(modpacksDir);
        Directory.CreateDirectory(gameConfigDir);
        Directory.CreateDirectory(profileDir);
        if (controllerMapsDir is not null)
            Directory.CreateDirectory(controllerMapsDir);

        File.WriteAllText(modpackPath, modpack.ToJsonString(Indented));
        File.WriteAllText(comfortMapPath, comfortMap.ToJsonString(Indented));
        File.WriteAllText(gameConfigPath, gameConfig.ToJsonString(Indented));
        File.WriteAllText(profilePath, profile.ToJsonString(Indented));
        if (controllerMapPath is not null)
            File.WriteAllText(controllerMapPath, controllerMap.ToJsonString(Indented));

        return new ScaffoldResult(true, targets, null);
    }
}
