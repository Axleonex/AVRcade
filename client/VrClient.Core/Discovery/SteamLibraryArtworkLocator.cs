namespace VrClient.Core.Discovery;

public static class SteamLibraryArtworkLocator
{
    private static readonly string[] PortraitNames =
    [
        "library_600x900.jpg",
        "library_600x900.png",
        "library_capsule.jpg",
        "library_capsule.png"
    ];

    public static string? FindPortrait(
        string? steamAppId,
        IEnumerable<string>? steamRoots = null)
    {
        if (string.IsNullOrWhiteSpace(steamAppId) ||
            !steamAppId.All(char.IsAsciiDigit))
            return null;

        var roots = steamRoots ?? DefaultSteamRoots();
        foreach (var root in roots
                     .Where(path => !string.IsNullOrWhiteSpace(path))
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var appCache = Path.Combine(
                root,
                "appcache",
                "librarycache",
                steamAppId);
            if (!Directory.Exists(appCache))
                continue;

            foreach (var portraitName in PortraitNames)
            {
                var direct = Path.Combine(appCache, portraitName);
                if (File.Exists(direct))
                    return Path.GetFullPath(direct);

                try
                {
                    var nested = Directory
                        .EnumerateFiles(appCache, portraitName, SearchOption.AllDirectories)
                        .Order(StringComparer.OrdinalIgnoreCase)
                        .FirstOrDefault();
                    if (nested is not null)
                        return Path.GetFullPath(nested);
                }
                catch (UnauthorizedAccessException)
                {
                    // Steam artwork is optional. An unreadable cache must not
                    // prevent the catalogue from using its local fallback.
                }
                catch (IOException)
                {
                    // Treat a transient cache read failure as missing artwork.
                }
            }
        }

        return null;
    }

    private static IEnumerable<string> DefaultSteamRoots()
    {
        var programFilesX86 = Environment.GetFolderPath(
            Environment.SpecialFolder.ProgramFilesX86);
        var programFiles = Environment.GetFolderPath(
            Environment.SpecialFolder.ProgramFiles);
        var localAppData = Environment.GetFolderPath(
            Environment.SpecialFolder.LocalApplicationData);

        foreach (var baseDirectory in new[]
                 {
                     programFilesX86,
                     programFiles,
                     localAppData
                 })
        {
            if (!string.IsNullOrWhiteSpace(baseDirectory))
                yield return Path.Combine(baseDirectory, "Steam");
        }
    }
}
