namespace VrClient.Core.FalloutNewVegas;

using System.Text.Json;

public sealed record FnvComfortPreferences(
    string PlayMode,
    string TurnMode,
    string MovementOrientation,
    string DominantHand,
    string PerformancePreset);

public sealed record FnvComfortItem(
    string Feature,
    string Status,
    string Guidance,
    string EvidenceBoundary);

public sealed record FnvComfortPlan(
    FnvComfortPreferences Preferences,
    IReadOnlyList<FnvComfortItem> Items,
    bool WritesThirdPartyConfiguration);

public sealed class FalloutNewVegasComfortPlanner
{
    public FnvComfortPlan Build(FnvComfortPreferences preferences)
    {
        RequireChoice(preferences.PlayMode, "seated", "standing");
        RequireChoice(preferences.TurnMode, "snap", "smooth");
        RequireChoice(preferences.MovementOrientation, "controller", "head");
        RequireChoice(preferences.DominantHand, "left", "right");
        RequireChoice(preferences.PerformancePreset, "quality", "balanced", "performance");

        var items = new List<FnvComfortItem>
        {
            new("play_mode", "hardware_pending",
                $"Calibrate SteamVR floor/height for {preferences.PlayMode} play, then recenter SteamVR and press F8 to recenter AVRcade's native view.",
                "FNVR does not publish a stable seated/standing configuration key."),
            new("turning", "manual_dependency_setting",
                $"Use {preferences.TurnMode} turning only if the selected controller mapping/mod exposes it; verify in-headset before saving.",
                "No FNVR V2 upstream key for snap/smooth turning is documented, so AVRcade does not write one."),
            new("movement_orientation", preferences.MovementOrientation == "controller" ? "upstream_supported" : "manual_override_warning",
                preferences.MovementOrientation == "controller"
                    ? "FNVR documents controller-centric movement; validate direction with both hands assigned."
                    : "Head-oriented movement is not documented by FNVR V2; treat this as an external input-mod experiment.",
                "FNVR README describes controller-centric movement."),
            new("handedness", "tracker_ui",
                $"In the FNVR tracker, cycle the {preferences.DominantHand} controller until Primary pose readings move; assign the other controller as Secondary.",
                "FNVR V2 documents Primary/Secondary controller cycling in its visible UI, not a stable config key."),
            new("weapon_alignment", "hardware_pending",
                "In game, hold the X keyboard action (or an explicit SteamVR controller binding for X) to drag the FNVR weapon offset; double-tap X to reset. Confirm muzzle and impact direction.",
                "FNVR documents manual weapon-offset calibration and recommends headset testing."),
            new("hud_and_dialogue", "manual_guidance",
                "Adjust HUD scale/placement with compatible user-selected HUD tools in the isolated profile. Test dialogue, terminals, and Pip-Boy without auto-disabling mods.",
                "FNVR does not publish an AVRcade-safe HUD configuration key."),
            new("gestures", "tracker_ui",
                "Stand in the normal play posture and use each Set Current control in FNVR Tracker; then tune positional/rotational sensitivity in the tracker UI.",
                "Gesture locations are hardware-specific and FNVR documents setting them interactively."),
            new("native_openxr", "built_in",
                "AVRcade supplies stereo projection and head pose through OpenXR. Press F8 after changing play posture or SteamVR origin.",
                "Native stereo math and lifecycle are automated; optical comfort and tracking correctness remain headset verification gates."),
            new("performance", "hardware_pending",
                $"Start with the {preferences.PerformancePreset} preset as guidance only, measure frame timing, and reduce expensive post-processing before changing gameplay mods.",
                "No hardware-independent FNVR preset keys are published.")
        };
        return new FnvComfortPlan(preferences, items, WritesThirdPartyConfiguration: false);
    }

    public void Write(string path, FnvComfortPlan plan)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, JsonSerializer.Serialize(new
        {
            schema_version = 1,
            preferences = new
            {
                play_mode = plan.Preferences.PlayMode,
                turn_mode = plan.Preferences.TurnMode,
                movement_orientation = plan.Preferences.MovementOrientation,
                dominant_hand = plan.Preferences.DominantHand,
                performance_preset = plan.Preferences.PerformancePreset
            },
            writes_third_party_configuration = plan.WritesThirdPartyConfiguration,
            items = plan.Items.Select(item => new
            {
                feature = item.Feature,
                status = item.Status,
                guidance = item.Guidance,
                evidence_boundary = item.EvidenceBoundary
            })
        }, new JsonSerializerOptions { WriteIndented = true }));
    }

    private static void RequireChoice(string value, params string[] allowed)
    {
        if (!allowed.Contains(value, StringComparer.OrdinalIgnoreCase))
            throw new ArgumentException($"unsupported choice '{value}'; expected one of: {string.Join(", ", allowed)}");
    }
}
