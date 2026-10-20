using System.Text.Json;
using System.Text.RegularExpressions;

namespace VrClient.Core.Config;

// Physical input -> the input whose original action it now performs.
// A permutation preserves every action and prevents duplicate/missing hotkeys.
public sealed class ControllerCustomization(string? root = null)
{
    public string Root { get; } = root ?? VrClient.Core.AppDataPaths.Local("controllers");
    public static readonly string[] Slots = ["left_primary", "left_secondary", "left_stick_click",
        "right_primary", "right_secondary", "right_stick_click", "left_trigger", "left_grip", "right_trigger", "right_grip"];

    public string PathFor(string game, string device)
    {
        ValidateIdentity(game, device);
        return Path.Combine(Root, game, device + ".json");
    }

    public static void ValidateIdentity(string game, string device)
    {
        if (!Regex.IsMatch(game, "^[a-z0-9]+(?:-[a-z0-9]+)*$") ||
            device is not ("quest-touch" or "valve-index"))
            throw new InvalidDataException("Invalid game or controller identity.");
    }

    public Dictionary<string, string> Load(string game, string device)
    {
        var path = PathFor(game, device);
        if (!File.Exists(path)) return Identity();
        var map = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(path))
                  ?? throw new InvalidDataException("Controller settings are empty. Reset this controller to restore defaults.");
        Validate(map);
        return map;
    }

    public void Save(string game, string device, IReadOnlyDictionary<string, string> map)
    {
        Validate(map);
        AtomicFile.WriteAllText(PathFor(game, device), JsonSerializer.Serialize(map,
            new JsonSerializerOptions { WriteIndented = true }));
    }

    private string DefaultsPathFor(string game, string device)
    {
        ValidateIdentity(game, device);
        return Path.Combine(Root, game, "defaults", device + ".json");
    }
    public bool HasSelectedDefaults(string game, string device) => File.Exists(DefaultsPathFor(game, device));
    public Dictionary<string, string> LoadDefaults(string game, string device)
    {
        var path = DefaultsPathFor(game, device);
        if (!File.Exists(path)) return Identity();
        var map = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(path))
            ?? throw new InvalidDataException("Saved controller defaults are empty.");
        Validate(map);
        return map;
    }
    public void SaveAsDefaults(string game, string device, IReadOnlyDictionary<string, string> map)
    {
        Validate(map);
        AtomicFile.WriteAllText(DefaultsPathFor(game, device), JsonSerializer.Serialize(map,
            new JsonSerializerOptions { WriteIndented = true }));
    }
    public void Reset(string game, string device) => Save(game, device, LoadDefaults(game, device));
    public void RestoreOriginal(string game, string device) => Save(game, device, Identity());
    public string ActiveDevice(string game)
    {
        ValidateIdentity(game, "quest-touch");
        var path = Path.Combine(Root, game, "active-device.txt");
        var device = File.Exists(path) ? File.ReadAllText(path).Trim() : "quest-touch";
        ValidateIdentity(game, device); return device;
    }
    public void SelectDevice(string game, string device)
    {
        ValidateIdentity(game, device);
        AtomicFile.WriteAllText(Path.Combine(Root, game, "active-device.txt"), device);
    }
    public static Dictionary<string, string> Identity() => Slots.ToDictionary(s => s, s => s);
    public static string Group(string slot) => slot switch
    {
        "left_primary" or "left_secondary" or "left_stick_click" => "left-button",
        "right_primary" or "right_secondary" or "right_stick_click" => "right-button",
        "left_trigger" or "left_grip" => "left-analog",
        "right_trigger" or "right_grip" => "right-analog",
        _ => throw new InvalidDataException("That input cannot be remapped.")
    };
    public static void Validate(IReadOnlyDictionary<string, string> map)
    {
        if (map.Count != Slots.Length || Slots.Any(s => !map.ContainsKey(s)) ||
            map.Values.Distinct(StringComparer.Ordinal).Count() != Slots.Length ||
            map.Any(p => !Slots.Contains(p.Value) || Group(p.Key) != Group(p.Value)))
            throw new InvalidDataException("Controller assignments must keep compatible inputs on the same hand and preserve every action.");
    }
    public static Dictionary<string, string> Swap(IReadOnlyDictionary<string, string> map, string physical, string action)
    {
        Validate(map);
        if (Group(physical) != Group(action)) throw new InvalidDataException("Choose a compatible control on the same hand.");
        var result = map.ToDictionary(p => p.Key, p => p.Value);
        var other = result.Single(p => p.Value == action).Key;
        (result[physical], result[other]) = (result[other], result[physical]);
        return result;
    }

    public static string Label(string device, string slot) => slot switch
    {
        "left_primary" => device == "valve-index" ? "Left A" : "Left X",
        "left_secondary" => device == "valve-index" ? "Left B" : "Left Y",
        "right_primary" => "Right A", "right_secondary" => "Right B",
        "left_stick_click" => "Left stick press", "right_stick_click" => "Right stick press",
        "left_trigger" => "Left trigger", "right_trigger" => "Right trigger",
        "left_grip" => "Left grip", "right_grip" => "Right grip", _ => slot
    };

    public static ControllerBindingReference DefaultBinding(ControllerDeviceReference defaults, string slot)
    {
        var binding = defaults.Bindings.FirstOrDefault(b => b.Slot == slot);
        if (binding != null) return binding;
        var stick = slot.Replace("_click", "");
        var original = defaults.Bindings.FirstOrDefault(b => b.Slot == stick);
        return new(slot, Label(defaults.Id, slot), "Original " + Label(defaults.Id, slot).ToLowerInvariant() + " action",
            original is null ? "Uses the conversion's default binding." : original.Action + ". " + original.Note);
    }

    public static ControllerDeviceReference ForEditing(string game, ControllerDeviceReference defaults)
    {
        string? PressAction(string side) => (game, side) switch
        {
            ("yapyap", "left") => "Ragdoll", ("yapyap", "right") => "Pause / settings",
            ("lethal-company", "left") => "Sprint", ("lethal-company", "right") => "Crouch",
            ("repo", "left") => "Chat / expression wheel", ("repo", "right") => "Crouch",
            ("big-walk", "right") => "Recenter view", _ => null
        };
        var list = defaults.Bindings.ToList();
        foreach (var side in new[] { "left", "right" })
        {
            var slot = side + "_stick_click";
            if (list.Any(b => b.Slot == slot) || PressAction(side) is not { } action) continue;
            list.Add(new(slot, Label(defaults.Id, slot), action));
        }
        return defaults with { Bindings = list };
    }

    public static ControllerDeviceReference Apply(ControllerDeviceReference defaults, IReadOnlyDictionary<string, string> map)
    {
        Validate(map);
        var bindings = defaults.Bindings.Where(b => !Slots.Contains(b.Slot)).ToList();
        foreach (var slot in Slots)
        {
            var physical = DefaultBinding(defaults, slot);
            var action = DefaultBinding(defaults, map[slot]);
            bindings.Add(physical with { Action = action.Action, Note = action.Note });
        }
        foreach (var chord in new[] { (Slot: "both_sticks", Left: "left_stick_click", Right: "right_stick_click"),
                                      (Slot: "both_grips", Left: "left_grip", Right: "right_grip"),
                                      (Slot: "cyberware", Left: "left_stick_click", Right: "left_grip") })
        {
            var index = bindings.FindIndex(b => b.Slot == chord.Slot);
            if (index < 0) continue;
            var left = map.Single(p => p.Value == chord.Left).Key;
            var right = map.Single(p => p.Value == chord.Right).Key;
            if (left != chord.Left || right != chord.Right)
                bindings[index] = bindings[index] with { Control = Label(defaults.Id, left) + " + " + Label(defaults.Id, right) };
        }
        var custom = map.Any(p => p.Key != p.Value);
        return defaults with { Bindings = bindings, Context = custom
            ? defaults.CustomContext ?? "Your saved assignments for this game and controller. Gestures use their labeled controls. Changes apply on the next VR launch."
            : defaults.Context };
    }

    public ControllerReference ApplySaved(ControllerReference reference)
    {
        var custom = false;
        var devices = reference.Devices.Select(device =>
        {
            var map = Load(reference.GameSlug, device.Id);
            custom |= map.Any(p => p.Key != p.Value);
            return Apply(ForEditing(reference.GameSlug, device), map);
        }).ToArray();
        return reference with { Devices = devices, Note = custom
            ? "Saved AVRcade assignments for this game. Game prompts may show the default button; use the assignments below."
            : reference.Note };
    }

    // Games without an authored guide still get the same editor. Never invent
    // game action names or claim that a generic reference was headset verified.
    public static ControllerReference Generic(string game) => new("unverified", game, "Controller assignments",
        new[] { "quest-touch", "valve-index" }.Select(device => new ControllerDeviceReference(device,
            device == "quest-touch" ? "Meta Quest Touch" : "Valve Index Controllers",
            Slots.Select(slot => new ControllerBindingReference(slot, Label(device, slot),
                "Original " + Label(device, slot).ToLowerInvariant() + " action")).Concat(new[] {
                    new ControllerBindingReference("left_stick", "Left stick tilt", "Original movement"),
                    new ControllerBindingReference("right_stick", "Right stick tilt", "Original view / movement") }).ToArray())).ToArray(),
        Note: "This conversion has no authored action guide yet. Assignments move its existing controls; action names remain those supplied by the game. Headset verification is pending.");
}
