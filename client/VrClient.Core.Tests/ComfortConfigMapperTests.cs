using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using VrClient.Core.Config;
using VrClient.Core.Model;
using Xunit;

public class ComfortConfigMapperTests
{
    [Fact]
    public void Updates_existing_keys_reports_absent_key_unmatched_and_invents_nothing()
    {
        var profilePath = Path.Combine(Path.GetTempPath(), Guid.NewGuid() + ".json");
        try
        {
            File.WriteAllText(profilePath,
                "{ \"comfort\": { \"turn_mode\": \"snap\", \"vignette\": true, \"world_scale\": 1.0 } }");
            var mapping = new ComfortMapping(new Dictionary<string, string>
            {
                ["turn_mode"] = "Comfort/TurnProvider",
                ["vignette"] = "Comfort/Vignette",
                ["world_scale"] = "Comfort/WorldScale"
            });
            var mapper = new ComfortConfigMapper();
            var entries = mapper.BuildCfgEntries(profilePath, mapping);
            Assert.Equal(3, entries.Count);

            var cfgText = string.Join("\n", new[]
            {
                "[Comfort]",
                "TurnProvider = Smooth",
                "Vignette = false"
            });
            var (newText, unmatched) = mapper.ApplyToCfg(cfgText, entries);

            Assert.Contains("TurnProvider = snap", newText);
            Assert.Contains("Vignette = true", newText);
            Assert.Equal(new[] { "Comfort/WorldScale" }, unmatched.ToArray());
            Assert.DoesNotContain("WorldScale", newText); // absent keys are never invented
            Assert.Equal(3, newText.Split('\n').Length);   // no lines added or removed
        }
        finally { File.Delete(profilePath); }
    }

    [Fact]
    public void Dotted_map_fields_resolve_nested_profile_values()
    {
        var profilePath = Path.Combine(Path.GetTempPath(), Guid.NewGuid() + ".json");
        try
        {
            File.WriteAllText(profilePath,
                "{ \"comfort\": { \"snap_turn\": { \"enabled\": true, \"degrees\": 30.0 }, \"vignette\": { \"enabled\": true } } }");
            var mapping = new ComfortMapping(new Dictionary<string, string>
            {
                ["snap_turn.degrees"] = "Input/SnapTurnSize",
                ["vignette.enabled"] = "Rendering/Vignette",
                ["snap_turn.missing_leaf"] = "Input/Nope"
            });
            var entries = new ComfortConfigMapper().BuildCfgEntries(profilePath, mapping);

            Assert.Equal(2, entries.Count);
            Assert.Contains(("Input", "SnapTurnSize", "30"), entries);
            Assert.Contains(("Rendering", "Vignette", "true"), entries);
        }
        finally { File.Delete(profilePath); }
    }
}
