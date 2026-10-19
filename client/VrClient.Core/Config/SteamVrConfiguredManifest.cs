using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using VrClient.Core.App;

namespace VrClient.Core.Config;

public sealed record ConfiguredManifestBackup(string OriginalBase64, string OriginalHash, string? AppliedHash);

/// Configuration adapter for a native/closed conversion that opens a fixed
/// manifest filename. Save original bytes privately and point only its default
/// binding URLs at generated copies. Reset restores the original manifest.
public static class SteamVrConfiguredManifest
{
    public static ActionOutcome StageBigWalk(ControllerRuntimeBindings runtime, string _, GameView game)
    {
        if (System.Diagnostics.Process.GetProcessesByName("Big Walk").Length != 0)
            return new(false, "Close Big Walk before preparing its controller assignments for a new launch.");
        var roots = runtime.ModRoots.Count > 0 ? runtime.ModRoots : game.InstallDir == null ? [] : new[] { game.InstallDir };
        var found = false;
        foreach (var root in roots.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var manifest = Path.Combine(root, "BepInEx", "patchers", "BigWalkVR.Runtime", "bigwalkvr_actions.json");
            if (!File.Exists(manifest)) continue;
            found = true; Stage(runtime, game.Slug, manifest);
            // The preloader deploys this source into GameRoot before Unity starts.
            // Direct installs already have a deployed copy; refresh that copy too.
            if (string.Equals(root, game.InstallDir, StringComparison.OrdinalIgnoreCase))
            {
                var deployed = Path.Combine(root, "bigwalkvr_actions.json");
                if (File.Exists(deployed)) Stage(runtime, game.Slug, deployed);
            }
        }
        var changed = new[] { "quest-touch", "valve-index" }.Any(d => runtime.Settings.Load(game.Slug, d).Any(p => p.Key != p.Value));
        return found || !changed ? new(true, "Big Walk controller bindings prepared.")
            : new(false, "Big Walk's VR manifest was not found in this installation/profile. Install its VR mod before launching customized controls.");
    }

    public static void Stage(ControllerRuntimeBindings runtime, string game, string source)
    {
        var key = Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(Path.GetFullPath(source).ToLowerInvariant())));
        var folder = Path.Combine(runtime.Settings.Root, game, "manifest-originals", key);
        var recordFile = Path.Combine(folder, "original.json");
        var currentBytes = File.ReadAllBytes(source); var currentHash = Convert.ToHexString(SHA256.HashData(currentBytes));
        ConfiguredManifestBackup? record = File.Exists(recordFile)
            ? JsonSerializer.Deserialize<ConfiguredManifestBackup>(File.ReadAllText(recordFile)) : null;
        var maps = new[] { "quest-touch", "valve-index" }.Select(d => runtime.Settings.Load(game, d)).ToArray();
        var customized = maps.Any(m => m.Any(p => p.Key != p.Value));
        if (record != null && currentHash != record.OriginalHash && currentHash != record.AppliedHash)
        {
            if (customized)
                throw new InvalidDataException("The SteamVR manifest changed outside AVRcade. Backups and the current file were preserved. Reset both controller families and launch once to adopt the updated profile defaults.");
            File.Copy(recordFile, recordFile + "." + Guid.NewGuid().ToString("N") + ".bak");
            record = null; // Explicit reset adopts the updated source without overwriting it.
        }
        record ??= new(Convert.ToBase64String(currentBytes), currentHash, null);
        var originalBytes = Convert.FromBase64String(record.OriginalBase64);
        if (Convert.ToHexString(SHA256.HashData(originalBytes)) != record.OriginalHash)
            throw new InvalidDataException("Controller manifest backup is invalid; no file was replaced.");
        if (!customized)
        {
            if (currentHash == record.AppliedHash) WriteBytes(source, originalBytes);
            AtomicFile.WriteAllText(recordFile, JsonSerializer.Serialize(record with { AppliedHash = null }));
            return;
        }
        var original = JsonNode.Parse(originalBytes) ?? throw new InvalidDataException("Empty SteamVR manifest.");
        if (original["action_sets"] is not JsonArray sets || !sets.Any(s => s?["name"]?.GetValue<string>() == "/actions/bigwalkvr"))
            throw new InvalidDataException("This is not a Big Walk controller manifest.");
        var compilationSource = original.DeepClone();
        foreach (var binding in compilationSource["default_bindings"]!.AsArray())
            binding!["binding_url"] = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(source)!, binding["binding_url"]!.GetValue<string>()));
        var defaultsFile = Path.Combine(folder, "defaults.json");
        AtomicFile.WriteAllText(defaultsFile, compilationSource.ToJsonString());
        var generated = runtime.BuildSteamVrManifest(game, defaultsFile);
        var rewritten = JsonNode.Parse(File.ReadAllText(generated))!;
        // Only the binding URLs change in the fixed native manifest; all action
        // declarations and the original binding files remain intact.
        original["default_bindings"] = rewritten["default_bindings"]!.DeepClone();
        var content = original.ToJsonString();
        var hash = Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(content)));
        AtomicFile.WriteAllText(recordFile, JsonSerializer.Serialize(record with { AppliedHash = hash }));
        AtomicFile.WriteAllText(source, content);
    }
    private static void WriteBytes(string path, byte[] bytes)
    {
        var temporary = path + ".avrcade.tmp"; File.WriteAllBytes(temporary, bytes); File.Move(temporary, path, true);
    }
}
