using HarmonyLib;
using UnityEngine;

namespace Avrcade.YapyapVr;

internal static class YapyapHeadsetAim
{
    private static Camera? _camera;
    private static Vector3 _origin, _direction;
    private static int _frame = -1;

    internal static void Capture(Camera camera)
    {
        _camera = camera;
        _origin = camera.transform.position;
        _direction = camera.transform.forward;
        _frame = Time.frameCount;
    }

    internal static void Clear() { _camera = null; _frame = -1; }

    internal static bool TryGet(YAPYAP.ControllerState? state, out Vector3 origin, out Vector3 direction)
    {
        origin = _origin; direction = _direction;
        var pawn = state?.Pawn;
        var mainCamera = YAPYAP.CameraManager.Instance?.MainCamera;
        return HeadsetAimGate.CanUse(YapyapVrPlugin.ControlsActive,
            pawn != null && pawn == YAPYAP.Pawn.LocalInstance && pawn.isLocalPlayer && !pawn.IsAI,
            _camera != null && mainCamera == _camera, _frame, Time.frameCount);
    }
}

[HarmonyPatch(typeof(YAPYAP.PawnVisionComponent), "get_CamOrigin")]
internal static class YapyapSelectionOrigin
{
    private static bool Prefix(YAPYAP.ControllerState ____state, ref Vector3 __result)
    {
        if (!YapyapHeadsetAim.TryGet(____state, out var origin, out _)) return true;
        __result = origin;
        return false;
    }
}

[HarmonyPatch(typeof(YAPYAP.PawnVisionComponent), "get_CamLookDirection")]
internal static class YapyapSelectionDirection
{
    private static bool Prefix(YAPYAP.PawnVisionComponent __instance,
        YAPYAP.ControllerState ____state, ref Vector3 __result)
    {
        if (__instance.CamLookLock || !YapyapHeadsetAim.TryGet(____state, out _, out var direction)) return true;
        __result = direction;
        return false;
    }
}
