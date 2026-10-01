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
        bool withVortex = false)
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
}
