namespace VrClient.Core.Tests;

public sealed class AtomicSettingsTests
{
    [Fact]
    public async Task ConcurrentApplicationCopiesDoNotShareATemporarySettingsFile()
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-atomic-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var path = Path.Combine(root, "settings.json");
            var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var candidates = Enumerable.Range(0, 24).Select(i => "{\"value\":\"" + new string((char)('A' + i), 65536) + "\"}").ToArray();
            var writers = candidates.Select(text => Task.Run(async () => { await gate.Task; AtomicFile.WriteAllText(path, text); })).ToArray();
            gate.SetResult();
            await Task.WhenAll(writers);
            Assert.Contains(File.ReadAllText(path), candidates);
            Assert.Empty(Directory.GetFiles(root, "*.tmp"));
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void FailedSettingsReplacementCleansOnlyItsOwnTemporaryFile()
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-atomic-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var directory = Path.Combine(root, "directory-target"); Directory.CreateDirectory(directory);
            var error = Record.Exception(() => AtomicFile.WriteAllText(directory, "fixture"));
            Assert.True(error is IOException or UnauthorizedAccessException, error?.ToString());
            Assert.Empty(Directory.GetFiles(root, "*.tmp"));
            Assert.True(Directory.Exists(directory));
        }
        finally { Directory.Delete(root, true); }
    }
}
