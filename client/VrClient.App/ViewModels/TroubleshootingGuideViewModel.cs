using Avalonia.Media;
using VrClient.App.Services;
using VrClient.Core.Compatibility;
using VrClient.Core.Launch;

namespace VrClient.App.ViewModels;

public enum HelpTone { Next, Check, Passed, Blocked }
public enum HelpAction { None, CheckGame, Finding, RecoverVersion, OpenSetup, LocateGame }
public sealed record HelpInstruction(int Number, string Text);

/// Presentation only: the original finding and its action remain intact.
public sealed class TroubleshootingCardViewModel(string title, string summary, HelpTone tone,
    IEnumerable<string> steps, HelpAction action = HelpAction.None, string actionLabel = "",
    TroubleshootingFinding? finding = null, int priority = 10)
{
    public string Title { get; } = title;
    public string Summary { get; } = summary;
    public HelpTone Tone { get; } = tone;
    public IReadOnlyList<HelpInstruction> Steps { get; } = steps.Select((text, index) => new HelpInstruction(index + 1, text)).ToArray();
    public HelpAction Action { get; } = action;
    public string ActionLabel { get; } = actionLabel;
    public TroubleshootingFinding? Finding { get; } = finding;
    public int Priority { get; } = priority;
    public bool HasAction => Action != HelpAction.None;
    public bool HasTechnicalDetails => Finding is not null;
    public string Detail => Finding?.Detail ?? "";
    public string OriginalInstructions => Finding?.Instructions ?? "";
    public string Label => Tone switch
    {
        HelpTone.Blocked => "■ VR BLOCKED", HelpTone.Check => "! CHECK NEEDED",
        HelpTone.Passed => "✓ CHECK PASSED", _ => "→ NEXT STEP"
    };
    public IBrush ToneBrush => LauncherThemeService.Brush(Tone switch
    {
        HelpTone.Blocked => "HelpStopBrush", HelpTone.Check => "HelpWarningBrush",
        HelpTone.Passed => "HelpSuccessBrush", _ => "HelpActionBrush"
    });
}

public sealed record TroubleshootingGuideViewModel(TroubleshootingCardViewModel Next,
    IReadOnlyList<TroubleshootingCardViewModel> OtherChecks, IReadOnlyList<TroubleshootingCardViewModel> PassedChecks)
{
    public string OtherChecksLabel => $"Other checks ({OtherChecks.Count})";
    public string PassedChecksLabel => $"Passed checks ({PassedChecks.Count})";
    public bool HasOtherChecks => OtherChecks.Count > 0;
    public bool HasPassedChecks => PassedChecks.Count > 0;

    public static TroubleshootingGuideViewModel Create(IReadOnlyList<TroubleshootingFinding> findings, GameVersionRecovery? version = null)
    {
        var cards = findings.Select(Describe).ToList();
        if (version is not null)
        {
            cards.RemoveAll(card => card.Finding?.Title == "Game and VR compatibility");
            cards.Add(new("Choose a compatible game version",
                $"Installed: {version.InstalledBuild}. VR needs: {version.RequiredBuild}.", HelpTone.Blocked,
                ["Back up your saves before changing versions.", "Choose a supported game version or an existing compatible copy."],
                HelpAction.RecoverVersion, "Choose compatible version", priority: 1));
        }
        var ordered = cards.Where(card => card.Tone != HelpTone.Passed).OrderBy(card => card.Priority).ToList();
        var next = ordered.FirstOrDefault() ?? (findings.Count == 0
            ? new TroubleshootingCardViewModel("Find the next fix", "Start with this game's selected installation.", HelpTone.Next,
                ["Choose Check this game.", "Follow the highlighted step.", "Check again after making a change."], HelpAction.CheckGame, "Check this game")
            : new TroubleshootingCardViewModel("These checks passed", "A passed file check does not confirm VR works.", HelpTone.Passed,
                ["Open the game in your headset.", "Check the image, tracking and controls."], HelpAction.OpenSetup, "Open VR setup"));
        return new(next, ordered.Where(card => !ReferenceEquals(card, next)).ToArray(),
            cards.Where(card => card.Tone == HelpTone.Passed).ToArray());
    }

    private static TroubleshootingCardViewModel Describe(TroubleshootingFinding finding)
    {
        if (finding.Status == "Passed")
            return new(finding.Title, "This check passed. Headset testing is separate.", HelpTone.Passed, [], finding: finding);
        TroubleshootingCardViewModel Card(string title, string summary, string[] steps, int priority,
            HelpTone tone = HelpTone.Check, HelpAction? action = null, string? label = null) =>
            new(title, summary, tone, steps, action ?? (finding.HasAction ? HelpAction.Finding : HelpAction.None),
                label ?? finding.ActionLabel, finding, priority);
        return finding.Title switch
        {
            "Game installation" => Card("Find the game folder", "The selected game folder is unavailable.",
                ["Reconnect its drive, if needed.", "Choose the game's installed folder.", "Check this game again."], 0,
                action: HelpAction.LocateGame, label: "Choose game folder"),
            "Installation changed" => Card("Confirm the game copy", "The selected installation changed during the check.",
                ["Confirm the folder shown on this page.", "Check this game again."], 0, action: HelpAction.CheckGame, label: "Check this game"),
            "Game executable" => Card("Repair missing game files", "The expected game program was not found.",
                ["Close the game.", "Verify its files in Steam.", "Wait for Steam to finish, then check again."], 0),
            "Game and VR compatibility" => Card("VR needs a compatibility check", "The current game and VR setup cannot be used together yet.",
                ["Open VR setup.", "Check the required game build and VR package."], 1, HelpTone.Blocked),
            "Interrupted VR component update" => Card("Finish the interrupted update", "An earlier VR update did not finish.",
                ["Close the game.", "Choose Repair setup.", "Check this game again."], 2),
            "Native game dependencies" => Card("Check a game or mod file", "A required file may be missing or incompatible.",
                ["Close the game.", "Read the named file under Technical details.", "Repair an original game file in Steam, or update the named mod."], 3),
            "Cyberpunk VR requirements" => Card("Add the missing VR requirement", "This copy needs More Occluders or one of its requirements.",
                ["Open VR setup.", "Complete the missing requirement.", "Check this game again."], 4),
            "VR components" => Card("Finish VR setup", "A VR component or game profile is missing.",
                ["Open VR setup.", "Complete the highlighted setup step.", "Check this game again."], 4),
            "VR runtime observation" when finding.NeedsAttention => Card("Connect your headset", "The expected headset software was not detected.",
                ["Start the headset software named in VR setup.", "Connect your headset.", "Check this game again."], 5),
            "VR runtime observation" => Card("Check inside your headset", "Headset software was detected. The image and tracking still need a test.",
                ["Put on your headset.", "Check the game image, tracking and controls."], 7),
            "Game's VR profile" => Card("Check the VR profile", "This game's VR profile needs a manual check.",
                ["Open VR setup.", "Follow the profile step.", "Test the image and controls in your headset."], 6),
            "Optional mods and controls" => Card("If the game opens but behaves oddly", "Use this check for extra mods or incorrect controls.",
                ["Compare VR only with VR + mods.", "Review extra mods in the Mods tab."], 9, HelpTone.Next),
            "Setup recheck" or "Action result" => Card("Check the result, then check again", "The full action result is under Technical details.",
                ["Wait for any download or repair to finish.", "Choose Check this game again."], 4,
                action: HelpAction.CheckGame, label: "Check this game"),
            _ => Card("A check needs attention", "Read this check's details before changing files.",
                ["Open Technical details below.", "Follow the reported fix.", "Check this game again."], 3)
        };
    }
}
