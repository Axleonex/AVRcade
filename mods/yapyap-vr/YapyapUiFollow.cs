using HarmonyLib;
using UnityEngine;

namespace Avrcade.YapyapVr;

/// FollowTarget can run before the tracked MainCamera updates. Re-anchor after
/// that pose update as well, so game pitch/yaw cannot leave menus above/below it.
[HarmonyPatch(typeof(Uuvr.FollowTarget), "OnBeforeRender")]
internal static class YapyapUiFollow
{
    private static Transform? _container;

    [HarmonyPrefix]
    private static bool FollowMainCamera(Uuvr.FollowTarget __instance)
    {
        if (__instance.gameObject.name != "VrUiContainer") return true;
        _container = __instance.transform;
        var camera = YAPYAP.CameraManager.Instance?.MainCamera ?? Camera.main;
        if (camera != null)
        {
            __instance.Target = camera.transform;
            Apply(camera);
        }
        return false;
    }

    internal static void Apply(Camera camera)
    {
        if (_container == null || !camera.isActiveAndEnabled) return;
        _container.localScale = Vector3.one;
        _container.SetPositionAndRotation(camera.transform.position, camera.transform.rotation);
    }
}
