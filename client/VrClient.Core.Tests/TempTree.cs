namespace VrClient.Core.Tests;

/// A throwaway folder tree for tests that need real files (game folders, manager
/// profiles, a stand-in Steam). Removed when the test class is disposed.
public abstract class TempTree : IDisposable
{
    protected string Root { get; } = Path.Combine(Path.GetTempPath(), $"avrcade-test-{Guid.NewGuid():N}");

    protected TempTree() => Directory.CreateDirectory(Root);

    public void Dispose()
    {
        Directory.Delete(Root, recursive: true);
        GC.SuppressFinalize(this);
    }

    /// Create a small file at Root/segments..., with its folders, and return its path.
    protected string Touch(params string[] segments)
    {
        var path = Path.Combine([Root, .. segments]);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, "fixture");
        return path;
    }
}
