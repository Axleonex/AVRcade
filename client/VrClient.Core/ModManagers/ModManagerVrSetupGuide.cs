namespace VrClient.Core.ModManagers;

/// <summary>Names the route's actual VR package without making AVRcade another mod catalog.</summary>
public static class ModManagerVrSetupGuide
{
    public static string PackageIdentity(CommunityVrRoute route) => route.PackageKey;

    public static string Instructions(CommunityVrRoute route, ModManagerKind manager)
    {
        var instructions = manager switch
        {
            ModManagerKind.R2Modman =>
                $"In r2modman, choose {route.GameFolder} and create a dedicated VR profile. " +
                $"On r2modman's mod pages, search for {route.Package} by {route.Namespace} " +
                $"(package ID {route.PackageKey}). Install it with its required dependencies. " +
                "Then reread manager profiles in AVRcade and select that VR profile. " +
                "If this game is unavailable in r2modman, choose a manager that supports it.",
            ModManagerKind.Thunderstore =>
                $"In Thunderstore Mod Manager, choose {route.GameFolder} and create a dedicated VR profile. " +
                $"On the manager's mod pages, search for {route.Package} by {route.Namespace} " +
                $"(package ID {route.PackageKey}). Install it with its required dependencies. " +
                "Then reread manager profiles in AVRcade and select that VR profile. " +
                "If this game is unavailable in the manager, choose one that supports it.",
            ModManagerKind.Vortex =>
                $"In Vortex, first confirm it supports {route.GameFolder} and this package's loader/layout. " +
                $"The VR mod is {route.Package} by {route.Namespace} (package ID {route.PackageKey}) on Thunderstore; " +
                "do not assume it appears in Vortex's Nexus catalog. Open the original mod page, " +
                "obtain its archive, and import it only if Vortex can deploy it correctly. " +
                "Add the listed dependencies, deploy a dedicated VR profile, and check it in Vortex. " +
                "If that route is unsupported, use r2modman or Thunderstore Mod Manager.",
            _ => throw new ArgumentOutOfRangeException(nameof(manager), manager, null)
        };
        return route.RequiresSteamVr ? instructions + " Start SteamVR before launching in VR." : instructions;
    }
}
