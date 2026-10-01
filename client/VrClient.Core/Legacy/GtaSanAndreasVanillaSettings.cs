using System.Text.Json;

namespace VrClient.Core.Legacy;

/// <summary>A separately owned, clean game folder for desktop play. Never copies or edits game files.</summary>
public sealed class GtaSanAndreasVanillaSettings(string? path = null)
{
    private sealed record Selection(string GameDirectory);

    private static readonly string[] KnownModFiles =
    [
        "openxr_loader.dll", "dinput8.dll", "dsound.dll", "vorbisHooked.dll",
        "ggmm.exe", "gtainterface.dll"
    ];

    private static readonly string[] KnownModDirectories = ["modloader", "cleo", "scripts"];

    public string PathOnDisk { get; } = path ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "gta-san-andreas-vanilla.json");

    public string? Load()
    {
        if (!File.Exists(PathOnDisk)) return null;
        try
        {
            return JsonSerializer.Deserialize<Selection>(File.ReadAllText(PathOnDisk))?.GameDirectory;
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    public void Save(string gameDirectory, string vrGameDirectory)
    {
        var error = Validate(gameDirectory, vrGameDirectory);
        if (error is not null) throw new InvalidOperationException(error);
        var fullPath = Path.GetFullPath(gameDirectory);
        Directory.CreateDirectory(Path.GetDirectoryName(PathOnDisk)!);
        var temporary = PathOnDisk + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(new Selection(fullPath)));
        File.Move(temporary, PathOnDisk, overwrite: true);
    }

    /// <returns>Null if this is a separate x86 folder without common installed mod hooks.</returns>
    public static string? Validate(string? gameDirectory, string? vrGameDirectory)
    {
        if (string.IsNullOrWhiteSpace(gameDirectory) || !Directory.Exists(gameDirectory))
            return "Select an existing clean GTA San Andreas folder first.";
        try
        {
            var fullPath = Path.GetFullPath(gameDirectory);
            if (!string.IsNullOrWhiteSpace(vrGameDirectory) &&
                Path.TrimEndingDirectorySeparator(fullPath).Equals(
                    Path.TrimEndingDirectorySeparator(Path.GetFullPath(vrGameDirectory)),
                    StringComparison.OrdinalIgnoreCase))
                return "Vanilla play needs a separate clean copy, not the VR/modded game folder.";
            if (GtaSanAndreasPreflight.ReadPeArchitecture(Path.Combine(fullPath, "gta_sa.exe")) != "x86")
                return "The clean folder needs a classic x86 gta_sa.exe.";
            if (Directory.EnumerateFiles(fullPath, "*.asi", SearchOption.TopDirectoryOnly).Any() ||
                Directory.EnumerateFiles(fullPath, "*.cleo", SearchOption.TopDirectoryOnly).Any() ||
                KnownModFiles.Any(name => File.Exists(Path.Combine(fullPath, name))) ||
                KnownModDirectories.Any(name => Directory.Exists(Path.Combine(fullPath, name))))
                return "The selected folder contains mod or VR hooks. Choose a clean game copy.";
            return null;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            return $"Could not check the clean game folder: {ex.Message}";
        }
    }
}
