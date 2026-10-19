using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using VrClient.Core.App;

namespace VrClient.Core.Config;

public sealed record ControllerBindingLease(string Target, string Backup, bool HadOriginal,
    string OriginalHash, string AppliedHash, string ExpectedExe, DateTimeOffset Created);

/// GTAVR's version 3-6 binding format, verified against the pinned installer's
/// static IL and data initializers. Its shared native file is leased for one
/// game session; AVRcade preferences and original defaults stay game-private.
public sealed class GtaVControllerBindings(ControllerCustomization? store = null, string? target = null)
{
    private readonly ControllerCustomization _store = store ?? new();
    private readonly string _target = target ?? Path.Combine(Environment.GetFolderPath(
        Environment.SpecialFolder.LocalApplicationData), "GTAVR", "input-bindings.ini");
    public string LeaseFile => Path.Combine(_store.Root, "gta-v", "native-lease.json");
    private static readonly Dictionary<string, int> Codes = new()
    {
        ["left_trigger"] = 1, ["right_trigger"] = 2, ["left_grip"] = 3, ["right_grip"] = 4,
        ["left_stick_click"] = 5, ["right_stick_click"] = 6, ["left_primary"] = 7,
        ["right_primary"] = 8, ["left_secondary"] = 9, ["right_secondary"] = 10
    };
    private static readonly int[] FootDefaults = [1, 2, 5, 8, 7, 10, 9, 11];
    private static readonly int[] DriveDefaults = [3, 4, 2, 1, 8];
    private static readonly string[] FootActions = ["Aim / brake", "Fire / accelerate", "Sprint / horn",
        "Jump / handbrake", "Enter / exit vehicle", "Reload", "Context action", "Weapon wheel"];

    public static ControllerReference Reference()
    {
        var generic = ControllerCustomization.Generic("gta-v");
        var driveActions = new[] { "Drive: steer left", "Drive: steer right", "Drive: accelerate", "Drive: brake", "Drive: handbrake" };
        var devices = new List<ControllerDeviceReference>();
        foreach (var device in generic.Devices)
        {
            var bindings = new List<ControllerBindingReference>();
            foreach (var binding in device.Bindings)
            {
                var action = binding.Action;
                if (Codes.TryGetValue(binding.Slot, out var code))
                {
                    var names = FootDefaults.Select((value,index) => (value,index)).Where(p => p.value == code).Select(p => FootActions[p.index])
                        .Concat(DriveDefaults.Select((value,index) => (value,index)).Where(p => p.value == code).Select(p => driveActions[p.index]));
                    action = string.Join("; ", names.DefaultIfEmpty("No default action"));
                }
                bindings.Add(binding with { Action = action });
            }
            devices.Add(device with { Bindings = bindings,
                Context = "GTAVR native defaults. Both secondary buttons still open the weapon wheel; that combination stays on physical Y/B or left B/right B. Select the controller tab you will use before launching. Native controller behavior needs headset verification.",
                CustomContext = "Saved GTA V assignments. The independent weapon-wheel gesture stays on physical Y/B (Quest) or left B/right B (Index). Select your controller tab before launch. Its profile is staged for the session; original native bindings restore after closing GTA V." });
        }
        return generic with { Title = "GTA V native controller bindings", Devices = devices };
    }

    public static string Transform(string? original, IReadOnlyDictionary<string, string> map)
    {
        ControllerCustomization.Validate(map);
        var entries = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (original != null)
        {
            if (!original.TrimStart().StartsWith("[GTAVR.InputBindings]", StringComparison.Ordinal))
                throw new InvalidDataException("The existing native controller file is not a GTAVR binding profile.");
            foreach (var line in original.Split('\n'))
            {
                var split = line.IndexOf('='); if (split < 0) continue;
                entries[line[..split].Trim()] = line[(split+1)..].Trim();
            }
        }
        if (entries.TryGetValue("version", out var version) && (!int.TryParse(version, out var v) || v is < 3 or > 6))
            throw new InvalidDataException("Unsupported GTA V binding file version.");
        var custom = entries.GetValueOrDefault("custom", "0") == "1";
        var replacements = map.ToDictionary(p => Codes[p.Value], p => Codes[p.Key]);
        void Bind(string prefix, int[] defaults, string devicePrefix)
        {
            for (var i = 0; i < defaults.Length; i++)
            {
                var key = prefix + i;
                var raw = custom ? entries.GetValueOrDefault(key, defaults[i].ToString()) : defaults[i].ToString();
                if (!int.TryParse(raw, out var code) || code is < 0 or > 11)
                    throw new InvalidDataException("Invalid GTA V binding: " + key);
                if (string.IsNullOrWhiteSpace(entries.GetValueOrDefault(devicePrefix + i)) && replacements.TryGetValue(code, out var changed)) code = changed;
                entries[key] = code.ToString();
            }
        }
        Bind("action", FootDefaults, "device"); Bind("drive", DriveDefaults, "driveDev");
        // Preserve keyboard, wheel/gamepad bindings, analog flags and all steering settings.
        entries.TryAdd("version", "3"); entries["custom"] = "1";
        entries.TryAdd("overlayKey", "46"); entries.TryAdd("controllerOverlay", "0");
        entries.TryAdd("overlayFallbacks", "1"); entries.TryAdd("driving", "0");
        entries.TryAdd("steeringCurve", "100"); entries.TryAdd("steeringSpring", "0");
        for (var i=0;i<8;i++) entries.TryAdd("analog"+i, "0");
        for (var i=0;i<5;i++) entries.TryAdd("driveAnalog"+i, i<4?"1":"0");
        return "[GTAVR.InputBindings]\n" + string.Join("\n", entries.Select(p => p.Key+"="+p.Value)) + "\n";
    }

    public ActionOutcome Prepare(string repoRoot, GameView game)
    {
        if (Process.GetProcessesByName("GTA5").Length != 0)
            return new(false, "Close GTA V before changing its controller assignments.");
        Recover();
        var map = _store.Load("gta-v", _store.ActiveDevice("gta-v"));
        if (map.All(p => p.Key == p.Value)) return new(true, "GTA V controller defaults retained.");
        var helper = Path.Combine(repoRoot, "native", "controller-remap", "package", "avrcade-controller-lease.exe");
        if (!File.Exists(helper)) return new(false, "The GTA V controller helper is missing. Repair AVRcade.");
        if (game.InstallDir == null) return new(false, "GTA V is not installed.");
        Stage(map, Path.Combine(game.InstallDir, "GTA5.exe"));
        try
        {
            var info = new ProcessStartInfo(helper) { UseShellExecute = false, CreateNoWindow = true };
            info.ArgumentList.Add(LeaseFile);
            using var process = Process.Start(info) ?? throw new IOException("Controller helper did not start.");
            return new(true, "GTA V controller profile prepared; original native bindings restore after the game closes.");
        }
        catch { Recover(); throw; }
    }

    public void Stage(IReadOnlyDictionary<string, string> map, string expectedExe)
    {
        if (File.Exists(LeaseFile)) throw new InvalidDataException("A previous GTA V binding session needs recovery.");
        var had = File.Exists(_target); var original = had ? File.ReadAllText(_target) : null;
        var transformed = Transform(original, map);
        var backup = Path.Combine(_store.Root, "gta-v", "native-original.ini");
        Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
        if (had) File.Copy(_target, backup, true);
        var bytes = Encoding.UTF8.GetBytes(transformed);
        var record = new ControllerBindingLease(Path.GetFullPath(_target), Path.GetFullPath(backup), had,
            had ? Hash(_target) : "", Convert.ToHexString(SHA256.HashData(bytes)), Path.GetFullPath(expectedExe), DateTimeOffset.UtcNow);
        // Journal before writing so a crash at either boundary is recoverable.
        AtomicFile.WriteAllText(LeaseFile, JsonSerializer.Serialize(record));
        AtomicFile.WriteAllText(_target, transformed);
    }

    public ControllerBindingLease? ReadLease()
    {
        if (!File.Exists(LeaseFile)) return null;
        var record = JsonSerializer.Deserialize<ControllerBindingLease>(File.ReadAllText(LeaseFile))
            ?? throw new InvalidDataException("Invalid binding session record.");
        if (!Path.GetFullPath(record.Target).Equals(Path.GetFullPath(_target), StringComparison.OrdinalIgnoreCase) ||
            !Path.GetFullPath(record.Backup).Equals(Path.Combine(Path.GetFullPath(_store.Root), "gta-v", "native-original.ini"), StringComparison.OrdinalIgnoreCase) ||
            Path.GetFileName(record.ExpectedExe) != "GTA5.exe")
            throw new InvalidDataException("Binding session path is outside its permitted scope.");
        return record;
    }

    public void Recover(bool preserveSessionEdits = false)
    {
        if (ReadLease() is not { } record) return;
        if (File.Exists(_target))
        {
            var current = Hash(_target);
            if (current != record.AppliedHash && current != record.OriginalHash)
            {
                var content = File.ReadAllText(_target);
                if (!preserveSessionEdits || !content.TrimStart().StartsWith("[GTAVR.InputBindings]", StringComparison.Ordinal))
                    throw new InvalidDataException("GTA V's native bindings changed during the session. AVRcade preserved them and the original backup; restore/reset them before the next customized launch.");
                _ = Transform(content, ControllerCustomization.Identity()); // Validate the native format first.
                File.Copy(_target, Path.Combine(_store.Root, "gta-v", "native-edited-during-session.ini"), true);
            }
        }
        else if (record.HadOriginal)
            throw new InvalidDataException("GTA V's native binding file disappeared. The original backup is preserved.");
        if (record.HadOriginal)
        {
            if (!File.Exists(record.Backup) || Hash(record.Backup) != record.OriginalHash)
                throw new InvalidDataException("Original binding backup changed; no file was overwritten.");
            var temporary = _target + ".avrcade-restore.tmp";
            File.Copy(record.Backup, temporary, true);
            File.Move(temporary, _target, true);
        }
        else if (File.Exists(_target)) File.Delete(_target);
        File.Delete(LeaseFile);
    }
    private static string Hash(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)));
}
