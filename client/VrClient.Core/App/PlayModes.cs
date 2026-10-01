namespace VrClient.Core.App;

/// The four ways every catalogue game can be played.
public enum PlayMode { VrOnly, VrWithMods, ModsNoVr, Vanilla }

public enum PlayModeAvailability { Ready, NeedsSetup, Unavailable }

/// What the mode's button does when the mode is not ready to launch.
public enum PlayModeSetup { None, InstallVr, OpenVrSetup, OpenMods, ChooseCleanFolder }

public sealed record PlayModeState(
    PlayMode Mode, PlayModeAvailability Availability, string Status, string Action,
    PlayModeSetup Setup = PlayModeSetup.None, bool Caution = false)
{
    public bool CanLaunch => Availability is PlayModeAvailability.Ready;
    public bool CanAct => Availability is not PlayModeAvailability.Unavailable;
}

/// A manager profile as the planner needs it: what it is called and whether it
/// can be started as-is.
public sealed record PlayModeProfile(
    string Label, string ManagerName, bool? VrModPresent, bool? LoaderPresent,
    bool HasOtherMods, bool DirectLaunch, bool LoaderLinkedInGame);

public sealed record UnityPlayFacts(
    bool GameInstalled, bool LaunchAllowed, bool ConversionInstalled, bool ConversionIsVrOnly,
    PlayModeProfile? VrProfile, PlayModeProfile? FlatProfile,
    bool AnyManagerInstalled, bool RuntimeDetected, bool RequiresSteamVr, bool SteamVrRunning);

public sealed record UnrealPlayFacts(
    bool GameInstalled, bool LaunchAllowed, bool VrToolsReady, bool ProfileReady,
    int ModFileCount, bool RuntimeDetected);

/// VrBlockedReason is set when the installed game build is not one the VR
/// backend supports; that rules out VR but not monitor play.
/// BackendInstallable says this copy of AVRcade carries the backend files to install.
public sealed record CyberpunkPlayFacts(
    bool GameInstalled, string? VrBlockedReason, bool VrBackendInstalled, bool RuntimeDetected,
    bool BackendInstallable);

public sealed record GtaPlayFacts(
    bool GameInstalled, string? VrBlockedReason, bool VrBridgeInstalled, bool CleanFolderReady,
    bool RuntimeDetected);

/// Pure rules for which of the four modes can start right now and, when one
/// cannot, the single next step. Every game always gets all four modes, in the
/// same order; nothing here touches the disk or starts a process.
public static class PlayModePlanner
{
    private const string NeedsGame = "Install the game in Steam first";
    private const string Blocked = "Blocked by AVRcade's safety policy";
    private const string NoRuntime = "No SteamVR or Virtual Desktop detected. Start your headset software first";

    public static IReadOnlyList<PlayModeState> Unity(UnityPlayFacts f)
    {
        if (Gate(f.GameInstalled, f.LaunchAllowed) is { } gate)
            return All(gate);

        var runtimeNote = f.RequiresSteamVr && !f.SteamVrRunning
            ? "Start SteamVR first" : f.RuntimeDetected ? null : NoRuntime;

        PlayModeState vrOnly;
        if (!f.ConversionInstalled)
            vrOnly = Setup(PlayMode.VrOnly, "VR mod not installed yet", "Install VR mod", PlayModeSetup.InstallVr);
        else if (!f.ConversionIsVrOnly)
            vrOnly = Unavailable(PlayMode.VrOnly, "Other mods share the game folder. Use VR + mods");
        else
            vrOnly = Ready(PlayMode.VrOnly, "VR mod installed", "Play in VR", runtimeNote);

        PlayModeState vrMods;
        if (f.VrProfile is { } vr)
            vrMods = vr.VrModPresent == false ? ModsSetup(PlayMode.VrWithMods, $"{vr.Label} has no VR mod. Add it in {vr.ManagerName}")
                : vr.LoaderPresent == false ? ModsSetup(PlayMode.VrWithMods, $"{vr.Label} has no mod loader. Check it in {vr.ManagerName}")
                : !vr.DirectLaunch ? Handoff(PlayMode.VrWithMods, vr, runtimeNote)
                : !vr.LoaderLinkedInGame ? NotLinked(PlayMode.VrWithMods, vr)
                : Ready(PlayMode.VrWithMods, vr.Label, "Play in VR with mods", runtimeNote);
        else if (f.ConversionInstalled && !f.ConversionIsVrOnly)
            vrMods = Ready(PlayMode.VrWithMods, "Uses the mods in the game folder", "Play in VR with mods", runtimeNote);
        else
            vrMods = ModsSetup(PlayMode.VrWithMods, f.AnyManagerInstalled
                ? "Pick a mod manager profile that includes the VR mod"
                : "Needs a mod manager with a VR profile");

        PlayModeState flatMods;
        if (f.FlatProfile is { } flat)
            flatMods = flat.VrModPresent == true ? ModsSetup(PlayMode.ModsNoVr, $"{flat.Label} includes the VR mod. Pick a profile without it")
                // A manager that does not expose its mod list (Vortex) is handed over to as-is.
                : flat.VrModPresent is null && !flat.DirectLaunch ? Handoff(PlayMode.ModsNoVr, flat)
                : flat.LoaderPresent != true || !flat.HasOtherMods ? ModsSetup(PlayMode.ModsNoVr, $"{flat.Label} needs a mod loader and at least one mod")
                : !flat.DirectLaunch ? Handoff(PlayMode.ModsNoVr, flat)
                : !flat.LoaderLinkedInGame ? NotLinked(PlayMode.ModsNoVr, flat)
                : Ready(PlayMode.ModsNoVr, flat.Label, "Play with mods");
        else
            flatMods = ModsSetup(PlayMode.ModsNoVr, f.AnyManagerInstalled
                ? "Pick a mod manager profile without the VR mod"
                : "Needs a mod manager with a mod profile");

        return [vrOnly, vrMods, flatMods,
            Ready(PlayMode.Vanilla, "Mod loader and VR are switched off for this launch", "Play vanilla")];
    }

    private static PlayModeState ModsSetup(PlayMode mode, string status) =>
        Setup(mode, status, "Set up mods", PlayModeSetup.OpenMods);

    /// AVRcade cannot start this profile itself, so the button opens its manager.
    private static PlayModeState Handoff(PlayMode mode, PlayModeProfile profile, string? caution = null) =>
        Ready(mode, $"{profile.Label}. Start the game from {profile.ManagerName}", $"Open {profile.ManagerName}", caution);

    private static PlayModeState NotLinked(PlayMode mode, PlayModeProfile profile) =>
        ModsSetup(mode, $"Press Start modded once in {profile.ManagerName} so it adds its mod loader to the game");

    public static IReadOnlyList<PlayModeState> Unreal(UnrealPlayFacts f)
    {
        if (Gate(f.GameInstalled, f.LaunchAllowed) is { } gate)
            return All(gate);

        var hasMods = f.ModFileCount > 0;
        var runtimeNote = f.RuntimeDetected ? null : NoRuntime;
        var found = f.ModFileCount == 1 ? "1 mod file in the game folder" : $"{f.ModFileCount} mod files in the game folder";
        var vrSetup = !f.VrToolsReady
            ? "VR tools not downloaded yet" : !f.ProfileReady ? "VR profile not installed yet" : null;

        var vrOnly = vrSetup is not null
            ? Setup(PlayMode.VrOnly, vrSetup, "Download VR setup", PlayModeSetup.InstallVr)
            : hasMods ? Unavailable(PlayMode.VrOnly, "Mods are in the game folder. Use VR + mods")
            : Ready(PlayMode.VrOnly, "VR tools and profile ready", "Play in VR", runtimeNote);
        var vrMods = vrSetup is not null
            ? Setup(PlayMode.VrWithMods, vrSetup, "Download VR setup", PlayModeSetup.InstallVr)
            : !hasMods ? ModsSetup(PlayMode.VrWithMods, "No mods found in the game folder yet")
            : Ready(PlayMode.VrWithMods, found, "Play in VR with mods", runtimeNote);
        var flatMods = hasMods
            ? Ready(PlayMode.ModsNoVr, found, "Play with mods")
            : ModsSetup(PlayMode.ModsNoVr, "No mods found in the game folder yet");
        var vanilla = hasMods
            ? Unavailable(PlayMode.Vanilla, "Mods are in the game folder. Disable them in your mod manager to play vanilla")
            : Ready(PlayMode.Vanilla, "Normal Steam launch, no VR", "Play vanilla");
        return [vrOnly, vrMods, flatMods, vanilla];
    }

    public static IReadOnlyList<PlayModeState> Cyberpunk(CyberpunkPlayFacts f)
    {
        if (!f.GameInstalled)
            return All(NeedsGame);

        var runtimeNote = f.RuntimeDetected ? null : NoRuntime;
        PlayModeState Vr(PlayMode mode, string status, string action) =>
            f.VrBlockedReason is { } blocked ? Unavailable(mode, blocked)
            : f.VrBackendInstalled ? Ready(mode, status, action, runtimeNote)
            : f.BackendInstallable
                ? Setup(mode, "VR backend not installed yet", "Install VR backend", PlayModeSetup.InstallVr)
                : Setup(mode, "This copy of AVRcade does not include the VR backend", "See VR setup", PlayModeSetup.OpenVrSetup);
        return [
            Vr(PlayMode.VrOnly, "VR backend installed. REDmod mods stay off", "Play in VR"),
            Vr(PlayMode.VrWithMods, "Loads REDmod and the mods deployed to the game folder", "Play in VR with mods"),
            Ready(PlayMode.ModsNoVr, "Monitor play with REDmod and deployed mods", "Play with mods"),
            Ready(PlayMode.Vanilla, "Monitor play, VR off, REDmod mods off", "Play vanilla")
        ];
    }

    public static IReadOnlyList<PlayModeState> GtaSanAndreas(GtaPlayFacts f)
    {
        // The classic version is no longer sold on Steam; the player points AVRcade at it.
        if (!f.GameInstalled)
            return All("Point AVRcade at your San Andreas folder first");

        var runtimeNote = f.RuntimeDetected ? null : NoRuntime;
        PlayModeState Vr(PlayMode mode, string status, string action) =>
            f.VrBlockedReason is { } blocked ? Unavailable(mode, blocked)
            : f.VrBridgeInstalled ? Ready(mode, status, action, runtimeNote)
            : Setup(mode, "VR bridge not installed yet", "Install VR bridge", PlayModeSetup.InstallVr);
        return [
            Vr(PlayMode.VrOnly, "ModLoader add-ons are switched off for this launch", "Play in VR"),
            Vr(PlayMode.VrWithMods, "Uses the mods installed in the game folder", "Play in VR with mods"),
            Ready(PlayMode.ModsNoVr, "Monitor play with the mods in the game folder", "Play with mods"),
            f.CleanFolderReady
                ? Ready(PlayMode.Vanilla, "Uses your separate clean game folder", "Play vanilla")
                : Setup(PlayMode.Vanilla, "Needs a separate, unmodded copy of the game", "Choose clean folder", PlayModeSetup.ChooseCleanFolder)
        ];
    }

    private static string? Gate(bool installed, bool allowed) =>
        !allowed ? Blocked : !installed ? NeedsGame : null;

    private static IReadOnlyList<PlayModeState> All(string reason) =>
        Enum.GetValues<PlayMode>().Select(mode => Unavailable(mode, reason)).ToArray();

    private static PlayModeState Ready(PlayMode mode, string status, string action, string? caution = null) =>
        new(mode, PlayModeAvailability.Ready, caution ?? status, action, Caution: caution is not null);

    private static PlayModeState Setup(PlayMode mode, string status, string action, PlayModeSetup setup) =>
        new(mode, PlayModeAvailability.NeedsSetup, status, action, setup);

    private static PlayModeState Unavailable(PlayMode mode, string status) =>
        new(mode, PlayModeAvailability.Unavailable, status, "Not available");
}
