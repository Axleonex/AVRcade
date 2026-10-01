using System.Diagnostics;
using System.Text;
using VrClient.Core.Launch;
using VrClient.Core.Model;

namespace VrClient.Core.ModManagers;

public sealed record ModdedLaunchResult(bool GameLaunchRequested, bool ManagerOpened,
    string Message);

public enum ModdedLaunchMode { VrWithMods, DesktopWithMods }

public sealed class ModdedVrLauncher
{
    public static IReadOnlyList<string> VanillaLaunchArguments =>
        ["--doorstop-enable", "false", "--doorstop-enabled", "false", WrapPlanner.DisableVrArg];

    public ModdedLaunchResult LaunchVanilla(string gameSlug, Verdict safety, bool acknowledge,
        bool dryRun = false, string? steamRoot = null)
    {
        if (!CommunityVrRoutes.All.TryGetValue(gameSlug, out var route))
            return new(false, false, "No supported Friendslop Steam route is defined for this game.");
        if (safety.IsBlocked())
            return new(false, false, "Safety policy blocks this launch.");
        if (safety == Verdict.Warn && !acknowledge)
            return new(false, false, "Review and acknowledge the private-session warning first.");
        var steam = new SteamOwnedLauncher().Launch(route.AppId, steamRoot, dryRun,
            launchArguments: VanillaLaunchArguments);
        return steam.Success
            ? new(true, false, dryRun
                ? $"Validated vanilla Steam arguments for AppID {route.AppId}; BepInEx and VR disable flags are present, but game behavior is unverified."
                : $"Steam vanilla request sent for AppID {route.AppId} with BepInEx and VR disable flags. Confirm the game remains flat; other manually installed game-file mods cannot be ruled out.")
            : new(false, false, steam.FailureText);
    }

    /// Whether the manager has already placed this profile's loader beside the game.
    public static bool IsLoaderLinked(string? gameDirectory, ModManagerProfile profile) =>
        profile.Directory is { } directory && DoorstopProfile.IsLinkedIntoGame(gameDirectory, directory);

    public ModdedLaunchResult Launch(ModManagerProfile profile, string? r2Arguments,
        Verdict safety, bool acknowledge, bool headsetConnected, bool dryRun = false,
        string? steamRoot = null, ModdedLaunchMode mode = ModdedLaunchMode.VrWithMods,
        string? gameDirectory = null)
    {
        if (!CommunityVrRoutes.All.TryGetValue(profile.GameSlug, out var route))
            return new(false, false, "No community VR route is defined for this game.");
        if (!File.Exists(profile.Manager.LaunchPath))
            return new(false, false, "The selected mod manager is no longer installed. Refresh profiles.");
        if (safety.IsBlocked())
            return new(false, false, "Safety policy blocks this launch.");
        // A manager that hides its mod list (Vortex) cannot be checked here; that
        // profile is handed over to the manager below instead of being refused.
        if (mode == ModdedLaunchMode.DesktopWithMods && profile.VrModPresent == true)
            return new(false, false, "Desktop with mods needs a profile with this game's VR mod disabled. AVRcade cannot prove flat mode for this profile.");
        if (mode == ModdedLaunchMode.DesktopWithMods && profile.VrModPresent == false && !profile.HasOtherMods)
            return new(false, false, "Desktop with mods needs at least one enabled non-loader mod in this profile.");

        if (TryResolveProfileArguments(profile, r2Arguments, mode, out var directArgs))
        {
            if (gameDirectory is not null && !IsLoaderLinked(gameDirectory, profile))
                return new(false, false,
                    $"The game folder has no mod loader yet. Press Start modded once in {profile.Manager.DisplayName} so it adds one, then launch from AVRcade.");
            if (safety == Verdict.Warn && !acknowledge)
                return new(false, false, "Review and acknowledge the private modded session warning first.");
            if (mode == ModdedLaunchMode.VrWithMods && !headsetConnected)
                return new(false, false, "Connect a VR headset and start its runtime before launching.");
            if (mode == ModdedLaunchMode.VrWithMods && route.RequiresSteamVr && !OpenXrRuntimeSelector.IsSteamVrRunning(
                    OpenXrRuntimeSelector.GetRunningVrProcessNames()))
                return new(false, false, $"{route.Package} requires SteamVR to be running.");

            var steam = new SteamOwnedLauncher().Launch(route.AppId, steamRoot, dryRun,
                launchArguments: directArgs);
            return steam.Success
                ? new(true, false, dryRun
                    ? $"Validated Steam launch for {profile.DisplayName}. Game and headset result not verified."
                    : mode == ModdedLaunchMode.DesktopWithMods
                        ? $"Steam desktop launch requested for {profile.DisplayName}. Confirm the game stayed flat; mod compatibility is unverified."
                        : $"Steam launch requested for {profile.DisplayName}. Confirm VR loaded in the headset; mod compatibility is unverified.")
                : new(false, false, steam.FailureText);
        }

        var guidance = HandoffText(profile, route, mode);
        if (dryRun)
            return new(false, true, guidance);
        try
        {
            var start = new ProcessStartInfo(profile.Manager.LaunchPath)
            {
                UseShellExecute = profile.Manager.Kind == ModManagerKind.Thunderstore
            };
            if (profile.Manager.Kind == ModManagerKind.Vortex)
            {
                start.ArgumentList.Add("--game");
                start.ArgumentList.Add(profile.ManagerGameId);
                start.ArgumentList.Add("--profile");
                start.ArgumentList.Add(profile.Id);
            }
            if (Process.Start(start) is null)
                return new(false, false, "Could not open the selected mod manager.");
            return new(false, true, guidance);
        }
        catch (Exception ex)
        {
            return new(false, false, $"Could not open {profile.Manager.DisplayName}: {ex.Message}");
        }
    }

    private static string HandoffText(ModManagerProfile profile, CommunityVrRoute route, ModdedLaunchMode mode)
    {
        var setup = mode == ModdedLaunchMode.DesktopWithMods
            ? "Keep the VR mod disabled in this profile. "
            : profile.VrModPresent == false
            ? $"Add {route.PackageKey} to this profile and let the manager resolve dependencies. "
            : profile.LoaderPresent == false
                ? "Check this profile's mod loader dependency in the manager. "
                : profile.VrModPresent is null
                    ? "Confirm the VR mod and loader in the selected profile. "
                    : string.Empty;
        var handoff = profile.Manager.Kind switch
        {
            ModManagerKind.Vortex => $"Requested {profile.Name} in Vortex. Confirm the game and active profile, deploy mods, then use Vortex's Play action when ready.",
            ModManagerKind.R2Modman => $"Opened r2modman. Select {profile.ManagerGameId} → {profile.Name}, then press Start Modded when ready.",
            _ => $"Opened Thunderstore Mod Manager. Select {profile.ManagerGameId} → {profile.Name}, then press Start Modded when ready."
        };
        return setup + handoff + " AVRcade has not launched the game. Use private, consenting modded multiplayer sessions only.";
    }

    /// Accept the selected profile's manager-generated Doorstop arguments when
    /// the player explicitly supplied custom options. Otherwise derive the
    /// standard arguments from a known BepInEx loader in that profile.
    public static bool TryResolveProfileArguments(ModManagerProfile profile, string? saved,
        out IReadOnlyList<string> arguments)
        => TryResolveProfileArguments(profile, saved, ModdedLaunchMode.VrWithMods, out arguments);

    public static bool TryResolveProfileArguments(ModManagerProfile profile, string? saved,
        ModdedLaunchMode mode, out IReadOnlyList<string> arguments)
    {
        arguments = [];
        if (profile.Manager.Kind is not (ModManagerKind.R2Modman or ModManagerKind.Thunderstore) ||
            profile.VrModPresent != (mode == ModdedLaunchMode.VrWithMods) || profile.LoaderPresent != true ||
            profile.Directory is null ||
            !CommunityVrRoutes.All.ContainsKey(profile.GameSlug)) return false;
        if (!string.IsNullOrWhiteSpace(saved))
        {
            if (profile.Manager.Kind != ModManagerKind.R2Modman ||
                !TryParseR2Arguments(profile, saved, out arguments, out _)) return false;
        }
        else
        {
            // The same arguments r2modman builds for a Doorstop 4 profile.
            if (DoorstopProfile.FindLoader(profile.Directory) is not { } target) return false;
            arguments = ["--doorstop-enabled", "true", "--doorstop-target-assembly",
                Path.GetFullPath(target)];
            var corlibs = Path.Combine(profile.Directory, "unstripped_corlib");
            if (Directory.Exists(corlibs))
                arguments = [.. arguments, "--doorstop-mono-dll-search-path-override", Path.GetFullPath(corlibs)];
        }

        // One trailing switch tells the mod, and AVRcade's Steam wrapper, which mode this is.
        var modeArgument = mode == ModdedLaunchMode.DesktopWithMods
            ? WrapPlanner.DisableVrArg : WrapPlanner.ForceVrArg;
        if (!arguments.Contains(modeArgument, StringComparer.OrdinalIgnoreCase))
            arguments = [.. arguments, modeArgument];
        return true;
    }

    /// Accept only the selected profile's manager-generated Doorstop arguments.
    /// They are passed as process arguments, never through a command shell.
    public static bool TryParseR2Arguments(ModManagerProfile profile, string? input,
        out IReadOnlyList<string> arguments, out string refusal)
    {
        arguments = [];
        refusal = "Copy the selected profile's 'Launching the game from outside the mod manager' arguments from r2modman Help into AVRcade.";
        if (profile.Manager.Kind != ModManagerKind.R2Modman || profile.Directory is null ||
            string.IsNullOrWhiteSpace(input)) return false;
        if (!TrySplitArguments(input, out var tokens) || tokens.Count == 0 ||
            tokens.Any(t => t.Contains("%command%", StringComparison.OrdinalIgnoreCase)))
        {
            refusal = "Paste only the launch arguments from r2modman Help, without a shell command or %command%.";
            return false;
        }
        if (CountFlag(tokens, "--doorstop-target") +
            CountFlag(tokens, "--doorstop-target-assembly") != 1 ||
            CountFlag(tokens, "--doorstop-enable") +
            CountFlag(tokens, "--doorstop-enabled") != 1 ||
            CountFlag(tokens, "--r2profile") > 1)
        {
            refusal = "The copied launch arguments contain ambiguous Doorstop or profile settings.";
            return false;
        }
        var target = ValueAfter(tokens, "--doorstop-target") ??
                     ValueAfter(tokens, "--doorstop-target-assembly");
        var enabled = ValueAfter(tokens, "--doorstop-enable") ??
                      ValueAfter(tokens, "--doorstop-enabled");
        var profileName = ValueAfter(tokens, "--r2profile");
        var expectedRoot = Path.GetFullPath(profile.Directory).TrimEnd(Path.DirectorySeparatorChar) +
                           Path.DirectorySeparatorChar;
        if (target is null || enabled != "true" ||
            (profileName is not null && profileName != profile.Name) ||
            !TargetMatchesProfile(target, expectedRoot) ||
            !DoorstopProfile.IsLoaderAssembly(target) ||
            !File.Exists(target))
        {
            refusal = "The launch arguments do not point to this profile's installed BepInEx loader. Copy them again from this profile's r2modman Help view.";
            return false;
        }
        arguments = tokens;
        refusal = string.Empty;
        return true;
    }

    private static string? ValueAfter(IReadOnlyList<string> args, string flag)
    {
        for (var i = 0; i + 1 < args.Count; i++)
            if (string.Equals(args[i], flag, StringComparison.OrdinalIgnoreCase))
                return args[i + 1];
        return null;
    }

    private static int CountFlag(IReadOnlyList<string> args, string flag) =>
        args.Count(value => string.Equals(value, flag, StringComparison.OrdinalIgnoreCase));

    private static bool TargetMatchesProfile(string target, string expectedRoot)
    {
        try
        {
            return Path.IsPathFullyQualified(target) &&
                Path.GetFullPath(target).StartsWith(expectedRoot, StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return false;
        }
    }

    private static bool TrySplitArguments(string input, out IReadOnlyList<string> args)
    {
        var list = new List<string>();
        var word = new StringBuilder();
        var quoted = false;
        foreach (var c in input)
        {
            if (c == '"') { quoted = !quoted; continue; }
            if (char.IsWhiteSpace(c) && !quoted)
            {
                if (word.Length > 0) { list.Add(word.ToString()); word.Clear(); }
            }
            else word.Append(c);
        }
        if (word.Length > 0) list.Add(word.ToString());
        args = list;
        return !quoted && list.Count > 0 && list[0].StartsWith('-') &&
            !list.Any(t => t is "&" or "|" or ";" or ">" or "<");
    }
}
