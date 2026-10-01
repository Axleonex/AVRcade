using VrClient.Core.Modpack;
using Xunit;

public class NexusApiTests
{
    [Fact]
    public void ParseUser_reads_name_and_premium_flag()
    {
        var u = NexusApi.ParseUser("""{"user_id": 12345, "key": "x", "name": "axl", "is_premium": false, "is_supporter": false}""");
        Assert.Equal("axl", u.Name);
        Assert.Equal(12345, u.UserId);
        Assert.False(u.IsPremium);
    }

    [Fact]
    public void ParseGame_reads_identity()
    {
        var g = NexusApi.ParseGame("""{"id": 1151, "name": "Fallout 4", "domain_name": "fallout4", "forum_url": "x"}""");
        Assert.Equal(1151, g.Id);
        Assert.Equal("fallout4", g.DomainName);
    }

    [Fact]
    public void ParseFiles_reads_file_list_and_tolerates_missing_md5()
    {
        var files = NexusApi.ParseFiles("""
            {"files": [
              {"file_id": 999, "file_name": "CoolMod-1.0.zip", "version": "1.0", "size_kb": 2048},
              {"file_id": 1000, "file_name": "CoolMod-1.1.zip", "version": "1.1", "size_kb": 3000, "md5": "abc123"}
            ], "file_updates": []}
            """);
        Assert.Equal(2, files.Count);
        Assert.Null(files[0].Md5);
        Assert.Equal("abc123", files[1].Md5);
        Assert.Equal(999, files[0].FileId);
    }

    [Fact]
    public void ParseFiles_empty_or_missing_array_yields_empty_list()
    {
        Assert.Empty(NexusApi.ParseFiles("""{"file_updates": []}"""));
        Assert.Empty(NexusApi.ParseFiles("""{"files": []}"""));
    }
}
