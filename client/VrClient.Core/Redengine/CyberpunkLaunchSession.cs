namespace VrClient.Core.Redengine;

using System.Text.Json;
using VrClient.Core.Launch;

public enum CyberpunkLaunchMode
{
    Flat,
    Vr
}

public sealed class CyberpunkLaunchSession
{
    public const string ActivationVariable = "VRCLIENT_VR_ACTIVE";

    /// The upstream configuration dialog waits on the game thread and can trip
    /// Cyberpunk's watchdog. Managed starts use the upstream automatic mode,
    /// preserving the player's saved headset, render resolution and preferences.
    public string? PrepareManagedLauncher(string gameRoot, CyberpunkLaunchMode mode, bool dryRun = false)
    {
        if (dryRun || mode is not CyberpunkLaunchMode.Vr) return null;
        var path = CyberpunkUpstreamSession.Resolve(gameRoot, "bin/x64/vrport-launcher.ini");
        var exists = File.Exists(path);
        var original = exists ? File.ReadAllText(path) : "";
        var newline = original.Contains("\r\n", StringComparison.Ordinal) ? "\r\n" : "\n";
        const string key = @"(?m)^show_launcher[ \t]*=[^\r\n]*";
        var changed = System.Text.RegularExpressions.Regex.IsMatch(original, key)
            ? System.Text.RegularExpressions.Regex.Replace(original, key, "show_launcher=0")
            : original + (original.Length > 0 && !original.EndsWith('\n') ? newline : "") + "show_launcher=0" + newline;
        if (!exists) changed += "launcher_delay_ms=1000" + newline;
        if (exists && changed == original) return null;

        string? backup = null;
        if (exists)
        {
            var hash = Hashing.Sha256OfFile(path);
            backup = CyberpunkUpstreamSession.Resolve(gameRoot,
                CyberpunkUpstreamSession.StoreDirectory + "/launcher-config-backups/" + hash + ".ini");
            Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
            if (!File.Exists(backup)) File.Copy(path, backup, overwrite: false);
            if (!Hashing.Sha256OfFile(backup).Equals(hash, StringComparison.OrdinalIgnoreCase))
                throw new IOException("The Cyberpunk launcher-settings backup changed. Both copies were preserved.");
            if (!Hashing.Sha256OfFile(path).Equals(hash, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Cyberpunk launcher settings changed during setup. Close the game and Recheck setup.");
        }
        AtomicFile.WriteAllText(path, changed);
        return backup;
    }

    public static bool IsVrActive(IReadOnlyDictionary<string, string> environment) =>
        environment.TryGetValue(ActivationVariable, out var value) &&
        string.Equals(value, "1", StringComparison.Ordinal);

    public IReadOnlyDictionary<string, string> BuildEnvironment(CyberpunkLaunchMode mode) =>
        mode is CyberpunkLaunchMode.Vr
            ? new Dictionary<string, string> { [ActivationVariable] = "1" }
            : new Dictionary<string, string>();

    public IReadOnlyList<string> BuildLaunchArguments(bool withVortex) =>
        withVortex ? ["-modded"] : [];

    public string WriteDiagnostics(
        string artifactsRoot,
        CyberpunkLaunchMode mode,
        string activationSource,
        XrRuntimeChoice? runtime,
        int processId,
        bool withVortex = false,
        bool withMods = false)
    {
        var directory = Path.Combine(artifactsRoot, "redengine", "cyberpunk-2077", "launches");
        Directory.CreateDirectory(directory);
        var now = DateTimeOffset.UtcNow;
        var path = Path.Combine(directory, $"launch-{now:yyyyMMdd-HHmmss-fff}-{processId}.json");
        var document = new
        {
            schema = "vrclient-cyberpunk-launch/1",
            launched_at_utc = now.ToString("O"),
            launch_mode = mode.ToString().ToLowerInvariant(),
            vortex_launch = withVortex,
            redmod_enabled = withMods || withVortex,
            startup_status = processId == -1 ? "dry_run" : "spawned",
            activation_source = activationSource,
            activation_variable_present = mode is CyberpunkLaunchMode.Vr,
            xr_runtime = runtime?.Name,
            xr_runtime_json = runtime?.JsonPath,
            process_id = processId
        };
        File.WriteAllText(path, JsonSerializer.Serialize(document, new JsonSerializerOptions
        {
            WriteIndented = true
        }));
        return path;
    }

    public void WriteStartupResult(string path, GameStartupResult result)
    {
        var document = JsonSerializer.Deserialize<Dictionary<string, object?>>(File.ReadAllText(path))!;
        document["startup_status"] = result.Running ? "process_running" : "startup_failed";
        document["startup_exit_code"] = result.ExitCode;
        document["startup_message"] = result.Message;
        AtomicFile.WriteAllText(path, JsonSerializer.Serialize(document, new JsonSerializerOptions { WriteIndented = true }));
    }
}
