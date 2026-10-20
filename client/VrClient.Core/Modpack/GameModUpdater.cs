using System.Diagnostics;
using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Config;
using VrClient.Core.Redengine;
using VrClient.Core.Unreal;

namespace VrClient.Core.Modpack;

public sealed record ModReleaseCheck(string Name, string Supported, string? Upstream, bool NewerAvailable, string Detail, string? Url = null);
public sealed record GameModUpdateResult(bool Ok, string Message, string CheckedAtUtc, IReadOnlyList<ModReleaseCheck> Releases);

/// Manual, game-scoped updates. Version-specific integrations keep their reviewed
/// package policy; newer author releases are reported separately from installation.
public sealed class GameModUpdater(string repoRoot, HttpClient http)
{
    private sealed class Download(HttpClient client) : IHttpDownloader
    {
        public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default) => client.GetByteArrayAsync(url, ct);
    }
    private readonly IReadOnlyDictionary<string, GameModUpdateRoute> _routes = GameModUpdateRoutes.Load(repoRoot);
    private delegate Task<ActionOutcome> Adapter(GameView game, AppController controller,
        IProgress<string>? progress, CancellationToken ct);

    public async Task<GameModUpdateResult> UpdateAsync(GameView game, AppController controller,
        IProgress<string>? progress = null, CancellationToken ct = default)
    {
        if (!_routes.TryGetValue(game.Slug, out var route))
            return new(false, "No public-mod update adapter is configured for this game.", DateTime.UtcNow.ToString("O"), []);
        if (game.InstallDir is not { } root || !Directory.Exists(root))
            return new(false, "Install or locate the game before updating its VR components.", DateTime.UtcNow.ToString("O"), []);
        if (game.Readiness is GameReadiness.Blocked)
            return new(false, game.ReadinessDetail, DateTime.UtcNow.ToString("O"), []);

        void EnsureClosed() => EnsureProcessesClosed(route.ProcessNames);
        EnsureClosed();
        var lockPath = ModUpdateTransaction.Resolve(root, ".avrcade/mod-updates/update.lock");
        Directory.CreateDirectory(Path.GetDirectoryName(lockPath)!);
        using var lease = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        ModUpdateTransaction.Recover(root);
        var releases = new List<ModReleaseCheck>();
        foreach (var source in route.Sources)
        {
            progress?.Report($"Checking {source.Name} upstream releases...");
            releases.Add(await CheckSourceAsync(source, SupportedReference(game, route, source), ct));
        }
        var adapters = new Dictionary<string, Adapter>
        {
            ["thunderstore"] = async (item, app, status, token) =>
            {
                var safety = app.EvaluateSafety(item.Slug);
                if (safety.Verdict is Model.Verdict.Block or Model.Verdict.UnknownBlocked)
                    return new(false, safety.Explanation);
                return await ThunderstoreModUpdater.UpdateAsync(repoRoot, item, new LiveThunderstoreApi(http),
                    new Download(http), EnsureClosed, status, token);
            },
            ["cyberpunk"] = UpdateCyberpunkAsync,
            ["uevr"] = UpdateUnrealAsync,
            ["paired"] = UpdatePairedAsync
        };
        EnsureClosed();
        var outcome = await adapters[route.Adapter](game, controller, progress, ct);
        var notices = releases.Where(release => release.NewerAvailable)
            .Select(release => $"{release.Name}: upstream {release.Upstream} is newer than supported {ShortReference(release.Supported)}; it needs a compatible AVRcade package before installation.");
        var incomplete = releases.Where(release => release.Upstream is null).Select(release => release.Detail);
        var scope = route.Adapter == "thunderstore"
            ? new[] { "This updates the game-folder VR conversion. Mod-manager profiles retain their own versions." }
            : Array.Empty<string>();
        var message = string.Join("\n", new[] { outcome.Message }.Concat(scope).Concat(notices).Concat(incomplete));
        var result = new GameModUpdateResult(outcome.Ok, message, DateTime.UtcNow.ToString("O"), releases);
        AtomicFile.WriteAllText(ModUpdateTransaction.Resolve(root, ".avrcade/mod-updates/last-result.json"), JsonSerializer.Serialize(result));
        return result;
    }

    private async Task<ActionOutcome> UpdateCyberpunkAsync(GameView game, AppController controller, IProgress<string>? progress, CancellationToken ct)
    {
        var state = CyberpunkUpstreamSession.ReadState(game.InstallDir!);
        var package = CyberpunkBackendInstaller.LoadPackage(Path.Combine(repoRoot, "config", "redengine", "native", "cyberpunk-2077-backend.json"));
        if (state?.Version == package.Version && game.ModInstalled)
            return new(true, $"Supported CyberpunkVR Port {package.Version} is installed. Your HUD and controller settings were kept.");
        progress?.Report($"Installing supported CyberpunkVR Port {package.Version}...");
        return await controller.InstallCyberpunkVrAsync(game.InstallDir!, new Download(http), ct);
    }

    private async Task<ActionOutcome> UpdateUnrealAsync(GameView game, AppController controller, IProgress<string>? progress, CancellationToken ct)
    {
        progress?.Report("Checking the supported UEVR tools and runtime...");
        var tools = await controller.PrepareUnrealAsync(game.Slug, new Download(http), ct);
        if (!tools.Ok) return tools;
        if (game.ProfileImportRequired)
        {
            progress?.Report("Checking this game's reviewed VR profile; preserving existing profile settings...");
            var profile = await controller.DownloadUnrealProfileAsync(game.Slug, new Download(http), ct);
            if (!profile.Ok) return profile;
        }
        return new(true, "Supported UEVR tools and this game's reviewed profile are installed. Existing valid profile settings were kept.");
    }

    private Task<ActionOutcome> UpdatePairedAsync(GameView game, AppController _, IProgress<string>? progress, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        progress?.Report("Checking the paired UUVR and AVRcade bridge package...");
        var installed = YapyapVrInstall.Inspect(game.InstallDir);
        return Task.FromResult(new ActionOutcome(installed.Installed,
            installed.Installed
                ? "The reviewed UUVR and AVRcade bridge package is installed. Independent UUVR builds are not substituted for the paired package."
                : installed.Detail));
    }

    private string SupportedReference(GameView game, GameModUpdateRoute route, ModReleaseSource source) => route.Adapter switch
    {
        "cyberpunk" => CyberpunkBackendInstaller.LoadPackage(Path.Combine(repoRoot, "config", "redengine", "native", "cyberpunk-2077-backend.json")).Version,
        "uevr" => UevrTools.LoadReleasePin(Path.Combine(repoRoot, "config", "unreal", "uevr-release.json"),
            UevrTools.LoadGame(Path.Combine(repoRoot, "config", "unreal", game.Slug + ".uevr.json")).Channel).Tag,
        _ => source.ReviewedReference ?? throw new InvalidDataException("An upstream source is missing its reviewed reference.")
    };

    private async Task<ModReleaseCheck> CheckSourceAsync(ModReleaseSource source, string supported, CancellationToken ct)
    {
        try
        {
            if (source.Kind == "commits")
            {
                using var compared = await ReadGithubAsync($"repos/{source.Repository}/compare/{Uri.EscapeDataString(supported)}...{Uri.EscapeDataString(source.Branch)}", ct);
                var root = compared.RootElement;
                var ahead = root.GetProperty("ahead_by").GetInt32();
                var head = root.GetProperty("commits").EnumerateArray().LastOrDefault();
                var upstream = ahead > 0 && head.ValueKind == JsonValueKind.Object ? head.GetProperty("sha").GetString()! : supported;
                return new(source.Name, supported, ShortReference(upstream), ahead > 0,
                    ahead > 0 ? "New upstream source commits exist; the paired bridge remains on its reviewed revision." : "The reviewed source revision is current.",
                    $"https://github.com/{source.Repository}");
            }
            using var releases = await ReadGithubAsync($"repos/{source.Repository}/releases?per_page=30", ct);
            var list = releases.RootElement.EnumerateArray().Where(item => !item.GetProperty("draft").GetBoolean())
                .OrderByDescending(item => item.GetProperty("published_at").GetDateTimeOffset()).ToArray();
            if (list.Length == 0) return new(source.Name, supported, null, false, $"{source.Name}: no published release metadata was returned.");
            var reviewed = list.FirstOrDefault(item => SameTag(item.GetProperty("tag_name").GetString()!, supported));
            using var reviewedRelease = reviewed.ValueKind == JsonValueKind.Undefined
                ? await ReadGithubAsync($"repos/{source.Repository}/releases/tags/{Uri.EscapeDataString(supported)}", ct) : null;
            if (reviewedRelease is not null) reviewed = reviewedRelease.RootElement;
            var latest = list[0];
            var latestTag = latest.GetProperty("tag_name").GetString()!;
            var newer = latest.GetProperty("published_at").GetDateTimeOffset() > reviewed.GetProperty("published_at").GetDateTimeOffset() && !SameTag(latestTag, supported);
            return new(source.Name, supported, latestTag, newer, newer ? "Newer upstream release found." : "No newer published upstream release found.", latest.GetProperty("html_url").GetString());
        }
        catch (Exception ex) when (ex is HttpRequestException or JsonException or InvalidOperationException or KeyNotFoundException)
        { return new(source.Name, supported, null, false, $"{source.Name}: upstream check could not complete ({ex.Message}). No current-version claim was made."); }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        { return new(source.Name, supported, null, false, $"{source.Name}: upstream check timed out. Its release status is unknown."); }
    }

    private async Task<JsonDocument> ReadGithubAsync(string resource, CancellationToken ct)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(TimeSpan.FromSeconds(20));
        using var request = new HttpRequestMessage(HttpMethod.Get, "https://api.github.com/" + resource);
        request.Headers.UserAgent.ParseAdd("AVRcade-ModUpdater/1.0");
        request.Headers.Accept.ParseAdd("application/vnd.github+json");
        using var response = await http.SendAsync(request, timeout.Token);
        response.EnsureSuccessStatusCode();
        return JsonDocument.Parse(await response.Content.ReadAsStringAsync(timeout.Token));
    }

    private static string ShortReference(string value) => value.Length == 40 && value.All(char.IsAsciiHexDigit) ? value[..8] : value;
    private static bool SameTag(string left, string right) => left.TrimStart('v', 'V').Equals(right.TrimStart('v', 'V'), StringComparison.OrdinalIgnoreCase);

    private static void EnsureProcessesClosed(IEnumerable<string> names)
    {
        foreach (var name in names)
        {
            var processes = Process.GetProcessesByName(name);
            try { if (processes.Length > 0) throw new IOException("Close the game before updating its VR components."); }
            finally { foreach (var process in processes) process.Dispose(); }
        }
    }
}
