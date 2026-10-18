using System.Collections.Concurrent;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace VrClient.Core.Streaming;

public sealed record ObsSnapshot(bool Streaming, bool Recording, bool RecordingPaused,
    string CurrentScene, IReadOnlyList<string> Scenes);

/// OBS WebSocket v5. A single reader dispatches responses while ignoring events;
/// state is queried, so changes made directly in OBS are reflected too.
public sealed class ObsConnection : IDisposable
{
    private readonly ClientWebSocket _socket = new();
    private readonly SemaphoreSlim _send = new(1, 1);
    private readonly ConcurrentDictionary<string, TaskCompletionSource<JsonElement>> _pending = new();
    private readonly CancellationTokenSource _lifetime = new();
    private Task? _reader;
    private volatile bool _identified;
    public bool IsConnected => _identified && _socket.State == WebSocketState.Open;
    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(6);

    public static string Authenticate(string password, string salt, string challenge)
    {
        var secret = Convert.ToBase64String(SHA256.HashData(Encoding.UTF8.GetBytes(password + salt)));
        return Convert.ToBase64String(SHA256.HashData(Encoding.UTF8.GetBytes(secret + challenge)));
    }

    public async Task ConnectAsync(int port, string password, CancellationToken cancellationToken = default)
    {
        if (port is < 1 or > 65535) throw new ArgumentOutOfRangeException(nameof(port), "Enter a port from 1 to 65535.");
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, _lifetime.Token);
        timeout.CancelAfter(RequestTimeout);
        try
        {
            await _socket.ConnectAsync(new Uri($"ws://127.0.0.1:{port}"), timeout.Token);
            var hello = await ReceiveAsync(timeout.Token);
            if (hello.GetProperty("op").GetInt32() != 0)
                throw new InvalidOperationException("OBS did not send a WebSocket v5 hello. Use OBS Studio 28 or newer.");
            var data = hello.GetProperty("d");
            var identify = new Dictionary<string, object> { ["rpcVersion"] = 1, ["eventSubscriptions"] = 0 };
            if (data.TryGetProperty("authentication", out var auth))
                identify["authentication"] = Authenticate(password, auth.GetProperty("salt").GetString()!,
                    auth.GetProperty("challenge").GetString()!);
            await SendAsync(new { op = 1, d = identify }, timeout.Token);
            var identified = await ReceiveAsync(timeout.Token);
            if (identified.GetProperty("op").GetInt32() != 2)
                throw new InvalidOperationException("OBS could not identify AVRcade. Check the WebSocket password.");
            _identified = true;
            _reader = ReadResponsesAsync();
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    public async Task<JsonElement> RequestAsync(string requestType, object? requestData = null,
        CancellationToken cancellationToken = default)
    {
        if (!IsConnected) throw new InvalidOperationException("OBS is disconnected. Connect again to control it.");
        var id = Guid.NewGuid().ToString("N");
        var response = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[id] = response;
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, _lifetime.Token);
        timeout.CancelAfter(RequestTimeout);
        try
        {
            await SendAsync(new { op = 6, d = new { requestType, requestId = id, requestData = requestData ?? new { } } }, timeout.Token);
            var result = await response.Task.WaitAsync(timeout.Token);
            var status = result.GetProperty("requestStatus");
            if (!status.GetProperty("result").GetBoolean())
            {
                var comment = status.TryGetProperty("comment", out var value) ? value.GetString() : "Request rejected";
                // OBS's response may contain private profile information. Use it only
                // for this session's UI, never persist or log requests/passwords.
                throw new InvalidOperationException($"OBS: {comment}");
            }
            return result.TryGetProperty("responseData", out var payload) ? payload.Clone() : default;
        }
        finally { _pending.TryRemove(id, out _); }
    }

    public async Task<ObsSnapshot> GetSnapshotAsync(CancellationToken cancellationToken = default)
    {
        var stream = await RequestAsync("GetStreamStatus", cancellationToken: cancellationToken);
        var record = await RequestAsync("GetRecordStatus", cancellationToken: cancellationToken);
        var scenes = await RequestAsync("GetSceneList", cancellationToken: cancellationToken);
        return new(stream.GetProperty("outputActive").GetBoolean(), record.GetProperty("outputActive").GetBoolean(),
            record.GetProperty("outputPaused").GetBoolean(), scenes.GetProperty("currentProgramSceneName").GetString()!,
            scenes.GetProperty("scenes").EnumerateArray().Select(s => s.GetProperty("sceneName").GetString()!).ToArray());
    }

    private async Task SendAsync(object message, CancellationToken token)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(message);
        await _send.WaitAsync(token);
        try { await _socket.SendAsync(bytes.AsMemory(), WebSocketMessageType.Text, true, token); }
        finally { _send.Release(); }
    }

    private async Task<JsonElement> ReceiveAsync(CancellationToken token)
    {
        var buffer = new byte[8192];
        using var message = new MemoryStream();
        while (true)
        {
            var result = await _socket.ReceiveAsync(buffer.AsMemory(), token);
            if (result.MessageType == WebSocketMessageType.Close)
                throw new InvalidOperationException((int?)_socket.CloseStatus == 4009
                    ? "OBS rejected the password. Copy it from OBS's WebSocket Server Settings."
                    : "OBS closed the connection. Check OBS and connect again.");
            if (result.MessageType != WebSocketMessageType.Text)
                throw new InvalidOperationException("OBS sent an unexpected binary message.");
            if (message.Length + result.Count > 1024 * 1024)
                throw new InvalidOperationException("OBS sent a message larger than the supported limit.");
            message.Write(buffer, 0, result.Count);
            if (result.EndOfMessage) break;
        }
        using var document = JsonDocument.Parse(message.ToArray());
        return document.RootElement.Clone();
    }

    private async Task ReadResponsesAsync()
    {
        Exception reason = new InvalidOperationException("OBS is disconnected. Connect again.");
        try
        {
            while (!_lifetime.IsCancellationRequested)
            {
                var message = await ReceiveAsync(_lifetime.Token);
                if (message.GetProperty("op").GetInt32() != 7) continue;
                var data = message.GetProperty("d");
                var id = data.GetProperty("requestId").GetString()!;
                if (_pending.TryRemove(id, out var response)) response.TrySetResult(data.Clone());
            }
        }
        catch (Exception ex) { reason = ex; }
        finally
        {
            _identified = false;
            foreach (var response in _pending.Values) response.TrySetException(reason);
            _pending.Clear();
        }
    }

    public void Dispose()
    {
        _identified = false;
        _lifetime.Cancel();
        _socket.Abort();
        _socket.Dispose();
        // Reader/send tasks may still be unwinding; their synchronization objects
        // intentionally remain usable until cancellation has propagated.
    }
}
