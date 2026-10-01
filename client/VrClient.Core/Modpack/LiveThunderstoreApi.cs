namespace VrClient.Core.Modpack;

public sealed class LiveThunderstoreApi(HttpClient http) : IThunderstoreApi
{
    public async Task<string> GetPackageIndexJsonAsync(string community, CancellationToken ct = default)
    {
        using var resp = await http.GetAsync(
            $"https://thunderstore.io/c/{community}/api/v1/package/", ct);
        resp.EnsureSuccessStatusCode();
        return await resp.Content.ReadAsStringAsync(ct);
    }
}
