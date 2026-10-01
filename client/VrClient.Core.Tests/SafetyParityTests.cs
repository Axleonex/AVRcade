using System;
using System.IO;
using VrClient.Core.Model;
using VrClient.Core.Safety;
using Xunit;

public class SafetyParityTests
{
    private static string RepoGameConfig => Path.Combine(AppContext.BaseDirectory, "config", "games", "repo.json");
    private static string RepoRules => Path.Combine(AppContext.BaseDirectory, "config", "safety", "default-rules.json");
    private static string DemoGameConfig => Path.Combine(AppContext.BaseDirectory, "Fixtures", "demo-coop", "config", "games", "demo-coop.json");
    private static string DemoRules => Path.Combine(AppContext.BaseDirectory, "Fixtures", "demo-coop", "config", "safety", "default-rules.json");

    [Fact]
    public void Repo_dotnet_verdict_class_matches_the_equivalence_map()
    {
        var v = new SafetyGate().Evaluate(RepoGameConfig, RepoRules);
        Assert.Equal("private_modded_coop_requires_acknowledgement", v.ReasonCode);
        var mapped = SafetyReasonMap.ClassFor(v.ReasonCode);
        Assert.NotNull(mapped);              // the reason is covered by the map (no gap)
        Assert.Equal(mapped, v.Verdict);     // .NET class == the map's canonical class
        Assert.Equal(Verdict.Warn, v.Verdict);
    }

    [Fact]
    public void DemoCoop_no_rule_is_fail_closed_and_mapped()
    {
        var v = new SafetyGate().Evaluate(DemoGameConfig, DemoRules);
        Assert.Equal("no_safety_rule", v.ReasonCode);
        var mapped = SafetyReasonMap.ClassFor(v.ReasonCode);
        Assert.NotNull(mapped);
        Assert.Equal(mapped, v.Verdict);
        Assert.Equal(Verdict.UnknownBlocked, v.Verdict);
    }

    [Fact]
    public void Every_dotnet_reason_the_gate_can_emit_is_in_the_map()
    {
        // Coverage guard: the five reason codes SafetyGate can return must all map.
        foreach (var reason in new[] {
            "no_safety_rule","anti_cheat_present","offline_only_ok",
            "private_modded_coop_requires_acknowledgement","online_scope_unsafe" })
            Assert.NotNull(SafetyReasonMap.ClassFor(reason));
    }
}
