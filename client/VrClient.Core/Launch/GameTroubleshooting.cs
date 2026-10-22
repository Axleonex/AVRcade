using VrClient.Core.App;
using VrClient.Core.Config;
using VrClient.Core.Modpack;
using VrClient.Core.Redengine;

namespace VrClient.Core.Launch;

public enum TroubleshootingAction { None, RepairSetup, VerifyFiles, VrSetup, Mods, Controls, UpdateMods }

public sealed record TroubleshootingFinding(string Title, string Status, string Detail, string Instructions,
    TroubleshootingAction Action = TroubleshootingAction.None)
{
    public bool NeedsAttention => Status == "Attention";
    public bool HasAction => Action != TroubleshootingAction.None;
    public string ActionLabel => Action switch
    {
        TroubleshootingAction.RepairSetup => "Repair this game's setup",
        TroubleshootingAction.VerifyFiles => "Verify game files in Steam",
        TroubleshootingAction.VrSetup => "Open VR setup",
        TroubleshootingAction.Mods => "Open Mods",
        TroubleshootingAction.Controls => "Open Controls",
        TroubleshootingAction.UpdateMods => "Check compatible VR updates",
        _ => string.Empty
    };
}

public sealed record GameTroubleshootingReport(string Slug, string? InstallDir, DateTimeOffset CheckedAt,
    IReadOnlyList<TroubleshootingFinding> Findings)
{
    public bool NeedsAttention => Findings.Any(finding => finding.NeedsAttention);
}

/// Bounded, read-only checks. No logs, snapshots, downloads, file writes or repair side effects.
public sealed class GameTroubleshooting
{
    public GameTroubleshootingReport Check(string repoRoot, GameView game, RuntimeStatusView runtime)
    {
        var findings = new List<TroubleshootingFinding>(8);
        void Add(string title, string status, string detail, string instructions,
            TroubleshootingAction action = TroubleshootingAction.None) =>
            findings.Add(new(title, status, Limit(detail), Limit(instructions), action));
        GameTroubleshootingReport Report() => new(game.Slug, game.InstallDir, DateTimeOffset.Now, findings.Take(8).ToArray());
        try
        {
            if (game.InstallDir is not { } root || !Directory.Exists(root))
            {
                Add("Game installation", "Attention", "This game's installation folder is unavailable.",
                    game.OfflineSteamLibrary is not null
                        ? "Reconnect this game's Steam library drive, then use Refresh."
                        : "Install this game in Steam, or use Locate installed game on its page, then run diagnostics again.");
                return Report();
            }
            Add("Game installation", "Passed", "Checking only: " + root,
                "These results apply to this installation. Other games are not scanned or changed.");
            var executables = ControllerRuntimeBindings.Executables(repoRoot, game);
            if (executables.Count == 0)
                Add("Game executable", "Attention", "The expected executable was not found in this folder.",
                    "Check that you selected the correct Steam installation. Steam verification can restore missing original files.", TroubleshootingAction.VerifyFiles);
            else
            {
                var native = new NativeDependencyHealth().Check(root, executables);
                Add("Native game dependencies", native.Healthy ? "Passed" : "Attention",
                    native.Healthy ? native.Message : string.Join("; ", native.Issues.Take(3).Select(issue =>
                        $"{issue.Consumer}: {issue.Kind} {issue.Library}" + (issue.Symbol is null ? "" : $" ({issue.Symbol})"))),
                    native.Healthy
                        ? "The checked native imports match. This does not validate every game file or prove the game works in a headset."
                        : "For original game files, use Verify game files in Steam below. For a third-party mod DLL, update that mod and its requirements through its creator or manager; verification does not establish mod compatibility.");
            }
            if (File.Exists(ModUpdateTransaction.Resolve(root, ".avrcade/mod-updates/pending.json")))
                Add("Interrupted VR component update", "Attention", "This game has an unfinished managed update.",
                    "Close this game, then repair its setup to recover the existing update transaction. Saved preferences are preserved.", TroubleshootingAction.RepairSetup);
            if (game.Readiness is GameReadiness.Blocked)
                Add("Game and VR compatibility", "Attention", game.ReadinessDetail,
                    game.VersionCompatibility is not null ? GameVersionCompatibility.RecoveryInstructions
                        : "Review this game's VR setup and supported build requirements. A changed game build may need a compatible VR mod release; Steam verification is not a downgrade.",
                    game.VersionCompatibility is not null ? TroubleshootingAction.UpdateMods : TroubleshootingAction.VrSetup);
            else if (game.Readiness is GameReadiness.NeedsVrMod or GameReadiness.NeedsUevr || !game.ModInstalled)
                Add("VR components", "Attention", game.ReadinessDetail,
                    "Open this game's VR setup and complete its listed component or profile requirements. Diagnostics does not download anything.", TroubleshootingAction.VrSetup);
            else if (game.Readiness is GameReadiness.ProfileUnverified || game.ProfileImportRequired && !game.ProfileImported)
                Add("Game's VR profile", "Check", game.ReadinessDetail,
                    "Follow this game's VR setup instructions. Confirm the image, tracking and controls in your headset before treating VR as working.", TroubleshootingAction.VrSetup);
            if (game.Engine == GameEngine.Redengine && game.Slug == "cyberpunk-2077" &&
                game.ModInstalled && !CyberpunkNexusRequirements.Installed(root))
                Add("Cyberpunk VR requirements", "Attention", "More Occluders or its managed requirements are not installed for this copy.",
                    "Open VR setup and complete the More Occluders requirement for this installation.", TroubleshootingAction.VrSetup);
            if (runtime.SteamVrRunning || runtime.VirtualDesktopRunning)
                Add("VR runtime observation", "Check", "Detected runtime software: " + runtime.SelectedRuntime + ".",
                    "Runtime software being open does not confirm the headset display or tracking. Connect your headset and check this game's image and controls. Shared runtime settings are not changed.");
            else
                Add("VR runtime observation", "Attention", "SteamVR and Virtual Desktop were not detected running.",
                    "Start the VR software required by this game's setup and connect your headset, then run diagnostics again. Other runtime routes may need a manual check.", TroubleshootingAction.VrSetup);
            Add("Optional mods and controls", "Instructions", "Optional mod conflicts and controller behavior need a game-specific check.",
                "If the game opens but behaves incorrectly, review its Mods tab and mod requirements. Compare the four play modes to isolate optional mods; use Controls for missing or incorrect assignments.", TroubleshootingAction.Mods);
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException or
            ArgumentException or InvalidOperationException or System.Text.Json.JsonException)
        {
            Add("Check could not finish", "Attention", ex.Message,
                "Close this game, reconnect its drive if needed, and run diagnostics again. Check folder permissions if access is denied.");
        }
        return Report();
    }

    private static string Limit(string text) => text.Length <= 1800 ? text : text[..1800] + "…";
}
