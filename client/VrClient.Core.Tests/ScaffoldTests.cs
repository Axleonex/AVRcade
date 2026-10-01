using VrClient.Core;
using VrClient.Core.Modpack;
using Xunit;

public class ScaffoldTests
{
    [Fact]
    public void Sha256_is_lowercase_hex_of_known_input()
    {
        // sha256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
        var hex = Hashing.Sha256OfBytes(System.Text.Encoding.ASCII.GetBytes("abc"));
        Assert.Equal("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
    }

    [Fact]
    public async System.Threading.Tasks.Task Notimplemented_api_throws_marker()
    {
        var ex = await Assert.ThrowsAsync<System.NotImplementedException>(
            () => new NotImplementedThunderstoreApi().GetPackageIndexJsonAsync("repo"));
        Assert.Contains("NOT-IMPLEMENTED", ex.Message);
    }
}
