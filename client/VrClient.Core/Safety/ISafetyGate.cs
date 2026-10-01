namespace VrClient.Core.Safety;
using VrClient.Core.Model;

public interface ISafetyGate
{
    /// Evaluate whether the game may be modified/launched, reading the existing
    /// safety rules + game fingerprint config. Never throws for a Block; returns
    /// a SafetyVerdict. Throws only on missing/malformed config files.
    SafetyVerdict Evaluate(string gameConfigPath, string safetyRulesPath);
}

public sealed class NotImplementedSafetyGate : ISafetyGate
{
    public SafetyVerdict Evaluate(string gameConfigPath, string safetyRulesPath)
        => throw new NotImplementedException("NOT-IMPLEMENTED: real gate arrives in Phase 4 Task 4.2");
}
