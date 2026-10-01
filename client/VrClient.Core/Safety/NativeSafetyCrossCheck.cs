namespace VrClient.Core.Safety;
using System.Diagnostics;
using System.Text.RegularExpressions;

/// Non-blocking cross-check against the native rules-level CLI (vrclient_safety_cli).
/// The .NET SafetyGate remains the runtime gate (M5D1); this only surfaces the native
/// engine's rule-level facts when its CLI is present, else reports Unavailable.
public sealed record CrossCheckResult(bool Available, bool RuleFound, bool Permitted, string Modes)
{
    public static readonly CrossCheckResult Unavailable = new(false, false, false, "");
}

public static class NativeSafetyCrossCheck
{
    /// Locate the CLI: VRCLIENT_SAFETY_CLI env var first, else the documented relative
    /// build path. Returns null when neither exists.
    public static string? LocateCli()
        => VrClient.Core.NativeCliLocator.Locate("vrclient_safety_cli.exe", "VRCLIENT_SAFETY_CLI");

    public static CrossCheckResult Run(string? cliPath, string gameConfigPath, string rulesPath, string mode)
    {
        if (string.IsNullOrEmpty(cliPath) || !File.Exists(cliPath))
            return CrossCheckResult.Unavailable;

        var psi = new ProcessStartInfo
        {
            FileName = cliPath,
            RedirectStandardOutput = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        psi.ArgumentList.Add("--game"); psi.ArgumentList.Add(gameConfigPath);
        psi.ArgumentList.Add("--rules"); psi.ArgumentList.Add(rulesPath);
        psi.ArgumentList.Add("--mode"); psi.ArgumentList.Add(mode);

        try
        {
            using var process = Process.Start(psi);
            if (process is null)
                return CrossCheckResult.Unavailable;
            var stdout = process.StandardOutput.ReadToEnd();
            process.WaitForExit(10_000);
            return Parse(stdout);
        }
        catch
        {
            return CrossCheckResult.Unavailable;
        }
    }

    /// Parse a `RULECHECK game_id=<..> rule_found=<..> permitted=<..> modes=<..>` line.
    internal static CrossCheckResult Parse(string stdout)
    {
        var line = stdout.Split('\n').FirstOrDefault(l => l.StartsWith("RULECHECK", StringComparison.Ordinal));
        if (line is null)
            return CrossCheckResult.Unavailable;
        var ruleFound = Field(line, "rule_found") == "true";
        var permitted = Field(line, "permitted") == "true";
        var modes = Field(line, "modes");
        return new CrossCheckResult(true, ruleFound, permitted, modes);
    }

    private static string Field(string line, string key)
    {
        var m = Regex.Match(line, $@"\b{Regex.Escape(key)}=(\S*)");
        return m.Success ? m.Groups[1].Value : "";
    }
}
