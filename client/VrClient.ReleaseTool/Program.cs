using VrClient.Core.Distribution;
using VrClient.Core.ReleaseDiagnostics;

if (args.Length == 0 || args[0] is "help" or "--help" or "-h")
{
    Console.WriteLine("VRClient Release Tool");
    Console.WriteLine("  manifest <path>                 Validate and list a local release manifest");
    Console.WriteLine("  doctor <runtime> <health> <rollback> <writable>");
    Console.WriteLine("    runtime: available|missing; health: healthy|missing|corrupt; booleans: true|false");
    return;
}

if (args[0] == "manifest" && args.Length == 2)
{
    var manifest = StaticReleaseManifestReader.Load(args[1]);
    Console.WriteLine($"schema={manifest.SchemaVersion} packages={manifest.Packages.Count}");
    foreach (var package in manifest.Packages)
        Console.WriteLine($"{package.PackageId}@{package.Version} [{package.Channel}]");
    return;
}

if (args[0] == "doctor" && args.Length == 5 &&
    TryRuntime(args[1], out var runtime) && TryHealth(args[2], out var health) &&
    bool.TryParse(args[3], out var rollback) && bool.TryParse(args[4], out var writable))
{
    var report = ReleaseDoctor.Evaluate(new ReleaseDoctorInput(runtime, health, rollback, writable));
    Console.WriteLine(report.CanLaunch ? "launch-ready" : "launch-blocked");
    foreach (var finding in report.Findings)
        Console.WriteLine($"{finding.Severity}: {finding.Code} — {finding.Message}");
    return;
}

Console.Error.WriteLine("Invalid command. Run 'help' for usage.");
Environment.ExitCode = 2;

static bool TryRuntime(string value, out bool runtime)
{
    runtime = value == "available";
    return value is "available" or "missing";
}

static bool TryHealth(string value, out ReleasePackageHealth health)
{
    health = value switch
    {
        "healthy" => ReleasePackageHealth.Healthy,
        "missing" => ReleasePackageHealth.Missing,
        "corrupt" => ReleasePackageHealth.Corrupt,
        _ => default
    };
    return value is "healthy" or "missing" or "corrupt";
}
