using VrClient.App.ViewModels;
using VrClient.App.Services;
using VrClient.Core.Compatibility;
using VrClient.Core.Config;
using VrClient.Core.Launch;
using Xunit;

namespace AVRcade.App.Tests;

public sealed class TroubleshootingGuideTests
{
    private static GameVersionRecovery Version => new("Cyberpunk 2077", "2.5", "2.31", "VR needs 2.31", "steam://gameproperties/1091500", false);
    [Fact]
    public void Before_checks_the_only_primary_action_is_to_check_the_game()
    {
        var guide = TroubleshootingGuideViewModel.Create([]);
        Assert.Equal(HelpAction.CheckGame, guide.Next.Action);
        Assert.Equal(HelpTone.Next, guide.Next.Tone);
        Assert.False(guide.HasOtherChecks);
        Assert.False(guide.HasPassedChecks);
    }
    [Fact]
    public void Version_mismatch_is_prioritized_over_runtime_and_optional_mod_checks()
    {
        var raw = new[] {
            new TroubleshootingFinding("Optional mods and controls", "Instructions", "mods", "original mods guidance", TroubleshootingAction.Mods),
            new TroubleshootingFinding("VR runtime observation", "Attention", "runtime", "start runtime", TroubleshootingAction.VrSetup),
            new TroubleshootingFinding("Game installation", "Passed", "folder", "selected folder")
        };
        var guide = TroubleshootingGuideViewModel.Create(raw, Version);
        Assert.Equal(HelpAction.RecoverVersion, guide.Next.Action);
        Assert.Equal(HelpTone.Blocked, guide.Next.Tone);
        Assert.Contains("2.5", guide.Next.Summary);
        Assert.Contains("2.31", guide.Next.Summary);
        Assert.Equal(2, guide.OtherChecks.Count);
        Assert.Single(guide.PassedChecks);
        Assert.Contains("CHECK PASSED", guide.PassedChecks[0].Label);
    }
    [Fact]
    public void Missing_folder_is_resolved_before_a_version_change()
    {
        var guide = TroubleshootingGuideViewModel.Create([
            new("Game installation", "Attention", "offline", "reconnect")], Version);
        Assert.Equal(HelpAction.LocateGame, guide.Next.Action);
        Assert.Equal(HelpAction.RecoverVersion, guide.OtherChecks.Single().Action);
    }
    [Fact]
    public void Headset_software_detection_never_becomes_a_green_headset_success()
    {
        var guide = TroubleshootingGuideViewModel.Create([
            new("VR runtime observation", "Check", "runtime detected", "check headset")]);
        Assert.Equal(HelpTone.Check, guide.Next.Tone);
        Assert.Empty(guide.PassedChecks);
        Assert.Contains("still need a test", guide.Next.Summary);
    }
    [Theory]
    [InlineData("Interrupted VR component update", TroubleshootingAction.RepairSetup)]
    [InlineData("Game executable", TroubleshootingAction.VerifyFiles)]
    [InlineData("VR components", TroubleshootingAction.VrSetup)]
    [InlineData("Optional mods and controls", TroubleshootingAction.Mods)]
    public void Simplifying_a_card_preserves_the_original_action_and_full_error(string title, TroubleshootingAction action)
    {
        var finding = new TroubleshootingFinding(title, "Attention", "precise file and version details", "complete original instructions", action);
        var card = TroubleshootingGuideViewModel.Create([finding]).Next;
        Assert.Same(finding, card.Finding);
        Assert.Equal(HelpAction.Finding, card.Action);
        Assert.Equal(finding.Detail, card.Detail);
        Assert.Equal(finding.Instructions, card.OriginalInstructions);
        Assert.InRange(card.Steps.Count, 1, 3);
        Assert.Equal(Enumerable.Range(1, card.Steps.Count), card.Steps.Select(step => step.Number));
    }
    [Fact]
    public void An_unknown_problem_keeps_its_details_without_inventing_a_repair()
    {
        var raw = new TroubleshootingFinding("Unfamiliar condition", "Attention", "specific error", "original fix");
        var card = TroubleshootingGuideViewModel.Create([raw]).Next;
        Assert.Equal(HelpAction.None, card.Action);
        Assert.Contains("Technical details", card.Steps[0].Text);
        Assert.Equal("original fix", card.OriginalInstructions);
    }
    [Fact]
    public void Passed_file_checks_do_not_claim_headset_readiness()
    {
        var guide = TroubleshootingGuideViewModel.Create([new("Game installation", "Passed", "folder", "folder check")]);
        Assert.Equal(HelpTone.Passed, guide.Next.Tone);
        Assert.Contains("does not confirm VR works", guide.Next.Summary);
    }
    [Fact]
    public void Every_semantic_color_has_a_visible_text_label()
    {
        foreach (var tone in Enum.GetValues<HelpTone>())
        {
            var card = new TroubleshootingCardViewModel("title", "summary", tone, []);
            Assert.True(card.Label.Length > 5);
            Assert.Contains(tone == HelpTone.Blocked ? "BLOCKED" : tone == HelpTone.Passed ? "PASSED" : tone == HelpTone.Check ? "CHECK" : "NEXT", card.Label);
        }
        Assert.NotEqual(LauncherThemeService.HelpColors(true)["HelpActionBrush"], LauncherThemeService.HelpColors(false)["HelpActionBrush"]);
    }
    [Theory, MemberData(nameof(ScopedCatalogueRefreshTests.Games), MemberType = typeof(ScopedCatalogueRefreshTests))]
    public void Every_catalogue_game_has_short_setup_steps(string slug, string _)
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null && !File.Exists(Path.Combine(directory.FullName, "config", "game-setup-guides.json"))) directory = directory.Parent;
        Assert.NotNull(directory);
        var guides = GameSetupGuides.Load(directory.FullName);
        Assert.True(guides.TryGetValue(slug, out var guide));
        Assert.InRange(guide.Steps.Count, 3, 8);
        Assert.All(guide.Steps, step => Assert.InRange(step.Length, 4, 150));
    }
}
