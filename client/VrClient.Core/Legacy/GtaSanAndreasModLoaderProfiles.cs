namespace VrClient.Core.Legacy;

public enum GtaSanAndreasVrMods { Existing, Clean, ModLoader }

/// <summary>Isolates ModLoader's optional add-ons per launch. In-place GGMM edits and
/// root-level ASI plugins are outside ModLoader's profile control.</summary>
public static class GtaSanAndreasModLoaderProfiles
{
    public const string CleanProfile = "VRClientClean";
    public const string ModsProfile = "VRClientMods";

    public static bool IsInstalled(string gameDir) =>
        File.Exists(Path.Combine(gameDir, "modloader.asi")) &&
        Directory.Exists(Path.Combine(gameDir, "modloader"));

    public static IReadOnlyList<string> PrepareArguments(
        string gameDir, GtaSanAndreasVrMods mode, bool dryRun)
    {
        if (mode == GtaSanAndreasVrMods.Existing) return [];
        if (!IsInstalled(gameDir))
        {
            if (mode == GtaSanAndreasVrMods.Clean) return [];
            throw new InvalidOperationException(
                "VR with ModLoader mods requires your own modloader.asi and modloader folder beside gta_sa.exe.");
        }

        var name = mode == GtaSanAndreasVrMods.Clean ? CleanProfile : ModsProfile;
        var content = $"[Profiles.{name}.Config]\nParents = $None\nIgnoreAllMods = " +
            (mode == GtaSanAndreasVrMods.Clean ? "true\n" : "false\n");
        var profilesDirectory = Path.Combine(gameDir, "modloader", ".profiles");
        var profilePath = Path.Combine(profilesDirectory, $"{name}.ini");
        if (File.Exists(profilePath))
        {
            if (!string.Equals(File.ReadAllText(profilePath).ReplaceLineEndings("\n"),
                    content, StringComparison.Ordinal))
                throw new InvalidOperationException(
                    $"ModLoader profile {profilePath} was edited; AVRcade will not overwrite it.");
        }
        else if (!dryRun)
        {
            Directory.CreateDirectory(profilesDirectory);
            using var stream = new FileStream(profilePath, FileMode.CreateNew, FileAccess.Write);
            using var writer = new StreamWriter(stream);
            writer.Write(content);
        }
        return ["-modprof", name];
    }
}
