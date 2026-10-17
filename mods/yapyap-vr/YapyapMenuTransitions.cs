using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.InputSystem;

namespace Avrcade.YapyapVr;

/// Record short-lived panels without changing their fade or interaction rules.
[HarmonyPatch]
internal static class YapyapMenuTransitions
{
    private static readonly Dictionary<int, float> OpenedAt = new();
    private static int _reports;

    private static IEnumerable<MethodBase> TargetMethods()
    {
        yield return AccessTools.Method(typeof(UIFader), "FadeIn");
        yield return AccessTools.Method(typeof(UIFader), "FadeOut");
        yield return AccessTools.Method(typeof(UIFader), "FadeOutInstant");
    }

    private static void Prefix(UIFader __instance, MethodBase __originalMethod)
    {
        if (!YapyapVrPlugin.ControlsActive || _reports >= 32) return;
        var canvas = __instance.GetComponentInParent<Canvas>();
        if (canvas == null || canvas.rootCanvas.name != "MainCanvas") return;
        var path = __instance.name;
        for (var parent = __instance.transform.parent; parent != null; parent = parent.parent)
            path = parent.name + "/" + path;
        // Inventory stays visible by design; loading and spoken subtitles fade
        // routinely. This diagnostic is for transient interaction/menu panels.
        if (path.Contains("Inventory") || path.Contains("Loading") || path.Contains("Subtitles")) return;
        if (!path.Contains("Interact") && !path.Contains("Tooltip") && !path.Contains("Settings") &&
            !path.Contains("Book") && !path.Contains("Dungeon") && !path.Contains("Modal") &&
            !path.Contains("Wheel") && !path.Contains("Shop") && !path.Contains("Merchant")) return;
        var id = __instance.GetInstanceID();
        if (__originalMethod.Name == "FadeIn")
        {
            if (OpenedAt.Count < 128 && !OpenedAt.ContainsKey(id)) OpenedAt[id] = Time.realtimeSinceStartup;
            return;
        }
        if (!OpenedAt.TryGetValue(id, out var opened)) return;
        OpenedAt.Remove(id);
        var elapsed = Time.realtimeSinceStartup - opened;
        if (elapsed > 1f) return;
        _reports++;
        var manager = YAPYAP.InputManager.Instance;
        var pad = Gamepad.current;
        UnityEngine.Debug.Log($"[AVRcade YAPYAP] Short UI transition: path={path}, close={__originalMethod.Name}, elapsed={elapsed:F3}s, frame={Time.frameCount}, focus={manager?.HasFocus}, locked={manager?.IsInputLocked}, A={pad?.buttonSouth.isPressed}, B={pad?.buttonEast.isPressed}, leftStick={pad?.leftStick.ReadValue()}, rightStick={pad?.rightStick.ReadValue()}. Callers: {new StackTrace(2, false)}");
    }
}
