using System.Diagnostics;
using System.Text.Json;
using VrClient.Streaming;

namespace VrClient.Core.Streaming;

public enum SpectatorAvailability { Unavailable, Ambiguous, WaitingForCamera, Ready }
public sealed record SpectatorSelection(SpectatorAvailability Availability,
    SpectatorStatus? Session, string? GameName);

/// Engine-independent discovery/control. Backends publish small JSON heartbeats;
/// no mod assemblies are loaded into the launcher and no game list is hardcoded.
public sealed class SpectatorBridge(string? directory = null)
{
    private static readonly JsonSerializerOptions Json = new() { IncludeFields = true };
    public string DirectoryPath { get; } = directory ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VRClient", "streaming");

    public string StatusPath(SpectatorStatus status) => Path.Combine(DirectoryPath,
        SpectatorProtocol.StatusFileName(status.ProcessId, status.SessionId));
    public string RequestPath(SpectatorStatus status) => Path.Combine(DirectoryPath,
        SpectatorProtocol.RequestFileName(status.ProcessId, status.SessionId));

    public IReadOnlyList<SpectatorStatus> Discover(DateTime? now = null,
        Func<int, string, bool>? processMatches = null)
    {
        try
        {
            if (!Directory.Exists(DirectoryPath)) return [];
            return Directory.EnumerateFiles(DirectoryPath, SpectatorProtocol.StatusPattern)
                .Select(path => ReadStatus(path, now ?? DateTime.UtcNow, processMatches ?? ProcessMatches))
                .OfType<SpectatorStatus>().OrderBy(s => s.GameSlug, StringComparer.Ordinal)
                .ThenBy(s => s.ProcessId).ToArray();
        }
        catch (Exception ex) when (IsReadError(ex)) { return []; }
    }

    private SpectatorStatus? ReadStatus(string path, DateTime now, Func<int, string, bool> processMatches)
    {
        try
        {
            // Heartbeats are deliberately tiny. Partial/malformed/oversized files
            // are skipped individually so one adapter cannot disable the others.
            if (new FileInfo(path).Length > 16384) return null;
            var status = JsonSerializer.Deserialize<SpectatorStatus>(File.ReadAllText(path), Json);
            if (status is null || !SpectatorProtocol.IsValid(status)) return null;
            if (!Path.GetFileName(path).Equals(SpectatorProtocol.StatusFileName(status.ProcessId, status.SessionId), StringComparison.Ordinal))
                return null;
            if (status.UpdatedUtcTicks <= 0 || status.UpdatedUtcTicks > now.Ticks ||
                now.Ticks - status.UpdatedUtcTicks > TimeSpan.FromSeconds(5).Ticks) return null;
            return processMatches(status.ProcessId, status.ProcessName) ? status : null;
        }
        catch (Exception ex) when (IsReadError(ex)) { return null; }
    }

    public SpectatorSelection Select(IReadOnlyDictionary<string, string> catalogue, string? selectedGame = null,
        DateTime? now = null, Func<int, string, bool>? processMatches = null)
    {
        // Titles come from the catalogue, not self-reported adapter labels.
        var sessions = Discover(now, processMatches).Where(s => catalogue.ContainsKey(s.GameSlug) &&
            s.SupportsSteadycam && (selectedGame is null || s.GameSlug == selectedGame)).ToArray();
        var name = selectedGame is not null && catalogue.TryGetValue(selectedGame, out var title) ? title : null;
        if (sessions.Length == 0) return new(SpectatorAvailability.Unavailable, null, name);
        if (sessions.Length > 1) return new(SpectatorAvailability.Ambiguous, null, name);
        var session = sessions[0];
        return new(session.CameraAvailable ? SpectatorAvailability.Ready : SpectatorAvailability.WaitingForCamera,
            session, catalogue[session.GameSlug]);
    }

    private static bool ProcessMatches(int processId, string processName)
    {
        using var process = Process.GetProcessById(processId);
        return !process.HasExited && process.ProcessName.Equals(processName, StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsReadError(Exception ex) => ex is JsonException or IOException or UnauthorizedAccessException
        or ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception;

    public string SetEnabled(SpectatorStatus session, bool enabled, DateTime? now = null,
        Func<int, string, bool>? processMatches = null)
    {
        if (!SpectatorProtocol.IsValid(session)) throw new ArgumentException("Invalid spectator session.");
        var current = ReadStatus(StatusPath(session), now ?? DateTime.UtcNow, processMatches ?? ProcessMatches);
        if (current is null || !current.CameraAvailable || !current.SupportsSteadycam ||
            current.GameSlug != session.GameSlug || current.AdapterId != session.AdapterId)
            throw new InvalidOperationException("The game camera is no longer available. Check the game and try again.");
        var request = SpectatorProtocol.Request(current, enabled);
        AtomicFile.WriteAllText(RequestPath(current), JsonSerializer.Serialize(request, Json));
        return request.RequestId;
    }
}
