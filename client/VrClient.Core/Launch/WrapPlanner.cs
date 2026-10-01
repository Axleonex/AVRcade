namespace VrClient.Core.Launch;

/// LI-1: the flat-play toggle for the Steam launch-option wrapper.
public enum WrapVrMode { Auto, On, Off }

/// LI-1: what the wrapper should do for one launch. Command tokens pass through
/// verbatim (mod-manager doorstop args preserved); flat mode only APPENDS
/// --disable-vr (RepoXR honors it; Unity ignores unknown args).
public sealed record WrapPlan(
    IReadOnlyList<string> Command,
    string? PinRuntimeJsonPath,
    string? PinRuntimeName,
    bool FlatMode,
    string Reason);

/// Pure decision core for `vrclient wrap [--vr on|off|auto] -- %command%`.
/// The CLI shell does all I/O (cfg pin write, spawn, wait); this class only
/// decides flat-vs-VR and which runtime to pin, so it is unit-testable headless.
public static class WrapPlanner
{
    public const string DisableVrArg = "--disable-vr";

    /// AVRcade adds this to a launch the player explicitly asked to be VR ("VR +
    /// mods"). The wrapper then never guesses flat from a missing SteamVR/Virtual
    /// Desktop process, and removes the marker before the game sees it. Without
    /// the wrapper installed the game simply ignores the unknown argument.
    public const string ForceVrArg = "--avrcade-vr";

    public static WrapVrMode ParseMode(string? text) => text?.ToLowerInvariant() switch
    {
        "on" => WrapVrMode.On,
        "off" => WrapVrMode.Off,
        _ => WrapVrMode.Auto,
    };

    public static WrapPlan Plan(
        IReadOnlyList<string> command,
        WrapVrMode mode,
        IReadOnlyCollection<string> runningVrProcessNames,
        IReadOnlyList<XrRuntimeChoice> availableRuntimes)
    {
        if (command.Count == 0)
            throw new ArgumentException("wrap: empty command after --", nameof(command));

        if (command.Contains(ForceVrArg, StringComparer.OrdinalIgnoreCase))
        {
            command = command.Where(token => !token.Equals(ForceVrArg, StringComparison.OrdinalIgnoreCase)).ToList();
            // A flat launch forced in the Steam launch option itself still wins.
            if (mode == WrapVrMode.Auto) mode = WrapVrMode.On;
        }

        var doorstopDisabled = DoorstopProfile.IsExplicitlyDisabled(command);
        // "Mods, no VR" and "Vanilla" already carry the switch; it is never added twice.
        var flatRequested = command.Contains(DisableVrArg, StringComparer.OrdinalIgnoreCase);
        var runtime = OpenXrRuntimeSelector.Select(runningVrProcessNames, availableRuntimes);
        var useVr = !doorstopDisabled && !flatRequested && (mode switch
        {
            WrapVrMode.On => true,
            WrapVrMode.Off => false,
            // auto: the headset posture IS the toggle - no VR runtime running means flat.
            _ => runtime is not null,
        });

        if (!useVr)
            return new WrapPlan(
                flatRequested ? command.ToList() : command.Append(DisableVrArg).ToList(),
                null, null, FlatMode: true,
                doorstopDisabled ? "doorstop disabled (vanilla)"
                    : flatRequested ? "flat launch requested"
                    : mode == WrapVrMode.Off ? "vr=off (forced flat)"
                    : "vr=auto and no VR runtime running (flat)");

        return new WrapPlan(
            command.ToList(), runtime?.JsonPath, runtime?.Name, FlatMode: false,
            runtime is null
                ? "vr=on but no VR runtime process detected; leaving the mod's own selection"
                : $"pin={runtime.Name}");
    }
}
