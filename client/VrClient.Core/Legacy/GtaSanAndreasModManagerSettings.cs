using System.Text.Json;

namespace VrClient.Core.Legacy;

public enum GtaSanAndreasModManagerKind { Custom, Ggmm, Sami }

public sealed record GtaSanAndreasModManagerSelection(
    string Executable, GtaSanAndreasModManagerKind Kind);

/// <summary>Remembers a user-selected tool; it does not install mods or control the tool's profiles.</summary>
public sealed class GtaSanAndreasModManagerSettings(string? path = null)
{
    public string PathOnDisk { get; } = path ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "gta-san-andreas-mod-manager.json");

    public string? Load() => LoadSelection()?.Executable;

    public GtaSanAndreasModManagerSelection? LoadSelection()
    {
        if (!File.Exists(PathOnDisk)) return null;
        try
        {
            var selection = JsonSerializer.Deserialize<GtaSanAndreasModManagerSelection>(
                File.ReadAllText(PathOnDisk));
            return selection is { Executable: not null } && Enum.IsDefined(selection.Kind)
                ? selection : null;
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    public void Save(string executable, GtaSanAndreasModManagerKind kind = GtaSanAndreasModManagerKind.Custom)
    {
        var error = Validate(executable, kind);
        if (error is not null) throw new InvalidOperationException(error);
        var fullPath = Path.GetFullPath(executable);
        Directory.CreateDirectory(Path.GetDirectoryName(PathOnDisk)!);
        var temporary = PathOnDisk + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(
            new GtaSanAndreasModManagerSelection(fullPath, kind)));
        File.Move(temporary, PathOnDisk, overwrite: true);
    }

    public static GtaSanAndreasModManagerKind InferKind(string executable)
    {
        var fileName = Path.GetFileName(executable);
        if (fileName.Equals("ggmm.exe", StringComparison.OrdinalIgnoreCase))
            return GtaSanAndreasModManagerKind.Ggmm;
        if (fileName.Equals("sami.exe", StringComparison.OrdinalIgnoreCase) ||
            fileName.StartsWith("San Andreas Mod Installer v", StringComparison.OrdinalIgnoreCase))
            return GtaSanAndreasModManagerKind.Sami;
        return GtaSanAndreasModManagerKind.Custom;
    }

    public static string? Validate(
        string? executable, GtaSanAndreasModManagerKind kind = GtaSanAndreasModManagerKind.Custom)
    {
        if (!Enum.IsDefined(kind)) return "Choose a supported manager type.";
        if (string.IsNullOrWhiteSpace(executable))
            return "Choose a San Andreas mod-manager application first.";
        try
        {
            if (!Path.GetExtension(executable).Equals(".exe", StringComparison.OrdinalIgnoreCase) ||
                !File.Exists(executable))
                return "Select an existing Windows mod-manager .exe file.";
            if (Path.GetFileName(executable).Equals("gta_sa.exe", StringComparison.OrdinalIgnoreCase))
                return "Select the mod-manager application, not gta_sa.exe.";
            if (kind is GtaSanAndreasModManagerKind.Ggmm &&
                !Path.GetFileName(executable).Equals("ggmm.exe", StringComparison.OrdinalIgnoreCase))
                return "Select GGMM's ggmm.exe file.";
            if (kind is GtaSanAndreasModManagerKind.Sami &&
                !Path.GetFileName(executable).Equals("sami.exe", StringComparison.OrdinalIgnoreCase))
                return "Select SAMI's installed sami.exe, not the downloaded setup program.";
            return null;
        }
        catch (Exception ex) when (ex is ArgumentException or IOException or UnauthorizedAccessException or NotSupportedException)
        {
            return $"Could not check the mod manager: {ex.Message}";
        }
    }
}
