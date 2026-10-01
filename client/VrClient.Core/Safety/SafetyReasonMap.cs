namespace VrClient.Core.Safety;
using VrClient.Core.Model;

/// The one conceptual safety model across two implementations. Each .NET reason
/// code maps to (a) the equivalent native vr_safety reason code and (b) the
/// canonical verdict class. Parity is asserted at the verdict-class level (M5D3):
/// the two engines use disjoint reason strings BY DESIGN (M1 D9), so string
/// equality is never asserted — class equivalence via this map is the invariant.
public static class SafetyReasonMap
{
    public sealed record Equivalence(string DotNetReason, string NativeReason, Verdict Class);

    private static readonly Equivalence[] Table =
    [
        new("no_safety_rule",                              "no_rule_for_game",          Verdict.UnknownBlocked),
        new("anti_cheat_present",                          "anti_cheat_detected",       Verdict.Block),
        new("offline_only_ok",                             "approved",                  Verdict.Allow),
        new("private_modded_coop_requires_acknowledgement","acknowledgement_required",  Verdict.Warn),
        new("online_scope_unsafe",                         "launch_mode_not_permitted", Verdict.Block),
    ];

    public static Equivalence? ForDotNetReason(string dotNetReason)
    {
        foreach (var e in Table)
            if (e.DotNetReason == dotNetReason) return e;
        return null;
    }

    /// The canonical verdict class the map assigns to a .NET reason code.
    /// Null when the reason is not in the map (a coverage gap the test catches).
    public static Verdict? ClassFor(string dotNetReason) => ForDotNetReason(dotNetReason)?.Class;
}
