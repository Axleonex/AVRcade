using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering.Universal;
using UnityEngine.Rendering;

namespace Avrcade.YapyapVr;

/// A plain capture camera inherits the world renderer, including a full-screen
/// dither pass that writes opaque alpha even with post processing disabled.
[HarmonyPatch(typeof(Uuvr.VrUi.PatchModes.CanvasRedirectPatchMode))]
internal static class YapyapUiTransparency
{
    private static readonly System.Reflection.FieldInfo? RendererListField =
        AccessTools.Field(typeof(UniversalRenderPipelineAsset), "m_RendererDataList");
    private static Camera? _configuredCamera;

    [HarmonyPostfix, HarmonyPatch("Awake")]
    private static void Postfix(Camera ____uiCaptureCamera)
    {
        if (____uiCaptureCamera == null) return;
        ____uiCaptureCamera.clearFlags = CameraClearFlags.SolidColor;
        ____uiCaptureCamera.backgroundColor = Color.clear;
        ____uiCaptureCamera.allowHDR = false;
        var data = ____uiCaptureCamera.GetComponent<UniversalAdditionalCameraData>() ??
            ____uiCaptureCamera.gameObject.AddComponent<UniversalAdditionalCameraData>();
        data.renderPostProcessing = false;
        data.renderShadows = false;
        data.volumeLayerMask = 0;
        Debug.Log("[AVRcade YAPYAP] UI capture transparency configured: clear alpha=0, HDR off, post processing off.");
        SelectUiRenderer(____uiCaptureCamera);
    }

    [HarmonyPostfix, HarmonyPatch("OnSettingChanged")]
    private static void AfterSettings(Camera ____uiCaptureCamera) => SelectUiRenderer(____uiCaptureCamera);

    private static void SelectUiRenderer(Camera camera)
    {
        if (camera == null || camera == _configuredCamera) return;
        var asset = GraphicsSettings.currentRenderPipeline as UniversalRenderPipelineAsset;
        var renderers = asset == null ? null : RendererListField?.GetValue(asset) as ScriptableRendererData[];
        if (renderers == null) return;
        for (var index = 0; index < renderers.Length; index++)
        {
            var renderer = renderers[index];
            if (renderer == null || renderer.name != "PC_Renderer_Empty" || renderer.rendererFeatures.Count != 0) continue;
            var data = camera.GetComponent<UniversalAdditionalCameraData>();
            if (data == null) return;
            data.SetRenderer(index);
            data.requiresColorOption = CameraOverrideOption.Off;
            data.requiresDepthOption = CameraOverrideOption.Off;
            _configuredCamera = camera;
            Debug.Log($"[AVRcade YAPYAP] UI capture uses {renderer.name} (index {index}); world renderer unchanged.");
            return;
        }
        Debug.LogWarning("[AVRcade YAPYAP] UI renderer PC_Renderer_Empty unavailable; captured alpha requires verification.");
    }
}
