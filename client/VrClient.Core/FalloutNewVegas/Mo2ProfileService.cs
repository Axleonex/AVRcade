namespace VrClient.Core.FalloutNewVegas;

using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

public sealed class FalloutNewVegasMo2ProfileService
{
    private const string ManagedFileName = "vrclient-fallout-new-vegas.json";
    private const string TransactionFileName = ".vrclient-fnv-transaction.json";
    private static readonly string[] CloneFiles =
    {
        "modlist.txt",
        "plugins.txt",
        "loadorder.txt",
        "archives.txt",
        "fallout.ini",
        "falloutprefs.ini",
        "lockedorder.txt"
    };

    public FnvConversionPlan Plan(FnvConversionRequest request)
    {
        var fresh = string.IsNullOrWhiteSpace(request.SourceProfileName);
        if (!fresh)
            ValidateProfileName(request.SourceProfileName, nameof(request.SourceProfileName));
        ValidateProfileName(request.VrProfileName, nameof(request.VrProfileName));
        var profilesRoot = Path.GetFullPath(Path.Combine(request.ModOrganizerInstanceDirectory, "profiles"));
        var source = fresh ? string.Empty : SafeChild(profilesRoot, request.SourceProfileName);
        var target = SafeChild(profilesRoot, request.VrProfileName);
        if (!fresh && !Directory.Exists(source))
            throw new InvalidOperationException($"source MO2 profile does not exist: {source}");
        if (string.Equals(source, target, StringComparison.OrdinalIgnoreCase) && request.Mode != FnvConversionMode.Restore)
            throw new InvalidOperationException("the VR profile must be separate from the selected non-VR source profile");
        if (!fresh && !File.Exists(Path.Combine(source, "modlist.txt")))
            throw new InvalidOperationException($"source MO2 profile has no modlist.txt: {source}");

        var warnings = new List<string>
        {
            "AVRcade clones profile configuration only; it never copies, deletes, disables, or reorders files in the MO2 mods directory.",
            "Save directories are not cloned. Confirm save compatibility before loading an existing save in VR."
        };
        var operations = new List<FnvFileOperation>();
        if (request.Mode == FnvConversionMode.Restore)
        {
            operations.Add(new FnvFileOperation(FnvFileOperationKind.DeleteManagedFile, string.Empty,
                Path.Combine(target, ManagedFileName), "Remove only the AVRcade-owned profile marker/configuration."));
            return new FnvConversionPlan(request, source, target, operations, warnings);
        }

        operations.Add(new FnvFileOperation(FnvFileOperationKind.CreateDirectory, string.Empty, target,
            "Create the isolated VR profile directory when it does not exist."));
        var portableExecutable = Path.Combine(Path.GetFullPath(request.ModOrganizerInstanceDirectory), "ModOrganizer.exe");
        if (File.Exists(portableExecutable))
        {
            operations.Add(new FnvFileOperation(FnvFileOperationKind.WriteIfMissing, string.Empty,
                Path.Combine(Path.GetFullPath(request.ModOrganizerInstanceDirectory), "portable.txt"),
                "Bind this explicitly selected MO2 directory as a portable instance without changing another instance."));
            var portableIni = string.Join(Environment.NewLine,
                "[General]",
                "gameName=Fallout New Vegas",
                $"gamePath={Path.GetFullPath(request.GameDirectory).Replace('\\', '/')}",
                $"selected_profile={request.VrProfileName}",
                string.Empty);
            operations.Add(new FnvFileOperation(FnvFileOperationKind.WriteIfMissing, portableIni,
                Path.Combine(Path.GetFullPath(request.ModOrganizerInstanceDirectory), "ModOrganizer.ini"),
                "Initialize the new portable instance with the verified game path and isolated VR profile."));
        }
        foreach (var fileName in CloneFiles)
        {
            if (fresh)
            {
                if (fileName is "modlist.txt" or "plugins.txt" or "loadorder.txt")
                    operations.Add(new FnvFileOperation(FnvFileOperationKind.WriteIfMissing, string.Empty,
                        Path.Combine(target, fileName), $"Initialize {fileName} for a fresh isolated profile."));
                continue;
            }
            var sourceFile = Path.Combine(source, fileName);
            if (File.Exists(sourceFile))
            {
                operations.Add(new FnvFileOperation(FnvFileOperationKind.CopyIfMissing, sourceFile,
                    Path.Combine(target, fileName), $"Clone {fileName} without overwriting an existing VR-profile file."));
            }
        }

        var managedDocument = JsonSerializer.Serialize(new Dictionary<string, object?>
        {
            ["schema_version"] = 1,
            ["managed_by"] = "VRClient",
            ["game"] = "fallout-new-vegas",
            ["source_profile"] = request.SourceProfileName,
            ["vr_profile"] = request.VrProfileName,
            ["game_directory"] = Path.GetFullPath(request.GameDirectory),
            ["policy"] = new Dictionary<string, object?>
            {
                ["never_modify_mods_directory"] = true,
                ["never_reorder_source_profile"] = true,
                ["third_party_packages"] = "user-supplied-only"
            }
        }, new JsonSerializerOptions { WriteIndented = true });
        operations.Add(new FnvFileOperation(FnvFileOperationKind.WriteManagedFile, managedDocument,
            Path.Combine(target, ManagedFileName), "Write the AVRcade-owned profile marker/configuration with backup-on-change."));

        if (File.Exists(Path.Combine(target, TransactionFileName)))
            warnings.Add("An interrupted AVRcade transaction journal exists; repair mode will resume safely and idempotently.");
        return new FnvConversionPlan(request, source, target, operations, warnings);
    }

    public FnvConversionResult Apply(FnvConversionPlan plan)
    {
        if (plan.Request.DryRun)
            return new FnvConversionResult(false, false, Array.Empty<string>(), plan.Warnings);
        if (!plan.Request.AcknowledgeMutation)
            throw new InvalidOperationException("conversion mutation requires explicit acknowledgement after reviewing the dry-run plan");

        var target = plan.VrProfileDirectory;
        if (plan.Request.Mode == FnvConversionMode.Restore && !Directory.Exists(target))
            return new FnvConversionResult(false, false, Array.Empty<string>(), plan.Warnings);
        var transactionPath = Path.Combine(target, TransactionFileName);
        var recovered = File.Exists(transactionPath);
        Directory.CreateDirectory(target);
        var applied = new List<string>();
        WriteAtomic(transactionPath, JsonSerializer.Serialize(new
        {
            schema_version = 1,
            status = "applying",
            mode = plan.Request.Mode.ToString().ToLowerInvariant(),
            operation_count = plan.Operations.Count,
            started_at_utc = DateTimeOffset.UtcNow.ToString("O")
        }, new JsonSerializerOptions { WriteIndented = true }));

        try
        {
            foreach (var operation in plan.Operations)
            {
                if (ApplyOperation(operation, target))
                    applied.Add(operation.Description);
            }
            File.Delete(transactionPath);
        }
        catch
        {
            // Leave the journal in place. A later repair run observes it and retries
            // only idempotent/copy-if-missing/VRClient-owned operations.
            throw;
        }
        return new FnvConversionResult(applied.Count > 0, recovered, applied, plan.Warnings);
    }

    private static bool ApplyOperation(FnvFileOperation operation, string profileDirectory)
    {
        switch (operation.Kind)
        {
            case FnvFileOperationKind.CreateDirectory:
                if (Directory.Exists(operation.Destination))
                    return false;
                Directory.CreateDirectory(operation.Destination);
                return true;

            case FnvFileOperationKind.CopyIfMissing:
                if (File.Exists(operation.Destination))
                    return false;
                Directory.CreateDirectory(Path.GetDirectoryName(operation.Destination)!);
                File.Copy(operation.Source, operation.Destination, overwrite: false);
                return true;

            case FnvFileOperationKind.WriteIfMissing:
                if (File.Exists(operation.Destination))
                    return false;
                Directory.CreateDirectory(Path.GetDirectoryName(operation.Destination)!);
                using (var stream = new FileStream(operation.Destination, FileMode.CreateNew, FileAccess.Write))
                using (var writer = new StreamWriter(stream))
                    writer.Write(operation.Source);
                return true;

            case FnvFileOperationKind.WriteManagedFile:
                if (File.Exists(operation.Destination))
                {
                    var old = File.ReadAllText(operation.Destination);
                    if (Normalize(old) == Normalize(operation.Source))
                        return false;
                    BackupManagedFile(profileDirectory, operation.Destination, old);
                }
                WriteAtomic(operation.Destination, operation.Source);
                return true;

            case FnvFileOperationKind.DeleteManagedFile:
                if (!File.Exists(operation.Destination))
                    return false;
                File.Delete(operation.Destination);
                return true;

            default:
                throw new ArgumentOutOfRangeException(nameof(operation.Kind));
        }
    }

    private static void BackupManagedFile(string profileDirectory, string managedPath, string content)
    {
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(content))).ToLowerInvariant()[..12];
        var backupDirectory = Path.Combine(profileDirectory, ".vrclient-backups");
        Directory.CreateDirectory(backupDirectory);
        var backupPath = Path.Combine(backupDirectory, $"{ManagedFileName}.{hash}.bak");
        if (!File.Exists(backupPath))
            File.WriteAllText(backupPath, content);
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
        var rootWithSeparator = Path.GetFullPath(root + Path.DirectorySeparatorChar);
        var candidate = Path.GetFullPath(Path.Combine(root, name));
        if (!candidate.StartsWith(rootWithSeparator, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("profile path escapes the MO2 profiles directory");
        return candidate;
    }

    private static void ValidateProfileName(string name, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(name) || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
            name.Contains(Path.DirectorySeparatorChar) || name.Contains(Path.AltDirectorySeparatorChar) || name is "." or "..")
            throw new ArgumentException("profile name must be a single safe directory name", parameterName);
    }

    private static string Normalize(string value) => value.Replace("\r\n", "\n").Trim();
}
