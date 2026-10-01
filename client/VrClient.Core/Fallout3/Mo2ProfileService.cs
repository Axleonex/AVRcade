namespace VrClient.Core.Fallout3;

using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

public sealed class Fallout3Mo2ProfileService
{
    public const string DefaultProfileName = "VRClient Fallout 3 VR";
    private const string Marker = "vrclient-fallout3-vr.json";
    private const string Journal = ".vrclient-fallout3-transaction.json";
    private static readonly string[] CloneFiles =
    {
        "modlist.txt", "plugins.txt", "loadorder.txt", "archives.txt",
        "fallout.ini", "falloutprefs.ini", "lockedorder.txt", "settings.ini"
    };
    private static readonly string[] ManagedFiles =
    {
        Marker, "VRClient-ReShadePreset.ini", "VRCLIENT-DEPTH-SETUP.txt", "VRCLIENT-NATIVE-EXPERIMENTAL.txt"
    };

    public Fallout3ConversionPlan Plan(Fallout3ConversionRequest request)
    {
        ValidateName(request.VrProfileName, nameof(request.VrProfileName));
        var fresh = string.IsNullOrWhiteSpace(request.SourceProfileName);
        if (!fresh) ValidateName(request.SourceProfileName, nameof(request.SourceProfileName));
        var instance = Path.GetFullPath(request.ModOrganizerInstanceDirectory);
        var profiles = Path.GetFullPath(Path.Combine(instance, "profiles"));
        var source = fresh ? string.Empty : SafeChild(profiles, request.SourceProfileName);
        var target = SafeChild(profiles, request.VrProfileName);
        if (!fresh && !Directory.Exists(source))
            throw new InvalidOperationException($"source MO2 profile does not exist: {source}");
        if (!fresh && string.Equals(source, target, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("the VR profile must remain separate from the normal Fallout 3 profile");

        var label = request.Backend switch
        {
            Fallout3Backend.DepthVr => Fallout3BackendLabels.DepthVr,
            Fallout3Backend.NativeVr => Fallout3BackendLabels.NativeExperimental,
            _ => Fallout3BackendLabels.Desktop
        };
        var warnings = new List<string>
        {
            $"Active backend: {label}.",
            "AVRcade never edits the source profile, MO2 mods directory, ESM/ESP order, or user saves.",
            "Third-party executables and shaders remain user-supplied from official sources; no restricted binary is bundled.",
            "Save compatibility is not universal. Back up saves before loading them with a different mod set."
        };
        var operations = new List<Fallout3FileOperation>();
        if (request.Mode == Fallout3ConversionMode.Uninstall)
        {
            operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.DeleteOwnedProfile, string.Empty,
                target, "Remove the isolated profile only if every remaining file is unchanged from AVRcade's creation record."));
            return new Fallout3ConversionPlan(request, source, target, label, operations, warnings);
        }
        if (request.Mode == Fallout3ConversionMode.Restore)
        {
            foreach (var file in ManagedFiles)
                operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.DeleteManagedFile, string.Empty,
                    Path.Combine(target, file), $"Remove AVRcade-owned {file}."));
            return new Fallout3ConversionPlan(request, source, target, label, operations, warnings);
        }

        operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.CreateDirectory, string.Empty, target,
            "Create the isolated Fallout 3 VR profile."));
        if (File.Exists(Path.Combine(instance, "ModOrganizer.exe")))
        {
            operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.WriteIfMissing, string.Empty,
                Path.Combine(instance, "portable.txt"), "Bind only the explicitly selected MO2 directory as a portable instance."));
            var ini = string.Join(Environment.NewLine, "[General]", "gameName=Fallout 3",
                $"gamePath={Path.GetFullPath(request.GameDirectory).Replace('\\', '/')}",
                $"selected_profile={request.VrProfileName}", string.Empty);
            operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.WriteIfMissing, ini,
                Path.Combine(instance, "ModOrganizer.ini"), "Initialize a new portable MO2 instance without overwriting an existing configuration."));
        }
        foreach (var file in CloneFiles)
        {
            var destination = Path.Combine(target, file);
            if (fresh)
            {
                if (file is "modlist.txt" or "plugins.txt" or "loadorder.txt")
                    operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.WriteIfMissing, string.Empty,
                        destination, $"Initialize empty {file} for the isolated profile."));
                continue;
            }
            var sourceFile = Path.Combine(source, file);
            if (File.Exists(sourceFile))
                operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.CopyIfMissing, sourceFile,
                    destination, $"Clone {file} without changing or reordering its contents."));
        }

        var preset = string.Join(Environment.NewLine,
            "PreprocessorDefinitions=RESHADE_DEPTH_LINEARIZATION_FAR_PLANE=1000",
            "Techniques=SuperDepth3D@SuperDepth3D.fx",
            "TechniqueSorting=SuperDepth3D@SuperDepth3D.fx",
            string.Empty);
        operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.WriteManagedFile, preset,
            Path.Combine(target, "VRClient-ReShadePreset.ini"), "Write a profile-scoped full-SBS SuperDepth3D starting preset."));
        var depthGuide = string.Join(Environment.NewLine,
            Fallout3BackendLabels.DepthVr,
            "Depth-derived stereo only; this is not independent engine-rendered stereo.",
            "1. Install official 32-bit ReShade for Fallout3.exe / DirectX 9.",
            "2. Supply SuperDepth3D from its official project; AVRcade cannot redistribute it.",
            "3. Select VRClient-ReShadePreset.ini in ReShade and verify DisplayDepth before enabling stereo.",
            "4. Run Osiris with Full-SBS desktop capture, then calibrate eye order, convergence and HUD scale.",
            "5. Osiris head output may emulate mouse/gamepad rotation. Positional tracking and tracked weapons are not promised.",
            string.Empty);
        operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.WriteManagedFile, depthGuide,
            Path.Combine(target, "VRCLIENT-DEPTH-SETUP.txt"), "Write honest Depth VR calibration guidance."));
        if (request.Backend == Fallout3Backend.NativeVr)
        {
            operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.WriteManagedFile,
                Fallout3BackendLabels.NativeExperimental + Environment.NewLine +
                "Activation is build-gated and automatically falls back to Depth VR until independent per-eye rendering is proven." + Environment.NewLine,
                Path.Combine(target, "VRCLIENT-NATIVE-EXPERIMENTAL.txt"), "Write the native experimental status marker."));
        }

        var tracked = CloneFiles.Select(file => new
            {
                path = file,
                sha256 = HashIfExists(Path.Combine(target, file)) ??
                    (!fresh ? HashIfExists(Path.Combine(source, file)) : file is "modlist.txt" or "plugins.txt" or "loadorder.txt" ? HashText(string.Empty) : null)
            })
            .Where(item => item.sha256 is not null).ToArray();
        var marker = JsonSerializer.Serialize(new
        {
            schema_version = 1,
            managed_by = "VRClient",
            game = "fallout-3",
            source_profile = request.SourceProfileName,
            vr_profile = request.VrProfileName,
            game_directory = Path.GetFullPath(request.GameDirectory),
            backend = label,
            cloned_files = tracked,
            policy = new { source_profile_immutable = true, preserve_load_order = true, third_party_binaries = "user-supplied-only" }
        }, new JsonSerializerOptions { WriteIndented = true });
        operations.Add(new Fallout3FileOperation(Fallout3FileOperationKind.WriteManagedFile, marker,
            Path.Combine(target, Marker), "Write the app ownership, recovery and backend marker."));
        if (File.Exists(Path.Combine(target, Journal)))
            warnings.Add("An interrupted conversion journal exists. Repair retries only idempotent and AVRcade-owned operations.");
        return new Fallout3ConversionPlan(request, source, target, label, operations, warnings);
    }

    public Fallout3ConversionResult Apply(Fallout3ConversionPlan plan)
    {
        if (plan.Request.DryRun)
            return new Fallout3ConversionResult(false, false, Array.Empty<string>(), plan.Warnings);
        if (!plan.Request.AcknowledgeMutation)
            throw new InvalidOperationException("writes require explicit acknowledgement after reviewing the dry-run conversion plan");
        var target = plan.VrProfileDirectory;
        if (plan.Request.Mode is Fallout3ConversionMode.Restore or Fallout3ConversionMode.Uninstall && !Directory.Exists(target))
            return new Fallout3ConversionResult(false, false, Array.Empty<string>(), plan.Warnings);
        Directory.CreateDirectory(target);
        var journal = Path.Combine(target, Journal);
        var recovered = File.Exists(journal);
        WriteAtomic(journal, JsonSerializer.Serialize(new
        {
            schema_version = 1, status = "applying", mode = plan.Request.Mode.ToString().ToLowerInvariant(),
            backend = plan.BackendLabel, operation_count = plan.Operations.Count,
            started_at_utc = DateTimeOffset.UtcNow.ToString("O")
        }, new JsonSerializerOptions { WriteIndented = true }));
        var applied = new List<string>();
        try
        {
            foreach (var operation in plan.Operations)
                if (ApplyOperation(operation, target)) applied.Add(operation.Description);
            if (File.Exists(journal)) File.Delete(journal);
        }
        catch { throw; }
        return new Fallout3ConversionResult(applied.Count > 0, recovered, applied, plan.Warnings);
    }

    private static bool ApplyOperation(Fallout3FileOperation operation, string profile)
    {
        switch (operation.Kind)
        {
            case Fallout3FileOperationKind.CreateDirectory:
                if (Directory.Exists(operation.Destination)) return false;
                Directory.CreateDirectory(operation.Destination); return true;
            case Fallout3FileOperationKind.CopyIfMissing:
                if (File.Exists(operation.Destination)) return false;
                Directory.CreateDirectory(Path.GetDirectoryName(operation.Destination)!);
                File.Copy(operation.Source, operation.Destination, false); return true;
            case Fallout3FileOperationKind.WriteIfMissing:
                if (File.Exists(operation.Destination)) return false;
                WriteNew(operation.Destination, operation.Source); return true;
            case Fallout3FileOperationKind.WriteManagedFile:
                if (File.Exists(operation.Destination))
                {
                    var existing = File.ReadAllText(operation.Destination);
                    if (Normalize(existing) == Normalize(operation.Source)) return false;
                    Backup(profile, operation.Destination, existing);
                }
                WriteAtomic(operation.Destination, operation.Source); return true;
            case Fallout3FileOperationKind.DeleteManagedFile:
                if (!File.Exists(operation.Destination)) return false;
                File.Delete(operation.Destination); return true;
            case Fallout3FileOperationKind.DeleteOwnedProfile:
                return DeleteOwnedProfile(operation.Destination);
            default: throw new ArgumentOutOfRangeException(nameof(operation.Kind));
        }
    }

    private static bool DeleteOwnedProfile(string profile)
    {
        if (!Directory.Exists(profile)) return false;
        var marker = Path.Combine(profile, Marker);
        if (!File.Exists(marker))
            throw new InvalidOperationException("uninstall refused: the profile has no AVRcade ownership marker");
        using var document = JsonDocument.Parse(File.ReadAllText(marker));
        if (document.RootElement.GetProperty("managed_by").GetString() != "VRClient" ||
            document.RootElement.GetProperty("game").GetString() != "fallout-3")
            throw new InvalidOperationException("uninstall refused: ownership marker does not identify an AVRcade Fallout 3 profile");
        var tracked = document.RootElement.GetProperty("cloned_files").EnumerateArray()
            .ToDictionary(item => item.GetProperty("path").GetString()!, item => item.GetProperty("sha256").GetString()!, StringComparer.OrdinalIgnoreCase);
        var allowed = new HashSet<string>(CloneFiles.Concat(ManagedFiles).Concat(new[] { Journal }), StringComparer.OrdinalIgnoreCase);
        var unknown = Directory.EnumerateFiles(profile, "*", SearchOption.AllDirectories)
            .Where(path => !path.Contains(Path.Combine(profile, ".vrclient-backups"), StringComparison.OrdinalIgnoreCase))
            .Where(path => !allowed.Contains(Path.GetRelativePath(profile, path))).ToArray();
        if (unknown.Length > 0)
            throw new InvalidOperationException("uninstall preserved the profile because it contains files the app does not own: " + string.Join(", ", unknown.Select(Path.GetFileName)));
        foreach (var file in CloneFiles)
        {
            var path = Path.Combine(profile, file);
            if (!File.Exists(path)) continue;
            if (!tracked.TryGetValue(file, out var expected) || !string.Equals(HashIfExists(path), expected, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException($"uninstall preserved the profile because {file} was changed after cloning");
        }
        Directory.Delete(profile, recursive: true);
        return true;
    }

    private static void Backup(string profile, string path, string content)
    {
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(content))).ToLowerInvariant()[..12];
        var directory = Path.Combine(profile, ".vrclient-backups");
        Directory.CreateDirectory(directory);
        var target = Path.Combine(directory, $"{Path.GetFileName(path)}.{hash}.bak");
        if (!File.Exists(target)) File.WriteAllText(target, content);
    }

    private static void WriteNew(string path, string content)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        using var writer = new StreamWriter(stream);
        writer.Write(content);
    }

    private static void WriteAtomic(string path, string content)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".tmp-" + Guid.NewGuid().ToString("N");
        File.WriteAllText(temporary, content);
        File.Move(temporary, path, overwrite: true);
    }

    private static string SafeChild(string root, string name)
    {
        var prefix = Path.GetFullPath(root + Path.DirectorySeparatorChar);
        var candidate = Path.GetFullPath(Path.Combine(root, name));
        if (!candidate.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("profile path escapes the MO2 profiles directory");
        return candidate;
    }

    private static void ValidateName(string name, string parameter)
    {
        if (string.IsNullOrWhiteSpace(name) || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
            name.Contains(Path.DirectorySeparatorChar) || name.Contains(Path.AltDirectorySeparatorChar) || name is "." or "..")
            throw new ArgumentException("profile name must be a single safe directory name", parameter);
    }

    private static string? HashIfExists(string path)
    {
        if (!File.Exists(path)) return null;
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static string HashText(string value) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value))).ToLowerInvariant();

    private static string Normalize(string value) => value.Replace("\r\n", "\n").Trim();
}
