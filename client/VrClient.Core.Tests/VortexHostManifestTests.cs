using System.Xml.Linq;
using VrClient.Core;
using Xunit;

public sealed class VortexHostManifestTests
{
    [Fact]
    public void WindowsManifestDeclaresSupportedOsForAvaloniaNativeControlHost()
    {
        var root = VrClient.Core.Tests.TestRepoRoot.Find();
        Assert.NotNull(root);
        var manifest = XDocument.Load(Path.Combine(root,
            "client", "VrClient.App", "app.manifest"));
        Assert.Contains(manifest.Descendants(), element =>
            element.Name.LocalName == "supportedOS" &&
            string.Equals((string?)element.Attribute("Id"),
                "{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void WindowsManifestUsesPerMonitorV1ForVortexWindowHosting()
    {
        var root = VrClient.Core.Tests.TestRepoRoot.Find();
        Assert.NotNull(root);
        var manifest = XDocument.Load(Path.Combine(root,
            "client", "VrClient.App", "app.manifest"));
        Assert.Contains(manifest.Descendants(), element =>
            element.Name.LocalName == "dpiAwareness" &&
            string.Equals(element.Value.Trim(), "PerMonitor", StringComparison.OrdinalIgnoreCase));
    }
}
