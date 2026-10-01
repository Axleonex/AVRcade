namespace VrClient.Core.Launch;
using VrClient.Core.Config;

/// The per-launch OpenXR runtime pin for community VR mods that run their own
/// runtime enumeration and ignore XR_RUNTIME_JSON (RepoXR, LCVR, CWVR). They do
/// honor `[Internal] OpenXRRuntimeFile` in their own cfg, and their settings UI
/// wipes an unrecognized value, so it is rewritten on every launch.
public static class ModRuntimePin
{
    /// Returns true when the cfg was rewritten. A missing cfg, or one without the
    /// key (another author's mod), is left untouched: the key is never invented.
    public static bool Apply(string modCfgPath, string? runtimeJsonPath)
    {
        if (!File.Exists(modCfgPath))
            return false;
        var (pinnedCfg, unmatched) = new ComfortConfigMapper().ApplyToCfg(
            File.ReadAllText(modCfgPath),
            [("Internal", "OpenXRRuntimeFile", runtimeJsonPath ?? "")]);
        if (unmatched.Count != 0)
            return false;
        File.WriteAllText(modCfgPath, pinnedCfg);
        return true;
    }
}
