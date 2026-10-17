using System;

namespace VrClient.Streaming;

// Source-linked by game adapters: no engine, launcher, JSON or third-party DLL
// dependency. Public fields work with Unity JsonUtility and standard JSON.
[Serializable]
public sealed class SpectatorStatus
{
    public int ProtocolVersion;
    public string GameSlug = "";
    public string AdapterId = "";
    public int ProcessId;
    public string ProcessName = "";
    public string SessionId = "";
    public long UpdatedUtcTicks;
    public bool SupportsSteadycam;
    public bool CameraAvailable;
    public bool Enabled;
    public string RequestId = "";
    public string CaptureHint = "";
    public string? Error;
}

[Serializable]
public sealed class SpectatorRequest
{
    public int ProtocolVersion;
    public string GameSlug = "";
    public string AdapterId = "";
    public int ProcessId;
    public string SessionId = "";
    public bool Enabled;
    public string RequestId = "";
}

public static class SpectatorProtocol
{
    public const int Version = 1;
    public const string StatusPattern = "spectator-*.status.json";

    public static string SessionKey(int processId, string sessionId)
    {
        if (processId <= 0 || !Guid.TryParseExact(sessionId, "N", out _))
            throw new ArgumentException("Invalid spectator process/session identity.");
        return processId + "-" + sessionId;
    }

    public static string StatusFileName(int processId, string sessionId)
        => "spectator-" + SessionKey(processId, sessionId) + ".status.json";
    public static string RequestFileName(int processId, string sessionId)
        => "spectator-" + SessionKey(processId, sessionId) + ".request.json";

    public static bool IsValid(SpectatorStatus status)
        => status.ProtocolVersion == Version && status.ProcessId > 0 &&
           Guid.TryParseExact(status.SessionId, "N", out _) &&
           !string.IsNullOrWhiteSpace(status.GameSlug) && status.GameSlug.Length <= 80 &&
           !string.IsNullOrWhiteSpace(status.AdapterId) && status.AdapterId.Length <= 120 &&
           !string.IsNullOrWhiteSpace(status.ProcessName) && status.ProcessName.Length <= 260 &&
           status.ProcessName.IndexOfAny(new[] { '/', '\\' }) < 0 &&
           status.CaptureHint != null && status.CaptureHint.Length <= 512;

    public static bool IsForSession(SpectatorRequest request, SpectatorStatus session)
        => IsValid(session) && request.ProtocolVersion == Version &&
           request.ProcessId == session.ProcessId && request.SessionId == session.SessionId &&
           request.GameSlug == session.GameSlug && request.AdapterId == session.AdapterId &&
           Guid.TryParseExact(request.RequestId, "N", out _);

    public static SpectatorRequest Request(SpectatorStatus session, bool enabled)
    {
        if (!IsValid(session)) throw new ArgumentException("Invalid spectator session.");
        return new SpectatorRequest
        {
            ProtocolVersion = Version, GameSlug = session.GameSlug, AdapterId = session.AdapterId,
            ProcessId = session.ProcessId, SessionId = session.SessionId,
            Enabled = enabled, RequestId = Guid.NewGuid().ToString("N")
        };
    }
}
