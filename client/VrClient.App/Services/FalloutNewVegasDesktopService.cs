using System.Diagnostics;
using System.Text.Json;
using VrClient.Core.Discovery;
using VrClient.Core.FalloutNewVegas;

namespace VrClient.App.Services;

public sealed class FalloutNewVegasDesktopSettings
{
    public string GameDirectory { get; set; } = string.Empty;
    public FnvStorefront Storefront { get; set; } = FnvStorefront.Steam;
    public string ModOrganizerExecutablePath { get; set; } = string.Empty;
    public string ModOrganizerInstanceDirectory { get; set; } = string.Empty;
    public string SourceProfileName { get; set; } = string.Empty;
    public string VrProfileName { get; set; } = "New Vegas VR";
    public string TrackerExecutablePath { get; set; } = string.Empty;
    public string NativeAdapterExecutablePath { get; set; } = string.Empty;
    public string SteamVrDirectory { get; set; } = string.Empty;
    public bool UseVirtualDesktop { get; set; } = true;
    public string VirtualDesktopExecutablePath { get; set; } = string.Empty;
    public bool UseVortexSync { get; set; }

    public string VrProfileDirectory => string.IsNullOrWhiteSpace(ModOrganizerInstanceDirectory) ||
        string.IsNullOrWhiteSpace(VrProfileName)
            ? string.Empty
            : Path.Combine(ModOrganizerInstanceDirectory, "profiles", VrProfileName);
}

public sealed class FalloutNewVegasDesktopService : IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private static readonly HashSet<string> VanillaPlugins = new(StringComparer.OrdinalIgnoreCase)
    {
        "FalloutNV.esm", "DeadMoney.esm", "HonestHearts.esm", "OldWorldBlues.esm",
        "LonesomeRoad.esm", "GunRunnersArsenal.esm", "CaravanPack.esm",
        "ClassicPack.esm", "MercenaryPack.esm", "TribalPack.esm"
    };
    private readonly string _repoRoot;
    private readonly string _settingsPath;
    private readonly string _dataRoot;
    private FnvLaunchSession? _session;

    public FalloutNewVegasDesktopService(string repoRoot, string? dataRoot = null)
    {
        _repoRoot = repoRoot;
        dataRoot ??= Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "VRClient", "fallout-new-vegas");
        _dataRoot = dataRoot;
        _settingsPath = Path.Combine(dataRoot, "desktop-settings.json");
    }

    public bool SessionActive => _session is not null;

    public ProcessStartInfo CreateNativeTestStartInfo(FalloutNewVegasDesktopSettings settings, bool stop = false)
    {
        var directory = NativeBuildDirectory();
        foreach (var name in new[] { "fnv-test-launcher.exe", "vrclient_fnv_stereo.dll", "openxr_loader.dll" })
            if (!File.Exists(Path.Combine(directory, name)))
                throw new FileNotFoundException("The experimental New Vegas native test build is incomplete. It has not been installed or launched.", name);
        if (!stop && settings.Storefront != FnvStorefront.Steam)
            throw new InvalidOperationException("The native New Vegas test currently supports only a purchased Steam installation.");
        // Direct process launch avoids shell prompts for a network-hosted workspace.
        var start = new ProcessStartInfo(Path.Combine(directory, "fnv-test-launcher.exe"))
        {
            UseShellExecute = false,
            WorkingDirectory = directory
        };
        if (stop)
            start.ArgumentList.Add("--stop");
        else
        {
            var game = Path.Combine(Path.GetFullPath(Required(settings.GameDirectory, "Select the New Vegas game folder.")), "FalloutNV.exe");
            if (!File.Exists(game))
                throw new FileNotFoundException("The selected New Vegas folder does not contain FalloutNV.exe.", game);
            start.ArgumentList.Add("--game");
            start.ArgumentList.Add(game);
        }
        return start;
    }

    public void StartNativeTest(FalloutNewVegasDesktopSettings settings, bool stop = false)
    {
        if (!stop)
            EnsureNativeModeClean(settings);
        using var process = Process.Start(CreateNativeTestStartInfo(settings, stop))
            ?? throw new InvalidOperationException("The native test launcher did not start.");
    }

    public void EnsureNativeModeClean(FalloutNewVegasDesktopSettings settings)
    {
        var game = Required(settings.GameDirectory, "Select the New Vegas game folder.");
        var data = Path.Combine(game, "Data");
        if (!Directory.Exists(data))
            throw new InvalidOperationException("The selected New Vegas game has no Data folder.");
        if (File.Exists(Path.Combine(data, "vortex.deployment.json")) ||
            File.Exists(Path.Combine(game, "vortex.deployment.json")))
            throw new InvalidOperationException("Native VR would also see Vortex-deployed files. Purge the New Vegas deployment in Vortex, or use VR with Vortex. AVRcade will not change Vortex for you.");
        var extras = Directory.EnumerateFiles(data)
            .Select(Path.GetFileName)
            .Where(name => name is not null &&
                (name.EndsWith(".esm", StringComparison.OrdinalIgnoreCase) ||
                 name.EndsWith(".esp", StringComparison.OrdinalIgnoreCase)) &&
                !VanillaPlugins.Contains(name))
            .ToArray();
        if (extras.Length > 0)
            throw new InvalidOperationException($"Native VR is not clean: Data contains non-vanilla plugin(s), including {extras[0]}. Use VR with Vortex or remove them through their mod manager.");
    }

    public FalloutNewVegasDesktopSettings LoadAndDetect()
    {
        FalloutNewVegasDesktopSettings settings;
        try
        {
            settings = File.Exists(_settingsPath)
                ? JsonSerializer.Deserialize<FalloutNewVegasDesktopSettings>(File.ReadAllText(_settingsPath), JsonOptions)
                    ?? new FalloutNewVegasDesktopSettings()
                : new FalloutNewVegasDesktopSettings();
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException)
        {
            settings = new FalloutNewVegasDesktopSettings();
        }

        var install = new FalloutNewVegasDiscovery().Discover().Installs.FirstOrDefault(candidate =>
            candidate.Support is FnvInstallSupport.Supported or FnvInstallSupport.ManualUnverified);
        if (string.IsNullOrWhiteSpace(settings.GameDirectory) && install is not null)
        {
            settings.GameDirectory = install.RootDirectory;
            settings.Storefront = install.Storefront;
        }

        settings.ModOrganizerExecutablePath = ExistingOrFirst(settings.ModOrganizerExecutablePath,
            Candidate(_dataRoot, "runtime", "mo2-2.5.2", "ModOrganizer.exe"),
            Candidate(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ModOrganizer", "ModOrganizer.exe"),
            Candidate(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Mod Organizer 2", "ModOrganizer.exe"),
            Candidate(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Mod Organizer 2", "ModOrganizer.exe"));
        ResolveProfileLocation(settings);

        settings.TrackerExecutablePath = ExistingOrFirst(settings.TrackerExecutablePath,
            Candidate(_dataRoot, "downloads", "Fallout-New_Virtual_Reality.exe"),
            Candidate(settings.GameDirectory, "Fallout - New Virtual Reality.exe"),
            Candidate(settings.GameDirectory, "FNVR_Tracker.exe"));
        settings.NativeAdapterExecutablePath = ExistingOrFirst(settings.NativeAdapterExecutablePath,
            Candidate(_repoRoot, "native", "fallout-new-vegas", "fnv-test-launcher.exe"),
            Candidate(_repoRoot, "build", "fnv-x86-current", "fnv-test-launcher.exe"),
            Candidate(_repoRoot, "build", "fnv-x86", "fnv-test-launcher.exe"));

        if (string.IsNullOrWhiteSpace(settings.SteamVrDirectory))
        {
            settings.SteamVrDirectory = new SteamLibraryScanner()
                .FindGame("250820", Path.Combine("bin", "win64", "vrmonitor.exe"), null)?.InstallDir
                ?? ExistingDirectoryOrEmpty(
                    Candidate(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                        "Steam", "steamapps", "common", "SteamVR"));
        }
        settings.VirtualDesktopExecutablePath = ExistingOrFirst(settings.VirtualDesktopExecutablePath,
            Candidate(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                "Virtual Desktop Streamer", "VirtualDesktop.Streamer.exe"),
            Candidate(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                "Virtual Desktop Streamer", "VirtualDesktopStreamer.exe"));
        return settings;
    }

    public void Save(FalloutNewVegasDesktopSettings settings)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_settingsPath)!);
        var temporary = _settingsPath + ".tmp-" + Guid.NewGuid().ToString("N");
        File.WriteAllText(temporary, JsonSerializer.Serialize(settings, JsonOptions));
        File.Move(temporary, _settingsPath, overwrite: true);
    }

    public FnvReadinessReport Check(FalloutNewVegasDesktopSettings settings)
    {
        var install = Inspect(settings);
        var report = new FalloutNewVegasPrerequisiteValidator().Validate(install, Prerequisites(settings));
        if (settings.UseVortexSync)
        {
            var sync = new FalloutNewVegasVortexSyncService().Validate(settings.GameDirectory,
                settings.VrProfileDirectory, Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
            report = new FnvReadinessReport(report.Checks.Append(new FnvCheckResult(
                "vortex_sync", sync.Ready ? FnvCheckSeverity.Pass : FnvCheckSeverity.Failure,
                sync.Ready ? "vortex_plugins_synced" : "vortex_sync_required", sync.Message)).ToArray());
        }
        // The current launcher uses MO2's portable instance. A folder with a modlist
        // alone must not qualify a different/global MO2 instance for launch.
        if (File.Exists(settings.ModOrganizerExecutablePath) &&
            (!File.Exists(Path.Combine(settings.ModOrganizerInstanceDirectory, "portable.txt")) ||
             !string.Equals(Path.GetFullPath(Path.GetDirectoryName(settings.ModOrganizerExecutablePath)!),
                 Path.GetFullPath(settings.ModOrganizerInstanceDirectory), StringComparison.OrdinalIgnoreCase)))
            return new FnvReadinessReport(report.Checks.Append(new FnvCheckResult(
                "mod_organizer_instance", FnvCheckSeverity.Failure, "mo2_instance_not_bound",
                "Profile files are prepared, but this MO2 installation is not bound to that isolated instance. Automatic MO2 instance setup is not yet implemented.")).ToArray());
        return report;
    }

    public FnvConversionPlan PreviewPreparation(FalloutNewVegasDesktopSettings settings)
    {
        ResolveProfileLocation(settings);
        Save(settings);
        return new FalloutNewVegasMo2ProfileService().Plan(ConversionRequest(settings, dryRun: true, acknowledge: false));
    }

    public FnvConversionResult ApplyPreparation(FalloutNewVegasDesktopSettings settings)
    {
        ResolveProfileLocation(settings);
        Save(settings);
        var service = new FalloutNewVegasMo2ProfileService();
        var plan = service.Plan(ConversionRequest(settings, dryRun: false, acknowledge: true));
        var result = service.Apply(plan);
        BindManagedPortableInstance(settings);
        return result;
    }

    public FnvVortexSyncPlan PreviewVortexSync(FalloutNewVegasDesktopSettings settings)
    {
        EnsureManagedVortexTarget(settings);
        return new FalloutNewVegasVortexSyncService().Preview(
            Required(settings.GameDirectory, "Select the New Vegas game folder."),
            Required(settings.VrProfileDirectory, "Prepare the isolated VR profile first."),
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
    }

    public FnvVortexSyncResult ApplyVortexSync(FalloutNewVegasDesktopSettings settings, FnvVortexSyncPlan plan)
    {
        EnsureManagedVortexTarget(settings);
        if (!string.Equals(Path.GetFullPath(settings.GameDirectory), Path.GetFullPath(plan.GameDirectory), StringComparison.OrdinalIgnoreCase) ||
            !string.Equals(Path.GetFullPath(settings.VrProfileDirectory), Path.GetFullPath(plan.ProfileDirectory), StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Game or VR profile changed after preview. Preview Vortex sync again.");
        var result = new FalloutNewVegasVortexSyncService().Apply(plan);
        settings.UseVortexSync = true;
        Save(settings);
        return result;
    }

    private void EnsureManagedVortexTarget(FalloutNewVegasDesktopSettings settings)
    {
        if (string.IsNullOrWhiteSpace(settings.ModOrganizerInstanceDirectory) ||
            string.IsNullOrWhiteSpace(settings.VrProfileName) ||
            settings.VrProfileName is "." or ".." ||
            settings.VrProfileName != Path.GetFileName(settings.VrProfileName) ||
            !IsUnderDataRoot(settings.ModOrganizerInstanceDirectory))
            throw new InvalidOperationException("Vortex sync is limited to AVRcade's isolated MO2 instance and VR profile.");
    }

    public FnvReadinessReport Launch(FalloutNewVegasDesktopSettings settings)
    {
        Save(settings);
        var install = Inspect(settings);
        var prerequisites = Prerequisites(settings);
        var report = Check(settings);
        if (!report.Ready)
            return report;

        StopSession();
        var logs = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "VRClient", "fallout-new-vegas", "logs", DateTimeOffset.Now.ToString("yyyyMMdd-HHmmss"));
        var plan = new FalloutNewVegasLaunchOrchestrator().BuildPlan(
            install, prerequisites, logs, NullIfEmpty(settings.VirtualDesktopExecutablePath));
        _session = new FalloutNewVegasLaunchOrchestrator().Start(plan, new SystemFnvProcessController(), dryRun: false);
        return report;
    }

    public void StopSession()
    {
        _session?.Dispose();
        _session = null;
    }

    public void Dispose() => StopSession();

    private void ResolveProfileLocation(FalloutNewVegasDesktopSettings settings)
    {
        // The executable directory is not an instance directory for a global MO2 install.
        // A fresh setup belongs to VRClient, never to an arbitrarily chosen normal profile.
        var executableDirectory = string.IsNullOrWhiteSpace(settings.ModOrganizerExecutablePath)
            ? null : Path.GetDirectoryName(Path.GetFullPath(settings.ModOrganizerExecutablePath));
        var staleManagedInstance = !string.IsNullOrWhiteSpace(settings.ModOrganizerInstanceDirectory) &&
            IsUnderDataRoot(settings.ModOrganizerInstanceDirectory) &&
            !File.Exists(Path.Combine(settings.ModOrganizerInstanceDirectory, "portable.txt"));
        if (string.IsNullOrWhiteSpace(settings.ModOrganizerInstanceDirectory) ||
            (staleManagedInstance && executableDirectory is not null &&
             IsUnderDataRoot(executableDirectory) &&
             File.Exists(Path.Combine(executableDirectory, "portable.txt"))))
        {
            settings.ModOrganizerInstanceDirectory = executableDirectory is not null && IsUnderDataRoot(executableDirectory)
                    ? executableDirectory
                    : Path.Combine(Path.GetDirectoryName(_settingsPath)!, "mo2-instance");
        }
        if (string.IsNullOrWhiteSpace(settings.VrProfileName))
            settings.VrProfileName = "New Vegas VR";
    }

    private void BindManagedPortableInstance(FalloutNewVegasDesktopSettings settings)
    {
        if (!File.Exists(settings.ModOrganizerExecutablePath))
            return;
        var executableDirectory = Path.GetFullPath(Path.GetDirectoryName(settings.ModOrganizerExecutablePath)!);
        var instanceDirectory = Path.GetFullPath(settings.ModOrganizerInstanceDirectory);
        if (!string.Equals(executableDirectory, instanceDirectory, StringComparison.OrdinalIgnoreCase) ||
            !IsUnderDataRoot(instanceDirectory))
            return;
        var marker = Path.Combine(instanceDirectory, "portable.txt");
        if (!File.Exists(marker))
            File.WriteAllText(marker, string.Empty);
    }

    private bool IsUnderDataRoot(string path)
    {
        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(_dataRoot)) + Path.DirectorySeparatorChar;
        var candidate = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path)) + Path.DirectorySeparatorChar;
        return candidate.StartsWith(root, StringComparison.OrdinalIgnoreCase);
    }

    private static FnvConversionRequest ConversionRequest(
        FalloutNewVegasDesktopSettings settings, bool dryRun, bool acknowledge) => new(
        Required(settings.ModOrganizerInstanceDirectory, "Select the MO2 instance folder."),
        settings.SourceProfileName.Trim(),
        Required(settings.VrProfileName, "Enter a name for the isolated VR profile."),
        Required(settings.GameDirectory, "Select the Fallout: New Vegas game folder."),
        FnvConversionMode.Convert, dryRun, acknowledge);

    private static FnvPrerequisiteOptions Prerequisites(FalloutNewVegasDesktopSettings settings) => new(
        NullIfEmpty(settings.ModOrganizerExecutablePath),
        NullIfEmpty(settings.VrProfileDirectory),
        NullIfEmpty(settings.TrackerExecutablePath),
        NullIfEmpty(settings.NativeAdapterExecutablePath),
        NullIfEmpty(settings.SteamVrDirectory),
        settings.UseVirtualDesktop,
        RunningProcessNames(),
        FalloutNewVegasPrerequisiteValidator.DetectActiveOpenXrRuntime());

    private static IReadOnlyCollection<string> RunningProcessNames()
    {
        try { return Process.GetProcesses().Select(process => process.ProcessName).ToArray(); }
        catch { return Array.Empty<string>(); }
    }

    private static FnvGameInstall Inspect(FalloutNewVegasDesktopSettings settings) =>
        new FalloutNewVegasDiscovery().Inspect(
            Required(settings.GameDirectory, "Select the Fallout: New Vegas game folder."),
            settings.Storefront);

    private static string Required(string value, string message) =>
        string.IsNullOrWhiteSpace(value) ? throw new InvalidOperationException(message) : value.Trim();

    private static string? NullIfEmpty(string value) => string.IsNullOrWhiteSpace(value) ? null : value.Trim();

    private static string Candidate(string root, params string[] segments) =>
        string.IsNullOrWhiteSpace(root) ? string.Empty : Path.Combine(new[] { root }.Concat(segments).ToArray());

    private static string ExistingOrFirst(string current, params string[] candidates)
    {
        if (!string.IsNullOrWhiteSpace(current) && File.Exists(current))
            return current;
        return candidates.FirstOrDefault(path => !string.IsNullOrWhiteSpace(path) && File.Exists(path)) ?? current;
    }

    private static string ExistingDirectoryOrEmpty(string path) => Directory.Exists(path) ? path : string.Empty;

    private string NativeBuildDirectory()
    {
        var packaged = Path.Combine(_repoRoot, "native", "fallout-new-vegas");
        if (File.Exists(Path.Combine(packaged, "fnv-test-launcher.exe")))
            return packaged;
        foreach (var name in new[] { "fnv-x86-current", "fnv-x86" })
        {
            var candidate = Path.Combine(_repoRoot, "build", name);
            if (File.Exists(Path.Combine(candidate, "fnv-test-launcher.exe")))
                return candidate;
        }
        return Path.Combine(_repoRoot, "build", "fnv-x86-current");
    }
}
