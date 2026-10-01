using System.Text.Json;
using VrClient.Core.Model;

namespace VrClient.Core.Modpack;

/// Resolves a declared modpack spec against the Thunderstore community package
/// index into a hash-pinnable lockfile (dependency-first, exact versions).
public sealed class ModpackResolver(IThunderstoreApi api)
{
    public static ModpackSpec LoadSpec(string modpackJsonPath)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(modpackJsonPath));
        var root = doc.RootElement;
        var mods = new List<ModDependency>();
        foreach (var m in root.GetProperty("mods").EnumerateArray())
            mods.Add(new ModDependency(
                m.GetProperty("namespace").GetString()!,
                m.GetProperty("name").GetString()!));
        return new ModpackSpec(
            root.GetProperty("game_slug").GetString()!,
            root.GetProperty("steam_app_id").GetString()!,
            root.GetProperty("community").GetString()!,
            mods);
    }

    public async Task<Lockfile> ResolveAsync(ModpackSpec spec, string resolvedAtUtc, CancellationToken ct = default)
    {
        var json = await api.GetPackageIndexJsonAsync(spec.Community, ct);
        using var doc = JsonDocument.Parse(json);
        var index = new List<JsonElement>();
        foreach (var pkg in doc.RootElement.EnumerateArray())
            index.Add(pkg);

        var packages = new List<LockedPackage>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var mod in spec.Mods)
            AddPackage(index, $"{mod.Namespace}-{mod.Name}", requiredVersion: null, packages, seen);

        return new Lockfile(spec.GameSlug, spec.Community, resolvedAtUtc,
            OrderDependenciesFirst(index, packages));
    }

    private static List<LockedPackage> OrderDependenciesFirst(
        List<JsonElement> index, List<LockedPackage> packages)
    {
        var byName = packages.ToDictionary(p => $"{p.Namespace}-{p.Name}", StringComparer.Ordinal);
        var ordered = new List<LockedPackage>();
        var visiting = new HashSet<string>(StringComparer.Ordinal);
        var visited = new HashSet<string>(StringComparer.Ordinal);

        void Visit(string name)
        {
            if (visited.Contains(name)) return;
            if (!visiting.Add(name))
                throw new InvalidOperationException($"dependency cycle at {name}");
            var selected = byName[name];
            var pkg = index.First(p => p.GetProperty("full_name").GetString() == name);
            var version = pkg.GetProperty("versions").EnumerateArray()
                .First(v => v.GetProperty("version_number").GetString() == selected.Version);
            foreach (var dep in version.GetProperty("dependencies").EnumerateArray())
            {
                var dependency = dep.GetString()!;
                var matched = byName.Keys.FirstOrDefault(n => dependency.StartsWith(n + "-", StringComparison.Ordinal));
                if (matched is not null)
                    Visit(matched);
            }
            visiting.Remove(name);
            visited.Add(name);
            ordered.Add(selected);
        }

        foreach (var package in packages)
            Visit($"{package.Namespace}-{package.Name}");
        return ordered;
    }

    private static void AddPackage(
        List<JsonElement> index, string fullName, string? requiredVersion,
        List<LockedPackage> packages, HashSet<string> seen)
    {
        if (!seen.Add(fullName))
        {
            if (requiredVersion is null)
                return;
            var installed = packages.FindIndex(p => $"{p.Namespace}-{p.Name}" == fullName);
            if (installed < 0)
                return; // dependency cycle still being traversed
            var selected = packages[installed].Version;
            if (selected == requiredVersion)
                return;
            if (!Version.TryParse(selected, out var selectedVersion) ||
                !Version.TryParse(requiredVersion, out var requestedVersion))
                throw new InvalidOperationException(
                    $"conflicting nonnumeric dependency versions for {fullName}: {selected} and {requiredVersion}");
            if (requestedVersion <= selectedVersion)
                return;
            // A newer transitive minimum can appear after an older one. Replace
            // the older package and traverse the newer version's dependencies.
            packages.RemoveAt(installed);
            seen.Remove(fullName);
            seen.Add(fullName);
        }

        JsonElement pkg = default;
        foreach (var candidate in index)
            if (candidate.GetProperty("full_name").GetString() == fullName)
            {
                pkg = candidate;
                break;
            }
        if (pkg.ValueKind == JsonValueKind.Undefined)
            throw new InvalidOperationException($"package not found in Thunderstore index: {fullName}");

        JsonElement version = default;
        if (requiredVersion is null)
        {
            // Thunderstore returns versions newest-first; index 0 is the latest.
            version = pkg.GetProperty("versions")[0];
        }
        else
        {
            foreach (var v in pkg.GetProperty("versions").EnumerateArray())
                if (v.GetProperty("version_number").GetString() == requiredVersion)
                {
                    version = v;
                    break;
                }
            if (version.ValueKind == JsonValueKind.Undefined)
                throw new InvalidOperationException($"version {requiredVersion} of {fullName} not found in Thunderstore index");
        }

        // Dependencies first. Match each "Owner-Name-Version" string against the
        // index by full_name prefix (avoids hyphen-splitting ambiguity).
        foreach (var depEl in version.GetProperty("dependencies").EnumerateArray())
        {
            var dep = depEl.GetString()!;
            string? depFullName = null;
            string? depVersion = null;
            foreach (var candidate in index)
            {
                var candidateFullName = candidate.GetProperty("full_name").GetString()!;
                if (dep.StartsWith(candidateFullName + "-", StringComparison.Ordinal))
                {
                    depFullName = candidateFullName;
                    depVersion = dep.Substring(candidateFullName.Length + 1);
                    break;
                }
            }
            if (depFullName is null)
            {
                // Some PEAK libraries declare the generic Mono BepInEx pack
                // even when the game-specific BepInExPack_PEAK loader is
                // already required by the VR mod. The generic pack is not
                // published in that community and must not replace its loader.
                if (dep.StartsWith("BepInEx-BepInExPack-", StringComparison.Ordinal) &&
                    seen.Any(name => name.StartsWith("BepInEx-BepInExPack_", StringComparison.Ordinal)))
                    continue;
                throw new InvalidOperationException($"dependency not found in Thunderstore index: {dep}");
            }
            AddPackage(index, depFullName, depVersion, packages, seen);
        }

        // sha256 stays empty at resolve time; the CLI `resolve` verb downloads once
        // to fill+verify it before the lockfile is written (Task 2.5 guard).
        packages.Add(new LockedPackage(
            pkg.GetProperty("owner").GetString()!,
            pkg.GetProperty("name").GetString()!,
            version.GetProperty("version_number").GetString()!,
            version.GetProperty("download_url").GetString()!,
            "",
            version.GetProperty("file_size").GetInt64()));
    }
}
