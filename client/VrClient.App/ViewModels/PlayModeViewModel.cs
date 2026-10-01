using Avalonia.Media;
using VrClient.App.Services;
using VrClient.Core.App;

namespace VrClient.App.ViewModels;

/// One of the four "Choose how to play" cards.
public sealed class PlayModeViewModel(PlayModeState state)
{
    // 24x24 stroke icons: a headset for the VR modes, a monitor for the flat ones;
    // the modded variants carry a small plus.
    private const string Headset = "M3,9 a3,3 0 0 1 3,-3 h12 a3,3 0 0 1 3,3 v6 a3,3 0 0 1 -3,3 h-3 l-2,-3 h-2 l-2,3 h-3 a3,3 0 0 1 -3,-3 z";
    private const string Monitor = "M5,4 h14 a2,2 0 0 1 2,2 v8 a2,2 0 0 1 -2,2 h-14 a2,2 0 0 1 -2,-2 v-8 a2,2 0 0 1 2,-2 z M8,20 h8 M12,16 v4";
    private const string Plus = " M19,0.5 v4 M17,2.5 h4";
    private static readonly Geometry HeadsetIcon = Geometry.Parse(Headset);
    private static readonly Geometry HeadsetPlusIcon = Geometry.Parse(Headset + Plus);
    private static readonly Geometry MonitorIcon = Geometry.Parse(Monitor);
    private static readonly Geometry MonitorPlusIcon = Geometry.Parse(Monitor + Plus);

    public PlayModeState State { get; } = state;
    public PlayMode Mode => State.Mode;
    public bool IsVr => Mode is PlayMode.VrOnly or PlayMode.VrWithMods;

    public string Title => Mode switch
    {
        PlayMode.VrOnly => "VR only",
        PlayMode.VrWithMods => "VR + mods",
        PlayMode.ModsNoVr => "Mods, no VR",
        _ => "Vanilla"
    };

    public string Description => Mode switch
    {
        PlayMode.VrOnly => "The game in your headset with just the VR conversion. No other mods.",
        PlayMode.VrWithMods => "VR together with the mods from your mod manager.",
        PlayMode.ModsNoVr => "On your monitor with your mods. VR stays off.",
        _ => "The game as it shipped. No mods, no VR."
    };

    public Geometry Icon => Mode switch
    {
        PlayMode.VrOnly => HeadsetIcon,
        PlayMode.VrWithMods => HeadsetPlusIcon,
        PlayMode.ModsNoVr => MonitorPlusIcon,
        _ => MonitorIcon
    };

    public string Status => State.Status;
    public string ActionText => State.Action;
    public bool CanAct => State.CanAct;
    public bool IsLaunch => State.CanLaunch;
    /// The VR launches carry the filled accent button; monitor launches a quieter one.
    public bool IsVrLaunch => IsLaunch && IsVr;
    public bool IsFlatLaunch => IsLaunch && !IsVr;
    public bool IsSetup => State.Availability is PlayModeAvailability.NeedsSetup;

    public IBrush StatusBrush => LauncherThemeService.Brush(State.Availability switch
    {
        PlayModeAvailability.Ready when State.Caution => "WarningBrush",
        PlayModeAvailability.Ready => "GoodBrush",
        PlayModeAvailability.NeedsSetup => "AccentBrush",
        _ => "InkSecondaryBrush"
    });

    public IBrush IconBrush => LauncherThemeService.Brush(
        State.Availability is PlayModeAvailability.Unavailable ? "InkSecondaryBrush" : "AccentBrush");
}
