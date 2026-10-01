namespace VrClient.Core.Discovery;
using System.Text.RegularExpressions;

public sealed class SteamGame
{
    public required string AppId { get; init; }
    public required string InstallDir { get; init; } // absolute path to the game folder
    public required string ExeName { get; init; }
    public required string ExecutablePath { get; init; }
    public required string ManifestPath { get; init; }
    public string? BuildId { get; init; }
}

public sealed class SteamLibraryScanner
{
    private readonly string _savedLibraryRootsPath;

    public SteamLibraryScanner(string? savedLibraryRootsPath = null) =>
        _savedLibraryRootsPath = savedLibraryRootsPath ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "AVRcade", "steam-library-roots.txt");

    /// Steam still lists the app in libraryfolders.vdf, but the library path
    /// itself is unavailable. This is not proof of ownership or an install.
    public string? FindOfflineLibrary(string appId, string? steamRoot = null)
    {
        if (string.IsNullOrWhiteSpace(appId) || !appId.All(char.IsAsciiDigit))
            return null;
        var roots = steamRoot is null ? DefaultSteamRoots() : [steamRoot];
        foreach (var root in roots.Where(Directory.Exists)
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var vdfPath = Path.Combine(root, "steamapps", "libraryfolders.vdf");
            if (!File.Exists(vdfPath)) continue;
            var vdf = File.ReadAllText(vdfPath);
            var paths = Regex.Matches(vdf, "\"path\"\\s+\"([^\"]+)\"");
            for (var index = 0; index < paths.Count; index++)
            {
                var match = paths[index];
                var library = match.Groups[1].Value.Replace("\\\\", "\\");
                if (Directory.Exists(library)) continue;
                var end = index + 1 < paths.Count ? paths[index + 1].Index : vdf.Length;
                var section = vdf[match.Index..end];
                if (Regex.IsMatch(section, $"\"{Regex.Escape(appId)}\"\\s+\"[^\"]+\""))
                    return library;
            }
        }
        return null;
    }

    /// Parse Steam's libraryfolders.vdf + each library's appmanifest_<appid>.acf to find
    /// the install dir for a given app id. steamRoot defaults to the registry/well-known
    /// path when null. Returns null when not found.
    public SteamGame? FindGame(string appId, string exeName, string? steamRoot = null)
    {
        var roots = steamRoot is null ? DefaultSteamRoots() : [steamRoot];
        foreach (var root in roots.Where(Directory.Exists)
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var libraries = new List<string> { root };
            var vdfPath = Path.Combine(root, "steamapps", "libraryfolders.vdf");
            if (File.Exists(vdfPath))
                foreach (Match m in Regex.Matches(File.ReadAllText(vdfPath), "\"path\"\\s+\"([^\"]+)\""))
                    libraries.Add(m.Groups[1].Value.Replace("\\\\", "\\"));

            foreach (var library in libraries.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                var manifestPath = Path.Combine(library, "steamapps", $"appmanifest_{appId}.acf");
                if (!File.Exists(manifestPath)) continue;
                var manifest = File.ReadAllText(manifestPath);
                var installDirMatch = Regex.Match(manifest, "\"installdir\"\\s+\"([^\"]+)\"");
                if (!installDirMatch.Success) continue;
                var installDir = Path.Combine(library, "steamapps", "common", installDirMatch.Groups[1].Value);
                var executablePath = Path.Combine(installDir, exeName);
                if (!File.Exists(executablePath)) continue;
                var buildIdMatch = Regex.Match(manifest, "\"buildid\"\\s+\"([^\"]+)\"");
                return new SteamGame
                {
                    AppId = appId,
                    InstallDir = installDir,
                    ExeName = exeName,
                    ExecutablePath = executablePath,
                    ManifestPath = manifestPath,
                    BuildId = buildIdMatch.Success ? buildIdMatch.Groups[1].Value : null
                };
            }
        }
        return null;
    }

    /// Remember an existing Steam library only after its manifest and game executable
    /// agree with the selected app. This never edits the game or Steam's own files.
    public bool RememberGameFolder(string appId, string exeName, string gameFolder)
    {
        if (string.IsNullOrWhiteSpace(gameFolder) || !Directory.Exists(gameFolder)) return false;
        var folder = Path.GetFullPath(gameFolder).TrimEnd(Path.DirectorySeparatorChar);
        var common = Directory.GetParent(folder);
        var steamApps = common?.Parent;
        var library = steamApps?.Parent;
        if (common is null || steamApps is null || library is null ||
            !common.Name.Equals("common", StringComparison.OrdinalIgnoreCase) ||
            !steamApps.Name.Equals("steamapps", StringComparison.OrdinalIgnoreCase)) return false;
        var found = FindGame(appId, exeName, library.FullName);
        if (found is null || !Path.GetFullPath(found.InstallDir).TrimEnd(Path.DirectorySeparatorChar)
                .Equals(folder, StringComparison.OrdinalIgnoreCase)) return false;
        var preference = _savedLibraryRootsPath;
        Directory.CreateDirectory(Path.GetDirectoryName(preference)!);
        var roots = new[] { library.FullName }.Concat(SavedLibraryRoots()
            .Where(path => !path.Equals(library.FullName, StringComparison.OrdinalIgnoreCase)))
            .Take(16);
        File.WriteAllLines(preference, roots);
        return true;
    }

    private IEnumerable<string> DefaultSteamRoots()
    {
        var roots = new List<string>();
        roots.AddRange(SavedLibraryRoots());
        var standard = DefaultSteamRoot();
        if (standard is not null) roots.Add(standard);
        // A removable Steam library may be mounted without being listed in the
        // current Steam install's libraryfolders.vdf. Probe only conventional
        // library directories at the volume root; never crawl arbitrary data.
        foreach (var drive in Environment.GetLogicalDrives())
        {
            roots.Add(Path.Combine(drive, "SteamLibrary"));
            roots.Add(Path.Combine(drive, "Steam"));
        }
        return roots;
    }

    private IEnumerable<string> SavedLibraryRoots()
    {
        try
        {
            if (!File.Exists(_savedLibraryRootsPath)) return [];
            return File.ReadAllLines(_savedLibraryRootsPath)
                .Where(Path.IsPathFullyQualified).Take(16).ToArray();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { return []; }
    }

    private static string? DefaultSteamRoot()
    {
        var candidates = new[]
        {
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)
        };
        foreach (var baseDir in candidates)
        {
            if (string.IsNullOrEmpty(baseDir))
                continue;
            var steam = Path.Combine(baseDir, "Steam");
            if (Directory.Exists(Path.Combine(steam, "steamapps")))
                return steam;
        }
        return null;
    }
}
