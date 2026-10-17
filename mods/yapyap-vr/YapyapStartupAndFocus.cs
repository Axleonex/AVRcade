using HarmonyLib;
using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.UI;
using Valve.VR;

namespace Avrcade.YapyapVr;

/// Stop the flat render texture before OnEnable can put its first frame in VR.
[HarmonyPatch(typeof(PixelPerfectScaler), "Awake")]
internal static class YapyapStartupScaler
{
    internal static void Hide(PixelPerfectScaler scaler)
    {
        var image = scaler.GetComponent<RawImage>();
        if (image != null) image.enabled = false;
        scaler.enabled = false; // Its OnDisable releases the owned camera render texture.
        // At Awake the scaler has not created its owned texture yet. Both game
        // scenes serialize a separate target on MainCamera; OnDisable leaves
        // that target attached, and UUVR skips cameras rendering to a texture.
        // Detach the primary view without destroying the game's shared asset.
        // Auxiliary cameras retain their render targets and stay outside VR.
        var camera = scaler.sourceCamera;
        if (camera != null && camera.name == "MainCamera" && camera.targetTexture != null)
        {
            camera.targetTexture = null;
            Debug.Log("[AVRcade YAPYAP] Detached MainCamera's saved flat render target for VR.");
        }
    }

    private static void Postfix(PixelPerfectScaler __instance) => Hide(__instance);
}

[HarmonyPatch(typeof(PixelPerfectScaler), "OnEnable")]
internal static class YapyapScalerReenable
{
    private static bool Prefix(PixelPerfectScaler __instance)
    {
        YapyapStartupScaler.Hide(__instance);
        return false;
    }
}

internal static class YapyapVrFocus
{
    private static readonly System.Reflection.MethodInfo FocusChanged =
        AccessTools.Method(typeof(YAPYAP.InputManager), "OnApplicationFocus");
    private static InputSettings? _settings;
    private static InputSettings.BackgroundBehavior _previousBackground;
    private static bool _previousRunInBackground;
    private static YAPYAP.InputManager? _manager;
    private static bool? _sceneFocus;
    internal static bool Active { get; private set; }
    internal static bool SceneInputAvailable => OpenVR.System?.IsInputAvailable() == true;

    internal static void Activate()
    {
        if (Active) return;
        _settings = InputSystem.settings;
        _previousBackground = _settings.backgroundBehavior;
        _previousRunInBackground = Application.runInBackground;
        _settings.backgroundBehavior = InputSettings.BackgroundBehavior.IgnoreFocus;
        Application.runInBackground = true;
        Active = true;
    }

    internal static void Update(BepInEx.Logging.ManualLogSource log)
    {
        var manager = YAPYAP.InputManager.Instance;
        var sceneFocus = SceneInputAvailable;
        if (manager == null || (_manager == manager && _sceneFocus == sceneFocus)) return;
        _manager = manager;
        _sceneFocus = sceneFocus;
        // Use the game's original method, preserving menu stack and input locks.
        FocusChanged.Invoke(manager, new object[] { sceneFocus });
        log.LogInfo($"VR focus: sceneInput={sceneFocus}, gameFocus={manager.HasFocus}, inputLocked={manager.IsInputLocked}.");
    }

    internal static void Restore()
    {
        if (!Active) return;
        Active = false;
        if (_settings != null && _settings.backgroundBehavior == InputSettings.BackgroundBehavior.IgnoreFocus)
            _settings.backgroundBehavior = _previousBackground;
        if (Application.runInBackground) Application.runInBackground = _previousRunInBackground;
        if (_manager != null) FocusChanged.Invoke(_manager, new object[] { Application.isFocused });
        _settings = null;
        _manager = null;
        _sceneFocus = null;
    }
}

[HarmonyPatch(typeof(YAPYAP.InputManager), "OnApplicationFocus")]
internal static class YapyapHeadsetFocus
{
    private static void Prefix(ref bool hasFocus) => hasFocus = VrFocusPolicy.GameHasFocus(
        hasFocus, YapyapVrFocus.Active, YapyapVrFocus.SceneInputAvailable);
}
