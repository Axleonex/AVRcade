using VrClient.Core.Launch;
using Xunit;

public class SteamLaunchOptionsTests
{
    private const string Wrapper = @"C:\vrclient\vrclient.exe";
    private const string Prefix = "\"" + Wrapper + "\" wrap --";

    private const string Fixture = """
        "UserLocalConfigStore"
        {
        	"Software"
        	{
        		"Valve"
        		{
        			"Steam"
        			{
        				"apps"
        				{
        					"3241660"
        					{
        						"LaunchOptions"		"-novid"
        						"Playtime"		"120"
        					}
        					"250820"
        					{
        						"Playtime"		"999"
        					}
        				}
        			}
        		}
        	}
        	"friends"
        	{
        		"note"		"untouched \"quoted\" value"
        	}
        }
        """;

    // ---- Compose / Strip ----

    [Fact]
    public void Compose_empty_wraps_bare_command()
        => Assert.Equal($"{Prefix} %command%", SteamLaunchOptions.Compose("", Wrapper));

    [Fact]
    public void Compose_existing_wrapper_chain_is_wrapped_whole()
        => Assert.Equal($"{Prefix} mangohud %command%",
            SteamLaunchOptions.Compose("mangohud %command%", Wrapper));

    [Fact]
    public void Compose_plain_game_args_go_after_command()
        => Assert.Equal($"{Prefix} %command% -novid",
            SteamLaunchOptions.Compose("-novid", Wrapper));

    [Fact]
    public void Compose_is_idempotent()
    {
        var once = SteamLaunchOptions.Compose("-novid", Wrapper);
        Assert.Equal(once, SteamLaunchOptions.Compose(once, Wrapper));
    }

    [Fact]
    public void Strip_restores_inner_command_and_empties_bare_wrap()
    {
        Assert.Equal("mangohud %command%",
            SteamLaunchOptions.Strip($"{Prefix} mangohud %command%", Wrapper));
        Assert.Equal("", SteamLaunchOptions.Strip($"{Prefix} %command%", Wrapper));
        Assert.Equal("-novid", SteamLaunchOptions.Strip("-novid", Wrapper)); // not ours: unchanged
    }

    // ---- per-game guard: only real numeric app ids may reach Steam ----

    [Theory]
    [InlineData("3241660", true)]
    [InlineData("250820", true)]
    [InlineData("", false)]
    [InlineData("<APP_ID>", false)]
    [InlineData("3241660x", false)]
    [InlineData("repo", false)]
    public void Installer_accepts_only_numeric_app_ids(string appId, bool valid)
        => Assert.Equal(valid, SteamLaunchOptionInstaller.IsValidAppId(appId));

    [Fact]
    public void Installer_refuses_placeholder_app_id_without_touching_anything()
    {
        var result = SteamLaunchOptionInstaller.Install(
            "<APP_ID>", Wrapper, remove: false, dryRun: false, log: _ => { });
        Assert.False(result.Ok);
        Assert.Contains("not numeric", result.Message);
    }

    // ---- localconfig.vdf editing ----

    [Fact]
    public void Get_reads_existing_value_and_null_when_absent()
    {
        Assert.Equal("-novid", SteamLaunchOptions.GetLaunchOptions(Fixture, "3241660"));
        Assert.Null(SteamLaunchOptions.GetLaunchOptions(Fixture, "250820"));
        Assert.Null(SteamLaunchOptions.GetLaunchOptions(Fixture, "999999"));
    }

    [Fact]
    public void Set_replaces_existing_value_and_preserves_everything_else()
    {
        var line = SteamLaunchOptions.Compose("", Wrapper);
        var updated = SteamLaunchOptions.SetLaunchOptions(Fixture, "3241660", line);
        Assert.Equal(line, SteamLaunchOptions.GetLaunchOptions(updated, "3241660"));
        Assert.Contains("\"Playtime\"\t\t\"120\"", updated.Replace("\r\n", "\n"));
        Assert.Contains("untouched \\\"quoted\\\" value", updated);
        // quotes in the wrapper path are VDF-escaped on disk
        Assert.Contains("\\\"" + Wrapper.Replace(@"\", @"\\") + "\\\"", updated);
    }

    [Fact]
    public void Set_inserts_key_into_app_block_missing_it()
    {
        var updated = SteamLaunchOptions.SetLaunchOptions(Fixture, "250820", "%command%");
        Assert.Equal("%command%", SteamLaunchOptions.GetLaunchOptions(updated, "250820"));
        Assert.Equal("-novid", SteamLaunchOptions.GetLaunchOptions(updated, "3241660"));
    }

    [Fact]
    public void Set_creates_app_block_when_absent()
    {
        var updated = SteamLaunchOptions.SetLaunchOptions(Fixture, "999999", "%command% -x");
        Assert.Equal("%command% -x", SteamLaunchOptions.GetLaunchOptions(updated, "999999"));
        Assert.Equal("-novid", SteamLaunchOptions.GetLaunchOptions(updated, "3241660"));
    }

    [Fact]
    public void Set_roundtrips_a_realistic_wrapper_line_through_escaping()
    {
        var line = SteamLaunchOptions.Compose("", Wrapper);
        var updated = SteamLaunchOptions.SetLaunchOptions(Fixture, "3241660", line);
        var again = SteamLaunchOptions.SetLaunchOptions(updated, "3241660", line);
        Assert.Equal(line, SteamLaunchOptions.GetLaunchOptions(again, "3241660"));
    }

    [Fact]
    public void Set_throws_on_a_file_that_is_not_localconfig()
    {
        Assert.Throws<InvalidOperationException>(() =>
            SteamLaunchOptions.SetLaunchOptions("\"nope\" { }", "1", "x"));
    }
}
