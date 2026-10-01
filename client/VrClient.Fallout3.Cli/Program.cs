using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Serialization;
using VrClient.Core.Fallout3;
using VrClient.Core.Launch;

var jsonOptions = new JsonSerializerOptions { WriteIndented = true };
jsonOptions.Converters.Add(new JsonStringEnumConverter());
string? Option(string name)
{
    for (var index = 1; index < args.Length - 1; ++index)
        if (args[index].Equals(name, StringComparison.OrdinalIgnoreCase)) return args[index + 1];
    return null;
}
bool Flag(string name) => args.Skip(1).Contains(name, StringComparer.OrdinalIgnoreCase);
int Emit(object value, int exitCode = 0) { Console.WriteLine(JsonSerializer.Serialize(value, jsonOptions)); return exitCode; }

if (args.Length == 0)
{
    Console.Error.WriteLine("usage: fallout3-vr <discover|check|plan|apply|repair|restore|uninstall|compatibility|launch> [options]");
    return 2;
}

var verb = args[0].ToLowerInvariant();
var discovery = new Fallout3Discovery();
var manual = Option("--game-dir");
var steamRoot = Option("--steam-root");
var gogRoot = Option("--gog-dir");
var discovered = discovery.Discover(
    steamRoot is null ? null : new[] { steamRoot },
    gogRoot is null ? null : new[] { gogRoot }, manual);
if (verb == "discover") return Emit(discovered, discovered.Installs.Count == 0 ? 3 : 0);
var install = discovered.Installs.FirstOrDefault()
    ?? throw new InvalidOperationException("Fallout 3 was not found. Pass --game-dir, --steam-root, or --gog-dir.");

Fallout3Backend RequestedBackend() => (Option("--backend") ?? "depth").ToLowerInvariant() switch
{
    "depth" => Fallout3Backend.DepthVr,
    "native" => Fallout3Backend.NativeVr,
    "desktop" => Fallout3Backend.Desktop,
    var value => throw new InvalidOperationException($"unknown backend '{value}' (expected depth, native, or desktop)")
};

string Instance() => Path.GetFullPath(Option("--mo2-instance") ?? throw new InvalidOperationException("missing --mo2-instance <directory>"));
string VrProfileName() => Option("--vr-profile") ?? Fallout3Mo2ProfileService.DefaultProfileName;
string VrProfileDirectory() => Path.Combine(Instance(), "profiles", VrProfileName());
Fallout3DependencyOptions Dependencies()
{
    var nativeAdapter = Option("--native-adapter");
    var nativeProfile = Option("--native-profile") ?? (string.IsNullOrWhiteSpace(nativeAdapter)
        ? null
        : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(nativeAdapter))!, "fallout3-native-profile.ini"));
    return new(
        Option("--mo2-exe") ?? Path.Combine(Instance(), "ModOrganizer.exe"),
        Option("--source-profile-dir"),
        Option("--reshade-dir") ?? install.RootDirectory,
        Option("--depth3d-dir") ?? install.RootDirectory,
        Option("--osiris"),
        nativeAdapter,
        nativeProfile,
        Option("--native-proof"),
        OpenXrRuntimeSelector.GetRunningVrProcessNames(),
        Option("--xr-runtime-manifest"));
}

Fallout3Readiness Readiness() => new Fallout3DependencyValidator().Validate(install, Dependencies(), RequestedBackend());

Fallout3ConversionPlan ConversionPlan(Fallout3ConversionMode mode, bool dryRun) =>
    new Fallout3Mo2ProfileService().Plan(new Fallout3ConversionRequest(
        Instance(), Option("--source-profile") ?? string.Empty, VrProfileName(), install.RootDirectory,
        RequestedBackend(), mode, dryRun, Flag("--acknowledge")));

try
{
    switch (verb)
    {
        case "check":
        {
            var report = Readiness();
            return Emit(report, report.Ready ? 0 : 4);
        }
        case "plan":
            return Emit(ConversionPlan(Fallout3ConversionMode.Convert, true));
        case "apply":
        case "repair":
        case "restore":
        case "uninstall":
        {
            var mode = verb switch
            {
                "repair" => Fallout3ConversionMode.Repair,
                "restore" => Fallout3ConversionMode.Restore,
                "uninstall" => Fallout3ConversionMode.Uninstall,
                _ => Fallout3ConversionMode.Convert
            };
            var plan = ConversionPlan(mode, Flag("--dry-run"));
            if (Flag("--dry-run")) return Emit(plan);
            var result = new Fallout3Mo2ProfileService().Apply(plan);
            return Emit(new { backend = plan.BackendLabel, result });
        }
        case "compatibility":
        {
            var report = new Fallout3CompatibilityAnalyzer().Analyze(
                VrProfileDirectory(), install.RootDirectory, RequestedBackend(), Option("--rules"));
            var output = Option("--output");
            if (!string.IsNullOrWhiteSpace(output))
            {
                Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);
                File.WriteAllText(output, JsonSerializer.Serialize(report, jsonOptions));
            }
            return Emit(report);
        }
        case "launch":
        {
            var readiness = Readiness();
            if (!readiness.Ready) return Emit(readiness, 4);
            var dependencies = Dependencies();
            var runtime = Option("--xr-runtime-manifest");
            var plan = new Fallout3LaunchOrchestrator().BuildPlan(install, readiness,
                dependencies.ModOrganizerExecutablePath!, VrProfileDirectory(),
                Option("--log-dir") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VRClient", "fallout-3", "logs"),
                dependencies.OsirisExecutablePath, runtime, Option("--steamvr-monitor"));
            if (Flag("--dry-run")) return Emit(plan);
            if (!Flag("--acknowledge"))
                throw new InvalidOperationException("launch requires --acknowledge after reviewing the readiness report and launch plan");
            var controller = new SystemFallout3ProcessController();
            using var session = new Fallout3LaunchOrchestrator().Start(plan, controller, false);
            Console.WriteLine(JsonSerializer.Serialize(new { plan.BackendLabel, started = session.Processes }, jsonOptions));
            Console.WriteLine("VRClient is monitoring Fallout3.exe. Press Ctrl+C to stop only VRClient-started helpers.");
            using var cancellation = new CancellationTokenSource();
            Console.CancelKeyPress += (_, eventArgs) => { eventArgs.Cancel = true; cancellation.Cancel(); };
            while (!cancellation.IsCancellationRequested)
            {
                if (Process.GetProcessesByName("Fallout3").Length == 0) break;
                Thread.Sleep(500);
            }
            return 0;
        }
        default:
            Console.Error.WriteLine("unknown verb: " + verb);
            return 2;
    }
}
catch (Exception error) when (error is InvalidOperationException or IOException or UnauthorizedAccessException or ArgumentException)
{
    return Emit(new { error = error.Message, backend = RequestedBackend().ToString() }, 5);
}
