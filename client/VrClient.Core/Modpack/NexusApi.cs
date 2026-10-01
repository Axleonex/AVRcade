namespace VrClient.Core.Modpack;
using System.Text.Json;

/// ECO-1: Nexus Mods ecosystem adapter — OFFICIAL API v1 only (their TOS forbids
/// scraping/unofficial automation). The API key is the USER'S personal key,
/// supplied via the NEXUS_API_KEY environment variable or a local file outside
/// the repo (%LOCALAPPDATA%\vrclient\nexus-api-key.txt) — never committed,
/// never shipped, never shared between users.
///
/// Known API constraint: direct download links are PREMIUM-only. The compliant
/// free-account flow is: user downloads the file via browser/nxm, VRClient
/// verifies it by MD5 reverse-lookup (md5_search) against the official mod
/// files, then installs. Premium accounts can use GetDownloadLinkAsync.
public sealed record NexusUser(string Name, int UserId, bool IsPremium);
public sealed record NexusGame(int Id, string Name, string DomainName);
public sealed record NexusModFile(long FileId, string FileName, string Version, long SizeKb, string? Md5);
public sealed record NexusDownloadProbe(bool Allowed, string Detail);

public sealed class NexusApi(HttpClient http, string apiKey)
{
    private const string BaseUrl = "https://api.nexusmods.com/v1";

    private async Task<(int Status, string Body)> GetAsync(string path, CancellationToken ct)
    {
        using var req = new HttpRequestMessage(HttpMethod.Get, $"{BaseUrl}{path}");
        req.Headers.Add("apikey", apiKey);
        req.Headers.Add("Application-Name", "vrclient");
        req.Headers.Add("Application-Version", "0.1");
        using var resp = await http.SendAsync(req, ct);
        return ((int)resp.StatusCode, await resp.Content.ReadAsStringAsync(ct));
    }

    /// Validate the key and identify the account (incl. premium status, which
    /// decides the download flow).
    public async Task<NexusUser> ValidateAsync(CancellationToken ct = default)
    {
        var (status, body) = await GetAsync("/users/validate.json", ct);
        if (status != 200)
            throw new InvalidOperationException($"nexus_key_invalid - validate returned HTTP {status}");
        return ParseUser(body);
    }

    public async Task<NexusGame> GetGameAsync(string domain, CancellationToken ct = default)
    {
        var (status, body) = await GetAsync($"/games/{domain}.json", ct);
        if (status != 200)
            throw new InvalidOperationException($"nexus_game_not_found - '{domain}' returned HTTP {status}");
        return ParseGame(body);
    }

    public async Task<IReadOnlyList<NexusModFile>> GetModFilesAsync(
        string domain, long modId, CancellationToken ct = default)
    {
        var (status, body) = await GetAsync($"/games/{domain}/mods/{modId}/files.json", ct);
        if (status != 200)
            throw new InvalidOperationException($"nexus_mod_files_failed - HTTP {status}");
        return ParseFiles(body);
    }

    /// Reverse MD5 lookup: confirms a manually-downloaded file is byte-identical
    /// to an official file of some mod on this game. The free-account integrity
    /// check.
    public async Task<bool> Md5BelongsToGameAsync(string domain, string md5Hex, CancellationToken ct = default)
    {
        var (status, _) = await GetAsync($"/games/{domain}/mods/md5_search/{md5Hex.ToLowerInvariant()}.json", ct);
        return status == 200; // 404 = no official file with this hash
    }

    /// Premium-only endpoint; on free accounts the API refuses — we surface that
    /// cleanly instead of failing.
    public async Task<NexusDownloadProbe> ProbeDownloadLinkAsync(
        string domain, long modId, long fileId, CancellationToken ct = default)
    {
        var (status, body) = await GetAsync($"/games/{domain}/mods/{modId}/files/{fileId}/download_link.json", ct);
        return status == 200
            ? new NexusDownloadProbe(true, "direct download links available (premium account)")
            : new NexusDownloadProbe(false,
                $"HTTP {status} - expected for non-premium accounts; free-account flow = manual browser download + md5 verification. Body: {Truncate(body)}");
    }

    // ---- parsing (tolerant: Nexus adds fields freely) ----

    public static NexusUser ParseUser(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var r = doc.RootElement;
        return new NexusUser(
            r.TryGetProperty("name", out var n) ? n.GetString() ?? "?" : "?",
            r.TryGetProperty("user_id", out var id) ? id.GetInt32() : 0,
            r.TryGetProperty("is_premium", out var p) && p.ValueKind == JsonValueKind.True);
    }

    public static NexusGame ParseGame(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var r = doc.RootElement;
        return new NexusGame(
            r.TryGetProperty("id", out var id) ? id.GetInt32() : 0,
            r.TryGetProperty("name", out var n) ? n.GetString() ?? "?" : "?",
            r.TryGetProperty("domain_name", out var d) ? d.GetString() ?? "?" : "?");
    }

    public static IReadOnlyList<NexusModFile> ParseFiles(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var files = new List<NexusModFile>();
        if (!doc.RootElement.TryGetProperty("files", out var arr) || arr.ValueKind != JsonValueKind.Array)
            return files;
        foreach (var f in arr.EnumerateArray())
            files.Add(new NexusModFile(
                f.TryGetProperty("file_id", out var id) ? id.GetInt64() : 0,
                f.TryGetProperty("file_name", out var fn) ? fn.GetString() ?? "?" : "?",
                f.TryGetProperty("version", out var v) ? v.GetString() ?? "?" : "?",
                f.TryGetProperty("size_kb", out var s) ? s.GetInt64() : 0,
                f.TryGetProperty("md5", out var m) ? m.GetString() : null));
        return files;
    }

    private static string Truncate(string s) => s.Length <= 160 ? s : s[..160] + "...";

    /// Key resolution: env var first, then the user-local file. NEVER a repo path.
    public static string? ResolveApiKey()
    {
        var env = Environment.GetEnvironmentVariable("NEXUS_API_KEY");
        if (!string.IsNullOrWhiteSpace(env))
            return env.Trim();
        var file = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "vrclient", "nexus-api-key.txt");
        return File.Exists(file) ? File.ReadAllText(file).Trim() : null;
    }
}
