namespace VrClient.Core.Modpack;
using VrClient.Core.Model;

public interface IHttpDownloader
{
    Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default);
}

public sealed class HashMismatchException(string message) : Exception(message);

public sealed class PackageDownloader(IHttpDownloader http)
{
    /// Download every package to <cacheDir>/<ns>-<name>-<ver>.zip, verifying sha256
    /// against the lockfile BEFORE writing the file to disk. On ANY mismatch: throw
    /// HashMismatchException with the package id, expected, and actual — and delete
    /// any partial file. Returns the list of verified local zip paths in lockfile order.
    public async Task<IReadOnlyList<string>> DownloadAllAsync(Lockfile lockfile, string cacheDir, CancellationToken ct = default)
    {
        LockfileIo.RequireHashesFilled(lockfile);
        Directory.CreateDirectory(cacheDir);
        var verifiedPaths = new List<string>();
        foreach (var package in lockfile.Packages)
        {
            var bytes = await http.GetBytesAsync(package.DownloadUrl, ct);
            var actual = Hashing.Sha256OfBytes(bytes);
            if (!string.Equals(actual, package.Sha256, StringComparison.Ordinal))
                throw new HashMismatchException(
                    $"sha256 mismatch for {package.Namespace}-{package.Name}-{package.Version}: expected={package.Sha256} actual={actual}");
            var zipPath = Path.Combine(cacheDir, $"{package.Namespace}-{package.Name}-{package.Version}.zip");
            File.WriteAllBytes(zipPath, bytes);
            verifiedPaths.Add(zipPath);
        }
        return verifiedPaths;
    }
}
