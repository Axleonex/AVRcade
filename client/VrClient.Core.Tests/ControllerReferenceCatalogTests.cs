using VrClient.Core.Config;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class ControllerReferenceCatalogTests
{
    [Theory]
    [InlineData("big-walk")]
    [InlineData("rv-there-yet")]
    public void SourceVerifiedMapsShowBothFamiliesAndTestingLimitations(string slug)
    {
        var reference = ControllerReferenceCatalog.Load(FindRepoRoot(), slug);
        Assert.NotNull(reference);
        Assert.Equal("source_verified", reference.VerificationState);
        Assert.Equal(["quest-touch", "valve-index"], reference.Devices.Select(d => d.Id));
        Assert.NotNull(reference.ConversionCredit);
        Assert.False(string.IsNullOrWhiteSpace(reference.Note));
        Assert.Equal(reference.Note, reference.Guidance);
        if (slug == "rv-there-yet")
        {
            Assert.Contains("not tracked-hand", reference.Guidance);
            Assert.Equal("Xbox B prompt", reference.Devices[0].LeftPrimary.Action);
            Assert.Equal("Xbox X prompt", reference.Devices[1].RightSecondary.Action);
        }
    }

    [Theory]
    [InlineData("template", true, true)]
    [InlineData("source_verified", false, true)]
    [InlineData("source_verified", true, false)]
    public void IncompleteSourceEvidenceIsNotDisplayed(string state, bool note, bool credit)
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-map-evidence-{Guid.NewGuid():N}");
        try
        {
            var reference = ControllerReferenceCatalog.Load(FindRepoRoot(), "rv-there-yet")!;
            reference = reference with { VerificationState = state, Note = note ? reference.Note : null,
                ConversionCredit = credit ? reference.ConversionCredit : null };
            var dir = Path.Combine(root, "config", "controller-maps");
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "rv-there-yet.json"),
                System.Text.Json.JsonSerializer.Serialize(reference));
            Assert.Null(ControllerReferenceCatalog.Load(root, "rv-there-yet"));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void LoadsPerGameQuestAndIndexReference()
    {
        var root = FindRepoRoot();
        var reference = ControllerReferenceCatalog.Load(root, "cyberpunk-2077");

        Assert.NotNull(reference);
        Assert.Equal(["quest-touch", "valve-index"], reference.Devices.Select(d => d.Id));
        Assert.Contains(reference.Devices[0].Bindings, binding =>
            binding.Control == "Both stick clicks" && binding.Action == "Hub / map");
        Assert.Equal("Interact / reload", reference.Devices[0].LeftPrimary.Action);
        Assert.Equal("X", reference.Devices[0].LeftPrimary.Control);
    }

    [Fact]
    public void CatalogIsSlugBasedAndHasNoEngineDependency()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-controller-map-{Guid.NewGuid():N}");
        try
        {
            var mapDir = Path.Combine(root, "config", "controller-maps");
            Directory.CreateDirectory(mapDir);
            File.WriteAllText(Path.Combine(mapDir, "future-game.json"), """
                {
                  "schema": "controller-map/1",
                  "verification_state": "verified",
                  "game_slug": "future-game",
                  "title": "Future game controls",
                  "devices": [{
                    "id": "quest-touch",
                    "display_name": "Quest Touch",
                    "bindings": [{
                      "slot": "right_primary",
                      "control": "A",
                      "action": "Use tool"
                    }]
                  }]
                }
                """);

            var reference = ControllerReferenceCatalog.Load(root, "future-game");

            Assert.NotNull(reference);
            Assert.Equal("Use tool", reference.Devices[0].RightPrimary.Action);
        }
        finally
        {
            if (Directory.Exists(root))
                Directory.Delete(root, recursive: true);
        }
    }

    [Theory]
    [InlineData(CyberpunkHudMode.Compact, "xr_hud_scale=-360.0000", "xr_hud_visible=1")]
    [InlineData(CyberpunkHudMode.Hidden, "xr_hud_scale=-360.0000", "xr_hud_visible=0")]
    [InlineData(CyberpunkHudMode.Comfortable, "xr_hud_scale=-220.0000", "xr_hud_visible=1")]
    public void HudModesGenerateStableUserOverrides(
        CyberpunkHudMode mode, string expectedInset, string expectedVisibility)
    {
        var method = typeof(CyberpunkHudSettings).GetMethod(
            "BuildIni", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!;
        var ini = (string)method.Invoke(null, [mode])!;

        Assert.Contains(expectedInset, ini);
        Assert.Contains(expectedVisibility, ini);
    }

    private static string FindRepoRoot() => TestRepoRoot.Find();
}
