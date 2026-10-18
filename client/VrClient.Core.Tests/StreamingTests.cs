using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using VrClient.Core.Streaming;

namespace VrClient.Core.Tests;

public sealed class StreamingTests
{
    [Fact]
    public async Task ObsHandshakeAndOutputsUseV5AndReadExternalChanges()
    {
        await using var server = new FakeObs("test-password");
        using var client = new ObsConnection();
        await client.ConnectAsync(server.Port, "test-password");
        Assert.True(client.IsConnected);
        Assert.Equal(new[] { "Gameplay", "Break" }, (await client.GetSnapshotAsync()).Scenes);
        Assert.False(server.Streaming); // connecting never goes live
        await client.RequestAsync("StartStream");
        await client.RequestAsync("StartRecord");
        var snapshot = await client.GetSnapshotAsync();
        Assert.True(snapshot.Streaming);
        Assert.True(snapshot.Recording);
        server.Streaming = false; // OBS UI or another controller
        Assert.False((await client.GetSnapshotAsync()).Streaming);
        await client.RequestAsync("SetCurrentProgramScene", new { sceneName = "Break" });
        Assert.Equal("Break", (await client.GetSnapshotAsync()).CurrentScene);
        var output = await client.RequestAsync("StopRecord");
        Assert.Equal("C:/Recordings/game.mkv", output.GetProperty("outputPath").GetString());
        client.Dispose();
        Assert.DoesNotContain("StopStream", server.Requests); // dispose never controls outputs
    }

    [Fact]
    public async Task ResponseIdsRemainMatchedAcrossConcurrentRequestsAndFragmentation()
    {
        await using var server = new FakeObs();
        using var client = new ObsConnection();
        await client.ConnectAsync(server.Port, "");
        var requests = Enumerable.Range(0, 12).Select(i => client.RequestAsync("Echo", new { number = i })).ToArray();
        var responses = await Task.WhenAll(requests);
        Assert.Equal(Enumerable.Range(0, 12), responses.Select(r => r.GetProperty("number").GetInt32()));
    }

    [Fact]
    public async Task WrongPasswordHasAnActionableError()
    {
        await using var server = new FakeObs("correct");
        using var client = new ObsConnection();
        var error = await Assert.ThrowsAsync<InvalidOperationException>(() => client.ConnectAsync(server.Port, "wrong"));
        Assert.Contains("password", error.Message, StringComparison.OrdinalIgnoreCase);
        Assert.False(client.IsConnected);
    }

    [Fact]
    public async Task RejectedOutputIsNotReportedAsSuccessful()
    {
        await using var server = new FakeObs();
        using var client = new ObsConnection();
        await client.ConnectAsync(server.Port, "");
        var error = await Assert.ThrowsAsync<InvalidOperationException>(() => client.RequestAsync("Reject"));
        Assert.Contains("Configure your stream", error.Message);
        Assert.False(server.Streaming);
    }

    [Fact]
    public async Task MissingResponseTimesOutAndDoesNotBreakLaterRequests()
    {
        await using var server = new FakeObs();
        using var client = new ObsConnection { RequestTimeout = TimeSpan.FromMilliseconds(500) };
        await client.ConnectAsync(server.Port, "");
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => client.RequestAsync("Ignore"));
        Assert.False((await client.GetSnapshotAsync()).Recording);
    }

    [Fact]
    public async Task DroppedConnectionFailsPendingRequest()
    {
        await using var server = new FakeObs();
        using var client = new ObsConnection();
        await client.ConnectAsync(server.Port, "");
        await Assert.ThrowsAsync<InvalidOperationException>(() => client.RequestAsync("Close"));
        Assert.False(client.IsConnected);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(65536)]
    public async Task PortsAreValidatedBeforeConnecting(int port)
    {
        using var client = new ObsConnection();
        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(() => client.ConnectAsync(port, ""));
    }

    [Fact]
    public void PreferencesPersistOnlyPortAndExecutableAndRecoverFromCorruptFile()
    {
        var directory = Path.Combine(Path.GetTempPath(), "avrcade-stream-settings-" + Guid.NewGuid());
        try
        {
            var settings = new StreamingSettings(Path.Combine(directory, "settings.json"));
            Assert.Equal(4455, settings.Load().Port);
            settings.Save(new(4456, "C:/portable/bin/64bit/obs64.exe"));
            Assert.Equal(4456, settings.Load().Port);
            Assert.DoesNotContain("password", File.ReadAllText(settings.PathOnDisk), StringComparison.OrdinalIgnoreCase);
            File.WriteAllText(settings.PathOnDisk, "{");
            Assert.Equal(4455, settings.Load().Port);
            Assert.Throws<ArgumentOutOfRangeException>(() => settings.Save(new(0)));
        }
        finally { Directory.Delete(directory, recursive: true); }
    }

    /// A real loopback WebSocket endpoint, isolated from any installed OBS.
    private sealed class FakeObs : IAsyncDisposable
    {
        private readonly HttpListener _listener = new();
        private readonly CancellationTokenSource _cancel = new();
        private readonly Task _server;
        private readonly string? _password;
        public int Port { get; }
        public volatile bool Streaming;
        public bool Recording;
        public string Scene = "Gameplay";
        public List<string> Requests { get; } = new();

        public FakeObs(string? password = null)
        {
            _password = password;
            var reservation = new TcpListener(IPAddress.Loopback, 0);
            reservation.Start();
            Port = ((IPEndPoint)reservation.LocalEndpoint).Port;
            reservation.Stop();
            _listener.Prefixes.Add($"http://127.0.0.1:{Port}/");
            _listener.Start();
            _server = RunAsync();
        }

        private async Task RunAsync()
        {
            try
            {
                var context = await _listener.GetContextAsync();
                using var socket = (await context.AcceptWebSocketAsync(null)).WebSocket;
                const string salt = "test-salt", challenge = "test-challenge";
                await Send(socket, new { op = 0, d = new { rpcVersion = 1,
                    authentication = _password is null ? null : new { salt, challenge } } });
                // Without authentication OBS omits the property entirely.
                // The fake's hello below is sent through the custom serialization.
                var identify = await Receive(socket);
                Assert.Equal(1, identify.GetProperty("op").GetInt32());
                var data = identify.GetProperty("d");
                Assert.Equal(1, data.GetProperty("rpcVersion").GetInt32());
                if (_password is not null)
                {
                    // Independent implementation of OBS's double hash.
                    var secret = Convert.ToBase64String(SHA256.HashData(Encoding.UTF8.GetBytes(_password + salt)));
                    var expected = Convert.ToBase64String(SHA256.HashData(Encoding.UTF8.GetBytes(secret + challenge)));
                    if (data.GetProperty("authentication").GetString() != expected)
                    { await socket.CloseAsync((WebSocketCloseStatus)4009, "Authentication failed", _cancel.Token); return; }
                }
                await Send(socket, new { op = 2, d = new { negotiatedRpcVersion = 1 } });
                while (!_cancel.IsCancellationRequested)
                {
                    var message = await Receive(socket);
                    data = message.GetProperty("d");
                    var type = data.GetProperty("requestType").GetString()!;
                    Requests.Add(type);
                    if (type == "Ignore") continue;
                    if (type == "Close") { await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, "closed", _cancel.Token); return; }
                    object response = new { };
                    switch (type)
                    {
                        case "StartStream": Streaming = true; break;
                        case "StopStream": Streaming = false; break;
                        case "StartRecord": Recording = true; break;
                        case "StopRecord": Recording = false; response = new { outputPath = "C:/Recordings/game.mkv" }; break;
                        case "SetCurrentProgramScene": Scene = data.GetProperty("requestData").GetProperty("sceneName").GetString()!; break;
                        case "GetStreamStatus": response = new { outputActive = Streaming }; break;
                        case "GetRecordStatus": response = new { outputActive = Recording, outputPaused = false }; break;
                        case "GetSceneList": response = new { currentProgramSceneName = Scene, scenes = new[] { new { sceneName = "Gameplay" }, new { sceneName = "Break" } } }; break;
                        case "Echo": response = data.GetProperty("requestData").Clone(); break;
                    }
                    // Ignore unsolicited events and correctly assemble fragmented responses.
                    await Send(socket, new { op = 5, d = new { eventType = "StreamStateChanged" } });
                    await Send(socket, new { op = 7, d = new { requestId = data.GetProperty("requestId").GetString(), requestType = type,
                        requestStatus = new { result = type != "Reject", code = type == "Reject" ? 500 : 100,
                            comment = type == "Reject" ? "Configure your stream" : "" }, responseData = response } });
                }
            }
            catch (Exception ex) when (ex is WebSocketException or OperationCanceledException or HttpListenerException or ObjectDisposedException) { }
        }

        private async Task Send(WebSocket socket, object value)
        {
            var bytes = JsonSerializer.SerializeToUtf8Bytes(value, new JsonSerializerOptions
            { DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull });
            var half = bytes.Length / 2;
            await socket.SendAsync(bytes.AsMemory(0, half), WebSocketMessageType.Text, false, _cancel.Token);
            await socket.SendAsync(bytes.AsMemory(half), WebSocketMessageType.Text, true, _cancel.Token);
        }

        private async Task<JsonElement> Receive(WebSocket socket)
        {
            var buffer = new byte[8192];
            using var stream = new MemoryStream();
            ValueWebSocketReceiveResult result;
            do
            {
                result = await socket.ReceiveAsync(buffer.AsMemory(), _cancel.Token);
                if (result.MessageType == WebSocketMessageType.Close) throw new OperationCanceledException();
                stream.Write(buffer, 0, result.Count);
            } while (!result.EndOfMessage);
            using var doc = JsonDocument.Parse(stream.ToArray());
            return doc.RootElement.Clone();
        }

        public async ValueTask DisposeAsync()
        {
            _cancel.Cancel();
            _listener.Close();
            await _server.WaitAsync(TimeSpan.FromSeconds(5));
            _cancel.Dispose();
        }
    }
}
