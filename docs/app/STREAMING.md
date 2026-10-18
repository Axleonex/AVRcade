# Streaming and spectator steadycam

Experience depth: operated / multi-state.
Job: connect OBS, record or stream the VR game, and keep the viewer camera steady.
Known: the launcher uses Avalonia, theme resources and a separate game process.
VRChat offers a smoothed desktop camera; OBS 28+ includes WebSocket control.
Assumed: OBS is on the same PC and the player has configured their stream account.
Open: headset validation of the new YAPYAP spectator camera.

Happy path: game page -> Stream beneath the status card -> open OBS -> enable its WebSocket server and enter its
password -> Connect -> check the scene in OBS -> Record or Go live.
Opening or connecting never starts an output. Closing the panel or launcher
leaves OBS's outputs running. Connection loss clears known status and disables
output controls; reconnect reads the current state before offering actions.
Setup expands on failure; the password stays in memory and is never saved.

SUBJECT: AVRcade's optional controls for players broadcasting VR gameplay.
TREATMENT: ui-tool.
PALETTE: existing City rain tokens (and the player's other palettes):
ground #10181E, surface #18242C, ink #E8F1F5, ink-2 #A7BBC5,
accent #5CC8E8; existing good/warning/danger semantics.
TYPE: existing Inter, 22px semibold title, 16px body, 13px secondary copy.
LAYOUT: one neutral, full-width Stream button beneath the status card in every
game page's left sidebar; a modeless 440px-wide owned
window with connection status, a scene selector, two output controls and one
steadycam switch. Setup lives in an expander. The header stays focused on
navigation and library controls. No overlays inside the headset.

## First setup

Use OBS Studio 28 or newer. In OBS, open **Tools -> WebSocket Server Settings**,
enable the server, and copy its password. AVRcade connects to 127.0.0.1, port
4455 by default; a custom local port can be entered. Credentials are session-only.
Use **Open OBS** to launch a detected installation or **Choose OBS** for a portable
installation. AVRcade remembers the executable and port.

Set up a Game Capture or Window Capture source in OBS for the game's desktop
window. For games rendering through SteamVR, capturing its VR View window is another option:

1. Connect the headset and launch the game in VR through SteamVR.
2. In SteamVR's desktop status-window menu, choose **Display VR View** or **Show VR View**.
3. In OBS, add a **Window Capture** source and select **[vrmonitor.exe]: VR View**.
4. Select **Windows 10 (1903 and up)** as the capture method if Automatic freezes the image. Keep VR View open, including when OBS covers it.
5. Fit the source to the canvas while preserving its aspect ratio. Configure game audio and microphone separately in OBS.
6. Check the preview and a short local recording, including head movement and audible game/microphone sound, before going live.

VR View mirrors the headset; it does not show a separate spectator steadycam. Games using another VR runtime need their desktop window or that runtime's mirror. Flat games need their desktop window. Seeing SteamVR Home does not verify game capture.

The launcher does not automatically create this scene or change OBS's capture method. These are optional OBS settings.
Check the OBS preview and audio before recording or going live. AVRcade can select
an existing scene but does not edit sources, encoders, profiles or stream keys.
Recording destinations and streaming services remain configured in OBS.

## Steadycam

OBS controls are catalogue-independent. Every current or future catalogue game
uses the same connection, scene selection and output controls, once its game
window or runtime mirror is configured as a capture source in OBS. Steadycam
is a separately advertised backend capability; absence or failure of a camera
adapter does not disable OBS controls.

YAPYAP is the first adapter. Its updated AVRcade bridge creates a separate mono
spectator camera only while steadycam is enabled. Position and orientation use
time-based exponential smoothing; source-camera changes, tracking loss and
teleports reset the filter. The spectator starts at the current head pose.
The headset camera, item aiming and controller input keep their original poses.
Turning the option off destroys the spectator and restores the normal desktop
mirror. Capture the **YAPYAP game window**, rather than SteamVR's eye mirror, to
record the stabilized view. The extra render may reduce game performance.

The launcher enables the switch only when the running bridge reports a fresh
camera heartbeat. Each request names the game, backend, process, launch session
and a unique request ID;
the switch reflects the bridge acknowledgement, not just a saved preference.
Steadycam starts off in each new game session. Other backends show an explicit
unavailable message until they have an equivalent spectator-camera adapter.

## Adding a game or camera adapter

Adding a game to the catalogue requires no streaming-specific settings or UI
code. The launcher sends its discovered slug/title list to the streaming module.
Each backend can implement the small [spectator protocol](../../../client/VrClient.Core/Streaming/SpectatorProtocol.cs)
and automatically appear in the existing panel. The launcher has no backend
registry or Unity/UEVR/native dependencies in its streaming module.

`SpectatorProtocol.cs` contains plain serializable classes and identity helpers,
using `System` only. C# adapters can source-link this one file instead of depending
on the launcher/Core DLL. Native or other-language adapters can emit the same JSON
shape. OBS control uses .NET's built-in WebSocket, cryptography and JSON support.
This feature adds no NuGet packages, OBS plugins, encoders or helper services.

Protocol v1 uses `%LOCALAPPDATA%/VRClient/streaming/`:

- On each game launch, create a new 32-character GUID `SessionId`; publish
  `spectator-{ProcessId}-{SessionId}.status.json` atomically every 0.5–1 seconds.
- Supply `ProtocolVersion`, catalogue `GameSlug`, backend `AdapterId`, live
  `ProcessId`/`ProcessName`, `SessionId`, `UpdatedUtcTicks`, `SupportsSteadycam`,
  `CameraAvailable`, `Enabled`, `RequestId`, and viewer-facing `CaptureHint`.
  An optional `Error` explains recovery. Names shown in the UI come from the catalogue.
- Read only `spectator-{ProcessId}-{SessionId}.request.json`. Validate version,
  game, backend, process, session and request ID using `IsForSession` before
  acting. Start off; never reuse a previous launch's desired state.
- Set `Enabled` and echo `RequestId` after the actual camera change. Preserve
  headset tracking/aiming, reset on teleports/camera changes, and dispose the
  spectator on disable/exit. Remove this session's two files on exit.
- The launcher ignores expired, malformed, oversized and incompatible statuses.
  A selected game page scopes the camera controls to that game. With multiple
  running games, open the desired game's page; duplicate camera sessions for one
  game are unavailable until the extra instance is closed. Commands are never
  redirected to another game/session. Each write rechecks camera freshness.

Heartbeat discovery verifies liveness and protocol compatibility, not plugin
authenticity. This is a local interface for trusted installed adapters, under
the user's existing account. A ready adapter still needs in-game capture,
headset, loading/teleport and performance validation for its backend.

## Evidence and sources

- [OBS remote control guide](https://obsproject.com/kb/remote-control-guide)
- [OBS WebSocket v5 protocol](https://github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md)
- [VRChat steadycam FOV](https://docs.vrchat.com/docs/configuration-file#first-person-steadycam-fov)
- [VRChat steadycam fixes](https://docs.vrchat.com/docs/vrchat-202623)
- [UUVR camera admission](https://github.com/Raicuparta/uuvr/blob/de8e9218f8a9310778f2f57c189bef9ccc8c4dc8/Uuvr/VrCamera/VrCameraManager.cs)

VRChat's documentation establishes the viewer-camera behavior, not its private
filter implementation. AVRcade's exponential filter is its own implementation.
