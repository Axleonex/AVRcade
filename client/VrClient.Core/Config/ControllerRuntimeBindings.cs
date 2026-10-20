using System.Text;
using System.Text.Json.Nodes;
using Microsoft.Win32;
using VrClient.Core.App;

namespace VrClient.Core.Config;

/// Small runtime adapters share the same validated per-game permutations.
/// Defaults are inputs, never output files; SteamVR receives generated copies.
public sealed class ControllerRuntimeBindings(ControllerCustomization? settings = null,
    IReadOnlyDictionary<string, ControllerBindingAdapter>? adapters = null)
{
    private readonly ControllerCustomization _settings = settings ?? new();
    public ControllerCustomization Settings => _settings;
    public IReadOnlyList<string> ModRoots { get; private set; } = [];
    private readonly IReadOnlyDictionary<string, ControllerBindingAdapter> _adapters = adapters ?? ControllerBindingAdapters.BuiltIn;
    private static readonly string[] Devices = ["quest-touch", "valve-index"];

    public static string OpenXrPath(string device, string slot) => slot switch
    {
        "left_primary" => device == "valve-index" ? "/user/hand/left/input/a/click" : "/user/hand/left/input/x/click",
        "left_secondary" => device == "valve-index" ? "/user/hand/left/input/b/click" : "/user/hand/left/input/y/click",
        "right_primary" => "/user/hand/right/input/a/click", "right_secondary" => "/user/hand/right/input/b/click",
        "left_stick_click" => "/user/hand/left/input/thumbstick/click", "right_stick_click" => "/user/hand/right/input/thumbstick/click",
        "left_trigger" => "/user/hand/left/input/trigger/value", "right_trigger" => "/user/hand/right/input/trigger/value",
        "left_grip" => "/user/hand/left/input/squeeze/" + (device == "valve-index" ? "force" : "value"),
        "right_grip" => "/user/hand/right/input/squeeze/" + (device == "valve-index" ? "force" : "value"),
        _ => throw new InvalidDataException("Unknown input.")
    };

    public string BuildOpenXr(string game)
    {
        var text = new StringBuilder();
        foreach (var device in Devices)
            foreach (var pair in _settings.Load(game, device).Where(p => p.Key != p.Value))
            {
                // Runtime suggestions use the old path; replace with its new physical input.
                text.AppendLine(device + "|" + OpenXrPath(device, pair.Value) + "|" + OpenXrPath(device, pair.Key));
                if (pair.Value.EndsWith("_trigger"))
                    text.AppendLine(device + "|" + OpenXrPath(device, pair.Value).Replace("/value", "/click") + "|" + OpenXrPath(device, pair.Key));
                if (device == "valve-index" && pair.Value.EndsWith("_grip"))
                    text.AppendLine(device + "|" + OpenXrPath(device, pair.Value).Replace("/force", "/value") + "|" + OpenXrPath(device, pair.Key));
            }
        return text.ToString();
    }

    public ActionOutcome Prepare(string repoRoot, GameView game, IReadOnlyList<string>? modRoots = null)
    {
        try
        {
            ModRoots = modRoots ?? [];
            var runtime = Path.Combine(_settings.Root, "runtime");
            var file = Path.Combine(runtime, game.Slug + ".bindings");
            var mapping = BuildOpenXr(game.Slug);
            var adapter = _adapters.GetValueOrDefault(game.Slug, ControllerBindingAdapters.OpenXr);
            var routesFile = Path.Combine(runtime, "routes.txt");
            var routes = File.Exists(routesFile) ? File.ReadAllLines(routesFile).ToList() : [];
            // Settings migration copies routes verbatim. This game's legacy binding
            // path is still its own route, not a conflict with another game.
            var legacyFile = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "VRClient", "controllers", "runtime", game.Slug + ".bindings");
            routes.RemoveAll(line => line.EndsWith("|" + file, StringComparison.OrdinalIgnoreCase) ||
                line.EndsWith("|" + legacyFile, StringComparison.OrdinalIgnoreCase));
            if (adapter.UsesOpenXr && game.InstallDir != null)
                foreach (var executable in Executables(repoRoot, game))
                {
                    if (routes.Any(line => line.StartsWith(executable + "|", StringComparison.OrdinalIgnoreCase)))
                        throw new InvalidDataException("Another game already owns this executable's controller route.");
                    routes.Add(executable + "|" + file);
                }
            var layer = Path.Combine(repoRoot, "native", "controller-remap", "package", "layer.json");
            if (adapter.UsesOpenXr && !string.IsNullOrWhiteSpace(mapping))
            {
                if (!File.Exists(layer) || !File.Exists(Path.Combine(Path.GetDirectoryName(layer)!, "avrcade-controller-remap.dll")))
                    throw new InvalidDataException("Controller remapping support is missing. Repair AVRcade or reset these assignments.");
                if (OperatingSystem.IsWindows())
                {
                    foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
                    {
                        var manifest = view == RegistryView.Registry64 ? layer : Path.Combine(Path.GetDirectoryName(layer)!, "x86", "layer.json");
                        if (!File.Exists(manifest)) throw new InvalidDataException("The 32-bit controller remapping support is missing. Repair AVRcade.");
                        using var registry = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, view);
                        using var key = registry.CreateSubKey(@"Software\Khronos\OpenXR\1\ApiLayers\Implicit");
                        key.SetValue(Path.GetFullPath(manifest), 0, RegistryValueKind.DWord);
                    }
                }
            }
            if (adapter.Stage?.Invoke(this, repoRoot, game) is { Ok: false } failure) return failure;
            AtomicFile.WriteAllText(file, mapping);
            AtomicFile.WriteAllText(routesFile, string.Join("\n", routes));
            return new(true, "Saved controller assignments prepared for the next VR launch.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Text.Json.JsonException or ArgumentException)
        { return new(false, "Could not prepare controller assignments: " + ex.Message); }
    }

    public static IReadOnlyList<string> Executables(string repoRoot, GameView game)
    {
        if (game.InstallDir == null) return [];
        var root = Path.GetFullPath(game.InstallDir);
        var candidates = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var directory in new[] { "games", "catalogue", "unreal", "gta-vr" })
            foreach (var suffix in directory == "unreal" ? new[] { ".uevr.json" } : new[] { ".json" })
            {
                var config = Path.Combine(repoRoot, "config", directory, game.Slug + suffix);
                if (!File.Exists(config)) continue;
                var json = JsonNode.Parse(File.ReadAllText(config))!;
                if (json["executable_names"] is JsonArray names)
                    foreach (var name in names) if (name?.GetValue<string>() is { } text) candidates.Add(text);
                if (json["executable"]?.GetValue<string>() is { } exe) candidates.Add(exe);
                if (json["executable_roles"]?["shipping_binary"]?.GetValue<string>() is { } shipping) candidates.Add(shipping);
            }
        // Native adapters may not carry a convention catalogue file yet.
        if (candidates.Count == 0 && Directory.Exists(root))
        {
            var nativeExecutable = game.Slug switch
            {
                "fallout-3" => "Fallout3.exe", "fallout-new-vegas" => "FalloutNV.exe",
                "gta-san-andreas" => "gta_sa.exe", "cyberpunk-2077" => "bin/x64/Cyberpunk2077.exe", _ => null
            };
            if (nativeExecutable != null) candidates.Add(nativeExecutable);
            else foreach (var exe in Directory.EnumerateFiles(root, "*.exe", SearchOption.TopDirectoryOnly)) candidates.Add(exe);
        }
        return candidates.Select(path => Path.GetFullPath(Path.Combine(root, path.Replace('/', Path.DirectorySeparatorChar))))
            .Where(path => path.StartsWith(root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
                && File.Exists(path)).ToArray();
    }

    public string BuildSteamVrManifest(string game, string defaultManifest)
    {
        var directory = Path.Combine(_settings.Root, game, "steamvr");
        Directory.CreateDirectory(directory);
        var manifest = JsonNode.Parse(File.ReadAllText(defaultManifest))!;
        var baseDirectory = Path.GetDirectoryName(defaultManifest)!;
        if (manifest["default_bindings"] is not JsonArray bindings)
            throw new InvalidDataException("SteamVR action manifest lacks default bindings.");
        foreach (var binding in bindings)
        {
            var url = binding!["binding_url"]!.GetValue<string>();
            var original = Path.GetFullPath(Path.Combine(baseDirectory, url));
            var type = binding["controller_type"]!.GetValue<string>();
            var device = type switch { "oculus_touch" => "quest-touch", "knuckles" => "valve-index", _ => null };
            if (device == null) { binding["binding_url"] = original; continue; }
            var map = _settings.Load(game, device);
            var chordMap = new Dictionary<string, string>(map);
            if (game is "yapyap" or "gta-iv")
                foreach (var slot in ControllerCustomization.Slots.Where(s => ControllerCustomization.Group(s).EndsWith("analog")))
                    map[slot] = slot; // The game bridge converts boolean grips and analog triggers.
            var content = RemapSteamVr(File.ReadAllText(original), device, map, chordMap);
            var output = Path.Combine(directory, type + ".json");
            AtomicFile.WriteAllText(output, content); binding["binding_url"] = output;
        }
        var result = Path.Combine(directory, "actions.json");
        AtomicFile.WriteAllText(result, manifest.ToJsonString()); return result;
    }

    public static string RemapSteamVr(string content, string device, IReadOnlyDictionary<string, string> map,
        IReadOnlyDictionary<string, string>? chordMap = null)
    {
        ControllerCustomization.Validate(map);
        var json = JsonNode.Parse(content)!;
        var replacements = map.ToDictionary(p => SteamPath(device, p.Value), p => SteamPath(device, p.Key));
        ControllerCustomization.Validate(chordMap ?? map);
        var chordsReplacement = (chordMap ?? map).ToDictionary(p => SteamPath(device, p.Value), p => SteamPath(device, p.Key));
        if (json["bindings"] is not JsonObject sets) throw new InvalidDataException("Invalid SteamVR binding file.");
        foreach (var set in sets.Select(p => p.Value).OfType<JsonObject>())
        {
            if (set["sources"] is JsonArray sources)
            {
                var edited = new JsonArray();
                foreach (var source in sources.OfType<JsonObject>())
                {
                    var path = source["path"]!.GetValue<string>();
                    var old = source.DeepClone().AsObject();
                    if (source["inputs"] is JsonObject inputs)
                        foreach (var input in inputs.ToArray())
                        {
                            // Stick press can move without moving its position/tilt binding.
                            if (input.Key is "touch" or "position") continue;
                            var analogSource = replacements.TryGetValue(path, out var analogTarget) && path != analogTarget;
                            var digital = input.Key == "click" && !analogSource;
                            var key = digital ? path + "/click" : path;
                            if (!replacements.TryGetValue(key, out var target) || target == key) continue;
                            var copy = source.DeepClone().AsObject();
                            var inputName = input.Key == "grab" ? "click" : input.Key;
                            copy["inputs"] = new JsonObject { [inputName] = input.Value?.DeepClone() };
                            copy["path"] = digital ? target[..^6] : target;
                            if (digital) copy["mode"] = target.Contains("joystick") || target.Contains("thumbstick") ? "joystick" : "button";
                            else copy["mode"] = "trigger"; // Touch/Index grip and trigger are trigger-type sources.
                            copy.Remove("parameters");
                            edited.Add(copy); old["inputs"]!.AsObject().Remove(input.Key);
                        }
                    if (old["inputs"] is not JsonObject remaining || remaining.Count > 0) edited.Add(old);
                }
                set["sources"] = edited;
            }
            if (set["chords"] is JsonArray chords)
                foreach (var chord in chords.OfType<JsonObject>())
                    if (chord["inputs"] is JsonArray inputs)
                        foreach (var input in inputs.OfType<JsonArray>())
                        {
                            var key = input[0]!.GetValue<string>() + "/" + input[1]!.GetValue<string>();
                            if (!chordsReplacement.TryGetValue(key, out var target))
                            {
                                var path = input[0]!.GetValue<string>();
                                if (!chordsReplacement.TryGetValue(path, out var targetPath)) continue;
                                target = targetPath + "/" + input[1]!.GetValue<string>();
                            }
                            input[0] = target[..target.LastIndexOf('/')]; input[1] = target[(target.LastIndexOf('/') + 1)..];
                        }
        }
        return json.ToJsonString();
    }

    private static string SteamPath(string device, string slot)
    {
        var xr = OpenXrPath(device, slot).Replace("/input/squeeze/", "/input/grip/");
        if (slot.EndsWith("_trigger") || slot.EndsWith("_grip")) return xr[..xr.LastIndexOf('/')];
        return device == "quest-touch" ? xr.Replace("thumbstick", "joystick") : xr;
    }
}
