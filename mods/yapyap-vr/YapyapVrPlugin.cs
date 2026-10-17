using System.Collections.Generic;
using BepInEx;
using BepInEx.Configuration;
using HarmonyLib;
using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.LowLevel;
using UnityEngine.UI;
using UnityEngine.XR;
using XrDevice = UnityEngine.XR.InputDevice;
using XrUsages = UnityEngine.XR.CommonUsages;

namespace Avrcade.YapyapVr;

[BepInPlugin(PluginGuid, "AVRcade YAPYAP VR Controls", PluginVersion)]
[BepInDependency("raicuparta.uuvr-modern", BepInDependency.DependencyFlags.HardDependency)]
public sealed class YapyapVrPlugin : BaseUnityPlugin
{
    public const string PluginGuid = "avrcade.yapyap-vr-controls";
    public const string PluginVersion = "0.3.39";
    private YapyapControllerHands? _controllerHands;
    private YapyapAvatarPerformance? _avatarPerformance;
    private readonly LeftHandShortcuts _fallbackLeftShortcuts = new();
    internal static bool ControlsActive { get; private set; }

    private static readonly System.Reflection.FieldInfo? SaveWindowOpenField =
        AccessTools.Field(typeof(YAPYAP.MenuController), "_saveWindowOpen");
    private static readonly System.Reflection.FieldInfo? InventoryOwnerField =
        AccessTools.Field(typeof(YAPYAP.UIInventory), "_playerInventory");
    private static readonly System.Reflection.FieldInfo? InventoryAutoHideField =
        AccessTools.Field(typeof(YAPYAP.UIInventory), "_autoHide");

    private readonly List<XrDevice> _devices = new();
    private readonly SteamVrActionReader _steamVrActions = new();
    private XrDevice? _leftDevice;
    private XrDevice? _rightDevice;
    private Gamepad? _virtualGamepad;
    private PlayerInput? _pairedPlayer;
    private bool _playerAutoSwitchWasDisabled;
    private bool _pairingFailureLogged;
    private ConfigEntry<bool>? _enabled;
    private ConfigEntry<float>? _deadzone;
    private ConfigEntry<bool>? _captureLobbyMenus;
    private float _nextCameraPatchAt;
    private Renderer? _vrUiRenderer;
    private bool _pauseHeld;
    private readonly InventoryGestures _inventoryGestures = new();
    private readonly InputReleaseGate _inventoryReleaseGate = new();
    private YAPYAP.UIInventory? _inventoryUi;
    private YAPYAP.Pawn? _inventoryPawn;
    private bool _inventoryOpen;
    private bool _inventoryAutoHide;
    private YAPYAP.UISettings[] _settingsPanels = System.Array.Empty<YAPYAP.UISettings>();
    private YAPYAP.MenuController[] _menuPanels = System.Array.Empty<YAPYAP.MenuController>();
    private float _nextUiScanAt;
    private bool? _usingSteamVrInput;
    private int _lastInputStatus = -1;
    private float _nextInputDiagnosticAt;

    private void Awake()
    {
        var uuvr = Uuvr.ModConfiguration.Instance;
        if (uuvr != null)
        {
            uuvr.PreferredVrApi.Value = Uuvr.ModConfiguration.VrApi.OpenVr;
            uuvr.CameraTracking.Value = Uuvr.ModConfiguration.CameraTrackingMode.Absolute;
            uuvr.PreferredUiPatchMode.Value = Uuvr.ModConfiguration.UiPatchMode.CanvasRedirect;
            uuvr.ScreenSpaceCanvasTypesToPatch.Value =
                Uuvr.ModConfiguration.ScreenSpaceCanvasType.NotToTexture;
            uuvr.PreferredUiRenderMode.Value = Uuvr.ModConfiguration.UiRenderMode.InWorld;
            uuvr.VrUiLayerOverride.Value = 1;
            uuvr.VrUiScale.Value = 1f;
            uuvr.VrUiPosition.Value = new Vector3(0f, 0f, 1.2f);
            uuvr.Config.Save();
            Logger.LogInfo("YAPYAP UUVR settings: OpenVR, absolute camera tracking with game yaw, canvas UI on visible layer.");
        }
        YapyapOcclusionMeshCompatibility.Configure(Config);
        new Harmony(PluginGuid).PatchAll(typeof(YapyapVrPlugin).Assembly);
        _enabled = Config.Bind("Controls", "Enabled", true,
            "Expose tracked controller input to YAPYAP's existing gamepad actions.");
        _deadzone = Config.Bind("Controls", "StickDeadzone", 0.12f,
            "Ignore small controller stick drift.");
        _captureLobbyMenus = Config.Bind("Diagnostics", "CaptureLobbyMenus", false,
            "Save one UI image, game-view image and shader report at the title and first save/lobby panel per launch. Disable after diagnosing the reported lobby artifact.");
        _controllerHands = new YapyapControllerHands(Config, Logger, _steamVrActions);
        _avatarPerformance = new YapyapAvatarPerformance(Config, Logger);
        Logger.LogInfo("YAPYAP VR controller bridge loaded. Voice casting remains on the selected headset microphone.");
    }

    private void Update()
    {
        YapyapUiDiagnostics.UpdateMenuIsolation(_captureLobbyMenus?.Value == true);
        PatchGameCamera();
        RefreshUiPanels();
        UpdateVrUiVisibility();
        if (_enabled?.Value != true)
        {
            RemoveVirtualGamepad();
            return;
        }

        RefreshDevices();
        var menu = YAPYAP.Pawn.LocalInstance == null || HasBlockingMenu();
        var hasSteamVrState = _steamVrActions.TryRead(Logger, menu, out var steamVrState);
        if (!hasSteamVrState && (_leftDevice is null || _rightDevice is null))
        {
            RemoveVirtualGamepad();
            return;
        }

        ControlsActive = true;
        _controllerHands?.UpdateBinding();
        _avatarPerformance?.Update();
        _virtualGamepad ??= InputSystem.AddDevice<Gamepad>("AVRcade YAPYAP VR");
        YapyapVrFocus.Activate();
        YapyapVrFocus.Update(Logger);
        if (!_virtualGamepad.enabled) InputSystem.EnableDevice(_virtualGamepad);
        PairWithGameInput();
        if (_usingSteamVrInput != hasSteamVrState)
        {
            _usingSteamVrInput = hasSteamVrState;
            Logger.LogInfo(hasSteamVrState ? "VR input source: SteamVR actions." : "VR input source: Unity XR fallback.");
        }
        var inventoryPressed = hasSteamVrState && _steamVrActions.InventoryPressed;
        var state = hasSteamVrState ? steamVrState : ReadXrGamepadState(menu, out inventoryPressed);
        var pausePressed = hasSteamVrState && _steamVrActions.PausePressed;
        if (!hasSteamVrState)
        {
            _rightDevice!.Value.TryGetFeatureValue(XrUsages.primary2DAxisClick, out pausePressed);
        }
        HandlePause(pausePressed);
        state = HandleInventory(state, inventoryPressed);
        InputSystem.QueueStateEvent(_virtualGamepad, state);
        UpdateVrUiVisibility();
        LogInputTransitions();
    }

    private void LogInputTransitions()
    {
        if (Time.realtimeSinceStartup < _nextInputDiagnosticAt) return;
        _nextInputDiagnosticAt = Time.realtimeSinceStartup + 0.5f;
        var manager = YAPYAP.InputManager.Instance;
        if (manager == null) return;
        var status = (manager.HasFocus ? 1 : 0) | (manager.IsInputLocked ? 2 : 0) |
            (_inventoryOpen ? 4 : 0) | (HasBlockingMenu() ? 8 : 0) |
            (_pairedPlayer?.inputIsActive == true ? 16 : 0);
        if (status == _lastInputStatus) return;
        _lastInputStatus = status;
        Logger.LogInfo($"VR input state: gameFocus={manager.HasFocus}, locked={manager.IsInputLocked}, inventory={_inventoryOpen}, menu={HasBlockingMenu()}, playerInputActive={_pairedPlayer?.inputIsActive}.");
    }

    private GamepadState ReadXrGamepadState(bool menu, out bool inventoryPressed)
    {
        var left = ReadController(_leftDevice!.Value);
        var right = ReadController(_rightDevice!.Value);
        var mapped = YapyapControlMap.Map(left, right, menu);
        _leftDevice!.Value.TryGetFeatureValue(XrUsages.primary2DAxisClick, out var stickClicked);
        _fallbackLeftShortcuts.Update(left.LeftTrigger, stickClicked);
        inventoryPressed = _fallbackLeftShortcuts.InventoryPressed;
        var deadzone = Mathf.Clamp01(_deadzone?.Value ?? 0.12f);
        var state = new GamepadState
        {
            leftStick = ApplyDeadzone(new Vector2(mapped.MoveX, mapped.MoveY), deadzone),
            rightStick = ApplyDeadzone(new Vector2(mapped.LookX, mapped.LookY), deadzone),
            leftTrigger = _fallbackLeftShortcuts.RagdollTrigger,
            rightTrigger = mapped.Fire
        };
        state = state.WithButton(GamepadButton.South, mapped.Buttons.HasFlag(VrButtons.South));
        state = state.WithButton(GamepadButton.East, mapped.Buttons.HasFlag(VrButtons.East));
        state = state.WithButton(GamepadButton.West, mapped.Buttons.HasFlag(VrButtons.West));
        state = state.WithButton(GamepadButton.North, mapped.Buttons.HasFlag(VrButtons.North));
        state = state.WithButton(GamepadButton.LeftShoulder, mapped.Buttons.HasFlag(VrButtons.LeftShoulder));
        state = state.WithButton(GamepadButton.RightShoulder, mapped.Buttons.HasFlag(VrButtons.RightShoulder));
        state = state.WithButton(GamepadButton.Start, mapped.Buttons.HasFlag(VrButtons.Start));
        return state;
    }

    private void PatchGameCamera()
    {
        if (Time.realtimeSinceStartup < _nextCameraPatchAt) return;
        _nextCameraPatchAt = Time.realtimeSinceStartup + 0.5f;

        foreach (var scaler in Resources.FindObjectsOfTypeAll<PixelPerfectScaler>())
        {
            if (scaler == null || !scaler.enabled || !scaler.gameObject.activeInHierarchy) continue;
            var camera = scaler.sourceCamera;
            scaler.enabled = false;
            var flatImage = scaler.GetComponent<RawImage>();
            if (flatImage != null) flatImage.enabled = false;
            Logger.LogInfo($"Routed {camera?.name ?? "game camera"} to VR and hid its flat render image.");
        }

    }

    /// The Gamepad scheme alone does not prove the VR controllers drive the
    /// player: it may be paired with a physical pad, or with an earlier virtual
    /// pad that was removed when the controllers dropped out. Pair by device.
    private void PairWithGameInput()
    {
        if (_virtualGamepad == null || YAPYAP.InputManager.Instance == null) return;
        var player = YAPYAP.InputManager.Instance.GetComponent<PlayerInput>();
        if (player == null) return;
        if (player != _pairedPlayer)
        {
            RestoreAutoSwitch();
            _pairedPlayer = player;
            _playerAutoSwitchWasDisabled = player.neverAutoSwitchControlSchemes;
        }
        // Keep a keyboard, mouse or physical pad from taking the player back mid-session.
        player.neverAutoSwitchControlSchemes = true;
        if (player.currentControlScheme == "Gamepad" && IsPaired(player, _virtualGamepad)) return;
        try { player.SwitchCurrentControlScheme("Gamepad", _virtualGamepad); }
        catch (System.Exception error)
        {
            if (!_pairingFailureLogged) Logger.LogWarning($"VR controller pairing failed: {error.Message}");
            _pairingFailureLogged = true;
            return;
        }
        if (player.currentControlScheme != "Gamepad" || !IsPaired(player, _virtualGamepad))
        {
            if (!_pairingFailureLogged)
                Logger.LogWarning("YAPYAP input could not pair the VR controllers with its Gamepad scheme.");
            _pairingFailureLogged = true;
            return;
        }
        _pairingFailureLogged = false;
        Logger.LogInfo($"YAPYAP input paired with the VR controllers ({player.currentControlScheme}).");
    }

    private static bool IsPaired(PlayerInput player, UnityEngine.InputSystem.InputDevice device)
    {
        foreach (var paired in player.devices)
        {
            if (paired == device) return true;
        }
        return false;
    }

    private void RestoreAutoSwitch()
    {
        if (_pairedPlayer != null)
            _pairedPlayer.neverAutoSwitchControlSchemes = _playerAutoSwitchWasDisabled;
        _pairedPlayer = null;
    }

    private void UpdateVrUiVisibility()
    {
        if (_vrUiRenderer == null)
        {
            foreach (var renderer in Resources.FindObjectsOfTypeAll<Renderer>())
            {
                if (renderer != null && renderer.gameObject.name == "VrUiQuad" &&
                    renderer.transform.parent?.name == "VrUiContainer")
                {
                    _vrUiRenderer = renderer;
                    break;
                }
            }
        }
        if (_vrUiRenderer == null) return;
        var pawn = YAPYAP.Pawn.LocalInstance;
        var gameplayHud = pawn != null && pawn.isLocalPlayer && !pawn.IsAI;
        var panelOpen = gameplayHud || _inventoryOpen;
        foreach (var settings in _settingsPanels)
        {
            if (settings != null && settings.IsOpen && settings.gameObject.activeInHierarchy)
            {
                panelOpen = true;
                break;
            }
        }
        if (!panelOpen && SaveWindowOpenField != null)
        {
            foreach (var menu in _menuPanels)
            {
                if (menu != null && menu.gameObject.activeInHierarchy &&
                    SaveWindowOpenField.GetValue(menu) is true)
                {
                    panelOpen = true;
                    if (_captureLobbyMenus?.Value == true) YapyapUiDiagnostics.Request(this, "lobby");
                    break;
                }
            }
        }
        if (_vrUiRenderer.enabled != panelOpen)
        {
            _vrUiRenderer.enabled = panelOpen;
            Logger.LogInfo(panelOpen ? "VR HUD/menu shown." : "VR HUD/menu hidden.");
        }
        if (gameplayHud) YapyapUiDiagnostics.Request(this);
        else if (!panelOpen && _captureLobbyMenus?.Value == true && ControlsActive && Time.timeSinceLevelLoad >= 5f)
            foreach (var menu in _menuPanels)
                if (menu != null && menu.isActiveAndEnabled && menu.gameObject.scene.IsValid())
                {
                    YapyapUiDiagnostics.Request(this, "title");
                    break;
                }
    }

    private void HandlePause(bool pressed)
    {
        if (pressed && !_pauseHeld)
        {
            foreach (var settings in _settingsPanels)
            {
                if (settings == null || !settings.gameObject.activeInHierarchy) continue;
                settings.ToggleSettings();
                Logger.LogInfo($"VR pause gesture toggled YAPYAP settings: open={settings.IsOpen}.");
                break;
            }
        }
        _pauseHeld = pressed;
    }

    private bool HasBlockingMenu()
    {
        foreach (var settings in _settingsPanels)
            if (settings != null && settings.gameObject.activeInHierarchy && settings.IsOpen) return true;
        if (SaveWindowOpenField != null)
            foreach (var menu in _menuPanels)
                if (menu != null && menu.gameObject.activeInHierarchy &&
                    SaveWindowOpenField.GetValue(menu) is true) return true;
        return false;
    }

    private void RefreshUiPanels()
    {
        if (Time.realtimeSinceStartup < _nextUiScanAt) return;
        _nextUiScanAt = Time.realtimeSinceStartup + 0.5f;
        _settingsPanels = Resources.FindObjectsOfTypeAll<YAPYAP.UISettings>();
        _menuPanels = Resources.FindObjectsOfTypeAll<YAPYAP.MenuController>();
        YapyapHud.Refresh(_inventoryOpen);
    }

    private GamepadState HandleInventory(GamepadState state, bool pressed)
    {
        var pawn = YAPYAP.Pawn.LocalInstance;
        var available = pawn != null && pawn.isLocalPlayer && !pawn.IsAI && pawn.Inventory != null &&
            YAPYAP.InputManager.Instance != null && YAPYAP.InputManager.Instance.HasFocus && !HasBlockingMenu();
        if (_inventoryUi != null && (pawn == null ||
            InventoryOwnerField?.GetValue(_inventoryUi) as YAPYAP.PawnInventory != pawn.Inventory))
        {
            CloseInventory();
            _inventoryGestures.Suspend();
        }
        if (_inventoryPawn != null && _inventoryPawn != pawn)
        {
            CloseInventory();
            _inventoryGestures.Suspend();
        }
        // Find the UI bound to this local inventory; do not open a remote player's UI.
        if (available && pressed && !_inventoryOpen && _inventoryUi == null)
            foreach (var ui in Resources.FindObjectsOfTypeAll<YAPYAP.UIInventory>())
                if (ui != null && ui.gameObject.activeInHierarchy && InventoryOwnerField != null &&
                    InventoryOwnerField.GetValue(ui) as YAPYAP.PawnInventory == pawn!.Inventory)
                { _inventoryUi = ui; break; }
        available &= _inventoryUi != null && _inventoryUi.gameObject.activeInHierarchy;
        var wasOpen = _inventoryOpen;
        var gesture = _inventoryGestures.Update(available, pressed,
            IsPressed(state, GamepadButton.South) || IsPressed(state, GamepadButton.West),
            IsPressed(state, GamepadButton.East), state.leftStick.x, state.leftStick.y);
        if (gesture.Open && !wasOpen)
        {
            _inventoryPawn = pawn;
            _inventoryAutoHide = InventoryAutoHideField?.GetValue(_inventoryUi) is true;
            _inventoryUi!.SetAutoHide(false);
            _inventoryOpen = true;
            YapyapHud.SetInteractive(true);
            YapyapUiDiagnostics.Request(this);
            Logger.LogInfo("VR inventory opened: right A/B or right grip stores, left/right retrieves, down swaps hands, left X/Index A or left trigger closes.");
        }
        else if (!gesture.Open && wasOpen) CloseInventory();

        if (gesture.Store) pawn!.Inventory!.CmdStoreHandItemInInventory(true);
        var consumedMask = (1u << (int)GamepadButton.South) | (1u << (int)GamepadButton.West) |
            (1u << (int)GamepadButton.East) | (1u << (int)GamepadButton.RightShoulder);
        var stick = state.leftStick;
        state.buttons = _inventoryReleaseGate.Filter(state.buttons, consumedMask,
            gesture.Open || wasOpen, stick.x, stick.y);
        if (_inventoryReleaseGate.SuppressMovement) state.leftStick = Vector2.zero;
        if (gesture.Open || wasOpen)
        {
            state.leftStick = Vector2.zero;
            state = state.WithButton(GamepadButton.South, false)
                .WithButton(GamepadButton.West, false).WithButton(GamepadButton.East, false)
                .WithButton(GamepadButton.RightShoulder, false);
            state = state.WithButton(GamepadButton.DpadLeft, gesture.Direction == InventoryDirection.Left)
                .WithButton(GamepadButton.DpadRight, gesture.Direction == InventoryDirection.Right)
                .WithButton(GamepadButton.DpadDown, gesture.Direction == InventoryDirection.Down);
        }
        return state;
    }

    private static bool IsPressed(GamepadState state, GamepadButton button) =>
        (state.buttons & (1u << (int)button)) != 0;

    private void CloseInventory()
    {
        if (_inventoryOpen && _inventoryUi != null)
        {
            _inventoryUi.SetAutoHide(_inventoryAutoHide);
            _inventoryUi.HideInventory();
        }
        _inventoryOpen = false;
        YapyapHud.SetInteractive(false);
        _inventoryPawn = null;
        _inventoryUi = null;
    }

    private void RefreshDevices()
    {
        _devices.Clear();
        InputDevices.GetDevicesWithCharacteristics(InputDeviceCharacteristics.Controller, _devices);
        _leftDevice = FindDevice(InputDeviceCharacteristics.Left);
        _rightDevice = FindDevice(InputDeviceCharacteristics.Right);
    }

    private XrDevice? FindDevice(InputDeviceCharacteristics hand)
    {
        foreach (var device in _devices)
        {
            if ((device.characteristics & hand) != 0 && device.isValid)
                return device;
        }
        return null;
    }

    private static VrControllerState ReadController(XrDevice device)
    {
        device.TryGetFeatureValue(XrUsages.primary2DAxis, out var stick);
        device.TryGetFeatureValue(XrUsages.trigger, out var trigger);
        device.TryGetFeatureValue(XrUsages.grip, out var grip);
        device.TryGetFeatureValue(XrUsages.primaryButton, out var primary);
        device.TryGetFeatureValue(XrUsages.secondaryButton, out var secondary);
        device.TryGetFeatureValue(XrUsages.menuButton, out var menu);
        var left = (device.characteristics & InputDeviceCharacteristics.Left) != 0;

        return left
            ? new VrControllerState(stick.x, stick.y, 0f, 0f, trigger, 0f, grip, 0f,
                primary, secondary, false, false, menu, false)
            : new VrControllerState(0f, 0f, stick.x, stick.y, 0f, trigger, 0f, grip,
                false, false, primary, secondary, false, menu);
    }

    private static Vector2 ApplyDeadzone(Vector2 value, float deadzone)
    {
        var magnitude = value.magnitude;
        if (magnitude <= deadzone) return Vector2.zero;
        return value.normalized * Mathf.Clamp01((magnitude - deadzone) / (1f - deadzone));
    }

    private void LateUpdate() => _avatarPerformance?.LateUpdateClearance();

    private void OnDestroy()
    {
        YapyapUiDiagnostics.RestoreMenuIsolation();
        RemoveVirtualGamepad();
        _controllerHands?.Dispose();
        _avatarPerformance?.Dispose();
    }

    private void RemoveVirtualGamepad()
    {
        ControlsActive = false;
        _avatarPerformance?.Suspend();
        _controllerHands?.Suspend();
        YapyapHeadsetAim.Clear();
        CloseInventory();
        _inventoryGestures.Suspend();
        _pauseHeld = true;
        UpdateVrUiVisibility();
        RestoreAutoSwitch();
        YapyapVrFocus.Restore();
        _usingSteamVrInput = null;
        _lastInputStatus = -1;
        if (_virtualGamepad is null) return;
        InputSystem.RemoveDevice(_virtualGamepad);
        _virtualGamepad = null;
    }
}
