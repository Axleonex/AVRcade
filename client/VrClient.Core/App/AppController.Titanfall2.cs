using System.Text.Json;
using VrClient.Core.Discovery;
using VrClient.Core.Launch;

namespace VrClient.Core.App;

public sealed partial class AppController
{
    private const string Titanfall2Slug = "titanfall-2";
    private const string Titanfall2AppId = "1237970";
    private const string Titanfall2Executable = "Titanfall2.exe";

    public const string Titanfall2InstallerName = "CircuitLordVRModInstaller.exe";
    public const string Titanfall2InstallerReleases =
        "https://github.com/CircuitLord/CircuitLordVRModInstaller/releases/latest";

    private static readonly string[] Titanfall2VrFiles =
    [
        "Titanfall2VRLauncher.exe",
        "TF2VR/Northstar.dll",
        "TF2VR/plugins/Titanfall2VR.dll",
        "TF2VR/tools/xr_probe.exe",
        "TF2VR/tools/crash_monitor.exe",
        "TF2VR/tools/launch.json"
    ];

    private void AddTitanfall2CatalogueEntry(List<GameView> views)
    {
        var path = Path.Combine(_repoRoot, "config", "catalogue", Titanfall2Slug + ".json");
        if (!File.Exists(path)) return;
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var config = document.RootElement;
        if (config.GetProperty("schema").GetString() != "flat-catalogue-game/1" ||
            config.GetProperty("game_id").GetString() != Titanfall2Slug) return;

        var install = _locator(Titanfall2AppId, Titanfall2Executable);
        var offline = install is null ? new SteamLibraryScanner().FindOfflineLibrary(Titanfall2AppId) : null;
        var vrInstalled = install is not null && HasTitanfall2VrProfile(install);
        var readiness = install is null ? GameReadiness.NeedsGame
            : vrInstalled ? GameReadiness.ProfileUnverified : GameReadiness.NeedsVrMod;
        var detail = offline is not null ? OfflineLibraryDetail(offline)
            : install is null ? "CircuitLord's installer can locate a Steam, EA app, or Xbox copy."
            : vrInstalled ? "The TF2VR profile was detected. Open CircuitLord's installer to play."
            : "Install Titanfall 2 VR through CircuitLord's installer.";
        views.Add(new(Titanfall2Slug, config.GetProperty("display_name").GetString()!, Titanfall2AppId,
            install, vrInstalled, "Allow", "titanfall2_campaign_handoff", GameEngine.Source,
            readiness, detail, OfflineSteamLibrary: offline));
    }

    private static bool HasTitanfall2VrProfile(string root)
    {
        var receipt = Path.Combine(root, ".circuitlord-vr-mods", "Titanfall2VR.json");
        if (!File.Exists(receipt) || !Titanfall2VrFiles.All(file => File.Exists(Path.Combine(root, file))))
            return false;
        try
        {
            using var record = JsonDocument.Parse(File.ReadAllText(receipt));
            return record.RootElement.ValueKind is JsonValueKind.Object &&
                record.RootElement.TryGetProperty("id", out var id) &&
                id.ValueKind is JsonValueKind.String && id.GetString() == "Titanfall2VR";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return false;
        }
    }

    public ActionOutcome LaunchTitanfall2Vanilla(bool dryRun = false, string? steamRoot = null)
    {
        if (_locator(Titanfall2AppId, Titanfall2Executable) is null)
            return new(false, "Install Titanfall 2 in Steam before starting it from AVRcade.");
        var launch = new SteamOwnedLauncher().Launch(Titanfall2AppId, steamRoot, dryRun);
        return new(launch.Success, launch.Success
            ? dryRun ? launch.Detail! : "Steam is starting Titanfall 2 on the monitor."
            : launch.FailureText);
    }
}
