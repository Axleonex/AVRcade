using System;
using System.IO;
using System.Text.Json;
using VrClient.Core.Model;
using VrClient.Core.Safety;
using Xunit;

public class SafetyGateTests
{
    private static string BasePath(string relative)
        => Path.Combine(AppContext.BaseDirectory, relative);

    [Fact]
    public void Golden_repo_verdict_matches_pinned_fixture()
    {
        using var golden = JsonDocument.Parse(
            File.ReadAllText(BasePath("Fixtures/repo-expected-verdict.json")));
        var expected = golden.RootElement;

        var verdict = new SafetyGate().Evaluate(
            BasePath("config/games/repo.json"),
            BasePath("config/safety/default-rules.json"));

        Assert.Equal("Warn", verdict.Verdict.ToString());
        Assert.Equal(expected.GetProperty("verdict").GetString(), verdict.Verdict.ToString());
        Assert.Equal("private_modded_coop_requires_acknowledgement", verdict.ReasonCode);
        Assert.Equal(expected.GetProperty("reason_code").GetString(), verdict.ReasonCode);
        Assert.Equal("safety.warn.private_modded_coop", verdict.MessageKey);
        Assert.Equal(expected.GetProperty("message_key").GetString(), verdict.MessageKey);
    }

    [Fact]
    public void Unknown_game_is_fail_closed_UnknownBlocked()
    {
        var tempConfig = Path.Combine(Path.GetTempPath(), Guid.NewGuid() + ".json");
        try
        {
            File.WriteAllText(tempConfig,
                "{ \"game_id\":\"totally-unknown-xyz\", \"support_policy\": { \"anti_cheat_risk\":\"unknown\", \"online_risk\":\"unknown\" } }");
            var verdict = new SafetyGate().Evaluate(
                tempConfig, BasePath("config/safety/default-rules.json"));
            Assert.Equal(Verdict.UnknownBlocked, verdict.Verdict);
            Assert.NotEqual(Verdict.Allow, verdict.Verdict);
        }
        finally { File.Delete(tempConfig); }
    }

    [Fact]
    public void Anti_cheat_signal_blocks()
    {
        var tempConfig = Path.Combine(Path.GetTempPath(), Guid.NewGuid() + ".json");
        try
        {
            File.WriteAllText(tempConfig,
                "{ \"game_id\":\"repo\", \"support_policy\": { \"anti_cheat_risk\":\"EasyAntiCheat\", \"online_risk\":\"private_modded_coop\" } }");
            var verdict = new SafetyGate().Evaluate(
                tempConfig, BasePath("config/safety/default-rules.json"));
            Assert.Equal(Verdict.Block, verdict.Verdict);
            Assert.Equal("anti_cheat_present", verdict.ReasonCode);
        }
        finally { File.Delete(tempConfig); }
    }
}
