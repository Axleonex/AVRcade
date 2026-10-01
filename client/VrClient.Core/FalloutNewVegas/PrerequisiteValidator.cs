namespace VrClient.Core.FalloutNewVegas;

using System.Buffers.Binary;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using Microsoft.Win32;

public sealed class FalloutNewVegasPrerequisiteValidator
{
    public FnvReadinessReport Validate(FnvGameInstall install, FnvPrerequisiteOptions options)
    {
        var checks = new List<FnvCheckResult>();
        if (install.Support is FnvInstallSupport.WrongDirectory or FnvInstallSupport.UnsupportedEdition)
        {
            checks.Add(Fail("game", "unsupported_game_install", string.Join("; ", install.Diagnostics), install.RootDirectory));
            return new FnvReadinessReport(checks);
        }

        checks.Add(CheckLargeAddressAware(install.ExecutablePath));
        checks.Add(CheckXnvse(install.RootDirectory));
        checks.Add(CheckFileOrProfileMod("jip_pp_ln_nvse", Path.Combine(install.RootDirectory, "Data", "nvse", "plugins", "jip_nvse.dll"),
            options.ModOrganizerProfileDirectory, "JIP PP LN", "Install the FNVR-required JIP PP LN plugin into the selected MO2 profile or Data/nvse/plugins."));
        checks.Add(CheckFileOrProfileMod("showoff_xnvse", Path.Combine(install.RootDirectory, "Data", "nvse", "plugins", "ShowOffNVSE.dll"),
            options.ModOrganizerProfileDirectory, "ShowOff", "Install ShowOff xNVSE Plugin so ShowOffNVSE.dll resolves through MO2 or Data/nvse/plugins."));
        checks.Add(CheckFileOrProfileMod("fnvr_plugin", Path.Combine(install.RootDirectory, "Data", "FNVR.esp"),
            options.ModOrganizerProfileDirectory, "Virtual Reality", "Install Fallout: New Virtual Reality from its official Nexus page into the VR profile."));
        checks.Add(CheckPluginEnabled(options.ModOrganizerProfileDirectory));
        checks.Add(CheckFile("fnvr_tracker", ResolveTracker(install.RootDirectory, options.TrackerExecutablePath),
            "Select the official open-source FNVR V2 tracker executable; it supplies controller poses and gestures."));
        checks.Add(CheckNativeAdapter(options.NativeAdapterExecutablePath));
        checks.AddRange(CheckSteamVr(options));
        checks.Add(CheckActiveOpenXrRuntime(options));
        checks.AddRange(CheckModOrganizer(options));
        return new FnvReadinessReport(checks);
    }

    public static bool IsLargeAddressAware(string executablePath)
    {
        try
        {
            using var stream = File.OpenRead(executablePath);
            Span<byte> offsetBytes = stackalloc byte[4];
            stream.Position = 0x3c;
            if (stream.Read(offsetBytes) != 4)
                return false;
            var peOffset = BinaryPrimitives.ReadInt32LittleEndian(offsetBytes);
            if (peOffset < 0 || peOffset > stream.Length - 24)
                return false;
            stream.Position = peOffset;
            Span<byte> header = stackalloc byte[24];
            if (stream.Read(header) != header.Length || header[0] != (byte)'P' || header[1] != (byte)'E')
                return false;
            var characteristics = BinaryPrimitives.ReadUInt16LittleEndian(header[22..24]);
            return (characteristics & 0x20) != 0;
        }
        catch
        {
            return false;
        }
    }

    private static FnvCheckResult CheckLargeAddressAware(string executablePath)
    {
        if (!IsLargeAddressAware(executablePath))
            return Fail("fnv_4gb_patcher", "large_address_aware_missing",
                "FalloutNV.exe is not large-address-aware. Run the official FNV 4GB Patcher 1.5 on a supported Steam or GOG installation, then recheck.", executablePath);
        try
        {
            using var file = File.OpenRead(executablePath);
            var hash = Convert.ToHexString(SHA256.HashData(file));
            if (hash == "FFD405CF9F5AF080F202DF9BD1E3C6CA54F0D541081B4DEEB36047A1EE53475A")
                return Fail("fnv_4gb_patcher", "steam_incompatible_bare_laa",
                    "This Steam executable has only the LAA flag toggled; that is not a verified Steam-compatible 4GB patch. Restore the original through Steam, then use the official FNV 4GB Patcher 1.5.", executablePath);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or CryptographicException)
        {
            return Fail("fnv_4gb_patcher", "large_address_aware_unverified",
                "The New Vegas executable could not be checked for Steam-compatible patching.", executablePath);
        }
        return Pass("fnv_4gb_patcher", "large_address_aware", "FalloutNV.exe has the PE large-address-aware flag; launch compatibility still needs a game test.", executablePath);
    }

    private static FnvCheckResult CheckXnvse(string gameRoot)
    {
        var loader = FindCaseInsensitive(gameRoot, "nvse_loader.exe");
        var runtime = FindCaseInsensitive(gameRoot, "nvse_1_4.dll");
        if (loader is null || runtime is null)
            return Fail("xnvse", "xnvse_missing", "xNVSE requires nvse_loader.exe and nvse_1_4.dll in the game root.", gameRoot);
        var version = TryVersion(loader) ?? TryVersion(runtime);
        return version is null
            ? Warn("xnvse", "xnvse_version_unreadable", "xNVSE files are present, but their embedded version could not be read; verify version 6.3.4 or newer.", loader)
            : !IsAtLeastVersion(version, "6.3.4")
                ? Fail("xnvse", "xnvse_version_too_old", $"xNVSE {version} is older than the required 6.3.4.", loader)
                : Pass("xnvse", "xnvse_present", $"xNVSE {version} detected.", loader, version);
    }

    private static FnvCheckResult CheckPluginEnabled(string? profileDirectory)
    {
        if (string.IsNullOrWhiteSpace(profileDirectory))
            return Fail("fnvr_plugin_enablement", "mo2_profile_not_selected", "Select an MO2 profile so FNVR.esp enablement can be verified.");
        var plugins = Path.Combine(profileDirectory, "plugins.txt");
        if (!File.Exists(plugins))
            return Fail("fnvr_plugin_enablement", "plugins_txt_missing", "The selected MO2 profile has no plugins.txt.", plugins);
        var enabled = File.ReadLines(plugins)
            .Select(line => line.Trim())
            .Any(line => !line.StartsWith('#') &&
                string.Equals(line.TrimStart('*'), "FNVR.esp", StringComparison.OrdinalIgnoreCase));
        return enabled
            ? Pass("fnvr_plugin_enablement", "fnvr_esp_enabled", "FNVR.esp is enabled in the selected profile.", plugins)
            : Fail("fnvr_plugin_enablement", "fnvr_esp_disabled", "Enable FNVR.esp in the isolated VR profile; AVRcade will not change load order silently.", plugins);
    }

    private static IEnumerable<FnvCheckResult> CheckSteamVr(FnvPrerequisiteOptions options)
    {
        var running = new HashSet<string>((options.RunningProcessNames ?? Array.Empty<string>())
            .Select(name => name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)
                ? Path.GetFileNameWithoutExtension(name)
                : Path.GetFileName(name)), StringComparer.OrdinalIgnoreCase);
        var steamVrRoot = options.SteamVrDirectory;
        var monitor = steamVrRoot is null ? null : new[]
        {
            Path.Combine(steamVrRoot, "bin", "win64", "vrmonitor.exe"),
            Path.Combine(steamVrRoot, "vrmonitor.exe")
        }.FirstOrDefault(File.Exists);
        yield return monitor is null
            ? Fail("steamvr", "steamvr_missing", "SteamVR is required. Select its installation directory.", steamVrRoot)
            : running.Contains("vrserver") || running.Contains("vrmonitor")
                ? Pass("steamvr", "steamvr_running", "SteamVR is installed and running.", monitor)
                : Warn("steamvr", "steamvr_not_running", "SteamVR is installed but not running; launch orchestration will start its visible monitor.", monitor);

        if (options.UseVirtualDesktop)
        {
            var streamerRunning = running.Contains("VirtualDesktop.Streamer") || running.Contains("VirtualDesktopStreamer");
            yield return streamerRunning
                ? Pass("virtual_desktop", "virtual_desktop_streamer_running", "Virtual Desktop Streamer is running; SteamVR remains the PC runtime path.")
                : Fail("virtual_desktop", "virtual_desktop_streamer_missing", "Start Virtual Desktop Streamer and connect the headset before using the Virtual Desktop to SteamVR path.");
        }
        else
        {
            yield return Pass("runtime_path", "direct_steamvr", "Configured for a direct/wired SteamVR path.");
        }
    }

    private static FnvCheckResult CheckActiveOpenXrRuntime(FnvPrerequisiteOptions options)
    {
        if (string.IsNullOrWhiteSpace(options.SteamVrDirectory))
            return Fail("openxr_runtime", "openxr_runtime_unchecked", "Select SteamVR before checking the active OpenXR runtime.");
        var steamRuntime = Path.Combine(options.SteamVrDirectory, "steamxr_win64.json");
        if (!File.Exists(steamRuntime))
            return Fail("openxr_runtime", "steamvr_openxr_manifest_missing",
                "SteamVR's steamxr_win64.json runtime manifest is missing; repair SteamVR before launch.", steamRuntime);
        var active = options.ActiveOpenXrRuntimePath;
        if (string.IsNullOrWhiteSpace(active))
            return Warn("openxr_runtime", "steamvr_openxr_process_override",
                "No global OpenXR runtime is registered. AVRcade will set XR_RUNTIME_JSON to SteamVR only for the New Vegas launch process.", steamRuntime);
        string root;
        string runtime;
        try
        {
            root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(options.SteamVrDirectory)) + Path.DirectorySeparatorChar;
            runtime = Path.GetFullPath(active);
        }
        catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return Warn("openxr_runtime", "steamvr_openxr_process_override",
                $"The global OpenXR runtime path is invalid ({error.Message}); AVRcade will override it with SteamVR only for New Vegas.", steamRuntime);
        }
        return runtime.StartsWith(root, StringComparison.OrdinalIgnoreCase) &&
               string.Equals(Path.GetFileName(runtime), "steamxr_win64.json", StringComparison.OrdinalIgnoreCase)
            ? Pass("openxr_runtime", "steamvr_openxr_active", "SteamVR is the active OpenXR runtime.", runtime)
            : Warn("openxr_runtime", "steamvr_openxr_process_override",
                $"The global OpenXR runtime is '{runtime}'. AVRcade will leave it unchanged and set XR_RUNTIME_JSON to SteamVR only for New Vegas so FNVR tracking and rendering share SteamVR coordinates.", steamRuntime);
    }

    public static string? DetectActiveOpenXrRuntime()
    {
        if (!OperatingSystem.IsWindows())
            return null;
        try
        {
            return Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1", "ActiveRuntime", null) as string;
        }
        catch (Exception error) when (error is UnauthorizedAccessException or System.Security.SecurityException or IOException)
        {
            return null;
        }
    }

    private static IEnumerable<FnvCheckResult> CheckModOrganizer(FnvPrerequisiteOptions options)
    {
        yield return CheckVersionedFile("mod_organizer_2", options.ModOrganizerExecutablePath, "2.5.2",
            "Select ModOrganizer.exe from a supported MO2 installation.");
        if (string.IsNullOrWhiteSpace(options.ModOrganizerProfileDirectory) || !Directory.Exists(options.ModOrganizerProfileDirectory))
        {
            yield return Fail("mod_organizer_profile", "mo2_profile_missing", "Select or create an isolated MO2 profile.", options.ModOrganizerProfileDirectory);
            yield break;
        }
        var modList = Path.Combine(options.ModOrganizerProfileDirectory, "modlist.txt");
        yield return File.Exists(modList)
            ? Pass("mod_organizer_profile", "mo2_profile_ready", "MO2 profile and modlist.txt detected.", options.ModOrganizerProfileDirectory)
            : Fail("mod_organizer_profile", "mo2_modlist_missing", "The selected folder is not a complete MO2 profile (modlist.txt missing).", options.ModOrganizerProfileDirectory);
    }

    private static string? ResolveTracker(string gameRoot, string? configured)
    {
        if (!string.IsNullOrWhiteSpace(configured))
            return configured;
        return new[]
        {
            Path.Combine(gameRoot, "Fallout - New Virtual Reality.exe"),
            Path.Combine(gameRoot, "FNVR_Tracker.exe")
        }.FirstOrDefault(File.Exists);
    }

    private static FnvCheckResult CheckNativeAdapter(string? launcherPath)
    {
        if (string.IsNullOrWhiteSpace(launcherPath) || !File.Exists(launcherPath))
            return Fail("native_openxr_adapter", "native_adapter_missing",
                "Build or select fnv-test-launcher.exe from the AVRcade x86 native build.", launcherPath);
        var directory = Path.GetDirectoryName(Path.GetFullPath(launcherPath))!;
        var missing = new[] { "vrclient_fnv_stereo.dll", "openxr_loader.dll" }
            .Where(name => !File.Exists(Path.Combine(directory, name))).ToArray();
        return missing.Length == 0
            ? Pass("native_openxr_adapter", "native_adapter_present",
                "AVRcade's VorpX-free native OpenXR launcher, stereo adapter, and loader are present.", launcherPath)
            : Fail("native_openxr_adapter", "native_adapter_incomplete",
                $"Native adapter directory is incomplete; missing: {string.Join(", ", missing)}.", directory);
    }

    private static FnvCheckResult CheckFile(string component, string? path, string remediation) =>
        !string.IsNullOrWhiteSpace(path) && File.Exists(path)
            ? Pass(component, $"{component}_present", $"{component} detected.", path)
            : Fail(component, $"{component}_missing", remediation, path);

    private static FnvCheckResult CheckFileOrProfileMod(
        string component, string path, string? profileDirectory, string profileNameFragment, string remediation)
    {
        if (File.Exists(path))
        {
            var version = TryVersion(path);
            return new FnvCheckResult(component, FnvCheckSeverity.Pass, $"{component}_present",
                $"{component} detected in the physical Data tree.", path, version);
        }
        if (!string.IsNullOrWhiteSpace(profileDirectory))
        {
            var modList = Path.Combine(profileDirectory, "modlist.txt");
            if (File.Exists(modList) && File.ReadLines(modList).Any(line =>
                line.TrimStart().StartsWith('+') && line.Contains(profileNameFragment, StringComparison.OrdinalIgnoreCase)))
            {
                return Warn(component, $"{component}_mo2_virtualized",
                    $"An enabled MO2 mod matching '{profileNameFragment}' is present, but its virtualized file cannot be inspected until MO2 launch. Verify the component log/version after launch.", modList);
            }
        }
        return Fail(component, $"{component}_missing", remediation, path);
    }

    private static FnvCheckResult CheckVersionedFile(string component, string? path, string minimumVersion, string remediation)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            return Fail(component, $"{component}_missing", remediation, path);
        var version = TryVersion(path);
        if (version is null)
            return Warn(component, $"{component}_version_unreadable",
                $"{component} is present, but its embedded version is unreadable; verify {minimumVersion} or newer.", path);
        return IsAtLeastVersion(version, minimumVersion)
            ? new FnvCheckResult(component, FnvCheckSeverity.Pass, $"{component}_present", $"{component} {version} detected.", path, version)
            : new FnvCheckResult(component, FnvCheckSeverity.Failure, $"{component}_version_too_old",
                $"{component} {version} is older than required {minimumVersion}.", path, version);
    }

    public static bool IsAtLeastVersion(string detected, string minimum)
    {
        static Version? Parse(string value)
        {
            var parts = Regex.Matches(value, @"\d+").Select(match => int.Parse(match.Value)).ToList();
            // xNVSE's Windows resources encode 6.4.9 as 0,6,4,9.
            if (parts.Count >= 4 && parts[0] == 0)
                parts.RemoveAt(0);
            if (parts.Count < 2)
                return null;
            while (parts.Count < 4)
                parts.Add(0);
            return new Version(parts[0], parts[1], parts[2], parts[3]);
        }
        var actual = Parse(detected);
        var required = Parse(minimum);
        return actual is not null && required is not null && actual >= required;
    }

    private static string? FindCaseInsensitive(string directory, string fileName) =>
        Directory.Exists(directory)
            ? Directory.EnumerateFiles(directory, "*", SearchOption.TopDirectoryOnly)
                .FirstOrDefault(path => string.Equals(Path.GetFileName(path), fileName, StringComparison.OrdinalIgnoreCase))
            : null;

    private static string? TryVersion(string path)
    {
        try
        {
            var version = FileVersionInfo.GetVersionInfo(path).FileVersion;
            return string.IsNullOrWhiteSpace(version) ? null : version;
        }
        catch
        {
            return null;
        }
    }

    private static FnvCheckResult Pass(string component, string code, string message, string? path = null, string? version = null) =>
        new(component, FnvCheckSeverity.Pass, code, message, path, version);
    private static FnvCheckResult Warn(string component, string code, string message, string? path = null) =>
        new(component, FnvCheckSeverity.Warning, code, message, path);
    private static FnvCheckResult Fail(string component, string code, string message, string? path = null) =>
        new(component, FnvCheckSeverity.Failure, code, message, path);
}
