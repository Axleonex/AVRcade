namespace VrClient.Core.Legacy;

using System.Text.Json;

public enum GtaSanAndreasPreflightStatus
{
    ProfileMissing,
    ProfileInvalid,
    GameNotFound,
    ExecutableNotFound,
    ArchitectureMismatch,
    FingerprintUnpinned,
    HashMismatch,
    BridgeRequired,
    BridgeNotFound,
    BridgeArchitectureMismatch,
    BridgeUnverified,
    BridgeHashMismatch,
    ReadyForExternalBridge
}

public sealed record GtaSanAndreasProfile(
    string Slug,
    string DisplayName,
    string ExecutableName,
    string RequiredArchitecture,
    string Renderer,
    string ExpectedSha256,
    string? BridgePath,
    string? BridgeSha256,
    string BridgeArchitecture,
    IReadOnlyList<string> ModFileGlobs,
    IReadOnlyList<string> ModDirectories);

public sealed record GtaSanAndreasPreflightResult(
    GtaSanAndreasPreflightStatus Status,
    int ExitCode,
    string? InstallRoot,
    string? ExecutablePath,
    string? ExpectedSha256,
    string? ActualSha256,
    string? ExpectedArchitecture,
    string? ActualArchitecture,
    string? ActualBridgeArchitecture,
    string? BridgePath,
    string? ExpectedBridgeSha256,
    string? ActualBridgeSha256,
    IReadOnlyList<string> ModArtifacts)
{
    /// True only when the game identity is exact; this does not imply that VR is playable.
    public bool GameIdentityReady => Status is
        GtaSanAndreasPreflightStatus.BridgeRequired or
        GtaSanAndreasPreflightStatus.BridgeNotFound or
        GtaSanAndreasPreflightStatus.BridgeArchitectureMismatch or
        GtaSanAndreasPreflightStatus.BridgeUnverified or
        GtaSanAndreasPreflightStatus.BridgeHashMismatch or
        GtaSanAndreasPreflightStatus.ReadyForExternalBridge;

    /// True means an exact, pinned external bridge was found. Headset/live evidence is still required.
    public bool Ready => Status == GtaSanAndreasPreflightStatus.ReadyForExternalBridge;
}

/// <summary>
/// Read-only, fail-closed eligibility evaluation for the classic Windows GTA SA executable.
/// It deliberately does not install an ASI loader, replace game files, or claim a VR bridge.
/// </summary>
public sealed class GtaSanAndreasPreflight
{
    private const ushort X86Machine = 0x014c;
    private const ushort X64Machine = 0x8664;
    private const ushort Arm64Machine = 0xaa64;
    private const string InstallRelativePath =
        @"Grand Theft Auto San Andreas + Utilities\GTA San Andreas";

    public const string SteamAppId = "12120";
    public const string ExecutableName = "gta_sa.exe";

    private static string RememberedInstallPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "gta-san-andreas-folder.txt");

    /// In order: the VRCLIENT_GTASA_DIR override, the folder the player pointed
    /// AVRcade at, the known classic-PC layout on any drive, then the Steam library.
    public static string? FindDefaultInstall()
    {
        var overridePath = Environment.GetEnvironmentVariable("VRCLIENT_GTASA_DIR");
        if (!IsGameInstall(overridePath))
            overridePath = ReadRememberedInstall();
        return FindDefaultInstall(overridePath, Environment.GetLogicalDrives())
            ?? new Discovery.SteamLibraryScanner().FindGame(SteamAppId, ExecutableName)?.InstallDir;
    }

    /// Remember a folder the player chose. Returns false when it holds no gta_sa.exe.
    public static bool RememberInstall(string folder)
    {
        if (!IsGameInstall(folder)) return false;
        Directory.CreateDirectory(Path.GetDirectoryName(RememberedInstallPath)!);
        File.WriteAllText(RememberedInstallPath, Path.GetFullPath(folder));
        return true;
    }

    private static string? ReadRememberedInstall()
    {
        try
        {
            return File.Exists(RememberedInstallPath) ? File.ReadAllText(RememberedInstallPath).Trim() : null;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    /// Resolve the known classic-PC layout without assuming that an external volume
    /// keeps the same drive letter. The optional inputs are a small test seam; the
    /// production overload above supplies the environment override and mounted roots.
    public static string? FindDefaultInstall(
        string? overridePath,
        IEnumerable<string> volumeRoots)
    {
        if (IsGameInstall(overridePath))
            return Path.GetFullPath(overridePath!);

        foreach (var root in volumeRoots
                     .Where(root => !string.IsNullOrWhiteSpace(root))
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            try
            {
                var candidate = Path.Combine(root, InstallRelativePath);
                if (IsGameInstall(candidate))
                    return Path.GetFullPath(candidate);
            }
            catch (ArgumentException)
            {
                // A malformed mounted-root entry is not a reason to stop scanning.
            }
            catch (NotSupportedException)
            {
                // A provider can expose a root that the current platform cannot parse.
            }
        }

        return null;
    }

    private static bool IsGameInstall(string? path)
    {
        if (string.IsNullOrWhiteSpace(path)) return false;
        try
        {
            return File.Exists(Path.Combine(path, ExecutableName));
        }
        catch (ArgumentException)
        {
            return false;
        }
        catch (NotSupportedException)
        {
            return false;
        }
    }

    public GtaSanAndreasProfile LoadProfile(string profilePath)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(profilePath));
        var root = document.RootElement;
        var game = root.GetProperty("game");
        var bridge = root.GetProperty("bridge");
        var modSurface = root.GetProperty("mod_surface");

        return new GtaSanAndreasProfile(
            RequiredString(game, "slug"),
            RequiredString(game, "display_name"),
            RequiredString(game, "executable_name"),
            RequiredString(game, "required_architecture"),
            RequiredString(game, "renderer"),
            RequiredString(game, "executable_sha256_observed"),
            OptionalString(bridge, "path"),
            OptionalString(bridge, "sha256"),
            RequiredString(bridge, "architecture"),
            StringArray(modSurface, "file_globs"),
            StringArray(modSurface, "directories"));
    }

    public GtaSanAndreasPreflightResult Evaluate(
        string profilePath,
        string? gameDir,
        string? bridgePath = null)
    {
        if (!File.Exists(profilePath))
            return Result(GtaSanAndreasPreflightStatus.ProfileMissing, 20);

        GtaSanAndreasProfile profile;
        try
        {
            profile = LoadProfile(profilePath);
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException or KeyNotFoundException)
        {
            return Result(GtaSanAndreasPreflightStatus.ProfileInvalid, 21);
        }

        if (string.IsNullOrWhiteSpace(gameDir) || !Directory.Exists(gameDir))
            return Result(GtaSanAndreasPreflightStatus.GameNotFound, 10,
                expectedSha256: profile.ExpectedSha256,
                expectedArchitecture: profile.RequiredArchitecture);

        var installRoot = Path.GetFullPath(gameDir);
        var executablePath = Path.Combine(installRoot, profile.ExecutableName);
        if (!File.Exists(executablePath))
            return Result(GtaSanAndreasPreflightStatus.ExecutableNotFound, 11,
                installRoot, executablePath,
                expectedSha256: profile.ExpectedSha256,
                expectedArchitecture: profile.RequiredArchitecture);

        var actualArchitecture = ReadPeArchitecture(executablePath);
        if (!string.Equals(actualArchitecture, profile.RequiredArchitecture, StringComparison.OrdinalIgnoreCase))
            return Result(GtaSanAndreasPreflightStatus.ArchitectureMismatch, 12,
                installRoot, executablePath,
                expectedSha256: profile.ExpectedSha256,
                expectedArchitecture: profile.RequiredArchitecture,
                actualArchitecture: actualArchitecture);

        if (string.IsNullOrWhiteSpace(profile.ExpectedSha256))
            return Result(GtaSanAndreasPreflightStatus.FingerprintUnpinned, 13,
                installRoot, executablePath,
                expectedArchitecture: profile.RequiredArchitecture,
                actualArchitecture: actualArchitecture);

        var actualHash = Hashing.Sha256OfFile(executablePath);
        if (!string.Equals(actualHash, profile.ExpectedSha256, StringComparison.OrdinalIgnoreCase))
            return Result(GtaSanAndreasPreflightStatus.HashMismatch, 14,
                installRoot, executablePath,
                expectedSha256: profile.ExpectedSha256,
                actualSha256: actualHash,
                expectedArchitecture: profile.RequiredArchitecture,
                actualArchitecture: actualArchitecture);

        var configuredBridgePath = bridgePath ?? profile.BridgePath;
        var artifacts = ScanModArtifacts(installRoot, profile, configuredBridgePath);
        if (string.IsNullOrWhiteSpace(configuredBridgePath))
            return Result(GtaSanAndreasPreflightStatus.BridgeRequired, 22,
                installRoot, executablePath,
                profile.ExpectedSha256, actualHash,
                profile.RequiredArchitecture, actualArchitecture,
                modArtifacts: artifacts);

        var resolvedBridgePath = Path.IsPathRooted(configuredBridgePath)
            ? Path.GetFullPath(configuredBridgePath)
            : Path.GetFullPath(Path.Combine(installRoot, configuredBridgePath));
        if (!File.Exists(resolvedBridgePath))
            return Result(GtaSanAndreasPreflightStatus.BridgeNotFound, 23,
                installRoot, executablePath,
                profile.ExpectedSha256, actualHash,
                profile.RequiredArchitecture, actualArchitecture,
                resolvedBridgePath, profile.BridgeSha256,
                modArtifacts: artifacts);

        var bridgeArchitecture = ReadPeArchitecture(resolvedBridgePath);
        if (!string.Equals(bridgeArchitecture, profile.BridgeArchitecture, StringComparison.OrdinalIgnoreCase))
            return Result(GtaSanAndreasPreflightStatus.BridgeArchitectureMismatch, 24,
                installRoot, executablePath,
                profile.ExpectedSha256, actualHash,
                profile.RequiredArchitecture, actualArchitecture,
                resolvedBridgePath, profile.BridgeSha256,
                actualBridgeArchitecture: bridgeArchitecture,
                modArtifacts: artifacts);

        if (string.IsNullOrWhiteSpace(profile.BridgeSha256))
            return Result(GtaSanAndreasPreflightStatus.BridgeUnverified, 25,
                installRoot, executablePath,
                profile.ExpectedSha256, actualHash,
                profile.RequiredArchitecture, actualArchitecture,
                resolvedBridgePath, profile.BridgeSha256,
                actualBridgeArchitecture: bridgeArchitecture,
                modArtifacts: artifacts);

        var actualBridgeHash = Hashing.Sha256OfFile(resolvedBridgePath);
        if (!string.Equals(actualBridgeHash, profile.BridgeSha256, StringComparison.OrdinalIgnoreCase))
            return Result(GtaSanAndreasPreflightStatus.BridgeHashMismatch, 26,
                installRoot, executablePath,
                profile.ExpectedSha256, actualHash,
                profile.RequiredArchitecture, actualArchitecture,
                resolvedBridgePath, profile.BridgeSha256, actualBridgeHash,
                actualBridgeArchitecture: bridgeArchitecture,
                modArtifacts: artifacts);

        return Result(GtaSanAndreasPreflightStatus.ReadyForExternalBridge, 0,
            installRoot, executablePath,
            profile.ExpectedSha256, actualHash,
            profile.RequiredArchitecture, actualArchitecture,
            resolvedBridgePath, profile.BridgeSha256, actualBridgeHash,
            actualBridgeArchitecture: bridgeArchitecture,
            modArtifacts: artifacts);
    }

    public static string? ReadPeArchitecture(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            using var reader = new BinaryReader(stream);
            if (stream.Length < 0x40 || reader.ReadUInt16() != 0x5a4d)
                return null;

            stream.Position = 0x3c;
            var peOffset = reader.ReadInt32();
            if (peOffset < 0 || peOffset > stream.Length - 6)
                return null;

            stream.Position = peOffset;
            if (reader.ReadUInt32() != 0x00004550)
                return null;

            return reader.ReadUInt16() switch
            {
                X86Machine => "x86",
                X64Machine => "x64",
                Arm64Machine => "arm64",
                _ => "unknown"
            };
        }
        catch (IOException)
        {
            return null;
        }
        catch (UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static IReadOnlyList<string> ScanModArtifacts(
        string installRoot,
        GtaSanAndreasProfile profile,
        string? configuredBridgePath)
    {
        var artifacts = new List<string>();
        var bridgeName = string.IsNullOrWhiteSpace(configuredBridgePath)
            ? null
            : Path.GetFileName(configuredBridgePath);
        try
        {
            foreach (var file in Directory.EnumerateFiles(installRoot, "*", SearchOption.TopDirectoryOnly))
            {
                var name = Path.GetFileName(file);
                if (!string.Equals(name, bridgeName, StringComparison.OrdinalIgnoreCase) &&
                    profile.ModFileGlobs.Any(pattern => Matches(name, pattern)))
                    artifacts.Add(name);
            }

            foreach (var directory in profile.ModDirectories)
            {
                if (Directory.Exists(Path.Combine(installRoot, directory)))
                    artifacts.Add($"{directory}{Path.DirectorySeparatorChar}");
            }
        }
        catch (IOException)
        {
            // Scanning is advisory. Identity and bridge checks remain fail-closed.
        }
        catch (UnauthorizedAccessException)
        {
            // Scanning is advisory. Identity and bridge checks remain fail-closed.
        }

        return artifacts
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private static bool Matches(string name, string pattern)
    {
        if (pattern == "*") return true;
        if (pattern.StartsWith("*", StringComparison.Ordinal))
            return name.EndsWith(pattern[1..], StringComparison.OrdinalIgnoreCase);
        return string.Equals(name, pattern, StringComparison.OrdinalIgnoreCase);
    }

    private static GtaSanAndreasPreflightResult Result(
        GtaSanAndreasPreflightStatus status,
        int exitCode,
        string? installRoot = null,
        string? executablePath = null,
        string? expectedSha256 = null,
        string? actualSha256 = null,
        string? expectedArchitecture = null,
        string? actualArchitecture = null,
        string? bridgePath = null,
        string? expectedBridgeSha256 = null,
        string? actualBridgeSha256 = null,
        string? actualBridgeArchitecture = null,
        IReadOnlyList<string>? modArtifacts = null)
        => new(status, exitCode, installRoot, executablePath,
            expectedSha256, actualSha256, expectedArchitecture,
            actualArchitecture, actualBridgeArchitecture, bridgePath,
            expectedBridgeSha256, actualBridgeSha256, modArtifacts ?? []);

    private static string RequiredString(JsonElement parent, string property)
    {
        var value = parent.GetProperty(property).GetString();
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"profile property '{property}' is empty")
            : value;
    }

    private static string? OptionalString(JsonElement parent, string property) =>
        parent.TryGetProperty(property, out var value) && value.ValueKind != JsonValueKind.Null
            ? value.GetString()
            : null;

    private static IReadOnlyList<string> StringArray(JsonElement parent, string property) =>
        parent.GetProperty(property).EnumerateArray()
            .Select(value => value.GetString())
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Cast<string>()
            .ToList();
}
