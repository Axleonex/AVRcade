using System.Diagnostics;
using System.Text.Json;

namespace VrClient.Core.Streaming;

public sealed record StreamingPreferences(int Port = 4455, string? ObsExecutable = null);

public sealed class StreamingSettings(string? path = null)
{
    public string PathOnDisk { get; } = path ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VRClient", "streaming.json");

    public StreamingPreferences Load()
    {
        try
        {
            if (!File.Exists(PathOnDisk)) return new();
            var result = JsonSerializer.Deserialize<StreamingPreferences>(File.ReadAllText(PathOnDisk));
            return result is { Port: >= 1 and <= 65535 } ? result : new();
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException) { return new(); }
    }

    public void Save(StreamingPreferences preferences)
    {
        if (preferences.Port is < 1 or > 65535) throw new ArgumentOutOfRangeException(nameof(preferences));
        AtomicFile.WriteAllText(PathOnDisk, JsonSerializer.Serialize(preferences));
    }

    public static bool IsObsExecutable(string? path) => !string.IsNullOrWhiteSpace(path) &&
        (Path.GetFileName(path).Equals("obs64.exe", StringComparison.OrdinalIgnoreCase) ||
         Path.GetFileName(path).Equals("obs32.exe", StringComparison.OrdinalIgnoreCase)) && File.Exists(path);

    public string? FindObs()
    {
        var selected = Load().ObsExecutable;
        if (IsObsExecutable(selected)) return selected;
        foreach (var root in new[] { Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                     Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86) })
            foreach (var binary in new[] { "64bit/obs64.exe", "32bit/obs32.exe" })
            {
                var candidate = Path.Combine(root, "obs-studio", "bin", binary);
                if (IsObsExecutable(candidate)) return candidate;
            }
        return null;
    }

    public void OpenObs()
    {
        var executable = FindObs() ?? throw new InvalidOperationException("OBS was not found. Choose your OBS executable or install OBS Studio.");
        // OBS uses its binary directory to locate data in portable installations.
        Process.Start(new ProcessStartInfo(executable) { UseShellExecute = true, WorkingDirectory = Path.GetDirectoryName(executable)! });
    }
}
