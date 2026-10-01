namespace VrClient.Core.Discovery;

public static class SteamLinks
{
    /// The public store page for a Steam app; AVRcade never downloads games itself.
    public static string StorePage(string appId) => $"https://store.steampowered.com/app/{appId}/";
}
