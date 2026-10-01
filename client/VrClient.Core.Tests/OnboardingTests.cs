using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using VrClient.Core.Onboarding;
using Xunit;

public class OnboardingTests
{
    private static (string modpacks, string games, string profiles) TempTree(string root)
    {
        var modpacks = Path.Combine(root, "config", "modpacks");
        var games = Path.Combine(root, "config", "games");
        var profiles = Path.Combine(root, "config", "profiles");
        Directory.CreateDirectory(modpacks);
        Directory.CreateDirectory(games);
        Directory.CreateDirectory(profiles);
        return (modpacks, games, profiles);
    }

    [Fact]
    public void Scaffold_writes_four_fail_closed_files_and_refuses_second_time()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        try
        {
            var (modpacks, games, profiles) = TempTree(root);
            var req = new ScaffoldRequest("probe-game", "12345", "probe-game",
                new List<(string, string)> { ("BepInEx", "BepInExPack"), ("ProbeAuthor", "ProbeVR") });
            var scaffolder = new GameScaffolder();

            var result = scaffolder.Scaffold(req, modpacks, games, profiles);
            Assert.True(result.Created);
            Assert.Equal(4, result.WrittenFiles.Count);

            // All four parse as JSON.
            foreach (var file in result.WrittenFiles)
                using (JsonDocument.Parse(File.ReadAllText(file))) { }

            // Game config is fail-closed until reviewed.
            using (var gameDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(games, "probe-game.json"))))
                Assert.Equal("unknown",
                    gameDoc.RootElement.GetProperty("support_policy").GetProperty("anti_cheat_risk").GetString());

            // Modpack lists the requested mods in order.
            using (var modpackDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(modpacks, "probe-game.modpack.json"))))
            {
                var mods = modpackDoc.RootElement.GetProperty("mods");
                Assert.Equal(2, mods.GetArrayLength());
                Assert.Equal("BepInExPack", mods[0].GetProperty("name").GetString());
                Assert.Equal("ProbeVR", mods[1].GetProperty("name").GetString());
            }

            var firstBytes = File.ReadAllBytes(Path.Combine(modpacks, "probe-game.modpack.json"));

            // Second scaffold refuses all-or-nothing and does not modify the first files.
            var second = scaffolder.Scaffold(req, modpacks, games, profiles);
            Assert.False(second.Created);
            Assert.NotNull(second.RefusalReason);
            Assert.Equal(firstBytes, File.ReadAllBytes(Path.Combine(modpacks, "probe-game.modpack.json")));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void Scaffold_can_seed_an_engine_agnostic_unverified_controller_map()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        try
        {
            var (modpacks, games, profiles) = TempTree(root);
            var controllerMaps = Path.Combine(root, "config", "controller-maps");
            var req = new ScaffoldRequest("probe-game", "12345", "probe-game",
                new List<(string, string)> { ("ProbeAuthor", "ProbeVR") });

            var result = new GameScaffolder().Scaffold(
                req, modpacks, games, profiles, controllerMaps);

            Assert.True(result.Created);
            Assert.Equal(5, result.WrittenFiles.Count);
            var mapPath = Path.Combine(controllerMaps, "probe-game.json");
            using var map = JsonDocument.Parse(File.ReadAllText(mapPath));
            Assert.Equal("template", map.RootElement.GetProperty("verification_state").GetString());
            Assert.Null(VrClient.Core.Config.ControllerReferenceCatalog.Load(root, "probe-game"));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void Checker_fails_fresh_scaffold_then_passes_after_posture_and_rule_set()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        try
        {
            var (modpacks, games, profiles) = TempTree(root);
            var req = new ScaffoldRequest("probe-game", "12345", "probe-game",
                new List<(string, string)> { ("BepInEx", "BepInExPack"), ("ProbeAuthor", "ProbeVR") });
            new GameScaffolder().Scaffold(req, modpacks, games, profiles);

            var rulesPath = Path.Combine(root, "config", "safety", "default-rules.json");
            Directory.CreateDirectory(Path.GetDirectoryName(rulesPath)!);
            File.WriteAllText(rulesPath, "{ \"rules\": [] }");

            var checker = new OnboardingChecker();
            var before = checker.Check("probe-game", modpacks, games, profiles, rulesPath);
            Assert.False(before.AllPass);
            Assert.False(GetItem(before, "game-config-anti-cheat-set").Pass);
            Assert.False(GetItem(before, "safety-rule-present").Pass);
            // Presence checks pass on a fresh scaffold.
            Assert.True(GetItem(before, "modpack-present").Pass);

            // Set a real safety posture in the game config.
            var gameConfigPath = Path.Combine(games, "probe-game.json");
            var patched = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(gameConfigPath))!;
            patched["support_policy"]!["anti_cheat_risk"] = "known_safe";
            patched["support_policy"]!["online_risk"] = "offline_only";
            File.WriteAllText(gameConfigPath, patched.ToJsonString());

            // Add a matching rule row.
            File.WriteAllText(rulesPath, "{ \"rules\": [ { \"game_id\": \"probe-game\" } ] }");

            var after = checker.Check("probe-game", modpacks, games, profiles, rulesPath);
            Assert.True(after.AllPass);
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    private static CheckItem GetItem(CheckReport report, string id)
        => report.Items.Single(i => i.Id == id);
}
