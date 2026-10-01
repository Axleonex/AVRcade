namespace VrClient.Core.Tests;

/// The source checkout root. VrClient.Core.RepoRoot.Find() stops at the test
/// output folder, because the catalog fixtures copied there satisfy its marker.
internal static class TestRepoRoot
{
    public static string Find()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "client", "VrClient.sln")))
                return dir.FullName;
            dir = dir.Parent;
        }
        throw new DirectoryNotFoundException("VRClient repo root not found.");
    }
}
