using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace VrClient.Core.FalloutNewVegas;

public sealed record FnvVortexSyncPlan(
    string GameDirectory, string ProfileDirectory, string SourcePath,
    IReadOnlyList<string> VortexPlugins, IReadOnlyList<string> VortexLoadOrder,
    IReadOnlyList<string> TargetPlugins, IReadOnlyList<string> TargetLoadOrder,
    IReadOnlyList<string> BaselinePlugins, IReadOnlyList<string> BaselineLoadOrder,
    string SourceHash, string PluginsHash, string LoadOrderHash, string? StateHash);

public sealed record FnvVortexSyncResult(bool Changed, int ImportedCount);
public sealed record FnvVortexSyncValidation(bool Ready, string Message);

/// <summary>Imports only the effective, deployed FNV plugin list into AVRcade's isolated MO2 profile.</summary>
public sealed class FalloutNewVegasVortexSyncService
{
    private const string StateName = "avrcade-vortex-sync.json";
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public FnvVortexSyncPlan Preview(string gameDirectory, string profileDirectory, string localAppData)
    {
        var source = Path.Combine(localAppData, "FalloutNV", "plugins.txt");
        var sourceOrder = Path.Combine(localAppData, "FalloutNV", "loadorder.txt");
        var plugins = Path.Combine(profileDirectory, "plugins.txt");
        var loadOrder = Path.Combine(profileDirectory, "loadorder.txt");
        var statePath = Path.Combine(profileDirectory, StateName);
        if (!File.Exists(source))
            throw new InvalidOperationException("FalloutNV/plugins.txt is missing. Deploy and enable your New Vegas mods in Vortex first.");
        if (!File.Exists(plugins) || !File.Exists(loadOrder))
            throw new InvalidOperationException("Prepare the isolated MO2 VR profile before syncing Vortex plugins.");
        if (!Directory.Exists(Path.Combine(gameDirectory, "Data")))
            throw new InvalidOperationException("The selected New Vegas game has no Data folder.");

        var sourceText = File.ReadAllText(source);
        var pluginText = File.ReadAllText(plugins);
        var loadOrderText = File.ReadAllText(loadOrder);
        var vortex = Parse(sourceText, "Vortex source");
        var vortexOrder = File.Exists(sourceOrder)
            ? Parse(File.ReadAllText(sourceOrder), "Vortex load order")
                .Where(name => vortex.Contains(name, StringComparer.OrdinalIgnoreCase))
                .Concat(vortex).Distinct(StringComparer.OrdinalIgnoreCase).ToArray()
            : vortex;
        var existing = Parse(pluginText, "VR profile");
        var existingOrder = Parse(loadOrderText, "VR load order");
        foreach (var name in vortex)
            if (!Directory.EnumerateFiles(Path.Combine(gameDirectory, "Data"))
                    .Any(path => string.Equals(Path.GetFileName(path), name, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidOperationException($"{name} is listed as active but is not deployed in the New Vegas Data folder. Deploy Vortex mods first.");

        SyncState? state = null;
        string? stateHash = null;
        if (File.Exists(statePath))
        {
            var stateText = File.ReadAllText(statePath);
            stateHash = Hash(stateText);
            state = JsonSerializer.Deserialize<SyncState>(stateText)
                ?? throw new InvalidOperationException("The VR profile's Vortex sync state is invalid.");
            if (state.Version != 1 || state.BaselinePlugins is null || state.BaselineLoadOrder is null)
                throw new InvalidOperationException("The VR profile's Vortex sync state has an unsupported format.");
            if (state.PluginsHash != Hash(pluginText) || state.LoadOrderHash != Hash(loadOrderText))
                throw new InvalidOperationException("The VR profile changed since Vortex sync. Review those MO2 edits before syncing again; AVRcade will not overwrite them.");
        }
        var baseline = state?.BaselinePlugins ?? existing;
        var baselineOrder = state?.BaselineLoadOrder ?? existingOrder;
        var target = vortex.Concat(baseline).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        var targetOrder = vortexOrder.Concat(baselineOrder).Concat(baseline)
            .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        return new FnvVortexSyncPlan(gameDirectory, profileDirectory, source, vortex, vortexOrder, target, targetOrder,
            baseline, baselineOrder, Hash(string.Join("\n", vortex) + "\0" + string.Join("\n", vortexOrder)),
            Hash(pluginText), Hash(loadOrderText), stateHash);
    }

    public FnvVortexSyncResult Apply(FnvVortexSyncPlan plan)
    {
        // A preview is a snapshot, not permission to overwrite files modified afterward.
        var fresh = Preview(plan.GameDirectory, plan.ProfileDirectory,
            Path.GetDirectoryName(Path.GetDirectoryName(plan.SourcePath)!)!);
        if (fresh.SourceHash != plan.SourceHash || fresh.PluginsHash != plan.PluginsHash ||
            fresh.LoadOrderHash != plan.LoadOrderHash || fresh.StateHash != plan.StateHash ||
            !fresh.TargetPlugins.SequenceEqual(plan.TargetPlugins, StringComparer.OrdinalIgnoreCase) ||
            !fresh.TargetLoadOrder.SequenceEqual(plan.TargetLoadOrder, StringComparer.OrdinalIgnoreCase) ||
            !fresh.BaselinePlugins.SequenceEqual(plan.BaselinePlugins, StringComparer.OrdinalIgnoreCase))
            throw new InvalidOperationException("Vortex or the VR profile changed after preview. Preview the sync again.");

        var pluginsPath = Path.Combine(plan.ProfileDirectory, "plugins.txt");
        var loadOrderPath = Path.Combine(plan.ProfileDirectory, "loadorder.txt");
        var statePath = Path.Combine(plan.ProfileDirectory, StateName);
        var targetText = string.Join("\n", plan.TargetPlugins) + (plan.TargetPlugins.Count > 0 ? "\n" : "");
        var targetOrderText = string.Join("\n", plan.TargetLoadOrder) + (plan.TargetLoadOrder.Count > 0 ? "\n" : "");
        // MO2's Gamebryo profile stores bare enabled plugin names, not Vortex's '*' prefix.
        var changed = File.ReadAllText(pluginsPath) != targetText || File.ReadAllText(loadOrderPath) != targetOrderText;
        if (!changed && File.Exists(statePath)) return new FnvVortexSyncResult(false, plan.VortexPlugins.Count);

        var backupDirectory = Path.Combine(plan.ProfileDirectory, "avrcade-vortex-backups");
        Directory.CreateDirectory(backupDirectory);
        var stamp = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fffffff");
        File.Copy(pluginsPath, Path.Combine(backupDirectory, stamp + "-plugins.txt"));
        File.Copy(loadOrderPath, Path.Combine(backupDirectory, stamp + "-loadorder.txt"));
        AtomicWrite(pluginsPath, targetText);
        AtomicWrite(loadOrderPath, targetOrderText);
        var state = new SyncState(1, plan.BaselinePlugins.ToArray(), plan.BaselineLoadOrder.ToArray(),
            plan.VortexPlugins.ToArray(), plan.SourceHash, Hash(targetText), Hash(targetOrderText));
        AtomicWrite(statePath, JsonSerializer.Serialize(state, JsonOptions));
        return new FnvVortexSyncResult(true, plan.VortexPlugins.Count);
    }

    public FnvVortexSyncValidation Validate(string gameDirectory, string profileDirectory, string localAppData)
    {
        try
        {
            var statePath = Path.Combine(profileDirectory, StateName);
            if (!File.Exists(statePath)) return new(false, "Preview and apply Vortex plugin sync before launch.");
            var plan = Preview(gameDirectory, profileDirectory, localAppData);
            var state = JsonSerializer.Deserialize<SyncState>(File.ReadAllText(statePath))!;
            if (state.SourceHash != plan.SourceHash)
                return new(false, "The Vortex plugin list changed. Preview and apply sync again.");
            return new(true, $"Vortex plugin list is synced ({plan.VortexPlugins.Count} deployed plugins).");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidOperationException or JsonException)
        {
            return new(false, error.Message);
        }
    }

    private static string[] Parse(string content, string label)
    {
        var result = new List<string>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var raw in content.Split('\n'))
        {
            var line = raw.Trim().TrimStart('\uFEFF');
            if (line.Length == 0 || line.StartsWith('#')) continue;
            line = line.TrimStart('*').Trim();
            if (line != Path.GetFileName(line) || line.IndexOfAny(['/', '\\', ':', '\0']) >= 0 ||
                !(line.EndsWith(".esp", StringComparison.OrdinalIgnoreCase) || line.EndsWith(".esm", StringComparison.OrdinalIgnoreCase)) ||
                line is ".esp" or ".esm" || !seen.Add(line))
                throw new InvalidOperationException($"{label} contains an invalid or duplicate plugin: {raw.Trim()}");
            result.Add(line);
        }
        return result.ToArray();
    }

    private static string Hash(string value) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value)));

    private static void AtomicWrite(string path, string text)
    {
        var temporary = path + ".tmp-" + Guid.NewGuid().ToString("N");
        File.WriteAllText(temporary, text);
        File.Move(temporary, path, overwrite: true);
    }

    private sealed record SyncState(int Version, string[] BaselinePlugins, string[] BaselineLoadOrder,
        string[] ImportedPlugins, string SourceHash, string PluginsHash, string LoadOrderHash);
}
