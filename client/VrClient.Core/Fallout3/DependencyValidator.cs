namespace VrClient.Core.Fallout3;

using System.Diagnostics;
using System.Text.Json;
using VrClient.Core.Launch;

public sealed class Fallout3DependencyValidator
{
    // The direct FOSE route does not require MO2 or the separate Depth VR stack.
    // Keep its gate distinct from the MO2 conversion workflow.
    public Fallout3Readiness ValidateNativeDirect(Fallout3Install install,
        Fallout3DependencyOptions options)
    {
        var checks = new List<Fallout3Check>
        {
            CheckInstall(install), CheckDlc(install), CheckFose(install), CheckRuntime(options)
        };
        var loader = Path.Combine(install.RootDirectory, "fose_loader.exe");
        checks.Add(File.Exists(loader)
            ? Pass("fose_loader", "fose_loader_present", "FOSE launcher detected.", loader)
            : Fail("fose_loader", "fose_loader_missing", "Install FOSE before the native headset test.", loader));
        if (!File.Exists(Path.Combine(install.RootDirectory, "fose_1_7.dll")))
            checks.Add(Fail("fose", "fose_runtime_missing", "The FOSE 1.7 runtime is required by the native plugin."));
        checks.AddRange(CheckNativeStack(install, options));
        var proof = NativeStatus(options.NativeStereoProofPath, install.ExecutableSha256);
        return new Fallout3Readiness(checks, Fallout3Backend.NativeVr,
            Fallout3Backend.NativeVr, proof,
            proof == Fallout3BackendStatus.Verified
                ? Fallout3BackendLabels.NativeVerified
                : Fallout3BackendLabels.NativeExperimental,
            Array.Empty<string>());
    }

    public Fallout3Readiness Validate(
        Fallout3Install install,
        Fallout3DependencyOptions options,
        Fallout3Backend requestedBackend)
    {
        var checks = new List<Fallout3Check>();
        var fallback = new List<string>();
        checks.Add(CheckInstall(install));
        checks.Add(CheckDlc(install));
        checks.Add(CheckMo2(options));
        checks.Add(CheckFose(install));
        checks.Add(CheckRuntime(options));

        var depthChecks = CheckDepthStack(install, options).ToArray();
        checks.AddRange(depthChecks);
        var nativeChecks = CheckNativeStack(install, options).ToArray();
        var proofStatus = NativeStatus(options.NativeStereoProofPath, install.ExecutableSha256);
        var profileReady = nativeChecks.Any(check => check.Code == "native_hook_profile_match" &&
            check.Severity == Fallout3Severity.Pass);
        var nativeStatus = profileReady
            ? proofStatus == Fallout3BackendStatus.Verified
                ? Fallout3BackendStatus.Verified
                : Fallout3BackendStatus.Experimental
            : Fallout3BackendStatus.Unavailable;

        var effective = requestedBackend;
        if (requestedBackend == Fallout3Backend.NativeVr &&
            nativeChecks.Any(c => c.Severity == Fallout3Severity.Failure))
        {
            effective = Fallout3Backend.DepthVr;
            fallback.Add("Native VR is not safely activatable for this build; falling back to Depth VR - Free.");
        }

        checks.AddRange(effective == Fallout3Backend.DepthVr
            ? nativeChecks.Select(check => check.Severity == Fallout3Severity.Failure
                ? check with
                {
                    Severity = Fallout3Severity.Warning,
                    Message = check.Message + " Depth VR remains the active backend."
                }
                : check)
            : nativeChecks);

        if (effective == Fallout3Backend.DepthVr && depthChecks.Any(c => c.Severity == Fallout3Severity.Failure))
            fallback.Add("Depth VR dependencies are incomplete; conversion may be planned, but launch remains blocked.");

        var label = effective switch
        {
            Fallout3Backend.Desktop => Fallout3BackendLabels.Desktop,
            Fallout3Backend.DepthVr => Fallout3BackendLabels.DepthVr,
            Fallout3Backend.NativeVr when nativeStatus == Fallout3BackendStatus.Verified => Fallout3BackendLabels.NativeVerified,
            Fallout3Backend.NativeVr => Fallout3BackendLabels.NativeExperimental,
            _ => Fallout3BackendLabels.Desktop
        };
        return new Fallout3Readiness(checks, requestedBackend, effective, nativeStatus, label, fallback);
    }

    private static Fallout3Check CheckInstall(Fallout3Install install) => install.Support switch
    {
        Fallout3InstallSupport.Supported => Pass("game", "supported_install", $"Supported {install.Storefront} installation detected.", install.ExecutablePath, install.FileVersion),
        Fallout3InstallSupport.ManualUnverified => Warn("game", "manual_install_unverified", "Manual installation found; verify legitimate ownership and executable build before writes.", install.ExecutablePath, install.FileVersion),
        Fallout3InstallSupport.WrongDirectory => Fail("game", "wrong_directory", "Fallout3.exe is missing from the selected directory.", install.RootDirectory),
        _ => Fail("game", "unsupported_build", $"Fallout3.exe {install.FileVersion ?? "unknown"} is not a supported build.", install.ExecutablePath, install.FileVersion)
    };

    private static Fallout3Check CheckDlc(Fallout3Install install)
    {
        var missing = install.Dlc.Where(pair => !pair.Value).Select(pair => pair.Key).ToArray();
        return missing.Length == 0
            ? Pass("goty_dlc", "all_dlc_present", "All five Fallout 3 GOTY DLC master files are present.", Path.Combine(install.RootDirectory, "Data"))
            : Fail("goty_dlc", "dlc_missing", $"Missing GOTY DLC: {string.Join(", ", missing)}. Verify the storefront installation; AVRcade never downloads Bethesda assets.", Path.Combine(install.RootDirectory, "Data"));
    }

    private static Fallout3Check CheckFose(Fallout3Install install)
    {
        var runtime = Path.Combine(install.RootDirectory, "fose_1_7.dll");
        if (install.FileVersion == "1.7.0.4")
            return Fail("fose", "anniversary_patch_required", "FOSE does not support Steam 1.7.0.4. Review and run the Fallout Anniversary Patcher from its official page, then recheck.", install.ExecutablePath);
        if (!File.Exists(runtime))
            return Warn("fose", "fose_missing", "FOSE is optional for Depth VR but required by the experimental native camera plugin. Install it from fose.silverlock.org.", runtime);
        return Pass("fose", "fose_present", "FOSE runtime detected. DLL-plugin compatibility still depends on exact executable build.", runtime, TryVersion(runtime));
    }

    private static Fallout3Check CheckMo2(Fallout3DependencyOptions options)
    {
        if (string.IsNullOrWhiteSpace(options.ModOrganizerExecutablePath) || !File.Exists(options.ModOrganizerExecutablePath))
            return Fail("mod_organizer_2", "mo2_missing", "Select ModOrganizer.exe. AVRcade uses a separate MO2 profile and never rewrites the normal profile.", options.ModOrganizerExecutablePath);
        if (!string.IsNullOrWhiteSpace(options.ModOrganizerProfileDirectory) && !Directory.Exists(options.ModOrganizerProfileDirectory))
            return Fail("mod_organizer_2", "mo2_profile_missing", "The selected source profile directory does not exist.", options.ModOrganizerProfileDirectory);
        return Pass("mod_organizer_2", "mo2_present", "Mod Organizer 2 is available for isolated profile creation.", options.ModOrganizerExecutablePath, TryVersion(options.ModOrganizerExecutablePath));
    }

    private static IEnumerable<Fallout3Check> CheckDepthStack(Fallout3Install install, Fallout3DependencyOptions options)
    {
        var reshadeRoot = options.ReShadeDirectory ?? install.RootDirectory;
        var proxy = new[] { "d3d9.dll", "ReShade32.dll" }.Select(name => Path.Combine(reshadeRoot, name)).FirstOrDefault(File.Exists);
        yield return proxy is null
            ? Fail("reshade", "reshade_dx9_missing", "Install the current official 32-bit ReShade build for Fallout3.exe using DirectX 9. AVRcade does not bundle or silently download it.", reshadeRoot)
            : Pass("reshade", "reshade_dx9_present", "A DirectX 9 ReShade proxy is present; in-game depth capture still needs visual verification.", proxy);

        var depthRoot = options.Depth3DDirectory ?? reshadeRoot;
        var shader = Find(depthRoot, "SuperDepth3D.fx");
        yield return shader is null
            ? Fail("superdepth3d", "superdepth3d_missing", "Supply SuperDepth3D from its official project. It is free for personal use but proprietary and cannot be redistributed by AVRcade.", depthRoot)
            : Pass("superdepth3d", "superdepth3d_user_supplied", "User-supplied SuperDepth3D detected. Output is depth-derived stereo, not engine-rendered dual-camera VR.", shader);

        yield return string.IsNullOrWhiteSpace(options.OsirisExecutablePath) || !File.Exists(options.OsirisExecutablePath)
            ? Fail("osiris", "osiris_missing", "Select osiris-vr-viewer.exe from the official MIT-licensed Osiris release.", options.OsirisExecutablePath)
            : Pass("osiris", "osiris_present", "Osiris OpenXR viewer detected. Configure full-SBS desktop capture and verify eye order in-headset.", options.OsirisExecutablePath);

        foreach (var conflict in new[] { "enbseries.dll", "dxgi.dll", "d3d11.dll" })
        {
            var path = Path.Combine(install.RootDirectory, conflict);
            if (File.Exists(path))
                yield return Warn("depth_conflict", "post_process_conflict", $"{conflict} may own the same graphics hook or change depth behavior. Preserve it and test a separate VR profile; AVRcade will not overwrite it.", path);
        }
    }

    private static IEnumerable<Fallout3Check> CheckNativeStack(Fallout3Install install, Fallout3DependencyOptions options)
    {
        if (string.IsNullOrWhiteSpace(options.NativeAdapterPath) || !File.Exists(options.NativeAdapterPath))
        {
            yield return Fail("native_adapter", "native_adapter_missing", "The AVRcade Fallout 3 native adapter has not been built/staged.", options.NativeAdapterPath);
            yield break;
        }
        yield return Pass("native_adapter", "native_adapter_present", "The build-gated native adapter is present. Presence alone does not prove independent per-eye rendering.", options.NativeAdapterPath);
        var profile = CheckNativeHookProfile(install, options.NativeHookProfilePath);
        yield return profile;
        if (profile.Severity == Fallout3Severity.Failure) yield break;

        var status = NativeStatus(options.NativeStereoProofPath, install.ExecutableSha256);
        yield return status switch
        {
            Fallout3BackendStatus.Verified => Pass("native_stereo_proof", "native_stereo_verified", "A version-bound proof records independent per-eye projection and submission.", options.NativeStereoProofPath),
            Fallout3BackendStatus.Experimental => Warn("native_stereo_proof", "native_stereo_experimental", "Native infrastructure is experimental; no accepted independent per-eye headset proof exists.", options.NativeStereoProofPath),
            _ => Warn("native_stereo_proof", "native_stereo_unverified", "The exact-build profile can run as Native VR - Experimental, but no accepted headset proof exists.", options.NativeStereoProofPath)
        };
    }

    private static Fallout3Check CheckNativeHookProfile(Fallout3Install install, string? path)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            return Fail("native_hook_profile", "native_hook_profile_missing",
                "Native VR requires a reviewed fallout3-native-profile.ini for this exact executable build.", path);
        try
        {
            var values = File.ReadLines(path)
                .Select(line => line.Trim())
                .Where(line => line.Length > 0 && line[0] is not ';' and not '#' and not '[')
                .Select(line => line.Split('=', 2))
                .Where(parts => parts.Length == 2)
                .ToDictionary(parts => parts[0].Trim(), parts => parts[1].Trim(), StringComparer.OrdinalIgnoreCase);
            var hashMatches = values.TryGetValue("ExecutableSha256", out var hash) &&
                string.Equals(hash, install.ExecutableSha256, StringComparison.OrdinalIgnoreCase);
            var numericKeys = new[] { "RenderHookRva", "RenderContextPointerRva", "SceneGraphPointerRva",
                "RendererPointerRva", "CameraOffset", "DeviceOffset" };
            var addressesValid = numericKeys.All(key => values.TryGetValue(key, out var text) &&
                TryUnsigned(text, out var value) && value != 0);
            var entryValid = values.TryGetValue("RenderEntryBytes", out var entry) &&
                entry.Split(new[] { ' ', ',', '-' }, StringSplitOptions.RemoveEmptyEntries) is var bytes &&
                bytes.Length is >= 5 and <= 16 && bytes.All(item => byte.TryParse(item,
                    System.Globalization.NumberStyles.HexNumber, null, out _));
            return hashMatches && addressesValid && entryValid
                ? Pass("native_hook_profile", "native_hook_profile_match",
                    "Reviewed hook profile matches the exact Fallout3.exe hash; Native VR may run as Experimental.", path)
                : Fail("native_hook_profile", "native_hook_profile_mismatch",
                    "The hook profile is incomplete or does not match this Fallout3.exe. Native activation is refused.", path);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException)
        {
            return Fail("native_hook_profile", "native_hook_profile_invalid",
                $"The native hook profile could not be validated: {error.Message}", path);
        }
    }

    private static bool TryUnsigned(string text, out uint value)
    {
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            return uint.TryParse(text[2..], System.Globalization.NumberStyles.HexNumber, null, out value);
        return uint.TryParse(text, out value);
    }

    private static Fallout3Check CheckRuntime(Fallout3DependencyOptions options)
    {
        var running = options.RunningProcessNames ?? Array.Empty<string>();
        var active = options.ActiveOpenXrRuntimePath;
        if (string.IsNullOrWhiteSpace(active) || !File.Exists(active))
            return Fail("openxr_runtime", "openxr_runtime_inactive", "No active OpenXR runtime manifest was detected. Select SteamVR OpenXR or VirtualDesktopXR in its own UI.", active);
        var name = active.Contains("virtualdesktop", StringComparison.OrdinalIgnoreCase)
            ? OpenXrRuntimeSelector.VirtualDesktopName
            : active.Contains("steam", StringComparison.OrdinalIgnoreCase)
                ? OpenXrRuntimeSelector.SteamVrName : "OpenXR";
        var processWarning = name == OpenXrRuntimeSelector.SteamVrName && !OpenXrRuntimeSelector.IsSteamVrRunning(running)
            ? " SteamVR is selected but not running or the headset may be asleep."
            : name == OpenXrRuntimeSelector.VirtualDesktopName && !OpenXrRuntimeSelector.IsVirtualDesktopRunning(running)
                ? " Virtual Desktop Streamer is not running or the headset is disconnected." : string.Empty;
        return processWarning.Length == 0
            ? Pass("openxr_runtime", "openxr_runtime_active", $"{name} runtime detected.", active)
            : Warn("openxr_runtime", "openxr_runtime_not_ready", $"{name} runtime manifest detected.{processWarning}", active);
    }

    public static Fallout3BackendStatus NativeStatus(string? proofPath, string? expectedExecutableSha256 = null)
    {
        if (string.IsNullOrWhiteSpace(proofPath) || !File.Exists(proofPath))
            return Fallout3BackendStatus.Unavailable;
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(proofPath));
            var root = document.RootElement;
            var exactBuild = root.TryGetProperty("exact_build_match", out var build) && build.GetBoolean();
            var perEye = root.TryGetProperty("independent_per_eye", out var eyes) && eyes.GetBoolean();
            var headset = root.TryGetProperty("headset_verified", out var verified) && verified.GetBoolean();
            var proofHash = root.TryGetProperty("executable_sha256", out var hash) ? hash.GetString() : null;
            var hashMatches = !string.IsNullOrWhiteSpace(expectedExecutableSha256) &&
                string.Equals(proofHash, expectedExecutableSha256, StringComparison.OrdinalIgnoreCase);
            return exactBuild && perEye && headset && hashMatches
                ? Fallout3BackendStatus.Verified
                : Fallout3BackendStatus.Experimental;
        }
        catch (Exception error) when (error is JsonException or IOException or UnauthorizedAccessException)
        {
            return Fallout3BackendStatus.Unavailable;
        }
    }

    private static string? Find(string root, string fileName)
    {
        if (!Directory.Exists(root)) return null;
        try { return Directory.EnumerateFiles(root, fileName, SearchOption.AllDirectories).FirstOrDefault(); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { return null; }
    }

    private static string? TryVersion(string path)
    {
        try { return FileVersionInfo.GetVersionInfo(path).FileVersion; } catch { return null; }
    }

    private static Fallout3Check Pass(string component, string code, string message, string? path = null, string? version = null) =>
        new(component, Fallout3Severity.Pass, code, message, path, version);
    private static Fallout3Check Warn(string component, string code, string message, string? path = null, string? version = null) =>
        new(component, Fallout3Severity.Warning, code, message, path, version);
    private static Fallout3Check Fail(string component, string code, string message, string? path = null, string? version = null) =>
        new(component, Fallout3Severity.Failure, code, message, path, version);
}
