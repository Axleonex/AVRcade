using System.Security.Cryptography;

namespace VrClient.Core;

/// Single source of truth for artifact hashing. Lowercase hex, no separators.
public static class Hashing
{
    public static string Sha256OfFile(string path)
    {
        using var stream = File.OpenRead(path);
        return ToHex(SHA256.HashData(stream));
    }

    public static string Sha256OfBytes(ReadOnlySpan<byte> bytes)
        => ToHex(SHA256.HashData(bytes));

    public static string Sha512OfBytes(ReadOnlySpan<byte> bytes)
        => ToHex(SHA512.HashData(bytes));

    private static string ToHex(byte[] hash)
        => Convert.ToHexString(hash).ToLowerInvariant();
}
