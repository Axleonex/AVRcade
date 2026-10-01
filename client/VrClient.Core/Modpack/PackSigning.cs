namespace VrClient.Core.Modpack;
using System.Diagnostics;
using System.Text.RegularExpressions;

/// Verifies a signature: true = trusted, false = refused, null = verifier unavailable.
public interface IPackVerifier
{
    bool? Verify(string manifestPath, string zipPath);
}

/// Fail-closed signature gate for client-produced packs (M5). A pack accompanied by a
/// sibling "<zip>.manifest.json" is a client-produced pack and MUST verify before install;
/// a pack with no manifest is a Thunderstore community mod (M1 trust-on-first-use), allowed.
public static class PackSigning
{
    public static (bool Allowed, string Reason) CheckBeforeInstall(string zipPath, IPackVerifier verifier)
    {
        var manifestPath = zipPath + ".manifest.json";
        if (!File.Exists(manifestPath))
            return (true, "unsigned_community_mod_allowed");

        return verifier.Verify(manifestPath, zipPath) switch
        {
            true => (true, "pack_signature_ok"),
            false => (false, "pack_signature_invalid"),
            null => (false, "pack_signature_unverifiable"), // fail-closed (M5D8)
        };
    }
}

/// Shells to the native vrclient_sign_cli --verify (env-deferred where no native build
/// exists). Returns null when the CLI is absent → PackSigning fails closed for a
/// manifested pack.
public sealed class NativeCliPackVerifier : IPackVerifier
{
    private readonly string _trustRootPath;
    private readonly string _revocationsPath;

    public NativeCliPackVerifier(string trustRootPath, string revocationsPath)
    {
        _trustRootPath = trustRootPath;
        _revocationsPath = revocationsPath;
    }

    public static string? LocateCli()
        => VrClient.Core.NativeCliLocator.Locate("vrclient_sign_cli.exe", "VRCLIENT_SIGN_CLI");

    public bool? Verify(string manifestPath, string zipPath)
    {
        var cli = LocateCli();
        if (cli is null)
            return null; // unavailable → fail-closed upstream

        var psi = new ProcessStartInfo
        {
            FileName = cli,
            RedirectStandardOutput = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        psi.ArgumentList.Add("--verify");
        psi.ArgumentList.Add("--manifest"); psi.ArgumentList.Add(manifestPath);
        psi.ArgumentList.Add("--trust-root"); psi.ArgumentList.Add(_trustRootPath);
        psi.ArgumentList.Add("--revocations"); psi.ArgumentList.Add(_revocationsPath);
        psi.ArgumentList.Add("--base-dir"); psi.ArgumentList.Add(Path.GetDirectoryName(zipPath) ?? ".");

        try
        {
            using var process = Process.Start(psi);
            if (process is null)
                return null;
            var stdout = process.StandardOutput.ReadToEnd();
            process.WaitForExit(10_000);
            var m = Regex.Match(stdout, @"\bok=(true|false)\b");
            return m.Success ? m.Groups[1].Value == "true" : (bool?)null;
        }
        catch
        {
            return null;
        }
    }
}
