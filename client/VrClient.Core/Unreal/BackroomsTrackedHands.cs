using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace VrClient.Core.Unreal;

/// <summary>Deploys the local Backrooms arm driver after its imported UEVR profile exists.</summary>
public static class BackroomsTrackedHands
{
    public const string GameSlug = "escape-the-backrooms";
    public const string ScriptFileName = "backrooms_hands.lua";
    public const string DataFileName = "avrcade-backrooms-hands.json";
    private const string ManifestFileName = ".avrcade-backrooms-hands.json";

    public static string Script
    {
        get
        {
            using var stream = typeof(BackroomsTrackedHands).Assembly.GetManifestResourceStream(
                "VrClient.BackroomsHands.lua") ?? throw new InvalidOperationException("Backrooms hand driver resource is missing.");
            using var reader = new StreamReader(stream, Encoding.UTF8);
            return reader.ReadToEnd();
        }
    }

    /// <returns>False for other games; those profiles are never touched.</returns>
    public static bool PrepareForLaunch(string gameSlug, string profileDirectory)
    {
        if (!string.Equals(gameSlug, GameSlug, StringComparison.Ordinal)) return false;
        if (!File.Exists(Path.Combine(profileDirectory, "config.txt")))
            throw new InvalidOperationException("Import the Backrooms UEVR profile before installing controller hands.");

        var scriptPath = Path.Combine(profileDirectory, "scripts", ScriptFileName);
        var manifestPath = Path.Combine(profileDirectory, ManifestFileName);
        var content = Script;
        var desiredHash = Hash(content);
        if (File.Exists(scriptPath) && Hash(File.ReadAllText(scriptPath)) != desiredHash)
        {
            var installedHash = ReadOwnedHash(manifestPath);
            if (installedHash != Hash(File.ReadAllText(scriptPath)))
                throw new InvalidOperationException("The Backrooms hand script was edited outside AVRcade. It was preserved; restore or remove it to enable automatic updates.");
            // Keep the previous owned version available for manual rollback.
            File.Copy(scriptPath, scriptPath + $".{Guid.NewGuid():N}.bak");
        }

        if (!File.Exists(scriptPath) || Hash(File.ReadAllText(scriptPath)) != desiredHash)
            WriteAtomic(scriptPath, content);
        WriteAtomic(manifestPath, JsonSerializer.Serialize(new { installedSha256 = desiredHash }));
        var dataPath = Path.Combine(profileDirectory, "data", DataFileName);
        // Preserve the user's Script UI disable preference across launches.
        if (!File.Exists(dataPath)) WriteAtomic(dataPath, "{\"enabled\":true}");
        return true;
    }

    private static string? ReadOwnedHash(string path)
    {
        if (!File.Exists(path)) return null;
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(path));
            return document.RootElement.TryGetProperty("installedSha256", out var hash) &&
                hash.ValueKind == JsonValueKind.String ? hash.GetString() : null;
        }
        catch (JsonException) { return null; }
    }

    private static string Hash(string text) => Convert.ToHexString(
        SHA256.HashData(Encoding.UTF8.GetBytes(text))).ToLowerInvariant();

    private static void WriteAtomic(string path, string content)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + $".{Guid.NewGuid():N}.tmp";
        try
        {
            File.WriteAllText(temporary, content, new UTF8Encoding(false));
            File.Move(temporary, path, overwrite: true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
