namespace VrClient.Core.Unreal;

public sealed record UnrealModInventory(string ModsFolder, IReadOnlyList<string> Files)
{
    public bool HasMods => Files.Count > 0;
}

/// Read-only look at the places Unreal mods are deployed to. Unreal has no
/// per-launch switch for them, so their presence decides which play modes are
/// honest: a game folder is either modded or it is not.
public static class UnrealMods
{
    private static readonly string[] PakExtensions = [".pak", ".utoc", ".ucas"];
    private static readonly string[] ScriptLoaderFiles = ["dwmapi.dll", "UE4SS.dll"];

    /// installDir is the Steam game folder; shippingBinaryRelative is the catalogue's
    /// "<Project>/Binaries/Win64/<Project>-Win64-Shipping.exe".
    public static UnrealModInventory Scan(string installDir, string shippingBinaryRelative)
    {
        var project = shippingBinaryRelative.Replace('\\', '/').Split('/')[0];
        var paks = Path.Combine(installDir, project, "Content", "Paks");
        var modsFolder = Path.Combine(paks, "~mods");
        var found = new List<string>();
        try
        {
            if (Directory.Exists(paks))
                foreach (var folder in Directory.EnumerateDirectories(paks))
                {
                    var name = Path.GetFileName(folder);
                    if (!name.StartsWith('~') && !name.Equals("mods", StringComparison.OrdinalIgnoreCase) &&
                        !name.Equals("LogicMods", StringComparison.OrdinalIgnoreCase))
                        continue;
                    found.AddRange(Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories)
                        .Where(file => PakExtensions.Contains(Path.GetExtension(file), StringComparer.OrdinalIgnoreCase))
                        .Select(file => Path.GetRelativePath(installDir, file)));
                }

            var binaries = Path.GetDirectoryName(Path.Combine(installDir, shippingBinaryRelative));
            if (binaries is not null && Directory.Exists(binaries))
            {
                found.AddRange(ScriptLoaderFiles.Select(file => Path.Combine(binaries, file))
                    .Where(File.Exists).Select(file => Path.GetRelativePath(installDir, file)));
                if (Directory.Exists(Path.Combine(binaries, "ue4ss")))
                    found.Add(Path.GetRelativePath(installDir, Path.Combine(binaries, "ue4ss")));
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // An unreadable folder is reported as "no mods found"; launch stays the user's call.
        }
        return new UnrealModInventory(modsFolder, found.OrderBy(path => path, StringComparer.OrdinalIgnoreCase).ToArray());
    }
}
