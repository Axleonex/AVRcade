namespace VrClient.Core.Launch;
using System.Diagnostics;
using System.Text.Json;
using VrClient.Core.Model;

public sealed class GameLauncher
{
    /// Build a LaunchPlan: verify REPO.exe exists, mod is installed, and pass through the
    /// safety verdict. Does NOT start the process.
    public LaunchPlan Plan(string gameDir, SafetyVerdict safety, bool modInstalled, string? exeName = null)
        => new(Path.Combine(gameDir, string.IsNullOrEmpty(exeName) ? "REPO.exe" : exeName), modInstalled, safety);

    /// Start the game process ONLY if plan.Safety.Verdict is Allow (or Warn with
    /// acknowledge==true) and plan.ModPresent. Returns the started process id. Throws
    /// InvalidOperationException with the refusal reason otherwise. dryRun==true returns -1
    /// without starting anything (used by tests + `--dry-run`).
    public int Launch(
        LaunchPlan plan,
        bool acknowledgeWarn,
        bool dryRun,
        XrRuntimeChoice? xrRuntime = null,
        IReadOnlyDictionary<string, string>? childEnvironment = null,
        IReadOnlyList<string>? launchArguments = null)
    {
        if (plan.Safety.Verdict.IsBlocked())
            throw new InvalidOperationException(
                $"launch refused: {plan.Safety.ReasonCode} - {plan.Safety.Explanation}");
        if (plan.Safety.Verdict == Verdict.Warn && !acknowledgeWarn)
            throw new InvalidOperationException(
                $"launch refused: warn_not_acknowledged - {plan.Safety.Explanation} (pass --acknowledge)");
        if (!plan.ModPresent)
            throw new InvalidOperationException(
                "launch refused: mod_not_installed - run 'vrclient convert' first");
        if (dryRun)
            return -1;
        if (!File.Exists(plan.GameExePath))
            throw new InvalidOperationException(
                $"launch refused: game_exe_not_found - {plan.GameExePath}");
        var startInfo = BuildStartInfo(plan, xrRuntime, childEnvironment, launchArguments);
        var process = Process.Start(startInfo)
            ?? throw new InvalidOperationException("launch refused: process_start_failed");
        return process.Id;
    }

    /// Build the exact child-process launch state without mutating VRClient's own environment.
    public ProcessStartInfo BuildStartInfo(
        LaunchPlan plan,
        XrRuntimeChoice? xrRuntime = null,
        IReadOnlyDictionary<string, string>? childEnvironment = null,
        IReadOnlyList<string>? launchArguments = null)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = plan.GameExePath,
            WorkingDirectory = Path.GetDirectoryName(plan.GameExePath)!,
            UseShellExecute = false
        };
        // Pin the game to the chosen OpenXR runtime (loader-spec per-process override),
        // so its frames go to the compositor that actually owns the headset display.
        // Do not leak a parent-process runtime pin into a launch that did not
        // select one explicitly. This keeps --xr-runtime system and the
        // no-override path deterministic for every game adapter.
        startInfo.Environment.Remove("XR_RUNTIME_JSON");
        if (xrRuntime is not null)
            startInfo.Environment["XR_RUNTIME_JSON"] = xrRuntime.JsonPath;
        if (childEnvironment is not null)
        {
            foreach (var (name, value) in childEnvironment)
                startInfo.Environment[name] = value;
        }
        if (launchArguments is not null)
            foreach (var argument in launchArguments)
                startInfo.ArgumentList.Add(argument);
        return startInfo;
    }

    /// Write a SessionEvidence JSON under artifacts/friendslop/<slug>/session-<launchedAtUtc>.json.
    public void WriteEvidence(string artifactsRoot, SessionEvidence evidence)
    {
        var dir = Path.Combine(artifactsRoot, "friendslop", evidence.GameSlug);
        Directory.CreateDirectory(dir);
        var fileStamp = evidence.LaunchedAtUtc.Replace(":", "").Replace("-", "");
        var path = Path.Combine(dir, $"session-{fileStamp}.json");
        var document = new Dictionary<string, object?>
        {
            ["game_slug"] = evidence.GameSlug,
            ["modpack_lock_sha256"] = evidence.ModpackLockSha256,
            ["launched_at_utc"] = evidence.LaunchedAtUtc,
            ["user_confirmed_vr_stereo"] = evidence.UserConfirmedVrStereo,
            ["user_confirmed_head_tracking"] = evidence.UserConfirmedHeadTracking,
            ["notes"] = evidence.Notes
        };
        File.WriteAllText(path, JsonSerializer.Serialize(document, new JsonSerializerOptions { WriteIndented = true }));
    }
}
