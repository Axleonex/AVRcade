using System.Collections.ObjectModel;
using System.Diagnostics;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using VrClient.Core.Streaming;
using VrClient.Streaming;

namespace VrClient.App.ViewModels;

public partial class StreamingViewModel : ObservableObject, IDisposable
{
    private readonly StreamingSettings _settings;
    private readonly SpectatorBridge _spectators;
    private IReadOnlyDictionary<string, string> _catalogue = new Dictionary<string, string>();
    private string? _selectedGameSlug;
    private readonly DispatcherTimer _timer;
    private readonly CancellationTokenSource _lifetime = new();
    private readonly SemaphoreSlim _operations = new(1, 1);
    private ObsConnection? _obs;
    private string? _obsExecutable;
    private SpectatorStatus? _camera;
    private string? _pendingCameraRequest;
    private string? _pendingCameraSession;
    private DateTime _cameraRequestAt;
    private bool _disposed;

    [ObservableProperty] private string _port = "4455";
    [ObservableProperty] private string _password = string.Empty;
    [ObservableProperty] private bool _connected;
    [ObservableProperty] private bool _busy;
    [ObservableProperty] private bool _setupExpanded = true;
    [ObservableProperty] private string _connectionText = "OBS is disconnected";
    [ObservableProperty] private string _message = "Open OBS, enable its WebSocket server, then connect.";
    [ObservableProperty] private bool _streaming;
    [ObservableProperty] private bool _recording;
    [ObservableProperty] private bool _recordingPaused;
    [ObservableProperty] private string? _selectedScene;
    [ObservableProperty] private string _currentScene = string.Empty;
    [ObservableProperty] private bool _steadycamEnabled;
    [ObservableProperty] private bool _canToggleSteadycam;
    [ObservableProperty] private string _steadycamText = "Steadycam becomes available when a running game supports it. OBS recording and streaming work independently.";
    [ObservableProperty] private string _spectatorContextText = "For the running game";
    [ObservableProperty] private string _captureText = DefaultCaptureText;
    private const string DefaultCaptureText = "Capture the game's desktop window or your VR runtime's mirror. Check the preview and audio in OBS. Closing this panel leaves your outputs running.";
    public ObservableCollection<string> Scenes { get; } = new();
    public string ToolbarText => !Connected ? "Stream" : Streaming && Recording ? "LIVE + REC" : Streaming ? "LIVE" : Recording ? "Recording" : "Stream · OBS";
    public string StreamButtonText => Streaming ? "Stop stream" : "Go live";
    public string RecordButtonText => Recording ? "Stop recording" : "Record";
    public string OutputText => !Connected ? "Output status unavailable"
        : Streaming && Recording ? RecordingPaused ? "Live · Recording paused" : "Live · Recording"
        : Streaming ? "Live" : Recording ? RecordingPaused ? "Recording paused" : "Recording" : "Not streaming or recording";
    public string SteadycamButtonText => SteadycamEnabled ? "Turn off" : "Turn on";
    public bool CanControl => Connected && !Busy;
    public bool CanConnect => !Connected && !Busy;
    public bool CanDisconnect => Connected && !Busy;

    public StreamingViewModel(SpectatorBridge? spectators = null, StreamingSettings? settings = null)
    {
        _spectators = spectators ?? new();
        _settings = settings ?? new();
        var preferences = _settings.Load();
        Port = preferences.Port.ToString();
        _obsExecutable = preferences.ObsExecutable;
        _timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _timer.Tick += TimerTick;
        _timer.Start();
        RefreshCamera();
    }

    partial void OnConnectedChanged(bool value) => RaiseState();
    partial void OnBusyChanged(bool value) => RaiseState();
    partial void OnStreamingChanged(bool value) => RaiseState();
    partial void OnRecordingChanged(bool value) => RaiseState();
    partial void OnRecordingPausedChanged(bool value) => RaiseState();
    partial void OnSteadycamEnabledChanged(bool value) => OnPropertyChanged(nameof(SteadycamButtonText));

    private void RaiseState()
    {
        foreach (var property in new[] { nameof(ToolbarText), nameof(StreamButtonText), nameof(RecordButtonText),
                     nameof(OutputText), nameof(CanControl), nameof(CanConnect), nameof(CanDisconnect) })
            OnPropertyChanged(property);
    }

    [RelayCommand]
    private async Task ConnectAsync()
    {
        if (!CanConnect) return;
        if (!int.TryParse(Port, out var port) || port is < 1 or > 65535)
        { Message = "Enter a port from 1 to 65535. OBS uses 4455 by default."; return; }
        Busy = true;
        ConnectionText = "Connecting to OBS…";
        try
        {
            _obs?.Dispose();
            _obs = new ObsConnection();
            await _obs.ConnectAsync(port, Password, _lifetime.Token);
            Apply(await _obs.GetSnapshotAsync(_lifetime.Token));
            Connected = true;
            ConnectionText = "Connected to OBS";
            SetupExpanded = false;
            Message = "Check your capture and audio in OBS before recording or going live.";
            _settings.Save(new(port, _obsExecutable));
        }
        catch (Exception ex) { ConnectionFailed(ex); }
        finally { Busy = false; }
    }

    [RelayCommand]
    private void Disconnect()
    {
        _obs?.Dispose();
        _obs = null;
        Connected = false;
        Streaming = Recording = RecordingPaused = false;
        ConnectionText = "OBS is disconnected";
        Message = "OBS keeps any active stream or recording running. Connect again to see its status.";
        SetupExpanded = true;
    }

    private void Apply(ObsSnapshot snapshot)
    {
        Streaming = snapshot.Streaming;
        Recording = snapshot.Recording;
        RecordingPaused = snapshot.RecordingPaused;
        CurrentScene = snapshot.CurrentScene;
        if (!Scenes.SequenceEqual(snapshot.Scenes))
        {
            var chosen = SelectedScene;
            Scenes.Clear();
            foreach (var scene in snapshot.Scenes) Scenes.Add(scene);
            SelectedScene = chosen is not null && Scenes.Contains(chosen) ? chosen : snapshot.CurrentScene;
        }
        SelectedScene ??= snapshot.CurrentScene;
    }

    [RelayCommand]
    private Task ToggleStreamAsync() => ControlAsync(async obs =>
    {
        // Refresh first. Do not turn a stale Stop click into an unintended start.
        var intendedStop = Streaming;
        Apply(await obs.GetSnapshotAsync(_lifetime.Token));
        if (Streaming != intendedStop) { Message = "Stream status changed in OBS. Check the updated controls and try again."; return; }
        await obs.RequestAsync(intendedStop ? "StopStream" : "StartStream", cancellationToken: _lifetime.Token);
        Message = intendedStop ? "Stream stop requested. Check the status above."
            : "Stream start requested. Check the status above and your OBS preview.";
    });

    [RelayCommand]
    private Task ToggleRecordAsync() => ControlAsync(async obs =>
    {
        var intendedStop = Recording;
        Apply(await obs.GetSnapshotAsync(_lifetime.Token));
        if (Recording != intendedStop) { Message = "Recording status changed in OBS. Check the updated controls and try again."; return; }
        var result = await obs.RequestAsync(intendedStop ? "StopRecord" : "StartRecord", cancellationToken: _lifetime.Token);
        Message = intendedStop && result.TryGetProperty("outputPath", out var path)
            ? $"Recording saved: {path.GetString()}" : intendedStop ? "Recording stop requested. Check the status above."
            : "Recording requested. Check the status above.";
    });

    [RelayCommand]
    private Task UseSceneAsync() => ControlAsync(async obs =>
    {
        if (SelectedScene is null) return;
        await obs.RequestAsync("SetCurrentProgramScene", new { sceneName = SelectedScene }, _lifetime.Token);
        Message = $"OBS is using {SelectedScene}.";
    });

    private async Task ControlAsync(Func<ObsConnection, Task> action)
    {
        if (!CanControl || _obs is null) return;
        Busy = true;
        var acquired = false;
        try
        {
            await _operations.WaitAsync(_lifetime.Token);
            acquired = true;
            if (_obs is null || !Connected) return;
            await action(_obs);
            Apply(await _obs.GetSnapshotAsync(_lifetime.Token));
        }
        catch (Exception ex)
        {
            if (_obs?.IsConnected == true && ex is InvalidOperationException) Message = ex.Message + " Check the OBS preview and output settings.";
            else ConnectionFailed(ex);
        }
        finally { if (acquired) _operations.Release(); Busy = false; }
    }

    private async void TimerTick(object? sender, EventArgs e)
    {
        if (_disposed) return;
        RefreshCamera();
        if (Busy || !Connected || _obs is null || !await _operations.WaitAsync(0)) return;
        var connection = _obs;
        try
        {
            var snapshot = await connection.GetSnapshotAsync(_lifetime.Token);
            if (ReferenceEquals(_obs, connection) && Connected) Apply(snapshot);
        }
        catch (Exception ex)
        {
            if (ReferenceEquals(_obs, connection)) ConnectionFailed(ex);
        }
        finally { _operations.Release(); }
    }

    private void ConnectionFailed(Exception exception)
    {
        if (_disposed) return;
        _obs?.Dispose();
        _obs = null;
        Connected = false;
        Streaming = Recording = RecordingPaused = false;
        ConnectionText = "OBS is disconnected";
        SetupExpanded = true;
        Message = exception is OperationCanceledException ? "OBS did not respond in time. Check the server and port, then connect again."
            : exception is System.Net.WebSockets.WebSocketException ? "Cannot reach OBS. Open OBS and enable its WebSocket server, then connect again."
            : exception.Message;
    }

    [RelayCommand]
    private void OpenObs()
    {
        try { _settings.OpenObs(); Message = "OBS opened. Enable its WebSocket server, then connect."; }
        catch (Exception ex) { Message = ex.Message; SetupExpanded = true; }
    }

    public void ChooseObs(string path)
    {
        if (!StreamingSettings.IsObsExecutable(path)) { Message = "Choose OBS Studio's obs64.exe or obs32.exe."; return; }
        _obsExecutable = path;
        try
        {
            var port = int.TryParse(Port, out var value) && value is >= 1 and <= 65535 ? value : 4455;
            _settings.Save(new(port, path));
            Message = "OBS selected. Use Open OBS, then connect.";
        }
        catch (Exception ex) { Message = $"Could not remember OBS: {ex.Message}"; }
    }

    [RelayCommand]
    private void OpenCaptureGuide() => OpenLink("https://obsproject.com/kb/game-capture-guide");
    [RelayCommand]
    private void GetObs() => OpenLink("https://obsproject.com/download");
    private void OpenLink(string url)
    {
        try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); }
        catch (Exception ex) { Message = $"Could not open the guide: {ex.Message}"; }
    }

    public void SetCatalogue(IEnumerable<KeyValuePair<string, string>> games)
    {
        _catalogue = games.ToDictionary(g => g.Key, g => g.Value, StringComparer.Ordinal);
        RefreshCamera();
    }

    public void SetGameContext(string? slug)
    {
        if (_selectedGameSlug == slug) return;
        _selectedGameSlug = slug;
        // Do not carry an unacknowledged action into a different game's page.
        _pendingCameraRequest = _pendingCameraSession = null;
        RefreshCamera();
    }

    private void RefreshCamera()
    {
        var selection = _spectators.Select(_catalogue, _selectedGameSlug);
        _camera = selection.Session;
        SpectatorContextText = selection.GameName ?? "For the running game";
        SteadycamEnabled = _camera is { Enabled: true };
        CaptureText = SteadycamEnabled && !string.IsNullOrWhiteSpace(_camera?.CaptureHint)
            ? _camera.CaptureHint + " Check the preview and audio in OBS. Closing this panel leaves your outputs running."
            : DefaultCaptureText;
        if (_pendingCameraRequest is not null)
        {
            var session = _camera is null ? null : SpectatorProtocol.SessionKey(_camera.ProcessId, _camera.SessionId);
            if (session != _pendingCameraSession)
            {
                // The game exited/restarted or another adapter appeared. Never
                // silently redirect its outstanding command to a new session.
                _pendingCameraRequest = _pendingCameraSession = null;
                CanToggleSteadycam = false;
                SteadycamText = "The game camera session changed. Check the running game and try again.";
                return;
            }
            if (_camera?.RequestId == _pendingCameraRequest)
                _pendingCameraRequest = _pendingCameraSession = null;
            else if (DateTime.UtcNow - _cameraRequestAt < TimeSpan.FromSeconds(6))
            { CanToggleSteadycam = false; SteadycamText = "Waiting for the game camera…"; return; }
            else
            { _pendingCameraRequest = _pendingCameraSession = null; SteadycamText = "The game did not confirm the change. Check that it is running in VR and try again."; CanToggleSteadycam = _camera is { CameraAvailable: true }; return; }
        }
        CanToggleSteadycam = selection.Availability == SpectatorAvailability.Ready;
        SteadycamText = _camera?.Error is { Length: > 0 } error ? error
            : selection.Availability == SpectatorAvailability.Ambiguous
                ? "Multiple spectator cameras are running. Open the game you want in the library; close any extra instance of that game."
            : _camera is null ? selection.GameName is null
                ? "Steadycam becomes available when a running game supports it. OBS recording and streaming work independently."
                : $"Steadycam is unavailable for {selection.GameName} right now. You can still record or stream its desktop view through OBS."
            : !_camera.CameraAvailable ? $"Waiting for {selection.GameName}'s spectator camera. Enter the game in VR."
            : SteadycamEnabled ? "On · Your headset view stays responsive. " + _camera.CaptureHint
            : "Off · Smooth the spectator view. An extra camera render may reduce performance.";
    }

    [RelayCommand]
    private void ToggleSteadycam()
    {
        RefreshCamera();
        if (!CanToggleSteadycam || _camera is null) return;
        try
        {
            _pendingCameraRequest = _spectators.SetEnabled(_camera, !SteadycamEnabled);
            _pendingCameraSession = SpectatorProtocol.SessionKey(_camera.ProcessId, _camera.SessionId);
            _cameraRequestAt = DateTime.UtcNow;
            CanToggleSteadycam = false;
            SteadycamText = "Waiting for the game camera…";
        }
        catch (Exception ex) { SteadycamText = $"Could not change steadycam: {ex.Message}"; }
    }

    public void Dispose()
    {
        _disposed = true;
        _timer.Stop();
        _timer.Tick -= TimerTick;
        _lifetime.Cancel();
        _obs?.Dispose();
        Password = string.Empty;
    }
}
