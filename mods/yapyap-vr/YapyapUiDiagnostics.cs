using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using BepInEx;
using BepInEx.Configuration;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering.Universal;
using UnityEngine.Rendering;
using UnityEngine.UI;

namespace Avrcade.YapyapVr;

/// Capture the actual alpha path once, without changing UI or world rendering.
internal static class YapyapUiDiagnostics
{
    private static readonly HashSet<string> Requested = new();
    private static readonly Dictionary<Behaviour, bool> IsolatedEffects = new();
    private static readonly Dictionary<UniversalAdditionalCameraData, bool> IsolatedCameras = new();
    private static readonly Dictionary<ScriptableRendererFeature, bool> IsolatedFeatures = new();
    private static string _isolation = "none";
    private static float _nextIsolationCheck;
    private static bool _menuDiagnosticsEnabled, _occlusionReported, _occlusionSkipReported;

    internal static void UpdateMenuIsolation(bool enabled)
    {
        _menuDiagnosticsEnabled = enabled;
        if (!enabled || UnityEngine.SceneManagement.SceneManager.GetActiveScene().name != "Menu")
        {
            RestoreMenuIsolation();
            return;
        }
        if (Time.realtimeSinceStartup < _nextIsolationCheck) return;
        _nextIsolationCheck = Time.realtimeSinceStartup + 1f;
        var file = Path.Combine(Paths.BepInExRootPath, "diagnostics", "yapyap-ui", "isolate-menu.txt");
        string requested;
        try { requested = File.Exists(file) ? File.ReadAllText(file).Trim().ToLowerInvariant() : "none"; }
        catch (IOException exception)
        {
            Debug.LogWarning($"[AVRcade YAPYAP] Menu isolation request unavailable: {exception.Message}");
            return;
        }
        if (requested == _isolation) return;
        RestoreMenuIsolation();
        if (requested == "occlusion")
        {
            _isolation = requested;
            _occlusionSkipReported = false;
            Debug.Log("[AVRcade YAPYAP] Temporary menu isolation: occlusion, skipping XR hidden-area mesh draws only.");
            return;
        }
        if (requested == "aux")
        {
            _isolation = requested;
            foreach (var camera in Resources.FindObjectsOfTypeAll<Camera>())
            {
                if (camera == null || !camera.isActiveAndEnabled || camera.gameObject.scene.name != "Menu" ||
                    (camera.name != "UICamera" && camera.name != "RaycastCamera")) continue;
                var data = camera.GetComponent<UniversalAdditionalCameraData>();
                if (data == null) continue;
                IsolatedCameras.Add(data, data.allowXRRendering);
                data.allowXRRendering = false;
            }
            Debug.Log($"[AVRcade YAPYAP] Temporary menu isolation: aux, {IsolatedCameras.Count} auxiliary cameras excluded from XR; camera components and raycasters remain active.");
            return;
        }
        if (requested.StartsWith("feature:", StringComparison.Ordinal))
        {
            var renderer = MainRendererData();
            if (renderer == null) return;
            foreach (var feature in renderer.rendererFeatures)
            {
                if (feature == null || !feature.isActive || !string.Equals(feature.name, requested.Substring(8), StringComparison.OrdinalIgnoreCase)) continue;
                IsolatedFeatures.Add(feature, feature.isActive);
                feature.SetActive(false);
            }
            _isolation = requested;
            Debug.Log($"[AVRcade YAPYAP] Temporary menu isolation: {requested}, {IsolatedFeatures.Count} renderer features paused.");
            return;
        }
        // A closed set of decorative components; never hide buttons or scene geometry.
        var type = requested == "boids" ? "GPUAnimatedBoidsVolume" : requested == "outline" ? "Outline" : requested == "volumes" ? "Volume" : null;
        if (type == null) return;
        _isolation = requested;
        foreach (var effect in Resources.FindObjectsOfTypeAll<MonoBehaviour>())
        {
            if (effect == null || !effect.enabled || !effect.gameObject.activeInHierarchy ||
                effect.gameObject.scene.name != "Menu" || effect.GetType().Name != type) continue;
            IsolatedEffects.Add(effect, effect.enabled);
            effect.enabled = false;
        }
        Debug.Log($"[AVRcade YAPYAP] Temporary menu isolation: {requested}, {IsolatedEffects.Count} decorative components paused. Remove isolate-menu.txt or write none to restore.");
    }

    internal static void RestoreMenuIsolation()
    {
        foreach (var entry in IsolatedEffects) if (entry.Key != null) entry.Key.enabled = entry.Value;
        IsolatedEffects.Clear();
        foreach (var entry in IsolatedCameras) if (entry.Key != null) entry.Key.allowXRRendering = entry.Value;
        IsolatedCameras.Clear();
        foreach (var entry in IsolatedFeatures) if (entry.Key != null) entry.Key.SetActive(entry.Value);
        IsolatedFeatures.Clear();
        _isolation = "none";
    }

    internal static bool BeforeOcclusionMesh(UnityEngine.Experimental.Rendering.XRPass pass)
    {
        if (!_menuDiagnosticsEnabled || UnityEngine.SceneManagement.SceneManager.GetActiveScene().name != "Menu") return true;
        if (!_occlusionReported)
        {
            _occlusionReported = true;
            var mesh = AccessTools.Field(pass.GetType(), "m_OcclusionMesh")?.GetValue(pass);
            // XRPass may be subclassed; the field is declared on the base type.
            mesh ??= AccessTools.Field(typeof(UnityEngine.Experimental.Rendering.XRPass), "m_OcclusionMesh")?.GetValue(pass);
            var material = mesh == null ? null : AccessTools.Field(mesh.GetType(), "m_Material")?.GetValue(mesh) as Material;
            var left = pass.viewCount > 0 ? pass.GetOcclusionMesh(0) : null;
            Debug.Log($"[AVRcade YAPYAP] XR hidden-area mesh: material={material?.name}; shader={material?.shader?.name}; supported={material?.shader?.isSupported}; scale={pass.occlusionMeshScale}; views={pass.viewCount}; mesh={left?.name}; vertices={left?.vertexCount}; bounds={left?.bounds}.");
        }
        if (_isolation != "occlusion") return true;
        if (!_occlusionSkipReported)
        {
            _occlusionSkipReported = true;
            Debug.Log("[AVRcade YAPYAP] XR hidden-area mesh draw suppressed for the current menu comparison.");
        }
        return false;
    }

    internal static void Request(MonoBehaviour owner, string context = "inventory")
    {
        if (!Requested.Add(context)) return;
        owner.StartCoroutine(Capture(context));
    }

    private static IEnumerator Capture(string context)
    {
        // A local pawn appears before the loading screen finishes fading. Do
        // not mistake that legitimate backdrop for an opaque HUD failure.
        yield return new WaitForSecondsRealtime(0.5f);
        var loadingField = AccessTools.Field(typeof(YAPYAP.UIGame), "loadingScreenCanvasGroup");
        var loadingGroups = Resources.FindObjectsOfTypeAll<YAPYAP.UIGame>()
            .Where(g => g != null && g.isActiveAndEnabled)
            .Select(g => loadingField?.GetValue(g) as CanvasGroup).ToArray();
        var waits = 0;
        while (waits++ < 40 && loadingGroups.Any(g => g != null && g.gameObject.activeInHierarchy && g.alpha > 0.01f))
            yield return new WaitForSecondsRealtime(0.25f);
        yield return new WaitForEndOfFrame();
        var camera = Resources.FindObjectsOfTypeAll<Camera>().FirstOrDefault(c =>
            c != null && c.name == "VrUiCaptureCamera" && c.enabled && c.targetTexture != null);
        if (camera == null)
        {
            Requested.Remove(context);
            Debug.LogWarning($"[AVRcade YAPYAP] {context} UI capture unavailable; retry on next opening.");
            yield break;
        }

        var previous = RenderTexture.active;
        Texture2D? pixels = null;
        Texture2D? mirror = null;
        try
        {
            var target = camera.targetTexture;
            pixels = new Texture2D(target.width, target.height, TextureFormat.RGBA32, false);
            RenderTexture.active = target;
            pixels.ReadPixels(new Rect(0, 0, target.width, target.height), 0, 0);
            pixels.Apply(false, false);
            var colors = pixels.GetPixels32();
            var transparent = 0;
            var opaque = 0;
            var opaqueBlack = 0;
            var magenta = 0;
            foreach (var color in colors)
            {
                if (color.a == 0) transparent++;
                if (color.a == 255) opaque++;
                if (color.a == 255 && color.r <= 2 && color.g <= 2 && color.b <= 2) opaqueBlack++;
                if (color.a > 0 && color.r > 200 && color.g < 80 && color.b > 200) magenta++;
            }

            var report = new StringBuilder();
            report.AppendLine($"context={context} scene={UnityEngine.SceneManagement.SceneManager.GetActiveScene().name}");
            var data = camera.GetComponent<UniversalAdditionalCameraData>();
            report.AppendLine($"capture={target.width}x{target.height} format={target.graphicsFormat} clear={camera.clearFlags} background={camera.backgroundColor} postProcess={data?.renderPostProcessing} mask={camera.cullingMask} rendererIndex={AccessTools.Field(typeof(UniversalAdditionalCameraData), "m_RendererIndex")?.GetValue(data)}");
            report.AppendLine($"pixels={colors.Length} alphaZero={transparent} alphaFull={opaque} opaqueBlack={opaqueBlack} magenta={magenta} partialAlpha={colors.Length - transparent - opaque}");
            report.AppendLine($"loadingAlpha={string.Join(",", loadingGroups.Select(g => g == null ? "missing" : g.alpha.ToString()))}");
            foreach (var renderer in Resources.FindObjectsOfTypeAll<Renderer>().Where(r => r != null && r.name == "VrUiQuad"))
            {
                report.AppendLine($"quad active={renderer.gameObject.activeInHierarchy} layer={renderer.gameObject.layer} shader={renderer.sharedMaterial?.shader?.name} supported={renderer.sharedMaterial?.shader?.isSupported} color={renderer.sharedMaterial?.color} queue={renderer.sharedMaterial?.renderQueue} sameTexture={MainTexture(renderer.sharedMaterial) == target}");
                var main = YAPYAP.CameraManager.Instance?.MainCamera ?? Camera.main ??
                    Resources.FindObjectsOfTypeAll<Camera>().FirstOrDefault(c => c != null && c.isActiveAndEnabled && c.name == "MainCamera");
                if (main != null)
                    report.AppendLine($"quad cameraRelative={main.transform.InverseTransformPoint(renderer.transform.position)} rotationError={Quaternion.Angle(main.transform.rotation, renderer.transform.rotation)}");
            }
            foreach (var inventory in Resources.FindObjectsOfTypeAll<YAPYAP.UIInventory>())
            {
                if (inventory == null || !YapyapHud.IsLocal(inventory)) continue;
                var panel = inventory.transform.Find("InventoryPanel") as RectTransform;
                if (panel != null)
                    report.AppendLine($"localInventory anchor={panel.anchorMin} offset={panel.anchoredPosition} scale={panel.localScale}");
                var health = inventory.transform.parent?.Find("BottomLeft") as RectTransform;
                if (health != null)
                    report.AppendLine($"localHealth anchor={health.anchorMin} offset={health.anchoredPosition} scale={health.localScale}");
                var score = inventory.transform.parent?.parent?.Find("ScoreUI") as RectTransform;
                if (score != null)
                    report.AppendLine($"localScore anchor={score.anchorMin} offset={score.anchoredPosition} scale={score.localScale}");
                var subtitles = inventory.transform.parent?.parent?.Find("Subtitles") as RectTransform;
                if (subtitles != null)
                    report.AppendLine($"localSubtitles anchor={subtitles.anchorMin} offset={subtitles.anchoredPosition}");
                var timer = inventory.transform.parent?.parent?.Find("UIGameRound/GameTimerUI") as RectTransform;
                if (timer != null)
                    report.AppendLine($"localTimer anchor={timer.anchorMin} offset={timer.anchoredPosition}");
                var notifications = inventory.transform.parent?.parent?.Find("UIGameRound/TaskMessages") as RectTransform;
                if (notifications != null)
                    report.AppendLine($"localNotifications anchors={notifications.anchorMin}/{notifications.anchorMax} offset={notifications.anchoredPosition}");
                var canvas = inventory.transform.parent?.parent as RectTransform;
                var compass = score?.Find("TopRight");
                if (canvas != null && timer != null && compass != null)
                {
                    var clockBounds = RectTransformUtility.CalculateRelativeRectTransformBounds(canvas, timer);
                    var compassBounds = RectTransformUtility.CalculateRelativeRectTransformBounds(canvas, compass);
                    var row = clockBounds;
                    row.Encapsulate(compassBounds);
                    report.AppendLine($"topHud visibleRowCenter={row.center} clockBounds={clockBounds} compassBounds={compassBounds}");
                }
            }

            foreach (var sceneCamera in Resources.FindObjectsOfTypeAll<Camera>().Where(c => c != null && c.isActiveAndEnabled))
                report.AppendLine($"camera={sceneCamera.name} depth={sceneCamera.depth} stereo={sceneCamera.stereoEnabled} allowXR={sceneCamera.GetComponent<UniversalAdditionalCameraData>()?.allowXRRendering} target={sceneCamera.targetTexture?.name} mask={sceneCamera.cullingMask}");
            var rendererData = MainRendererData();
            if (rendererData != null)
                foreach (var feature in rendererData.rendererFeatures)
                    if (feature != null) report.AppendLine($"rendererFeature={feature.name} type={feature.GetType().FullName} active={feature.isActive}");

            if (context != "inventory")
            {
                var main = YAPYAP.CameraManager.Instance?.MainCamera ?? Camera.main ??
                    Resources.FindObjectsOfTypeAll<Camera>().FirstOrDefault(c => c != null && c.isActiveAndEnabled && c.name == "MainCamera");
                var planes = main == null ? null : GeometryUtility.CalculateFrustumPlanes(main);
                foreach (var worldRenderer in Resources.FindObjectsOfTypeAll<Renderer>().Where(r =>
                             r != null && r.enabled && r.gameObject.activeInHierarchy && r.gameObject.scene.IsValid()))
                {
                    if (main != null && ((main.cullingMask & (1 << worldRenderer.gameObject.layer)) == 0 ||
                        (planes != null && !GeometryUtility.TestPlanesAABB(planes, worldRenderer.bounds)))) continue;
                    var bounds = worldRenderer.bounds;
                    var screen = main == null ? Vector3.zero : main.WorldToViewportPoint(bounds.center);
                    var screenSize = main == null ? Vector3.zero : main.WorldToViewportPoint(bounds.center + main.transform.right * bounds.extents.magnitude) - screen;
                    var path = worldRenderer.name;
                    for (var parent = worldRenderer.transform.parent; parent != null; parent = parent.parent)
                        path = parent.name + "/" + path;
                    var slot = 0;
                    foreach (var material in worldRenderer.sharedMaterials)
                    {
                        report.AppendLine($"world={path} type={worldRenderer.GetType().Name} layer={worldRenderer.gameObject.layer} viewport={screen} viewportRadius={screenSize.x} bounds={bounds} slot={slot++} material={material?.name} shader={material?.shader?.name} supported={material?.shader?.isSupported} texture={MainTexture(material)?.name} keywords={(material == null ? "" : string.Join(",", material.shaderKeywords))}");
                    }
                }
                foreach (var effect in Resources.FindObjectsOfTypeAll<MonoBehaviour>().Where(b => b != null &&
                             b.isActiveAndEnabled && b.gameObject.scene.name == "Menu" && b.GetType().Name == "GPUAnimatedBoidsVolume"))
                {
                    var gpu = AccessTools.Field(effect.GetType(), "_renderer")?.GetValue(effect);
                    var materials = gpu == null ? null : AccessTools.Field(gpu.GetType(), "_runtimeMaterials")?.GetValue(gpu) as Material[];
                    if (materials == null) continue;
                    foreach (var material in materials)
                        report.AppendLine($"procedural={effect.name} position={effect.transform.position} material={material?.name} shader={material?.shader?.name} supported={material?.shader?.isSupported} texture={MainTexture(material)?.name}");
                }
            }

            // Include world-space menu graphics as well as the captured overlay:
            // the reported pink icon may live on either rendering path.
            var corners = new Vector3[4];
            foreach (var graphic in Resources.FindObjectsOfTypeAll<Graphic>().Where(g =>
                         g != null && g.isActiveAndEnabled &&
                         g.canvas != null &&
                         g.canvasRenderer.GetInheritedAlpha() > 0.001f)
                     .OrderByDescending(g => Mathf.Abs(g.rectTransform.rect.width * g.rectTransform.rect.height)).Take(256))
            {
                var rect = graphic.rectTransform.rect;
                var parent = graphic.transform;
                var path = parent.name;
                while (parent.parent != null) { parent = parent.parent; path = parent.name + "/" + path; }
                var root = graphic.canvas.rootCanvas;
                graphic.rectTransform.GetWorldCorners(corners);
                var viewCamera = root.renderMode == RenderMode.ScreenSpaceOverlay ? null : root.worldCamera;
                var first = RectTransformUtility.WorldToScreenPoint(viewCamera, corners[0]);
                var last = RectTransformUtility.WorldToScreenPoint(viewCamera, corners[2]);
                var shader = graphic.materialForRendering?.shader;
                report.AppendLine($"graphic={path} type={graphic.GetType().Name} size={rect.width}x{rect.height} color={graphic.color} rendererAlpha={graphic.canvasRenderer.GetAlpha()} inheritedAlpha={graphic.canvasRenderer.GetInheritedAlpha()} shader={shader?.name} supported={shader?.isSupported} texture={graphic.mainTexture?.name} camera={viewCamera?.name} screen={first}/{last} culled={graphic.canvasRenderer.cull}");
            }

            var directory = Path.Combine(Paths.BepInExRootPath, "diagnostics", "yapyap-ui");
            Directory.CreateDirectory(directory);
            var name = context + "-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff");
            File.WriteAllBytes(Path.Combine(directory, name + ".png"), pixels.EncodeToPNG());
            File.WriteAllText(Path.Combine(directory, name + ".txt"), report.ToString());
            // Read the already rendered backbuffer; never re-render/reconfigure
            // a stereo camera to obtain the diagnostic game view.
            RenderTexture.active = previous;
            if (previous == null && Screen.width > 0 && Screen.height > 0 &&
                (long)Screen.width * Screen.height <= 4096L * 4096L)
            {
                mirror = new Texture2D(Screen.width, Screen.height, TextureFormat.RGB24, false);
                mirror.ReadPixels(new Rect(0, 0, Screen.width, Screen.height), 0, 0);
                mirror.Apply(false, false);
                File.WriteAllBytes(Path.Combine(directory, name + "-game.png"), mirror.EncodeToPNG());
            }
            Debug.Log($"[AVRcade YAPYAP] {context} alpha capture saved: {Path.Combine(directory, name)}; alphaZero={transparent}/{colors.Length}, opaqueBlack={opaqueBlack}, magenta={magenta}.");
        }
        catch (Exception exception)
        {
            Debug.LogWarning($"[AVRcade YAPYAP] {context} capture failed: {exception.Message}");
        }
        finally
        {
            RenderTexture.active = previous;
            if (pixels != null) UnityEngine.Object.Destroy(pixels);
            if (mirror != null) UnityEngine.Object.Destroy(mirror);
        }
    }

    private static Texture? MainTexture(Material? material)
    {
        if (material == null) return null;
        if (material.HasProperty("_MainTex")) return material.GetTexture("_MainTex");
        return material.HasProperty("_BaseMap") ? material.GetTexture("_BaseMap") : null;
    }

    private static ScriptableRendererData? MainRendererData()
    {
        var asset = GraphicsSettings.currentRenderPipeline as UniversalRenderPipelineAsset;
        var list = asset == null ? null : AccessTools.Field(typeof(UniversalRenderPipelineAsset), "m_RendererDataList")?.GetValue(asset) as ScriptableRendererData[];
        if (list == null) return null;
        var camera = YAPYAP.CameraManager.Instance?.MainCamera ?? Camera.main ??
            Resources.FindObjectsOfTypeAll<Camera>().FirstOrDefault(c => c != null && c.isActiveAndEnabled && c.name == "MainCamera");
        var data = camera == null ? null : camera.GetComponent<UniversalAdditionalCameraData>();
        var index = data == null ? -1 : (AccessTools.Field(typeof(UniversalAdditionalCameraData), "m_RendererIndex")?.GetValue(data) is int actual ? actual : -1);
        if (index < 0) index = AccessTools.Field(typeof(UniversalRenderPipelineAsset), "m_DefaultRendererIndex")?.GetValue(asset) is int fallback ? fallback : 0;
        return index >= 0 && index < list.Length ? list[index] : null;
    }
}

[HarmonyPatch]
internal static class YapyapOcclusionMeshCompatibility
{
    private static readonly FieldInfo? MaskField = AccessTools.Field(typeof(UnityEngine.Experimental.Rendering.XRPass), "m_OcclusionMesh");
    private static readonly FieldInfo? MaterialField = MaskField == null ? null : AccessTools.Field(MaskField.FieldType, "m_Material");
    private static ConfigEntry<bool>? _skipUnsupported;
    private static Shader? _checkedShader;
    private static bool _unsupported;

    internal static void Configure(ConfigFile config)
    {
        _skipUnsupported = config.Bind("Rendering", "SkipUnsupportedXrMask", true,
            "Skip only XR hidden-area mask draws whose shader is unsupported or the Unity error shader. Fixes the confirmed pink square in this non-VR game build; supported mask shaders render normally. Independent of diagnostics and active in menus/gameplay. Skipping the mask may increase GPU work outside the visible lens area.");
        if (MaskField == null || MaterialField == null)
            Debug.LogWarning("[AVRcade YAPYAP] XR mask compatibility unavailable: pipeline field layout changed; native rendering retained.");
    }

    private static IEnumerable<MethodBase> TargetMethods() =>
        typeof(UnityEngine.Experimental.Rendering.XRPass).GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Where(method => method.Name == "RenderOcclusionMesh");

    private static bool Prefix(UnityEngine.Experimental.Rendering.XRPass __instance)
    {
        if (_skipUnsupported?.Value == true && __instance.viewCount > 0)
        {
            var mask = MaskField?.GetValue(__instance);
            var material = mask == null ? null : MaterialField?.GetValue(mask) as Material;
            var shader = material == null ? null : material.shader;
            if (shader != null)
            {
                // The same XR-system shader serves successive eye/camera passes.
                // Cache its support/name test to avoid string reads per draw.
                if (_checkedShader != shader)
                {
                    _checkedShader = shader;
                    _unsupported = !shader.isSupported || shader.name == "Hidden/InternalErrorShader";
                    if (_unsupported)
                        Debug.LogWarning($"[AVRcade YAPYAP] XR mask compatibility: skipping unavailable shader {shader.name}; supported={shader.isSupported}. Scene rendering and stereo tracking remain active.");
                }
                if (_unsupported) return false;
            }
        }
        return YapyapUiDiagnostics.BeforeOcclusionMesh(__instance);
    }
}
