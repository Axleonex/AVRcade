namespace VrClient.Core.Unreal;

public readonly record struct UevrConfigSnapshot(
    long Length, DateTime LastWriteUtc, bool HasRuntimeKey);

public static class UevrAttachEvidence
{
    public static bool IsFreshHook(UevrConfigSnapshot before, UevrConfigSnapshot after)
        => after.Length > before.Length + 50 ||
           (after.HasRuntimeKey &&
            (!before.HasRuntimeKey || after.LastWriteUtc > before.LastWriteUtc));
}
