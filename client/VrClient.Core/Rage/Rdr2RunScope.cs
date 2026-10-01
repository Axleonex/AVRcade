namespace VrClient.Core.Rage;

/// Title/run-scoped names used by RDR2-owned profile, diagnostics, lock, and IPC paths.
/// This deliberately contains no process-global converter lock or XR eviction operation.
public sealed record Rdr2RunScope(string StateRoot, string RunId)
{
    public const string Title = "red-dead-redemption-2";
    public string ProfilesRoot => Path.Combine(StateRoot, "profiles");
    public string EvidenceRoot => Path.Combine(StateRoot, "evidence", RunId);
    public string StagingRoot => Path.Combine(StateRoot, "staging", RunId);
    public string TransactionRoot => Path.Combine(StateRoot, "transactions", RunId);
    public string LockName => $"vrclient:{Title}:{RunId}";
    public string IpcEndpoint => $"vrclient.{Title}.{RunId}";
    public string XrFocusState(bool runtimeCanGrantFocus) => runtimeCanGrantFocus ? "xr_focus_granted" : "waiting_for_xr_focus";
}
