using VrClient.Core.Config;
using System.Xml.Linq;

namespace VrClient.Core.Tests;

public sealed class CommunityConversionReferenceTests
{
    [Fact]
    public void LoadsLethalCompanyControlsAndUpstreamCredit()
    {
        var reference = ControllerReferenceCatalog.Load(FindRepoRoot(), "lethal-company");

        Assert.NotNull(reference);
        Assert.Equal(["quest-touch", "valve-index"], reference.Devices.Select(d => d.Id));
        Assert.Equal("DaXcess", reference.ConversionCredit?.Creator);
        Assert.Equal("LethalCompanyVR (LCVR)", reference.ConversionCredit?.Project);
        Assert.StartsWith("https://thunderstore.io/", reference.ConversionCredit?.ProjectUrl);
        Assert.Contains(reference.Devices[0].Bindings, binding =>
            binding.Control == "Right trigger" && binding.Action.Contains("Use item"));
    }

    [Fact]
    public void LoadsRepoControlsAndUpstreamCredit()
    {
        var reference = ControllerReferenceCatalog.Load(FindRepoRoot(), "repo");

        Assert.NotNull(reference);
        Assert.Equal(["quest-touch", "valve-index"], reference.Devices.Select(d => d.Id));
        Assert.Equal("DaXcess", reference.ConversionCredit?.Creator);
        Assert.Equal("RepoXR", reference.ConversionCredit?.Project);
        Assert.Contains(reference.Devices[0].Bindings, binding =>
            binding.Control == "Left stick / click" &&
            binding.Action.Contains("chat", StringComparison.OrdinalIgnoreCase));
        Assert.Contains(reference.Devices[0].Bindings, binding =>
            binding.Control == "Right grip" && binding.Action.Contains("Interact"));
    }

    [Fact]
    public void RepoControllerProfilesCoverTheOfficialDefaultActionSlots()
    {
        var reference = ControllerReferenceCatalog.Load(FindRepoRoot(), "repo");
        var expectedSlots = new[]
        {
            "left_stick",
            "right_stick",
            "left_primary",
            "left_secondary",
            "right_primary",
            "right_secondary",
            "left_trigger",
            "right_trigger",
            "left_grip",
            "right_grip"
        };

        Assert.NotNull(reference);
        foreach (var device in reference.Devices)
        {
            Assert.Equal(expectedSlots.Order(), device.Bindings.Select(binding => binding.Slot).Order());
            Assert.Contains(device.Bindings, binding =>
                binding.Slot == "right_primary" && binding.Action.Contains("Jump"));
            Assert.Contains(device.Bindings, binding =>
                binding.Slot == "left_secondary" && binding.Action.Contains("Reset height"));
            Assert.Contains(device.Bindings, binding =>
                binding.Slot == "right_grip" && binding.Action.Contains("Interact"));
        }
    }

    [Fact]
    public void ControllerDiagramUrisTargetThePackagedAppAssembly()
    {
        var root = FindRepoRoot();
        var project = XDocument.Load(Path.Combine(
            root, "client", "VrClient.App", "VrClient.App.csproj"));
        var assemblyName = project.Descendants("AssemblyName").Single().Value;
        var references = new[]
        {
            ControllerReferenceCatalog.Load(root, "lethal-company"),
            ControllerReferenceCatalog.Load(root, "repo")
        };

        foreach (var reference in references)
        {
            Assert.NotNull(reference);
            foreach (var device in reference.Devices)
            {
                Assert.Equal(assemblyName, device.DiagramAssetUri.Host);
                Assert.True(File.Exists(Path.Combine(
                    root,
                    "client",
                    "VrClient.App",
                    device.DiagramAssetUri.AbsolutePath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar))));
            }
        }
    }

    private static string FindRepoRoot() => TestRepoRoot.Find();
}
