using VrClient.Core.App;

namespace VrClient.Core.Config;

public sealed record ControllerBindingAdapter(bool UsesOpenXr,
    Func<ControllerRuntimeBindings, string, GameView, ActionOutcome>? Stage = null);

/// New OpenXR games need only a guide/catalogue entry. A different native input
/// format adds one adapter registration, without changing the editor or store.
public static class ControllerBindingAdapters
{
    public static ControllerBindingAdapter OpenXr { get; } = new(true);
    public static IReadOnlyDictionary<string, ControllerBindingAdapter> BuiltIn { get; } =
        new Dictionary<string, ControllerBindingAdapter>(StringComparer.OrdinalIgnoreCase)
        {
            ["big-walk"] = new(false, SteamVrConfiguredManifest.StageBigWalk),
            ["gta-v"] = new(false, (runtime, root, game) =>
                new GtaVControllerBindings(runtime.Settings).Prepare(root, game)),
            ["yapyap"] = new(false, (runtime, _, game) =>
            {
                if (game.InstallDir == null) return new(false, "YAPYAP is not installed.");
                runtime.BuildSteamVrManifest(game.Slug, Path.Combine(game.InstallDir,
                    "yapyap_Data", "StreamingAssets", "SteamVR", "actions.json"));
                return new(true, "YAPYAP bindings prepared.");
            }),
            ["gta-iv"] = new(false, (runtime, root, game) =>
            {
                runtime.BuildSteamVrManifest(game.Slug, Path.Combine(root, "native", "gta-hands", "package", "actions.json"));
                return new(true, "GTA IV bindings prepared.");
            })
        };
}
