using System.Text.Json;
using VrClient.Core.Streaming;
using VrClient.Streaming;

namespace VrClient.Core.Tests;

public sealed class SpectatorBridgeTests : IDisposable
{
    private static readonly JsonSerializerOptions Json = new() { IncludeFields = true };
    private readonly string _directory = Path.Combine(Path.GetTempPath(), "avrcade-spectators-" + Guid.NewGuid().ToString("N"));
    private readonly DateTime _now = DateTime.UtcNow;
    private readonly SpectatorBridge _bridge;
    private static bool Running(int pid, string name)
        => pid == 123 && name == "first-game" || pid == 456 && name == "second-game";

    public SpectatorBridgeTests()
    {
        Directory.CreateDirectory(_directory);
        _bridge = new(_directory);
    }

    private SpectatorStatus Session(string slug = "future-game", int pid = 123, string processName = "first-game") => new()
    {
        ProtocolVersion = SpectatorProtocol.Version, GameSlug = slug, AdapterId = "test.backend",
        ProcessId = pid, ProcessName = processName, SessionId = Guid.NewGuid().ToString("N"),
        UpdatedUtcTicks = _now.Ticks, SupportsSteadycam = true, CameraAvailable = true,
        CaptureHint = "Capture this game's spectator window."
    };
    private void Publish(SpectatorStatus session) => File.WriteAllText(_bridge.StatusPath(session), JsonSerializer.Serialize(session, Json));

    [Fact]
    public void UnknownFutureBackendIsDiscoveredWithoutAGameSpecificRegistryEntry()
    {
        var status = Session();
        Publish(status);
        var catalogue = new Dictionary<string, string> { ["future-game"] = "A newly added game" };
        var selection = _bridge.Select(catalogue, now: _now, processMatches: Running);
        Assert.Equal(SpectatorAvailability.Ready, selection.Availability);
        Assert.Equal("A newly added game", selection.GameName);
        Assert.Equal("test.backend", selection.Session!.AdapterId);
    }

    [Fact]
    public void AddingACatalogueEntryMakesItsRunningAdapterAvailableAutomatically()
    {
        Publish(Session("new-catalogue-game"));
        var catalogue = new Dictionary<string, string> { ["existing-game"] = "Existing game" };
        Assert.Equal(SpectatorAvailability.Unavailable, _bridge.Select(catalogue, now: _now, processMatches: Running).Availability);
        catalogue["new-catalogue-game"] = "New game";
        Assert.Equal(SpectatorAvailability.Ready, _bridge.Select(catalogue, now: _now, processMatches: Running).Availability);
    }

    [Fact]
    public void MultipleGamesRequireExplicitContextAndCommandsStayWithThatSession()
    {
        var first = Session("one");
        var second = Session("two", 456, "second-game");
        Publish(first); Publish(second);
        var catalogue = new Dictionary<string, string> { ["one"] = "First game", ["two"] = "Second game" };
        Assert.Equal(SpectatorAvailability.Ambiguous, _bridge.Select(catalogue, now: _now, processMatches: Running).Availability);
        var selected = _bridge.Select(catalogue, "two", _now, Running);
        Assert.Equal("Second game", selected.GameName);
        var id = _bridge.SetEnabled(selected.Session!, true, _now, Running);
        var request = JsonSerializer.Deserialize<SpectatorRequest>(File.ReadAllText(_bridge.RequestPath(second)), Json)!;
        Assert.Equal(id, request.RequestId);
        Assert.True(SpectatorProtocol.IsForSession(request, second));
        Assert.False(SpectatorProtocol.IsForSession(request, first));
        Assert.False(File.Exists(_bridge.RequestPath(first)));
        Assert.False(_bridge.Discover(_now, Running).Single(s => s.GameSlug == "two").Enabled);
    }

    [Fact]
    public void AGamePageCannotOperateAnUnrelatedRunningGamesCamera()
    {
        Publish(Session("one"));
        var catalogue = new Dictionary<string, string> { ["one"] = "First game", ["two"] = "Second game" };
        var selection = _bridge.Select(catalogue, "two", _now, Running);
        Assert.Equal(SpectatorAvailability.Unavailable, selection.Availability);
        Assert.Equal("Second game", selection.GameName);
        Assert.Null(selection.Session);
    }

    [Fact]
    public void TwoInstancesOfOneGameAreNeverSelectedArbitrarily()
    {
        Publish(Session("one")); Publish(Session("one", 456, "second-game"));
        var result = _bridge.Select(new Dictionary<string, string> { ["one"] = "Game" }, "one", _now, Running);
        Assert.Equal(SpectatorAvailability.Ambiguous, result.Availability);
        Assert.Null(result.Session);
    }

    [Fact]
    public void CorruptAndIncompatibleAdaptersDoNotDisableAHealthyAdapter()
    {
        Publish(Session());
        var incompatible = Session("another", 456, "second-game");
        incompatible.ProtocolVersion = 99;
        Publish(incompatible);
        File.WriteAllText(Path.Combine(_directory, "spectator-corrupt.status.json"), "{");
        var result = _bridge.Discover(_now, Running);
        Assert.Single(result);
        Assert.Equal("future-game", result[0].GameSlug);
    }

    [Fact]
    public void HeartbeatsRequireFreshTimeAndAMatchingLiveProcess()
    {
        var status = Session(); Publish(status);
        Assert.Single(_bridge.Discover(_now, Running));
        Assert.Empty(_bridge.Discover(_now.AddSeconds(6), Running));
        Assert.Empty(_bridge.Discover(_now.AddSeconds(-1), Running));
        Assert.Empty(_bridge.Discover(_now, (_, _) => false));
        status.ProcessName = "wrong-executable"; Publish(status);
        Assert.Empty(_bridge.Discover(_now, Running));
        status.ProcessName = "first-game"; status.UpdatedUtcTicks = long.MinValue; Publish(status);
        Assert.Empty(_bridge.Discover(_now, Running));
    }

    [Fact]
    public void MissingCameraIsReportedAndRequestsAreNotWritten()
    {
        var status = Session(); status.CameraAvailable = false; Publish(status);
        var result = _bridge.Select(new Dictionary<string, string> { [status.GameSlug] = "Game" }, now: _now, processMatches: Running);
        Assert.Equal(SpectatorAvailability.WaitingForCamera, result.Availability);
        Assert.Throws<InvalidOperationException>(() => _bridge.SetEnabled(status, true, _now, Running));
        Assert.False(File.Exists(_bridge.RequestPath(status)));
    }

    [Fact]
    public void CapabilityMustBeAdvertisedExplicitly()
    {
        var status = Session(); status.SupportsSteadycam = false; Publish(status);
        Assert.Equal(SpectatorAvailability.Unavailable,
            _bridge.Select(new Dictionary<string, string> { [status.GameSlug] = "Game" }, now: _now, processMatches: Running).Availability);
        Assert.Throws<InvalidOperationException>(() => _bridge.SetEnabled(status, true, _now, Running));
    }

    [Fact]
    public void ExpiredSessionIsRecheckedBeforeWritingACommand()
    {
        var status = Session(); Publish(status);
        Assert.Throws<InvalidOperationException>(() => _bridge.SetEnabled(status, true, _now.AddSeconds(6), Running));
        Assert.False(File.Exists(_bridge.RequestPath(status)));
    }

    [Fact]
    public void RestartedGameWithReusedPidRejectsTheOldRequest()
    {
        var oldSession = Session();
        var newSession = Session(); // same game, backend and PID, different launch
        var request = SpectatorProtocol.Request(oldSession, true);
        Assert.True(SpectatorProtocol.IsForSession(request, oldSession));
        Assert.False(SpectatorProtocol.IsForSession(request, newSession));
        Assert.NotEqual(_bridge.RequestPath(oldSession), _bridge.RequestPath(newSession));
        request.ProtocolVersion = 99;
        Assert.False(SpectatorProtocol.IsForSession(request, oldSession));
    }

    [Theory]
    [InlineData("../outside")]
    [InlineData("")]
    [InlineData("not-a-guid")]
    public void SessionIdentityCannotEscapeTheBridgeDirectory(string session)
        => Assert.Throws<ArgumentException>(() => SpectatorProtocol.RequestFileName(123, session));

    [Fact]
    public void StatusFileMustMatchItsClaimedSession()
    {
        var status = Session(); Publish(status);
        var path = _bridge.StatusPath(status);
        status.SessionId = Guid.NewGuid().ToString("N");
        File.WriteAllText(path, JsonSerializer.Serialize(status, Json));
        Assert.Empty(_bridge.Discover(_now, Running));
    }

    [Fact]
    public void SharedContractReadsAFlatJsonHeartbeatWithoutEngineAssemblies()
    {
        // Hand-authored wire data matching public-field serializers used by game
        // adapters. No Core-generated object is used to produce this fixture.
        const string session = "12ab12ab12ab12ab12ab12ab12ab12ab";
        var path = Path.Combine(_directory, "spectator-123-" + session + ".status.json");
        var wire = "{\"ProtocolVersion\":1,\"GameSlug\":\"future-game\",\"AdapterId\":\"native.backend\",\"ProcessId\":123," +
            "\"ProcessName\":\"first-game\",\"SessionId\":\"" + session + "\",\"UpdatedUtcTicks\":" + _now.Ticks +
            ",\"SupportsSteadycam\":true,\"CameraAvailable\":true,\"Enabled\":false,\"CaptureHint\":\"Capture the mirror.\"}";
        File.WriteAllText(path, wire);
        var result = Assert.Single(_bridge.Discover(_now, Running));
        Assert.Equal("native.backend", result.AdapterId);
        Assert.Equal("Capture the mirror.", result.CaptureHint);
        var id = _bridge.SetEnabled(result, true, _now, Running);
        using var request = JsonDocument.Parse(File.ReadAllText(_bridge.RequestPath(result)));
        Assert.Equal(1, request.RootElement.GetProperty("ProtocolVersion").GetInt32());
        Assert.Equal(session, request.RootElement.GetProperty("SessionId").GetString());
        Assert.Equal(id, request.RootElement.GetProperty("RequestId").GetString());
        Assert.True(request.RootElement.GetProperty("Enabled").GetBoolean());
    }

    public void Dispose() => Directory.Delete(_directory, recursive: true);
}
