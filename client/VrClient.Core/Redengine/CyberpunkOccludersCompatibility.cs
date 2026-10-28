using System.Text;
using System.Text.Json;
using VrClient.Core.Modpack;

namespace VrClient.Core.Redengine;

/// Local compatibility transform for the user's separately acquired Scripted v0.3.
/// Game 2.5 removed the instanced-node fields. Keep ordinary mesh occluders and
/// leave instanced nodes to the engine instead of guessing new native layouts.
public static class CyberpunkOccludersCompatibility
{
    public const string PatchId = "more-occluders-scripted-0.3-game-2.5-v1";
    public const string OriginalSha256 = "212806440079E92A3090109D0CC0FCA730E9E0C0A281C5DAF0ECBF6DA0254890";

    public static bool IsCompatible(string gameRoot, string relative) =>
        CyberpunkUpstreamSession.ReadState(gameRoot)?.Version != "0.1.9" ||
        !File.ReadAllText(CyberpunkUpstreamSession.PayloadPath(gameRoot, relative))
            .Contains("instMesh.occluderType", StringComparison.Ordinal);

    public static string RewriteInstancedBranch(string source)
    {
        var normalized = source.Replace("\r\n", "\n");
        const string branch = "\t\t\telse\n\t\t\t{\n\t\t\t\tlet instMesh = node as worldInstancedMeshNode;\n\t\t\t\tif(IsDefined(instMesh))\n\t\t\t\t{\n\t\t\t\t\tif(Equals(instMesh.occluderType, visWorldOccluderType.None) && !Equals(instMesh.meshAppearance,  n\"transparent\"))\n\t\t\t\t\t{\n\t\t\t\t\t\tinstMesh.occluderAutohideDistanceScale = this.distanceScale;\n\t\t\t\t\t\tinstMesh.occluderType = visWorldOccluderType.Default;\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n";
        if (normalized.Split(branch).Length != 2)
            throw new InvalidDataException("More Occluders has an unknown instanced-node implementation; it was preserved.");
        return normalized.Replace(branch,
            "\t\t\t// AVRcade 2.5 compatibility: instanced meshes use engine occlusion.\n");
    }

    public static void Apply(string gameRoot)
    {
        if (CyberpunkUpstreamSession.ReadState(gameRoot)?.Version != "0.1.9") return;
        var receiptPath = CyberpunkNexusRequirements.ReceiptPath(gameRoot);
        if (!File.Exists(receiptPath)) return;
        var receiptText = File.ReadAllText(receiptPath);
        var receipt = JsonSerializer.Deserialize<CyberpunkNexusRequirements.Receipt>(receiptText);
        if (receipt is null) return;
        foreach (var (relative, recordedHash) in receipt.Files.ToArray())
        {
            var payload = CyberpunkUpstreamSession.PayloadPath(gameRoot, relative);
            if (!File.Exists(payload)) continue;
            var original = File.ReadAllBytes(payload);
            var hash = Hashing.Sha256OfBytes(original);
            if (!hash.Equals(OriginalSha256, StringComparison.OrdinalIgnoreCase))
            {
                if (Encoding.UTF8.GetString(original).Contains("instMesh.occluderType", StringComparison.Ordinal))
                    throw new IOException("More Occluders references removed game 2.5 fields but has an unknown hash. The custom script was preserved.");
                continue;
            }
            if (!hash.Equals(recordedHash, StringComparison.OrdinalIgnoreCase))
                throw new IOException("More Occluders changed after import; its compatibility update was refused.");
            var patched = Encoding.UTF8.GetBytes(RewriteInstancedBranch(Encoding.UTF8.GetString(original)));
            var folder = Path.Combine(CyberpunkUpstreamSession.Store(gameRoot), "compatibility", PatchId);
            Directory.CreateDirectory(folder);
            var backup = Path.Combine(folder, "original.reds");
            if (!File.Exists(backup)) File.WriteAllBytes(backup, original);
            if (!File.Exists(Path.Combine(folder, "original-receipt.json")))
                File.WriteAllText(Path.Combine(folder, "original-receipt.json"), receiptText);
            try
            {
                File.WriteAllBytes(payload, patched);
                receipt.Files[relative] = Hashing.Sha256OfBytes(patched);
                AtomicFile.WriteAllText(receiptPath, JsonSerializer.Serialize(receipt));
                AtomicFile.WriteAllText(Path.Combine(folder, "patch.json"), JsonSerializer.Serialize(new
                {
                    patch = PatchId, source = receipt.Source, original_sha256 = hash,
                    patched_sha256 = receipt.Files[relative],
                    behavior = "Ordinary mesh occlusion preserved; unsupported instanced-mesh branch skipped on game 2.5."
                }));
            }
            catch
            {
                File.WriteAllBytes(payload, original);
                AtomicFile.WriteAllText(receiptPath, receiptText);
                throw;
            }
        }
    }
}
