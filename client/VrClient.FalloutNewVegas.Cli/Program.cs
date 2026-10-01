using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Serialization;
using VrClient.Core.FalloutNewVegas;

return await FalloutNewVegasCli.RunAsync(args);

internal static class FalloutNewVegasCli
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.SnakeCaseLower) }
    };

    public static async Task<int> RunAsync(string[] args)
    {
        if (args.Length == 0 || args[0] is "help" or "--help" or "-h")
        {
            PrintHelp();
            return 0;
        }
        try
        {
            var command = args[0].ToLowerInvariant();
            var options = Parse(args.Skip(1).ToArray());
            return command switch
            {
                "discover" => Discover(options),
                "preflight" => Preflight(options),
                "convert" => Convert(options, FnvConversionMode.Convert),
                "repair" => Convert(options, FnvConversionMode.Repair),
                "restore" => Convert(options, FnvConversionMode.Restore),
                "compatibility" => Compatibility(options),
                "comfort" => Comfort(options),
                "launch" => await LaunchAsync(options),
                _ => throw new ArgumentException($"unknown command: {command}")
            };
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(JsonSerializer.Serialize(new
            {
                status = "error",
                message = error.Message,
                detail = error.InnerException?.Message
            }, JsonOptions));
            return 2;
        }
    }

    private static int Discover(IReadOnlyDictionary<string, string?> options)
    {
        var steamRoots = Many(options, "steam-root");
        var gogDirectories = Many(options, "gog-dir");
        var result = new FalloutNewVegasDiscovery().Discover(
            steamRoots.Count == 0 ? null : steamRoots,
            gogDirectories.Count == 0 ? null : gogDirectories,
            Optional(options, "game-dir"));
        Print(result);
        return result.Installs.Count > 0 ? 0 : 1;
    }

    private static int Preflight(IReadOnlyDictionary<string, string?> options)
    {
        var install = Inspect(options);
        var prerequisites = Prerequisites(options);
        var report = new FalloutNewVegasPrerequisiteValidator().Validate(install, prerequisites);
        Print(new { ready = report.Ready, install, checks = report.Checks });
        return report.Ready ? 0 : 1;
    }

    private static int Convert(IReadOnlyDictionary<string, string?> options, FnvConversionMode mode)
    {
        var request = new FnvConversionRequest(
            Required(options, "mo2-instance"),
            Optional(options, "source-profile") ?? string.Empty,
            Required(options, "vr-profile"),
            Required(options, "game-dir"),
            mode,
            Flag(options, "dry-run"),
            Flag(options, "acknowledge"));
        var service = new FalloutNewVegasMo2ProfileService();
        var plan = service.Plan(request);
        if (request.DryRun)
        {
            Print(new { status = "dry_run", plan });
            return 0;
        }
        var result = service.Apply(plan);
        Print(new { status = "applied", plan, result });
        return 0;
    }

    private static int Compatibility(IReadOnlyDictionary<string, string?> options)
    {
        var analyzer = new FalloutNewVegasCompatibilityAnalyzer();
        var report = analyzer.Analyze(
            Required(options, "profile-dir"),
            Required(options, "game-dir"),
            FalloutNewVegasCompatibilityAnalyzer.LoadRules(Required(options, "rules")),
            DateTimeOffset.UtcNow.ToString("O"));
        var output = Required(options, "output");
        analyzer.WriteReport(output, report);
        Print(new { status = "written", output = Path.GetFullPath(output), report });
        return report.Mods.Any(mod => mod.Status == FnvCompatibilityStatus.Incompatible) ? 1 : 0;
    }

    private static int Comfort(IReadOnlyDictionary<string, string?> options)
    {
        var planner = new FalloutNewVegasComfortPlanner();
        var plan = planner.Build(new FnvComfortPreferences(
            Required(options, "play-mode"),
            Required(options, "turn-mode"),
            Required(options, "movement-orientation"),
            Required(options, "dominant-hand"),
            Required(options, "performance-preset")));
        var output = Required(options, "output");
        planner.Write(output, plan);
        Print(new { status = "written", output = Path.GetFullPath(output), plan });
        return 0;
    }

    private static async Task<int> LaunchAsync(IReadOnlyDictionary<string, string?> options)
    {
        var install = Inspect(options);
        var prerequisites = Prerequisites(options);
        var readiness = new FalloutNewVegasPrerequisiteValidator().Validate(install, prerequisites);
        if (!readiness.Ready)
        {
            Print(new { status = "refused", reason = "preflight_failed", checks = readiness.Checks });
            return 1;
        }
        var orchestrator = new FalloutNewVegasLaunchOrchestrator();
        var plan = orchestrator.BuildPlan(install, prerequisites, Required(options, "logs"), Optional(options, "virtual-desktop-exe"));
        if (Flag(options, "dry-run"))
        {
            Print(new { status = "dry_run", plan, checks = readiness.Checks });
            return 0;
        }

        using var session = orchestrator.Start(plan, new SystemFnvProcessController(), dryRun: false);
        Print(new { status = "started", route = plan.UsesVirtualDesktop ? "virtual-desktop-to-steamvr" : "direct-steamvr", session.Processes });
        var game = session.Processes.Last(process => process.Component.Kind == FnvLaunchComponentKind.NewVegas);
        using var cancellation = new CancellationTokenSource();
        Console.CancelKeyPress += (_, eventArgs) =>
        {
            eventArgs.Cancel = true;
            cancellation.Cancel();
        };
        while (!cancellation.IsCancellationRequested && session.CaptureExitCodes()[FnvLaunchComponentKind.NewVegas] is null)
            await Task.Delay(1000, cancellation.Token).ContinueWith(_ => { }, TaskScheduler.Default);
        var exits = session.CaptureExitCodes();
        Print(new { status = cancellation.IsCancellationRequested ? "cancelled" : "exited", game_process_id = game.ProcessId, exit_codes = exits });
        return cancellation.IsCancellationRequested ? 130 : exits[FnvLaunchComponentKind.NewVegas] ?? 0;
    }

    private static FnvGameInstall Inspect(IReadOnlyDictionary<string, string?> options)
    {
        var storefront = Enum.Parse<FnvStorefront>(Optional(options, "storefront") ?? "manual", ignoreCase: true);
        return new FalloutNewVegasDiscovery().Inspect(Required(options, "game-dir"), storefront);
    }

    private static FnvPrerequisiteOptions Prerequisites(IReadOnlyDictionary<string, string?> options) => new(
        Optional(options, "mo2-exe"),
        Optional(options, "profile-dir"),
        Optional(options, "tracker"),
        Optional(options, "native-adapter"),
        Optional(options, "steamvr"),
        Flag(options, "virtual-desktop"),
        RunningProcessNames(),
        FalloutNewVegasPrerequisiteValidator.DetectActiveOpenXrRuntime());

    private static IReadOnlyCollection<string> RunningProcessNames()
    {
        try { return Process.GetProcesses().Select(process => process.ProcessName).ToArray(); }
        catch { return Array.Empty<string>(); }
    }

    private static Dictionary<string, string?> Parse(string[] args)
    {
        var result = new Dictionary<string, string?>(StringComparer.OrdinalIgnoreCase);
        for (var index = 0; index < args.Length; index++)
        {
            var token = args[index];
            if (!token.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException($"expected --option, got: {token}");
            var key = token[2..];
            var value = index + 1 < args.Length && !args[index + 1].StartsWith("--", StringComparison.Ordinal)
                ? args[++index]
                : null;
            if (result.TryGetValue(key, out var existing) && value is not null)
                result[key] = existing is null ? value : existing + "\n" + value;
            else
                result[key] = value;
        }
        return result;
    }

    private static string Required(IReadOnlyDictionary<string, string?> options, string key) =>
        Optional(options, key) ?? throw new ArgumentException($"missing required option --{key}");
    private static string? Optional(IReadOnlyDictionary<string, string?> options, string key) =>
        options.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value) ? value : null;
    private static bool Flag(IReadOnlyDictionary<string, string?> options, string key) => options.ContainsKey(key);
    private static IReadOnlyList<string> Many(IReadOnlyDictionary<string, string?> options, string key) =>
        Optional(options, key)?.Split('\n', StringSplitOptions.RemoveEmptyEntries) ?? Array.Empty<string>();
    private static void Print(object value) => Console.WriteLine(JsonSerializer.Serialize(value, JsonOptions));

    private static void PrintHelp() => Console.WriteLine("""
        VRClient Fallout: New Vegas VR commands

          discover [--steam-root PATH] [--gog-dir PATH] [--game-dir PATH]
          preflight --game-dir PATH [--storefront steam|gog|manual] --mo2-exe PATH --profile-dir PATH --tracker PATH --native-adapter PATH --steamvr PATH [--virtual-desktop]
          convert|repair|restore --mo2-instance PATH [--source-profile NAME] --vr-profile NAME --game-dir PATH --dry-run
          convert|repair|restore ... --acknowledge
          compatibility --profile-dir PATH --game-dir PATH --rules PATH --output PATH
          comfort --play-mode seated|standing --turn-mode snap|smooth --movement-orientation controller|head --dominant-hand left|right --performance-preset quality|balanced|performance --output PATH
          launch --game-dir PATH --storefront steam|gog|manual --mo2-exe PATH --profile-dir PATH --tracker PATH --native-adapter PATH --steamvr PATH --logs PATH [--virtual-desktop --virtual-desktop-exe PATH] [--dry-run]

        Mutation commands require --acknowledge after a separate --dry-run review. Paths containing spaces or non-ASCII characters are supported when quoted by the shell.
        """);
}
