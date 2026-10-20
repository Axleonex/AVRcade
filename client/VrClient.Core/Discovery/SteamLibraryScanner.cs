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
        if (!ValidAppId(appId)) return null;
        if (steamRoot is null && File.Exists(SelectionPath(appId)) && SelectedFolder(appId) is null) return null;
        if (steamRoot is null && SelectedFolder(appId) is { } selected)
        {
            var library = Directory.GetParent(selected)?.Parent?.Parent?.FullName;
            return library is not null && !Directory.Exists(library) ? library : null;
        }
        var roots = steamRoot is null ? DefaultSteamRoots() : [steamRoot];
        foreach (var root in roots.Where(Directory.Exists)
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var vdfPath = Path.Combine(root, "steamapps", "libraryfolders.vdf");
            var vdf = ReadAvailable(vdfPath);
            if (vdf is null) continue;
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
        if (!ValidAppId(appId)) return null;
        if (steamRoot is null && File.Exists(SelectionPath(appId)) && SelectedFolder(appId) is null) return null;
        if (steamRoot is null && SelectedFolder(appId) is { } selected)
        {
            var library = Directory.GetParent(selected)?.Parent?.Parent?.FullName;
            var found = library is null ? null : FindInLibrary(appId, exeName, library);
            // A disconnected or moved selection needs the user's Locate action.
            // Never substitute an unselected copy on a different disk.
            return found is not null && SameFolder(found.InstallDir, selected) ? found : null;
        }
        var roots = steamRoot is null ? DefaultSteamRoots() : [steamRoot];
        foreach (var root in roots.Where(Directory.Exists)
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var libraries = new List<string> { root };
            var vdfPath = Path.Combine(root, "steamapps", "libraryfolders.vdf");
            if (ReadAvailable(vdfPath) is { } vdf)
                foreach (Match m in Regex.Matches(vdf, "\"path\"\\s+\"([^\"]+)\""))
                    libraries.Add(m.Groups[1].Value.Replace("\\\\", "\\"));

            foreach (var library in libraries.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                if (FindInLibrary(appId, exeName, library) is { } found) return found;
            }
        }
        return null;
    }

    /// Remember an existing Steam library only after its manifest and game executable
    /// agree with the selected app. This never edits the game or Steam's own files.
    public bool RememberGameFolder(string appId, string exeName, string gameFolder)
    {
        if (!ValidAppId(appId) || string.IsNullOrWhiteSpace(gameFolder) || !Directory.Exists(gameFolder)) return false;
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
        AtomicFile.WriteAllText(preference, string.Join(Environment.NewLine, roots) + Environment.NewLine);
        AtomicFile.WriteAllText(SelectionPath(appId), folder);
        return true;
    }

    private string SelectionPath(string appId) => Path.Combine(_savedLibraryRootsPath + ".games", appId + ".txt");
    private string? SelectedFolder(string appId)
    {
        var path = ReadAvailable(SelectionPath(appId))?.Trim();
        return !string.IsNullOrEmpty(path) && Path.IsPathFullyQualified(path) ? path : null;
    }
    private static bool ValidAppId(string appId) => !string.IsNullOrWhiteSpace(appId) && appId.All(char.IsAsciiDigit);
    private static bool SameFolder(string a, string b) => Path.GetFullPath(a).TrimEnd(Path.DirectorySeparatorChar)
        .Equals(Path.GetFullPath(b).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase);
    private static string? ReadAvailable(string path)
    {
        try { return File.Exists(path) ? File.ReadAllText(path) : null; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException) { return null; }
    }

    private static SteamGame? FindInLibrary(string appId, string exeName, string library)
    {
        try
        {
            var manifestPath = Path.Combine(library, "steamapps", $"appmanifest_{appId}.acf");
            var manifest = ReadAvailable(manifestPath);
            if (manifest is null) return null;
            var identity = Regex.Match(manifest, "\"appid\"\\s+\"([^\"]+)\"");
            if (identity.Success && identity.Groups[1].Value != appId) return null;
            var name = Regex.Match(manifest, "\"installdir\"\\s+\"([^\"]+)\"");
            if (!name.Success || name.Groups[1].Value is "." or ".." ||
                name.Groups[1].Value.IndexOfAny(['/', '\\', ':']) >= 0) return null;
            var installDir = Path.GetFullPath(Path.Combine(library, "steamapps", "common", name.Groups[1].Value));
            if (Path.IsPathRooted(exeName)) return null;
            var executable = Path.GetFullPath(Path.Combine(installDir, exeName.Replace('/', Path.DirectorySeparatorChar)));
            if (!executable.StartsWith(installDir + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) ||
                !File.Exists(executable)) return null;
            var build = Regex.Match(manifest, "\"buildid\"\\s+\"([^\"]+)\"");
            return new SteamGame { AppId = appId, InstallDir = installDir, ExeName = exeName,
                ExecutablePath = executable, ManifestPath = manifestPath, BuildId = build.Success ? build.Groups[1].Value : null };
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException) { return null; }
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
