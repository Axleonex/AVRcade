using HarmonyLib;
using UnityEngine;

namespace Avrcade.YapyapVr;

/// Keep the local player's base inventory HUD visible without consuming input.
/// The trigger still toggles the separate controller inventory selection mode.
internal static class YapyapHud
{
    private static readonly System.Reflection.FieldInfo? OwnerField =
        AccessTools.Field(typeof(YAPYAP.UIInventory), "_playerInventory");
    private static readonly System.Reflection.FieldInfo? AutoHideField =
        AccessTools.Field(typeof(YAPYAP.UIInventory), "_autoHide");
    private static YAPYAP.UIInventory? _inventory;
    private static RectTransform? _panel;
    private static Vector2 _contentCenter;
    private static RectTransform? _health;
    private static Bounds _healthBounds;
    private static Transform? _inventoryRow;
    private static Transform? _healthOrb;
    private static RectTransform? _score;
    private static Bounds _scoreBounds;
    private static RectTransform? _notifications;

    internal static bool IsLocal(YAPYAP.UIInventory ui)
    {
        var pawn = YAPYAP.Pawn.LocalInstance;
        return pawn != null && pawn.isLocalPlayer && !pawn.IsAI && pawn.Inventory != null &&
            OwnerField?.GetValue(ui) as YAPYAP.PawnInventory == pawn.Inventory;
    }

    internal static void Refresh(bool interactive)
    {
        foreach (var ui in Resources.FindObjectsOfTypeAll<YAPYAP.UIInventory>())
        {
            if (ui == null || !ui.gameObject.activeInHierarchy || !IsLocal(ui)) continue;
            if (_inventory != ui)
            {
                _inventory = ui;
                _panel = ui.transform.Find("InventoryPanel") as RectTransform;
                if (_panel != null)
                {
                    // This game's InventoryPanel has zero size and asymmetric
                    // children. Center their bounds, not just its empty origin.
                    var bounds = RectTransformUtility.CalculateRelativeRectTransformBounds(_panel);
                    _contentCenter = bounds.center;
                    _inventoryRow = _panel.Find("Items/Layout/Container/Container (1)");
                    _health = ui.transform.parent?.Find("BottomLeft") as RectTransform;
                    if (_health != null)
                    {
                        _healthBounds = RectTransformUtility.CalculateRelativeRectTransformBounds(_health);
                        _healthOrb = _health.Find("HealthOrb");
                    }
                }
                var score = ui.transform.parent?.parent?.Find("ScoreUI") as RectTransform;
                var decoration = score?.Find("TopRight");
                if (score != null && decoration != null)
                {
                    // Center the visible compass artwork rather than the wide
                    // text/added-score rectangles, which contain empty space.
                    _score = score;
                    _scoreBounds = RectTransformUtility.CalculateRelativeRectTransformBounds(score, decoration);
                    score.anchorMin = new Vector2(0.5f, 1f);
                    score.anchorMax = score.anchorMin;
                    score.anchoredPosition = new Vector2(-_scoreBounds.center.x * score.localScale.x,
                        -20f - _scoreBounds.max.y * score.localScale.y);
                }
                SetInteractive(false);
                if (interactive) SetInteractive(true);
                var subtitles = ui.transform.parent?.parent?.Find("Subtitles") as RectTransform;
                if (subtitles != null)
                {
                    subtitles.anchorMin = new Vector2(0.5f, 1f);
                    subtitles.anchorMax = subtitles.anchorMin;
                    subtitles.anchoredPosition = new Vector2(0f, -210f);
                }
                PlaceTopHud(ui.transform.parent?.parent);
                ui.SetAutoHide(false);
                Debug.Log("[AVRcade YAPYAP] Local inventory HUD stays visible in front of the headset.");
            }
            else if (AutoHideField?.GetValue(ui) is true) ui.SetAutoHide(false);
            CenterNotifications();
            return;
        }
        _inventory = null;
        _panel = null;
        _health = null;
        _inventoryRow = null;
        _healthOrb = null;
        _score = null;
        _notifications = null;
    }

    private static void PlaceTopHud(Transform? canvas)
    {
        var timer = canvas?.Find("UIGameRound/GameTimerUI") as RectTransform;
        var timerBounds = timer != null ? RectTransformUtility.CalculateRelativeRectTransformBounds(timer) : default;
        var timerWidth = timer != null ? timerBounds.size.x * timer.localScale.x : 0f;
        var scoreWidth = _score != null ? _scoreBounds.size.x * _score.localScale.x : 0f;
        var gap = timer != null && _score != null ? 32f : 0f;
        var leftEdge = -(timerWidth + gap + scoreWidth) * 0.5f;
        if (timer != null)
        {
            // Center the combined visible row, including the asymmetric clock
            // frame. Both original corner groups share the same top edge.
            timer.anchorMin = new Vector2(0.5f, 1f);
            timer.anchorMax = timer.anchorMin;
            timer.anchoredPosition = new Vector2(leftEdge - timerBounds.min.x * timer.localScale.x,
                -24f - timerBounds.max.y * timer.localScale.y);
        }
        if (_score != null)
        {
            _score.anchorMin = new Vector2(0.5f, 1f);
            _score.anchorMax = _score.anchorMin;
            _score.anchoredPosition = new Vector2(leftEdge + timerWidth + gap - _scoreBounds.min.x * _score.localScale.x,
                -24f - _scoreBounds.max.y * _score.localScale.y);
        }
        _notifications = canvas?.Find("UIGameRound/TaskMessages") as RectTransform;
        if (_notifications != null)
        {
            var min = _notifications.anchorMin;
            var max = _notifications.anchorMax;
            min.x = max.x = 0.5f;
            _notifications.anchorMin = min;
            _notifications.anchorMax = max;
            var offset = _notifications.anchoredPosition;
            offset.y = -300f; // Below the top HUD and dialogue, inside central view.
            _notifications.anchoredPosition = offset;
        }
    }

    private static void CenterNotifications()
    {
        if (_notifications == null) return;
        Bounds bounds = default;
        var found = false;
        // The pool activates entries later. Recheck on the existing half-second
        // UI refresh, centering their content instead of the right-pivot origin.
        for (var i = 0; i < _notifications.childCount; i++)
        {
            var entry = _notifications.GetChild(i);
            if (!entry.gameObject.activeInHierarchy || entry is not RectTransform) continue;
            var entryBounds = RectTransformUtility.CalculateRelativeRectTransformBounds(_notifications, entry);
            if (!found) { bounds = entryBounds; found = true; }
            else bounds.Encapsulate(entryBounds);
        }
        var center = found ? bounds.center.x : _notifications.rect.center.x;
        var offset = _notifications.anchoredPosition;
        offset.x = -center * _notifications.localScale.x;
        _notifications.anchoredPosition = offset;
    }

    internal static void SetInteractive(bool interactive)
    {
        if (_panel == null) return;
        var anchor = new Vector2(0.5f, interactive ? 0.5f : 0.3f);
        var scale = interactive ? 1.7f : 1.4f;
        _panel.anchorMin = anchor;
        _panel.anchorMax = anchor;
        _panel.localScale = Vector3.one * scale;
        _panel.anchoredPosition = -_contentCenter * scale;
        if (_health != null && _inventoryRow != null && _healthOrb != null)
        {
            // BottomLeft and UIInventory are siblings under the full-screen
            // UIPlayer root. Keep the orb artwork at its original size, with a
            // gap beyond the actual boxes and a shared visible orb/slot center.
            _health.anchorMin = anchor;
            _health.anchorMax = anchor;
            var row = RectTransformUtility.CalculateRelativeRectTransformBounds(_panel, _inventoryRow);
            var orb = RectTransformUtility.CalculateRelativeRectTransformBounds(_health, _healthOrb);
            _health.position = _panel.TransformPoint(new Vector3(row.max.x + 24f, row.center.y, 0f)) -
                _health.TransformVector(new Vector3(_healthBounds.min.x, orb.center.y, 0f));
        }
    }
}

[HarmonyPatch(typeof(YAPYAP.UIInventory), "HideInventory")]
internal static class YapyapPersistentInventory
{
    // Inventory mode closes normally; only its visual auto-hide is bypassed.
    private static bool Prefix(YAPYAP.UIInventory __instance)
    {
        if (!YapyapHud.IsLocal(__instance)) return true;
        __instance.StopDrag();
        return false;
    }
}
