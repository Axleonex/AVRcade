using VrClient.Core.Model;

namespace VrClient.Core.Modpack;

/// Abstraction over the Thunderstore package index so the resolver is testable
/// without network. Implementations: LiveThunderstoreApi (HTTP) and a fixture in tests.
public interface IThunderstoreApi
{
    /// Return the community package index JSON (Thunderstore /c/<community>/api/v1/package/).
    Task<string> GetPackageIndexJsonAsync(string community, CancellationToken ct = default);
}

/// Deliberate placeholder so Phase 1 compiles; Phase 2 replaces the resolver, not this.
public sealed class NotImplementedThunderstoreApi : IThunderstoreApi
{
    public Task<string> GetPackageIndexJsonAsync(string community, CancellationToken ct = default)
        => throw new NotImplementedException("NOT-IMPLEMENTED: live Thunderstore API arrives in Phase 2 Task 2.3");
}
