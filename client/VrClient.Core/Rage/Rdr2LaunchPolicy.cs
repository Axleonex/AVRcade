namespace VrClient.Core.Rage;

/// <summary>
/// Steam-forwarded launch arguments that form the native RDR2 bridge's
/// activation interface. Environment variables set on a newly invoked Steam
/// process are not used for activation because an already-running Steam
/// client does not inherit them.
/// </summary>
public static class Rdr2LaunchPolicy
{
    public const string RendererArgument = "-dx12";
    public const string StoryMarkerArgument = "-vrclient-rdr2-story";

    public static IReadOnlyList<string> SteamArguments { get; } =
        [RendererArgument, StoryMarkerArgument];
}
