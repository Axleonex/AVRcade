using HarmonyLib;
using UnityEngine;

namespace Avrcade.YapyapVr;

/// Keep the game's camera yaw when UUVR replaces its rotation with the headset pose.
[HarmonyPatch(typeof(Uuvr.UuvrPoseDriver), "UpdateTransform")]
internal static class YapyapCameraYaw
{
    [HarmonyPostfix]
    private static void RestoreGameYaw(Uuvr.UuvrPoseDriver __instance)
    {
        if (Uuvr.ModConfiguration.Instance?.CameraTracking.Value !=
            Uuvr.ModConfiguration.CameraTrackingMode.Absolute) return;
        var camera = YAPYAP.CameraManager.Instance;
        if (camera == null || camera.MainCamera == null ||
            __instance.GetComponent<Camera>() != camera.MainCamera) return;

        if (camera.cinemachineCam != null)
        {
            var gameYaw = camera.cinemachineCam.State.RawOrientation.eulerAngles.y;
            var headsetRotation = __instance.transform.localRotation;
            __instance.transform.rotation = Quaternion.Euler(0f, gameYaw, 0f) * headsetRotation;
        }
        YapyapHeadsetAim.Capture(camera.MainCamera);
        YapyapUiFollow.Apply(camera.MainCamera);
    }
}
