using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace VrClient.Core.Unreal;

/// <summary>
/// Opt-in RV There Yet? first-person controller-hand prototype. This owns only
/// its own UEVR script and data file; it never edits the imported community
/// profile, game files, or another UEVR script.
/// </summary>
public sealed class RvTrackedHands(string? settingsPath = null, string? profileDirectory = null)
{
    public const string ScriptFileName = "vrclient_rv_tracked_hands.lua";
    private const string DataFileName = "vrclient-rv-hands.json";
    private readonly string _settingsPath = settingsPath ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "rv-tracked-hands.json");
    private readonly string _profileDirectory = profileDirectory ?? Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "UnrealVRMod", "Ride-Win64-Shipping");

    public bool Enabled => Load().Enabled;

    public void SetEnabled(bool enabled)
    {
        var saved = Load();
        Save(saved with { Enabled = enabled });
        // An already-running script must stop immediately. Enabling waits for
        // a verified script sync at the next AVRcade launch.
        if (!enabled && Directory.Exists(_profileDirectory)) WriteData(false);
    }

    /// <summary>Called before AVRcade launches RV, never for other games.</summary>
    public void PrepareForLaunch()
    {
        var saved = Load();
        WriteData(false);
        if (!saved.Enabled) return;

        var destination = Path.Combine(_profileDirectory, "scripts", ScriptFileName);
        var content = Encoding.UTF8.GetBytes(Script);
        var desiredHash = Hash(content);
        if (File.Exists(destination))
        {
            var currentHash = Hash(File.ReadAllBytes(destination));
            if (currentHash == desiredHash)
            {
                WriteData(true);
                return;
            }
            if (saved.InstalledScriptSha256 != currentHash)
                throw new InvalidOperationException(
                    "The RV tracked-hands script was edited outside AVRcade. AVRcade will not replace it; disable tracked hands or review the script first.");
        }
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        var temporary = destination + ".tmp";
        File.WriteAllBytes(temporary, content);
        File.Move(temporary, destination, overwrite: true);
        Save(saved with { InstalledScriptSha256 = desiredHash });
        WriteData(true);
    }

    public string ScriptPath => Path.Combine(_profileDirectory, "scripts", ScriptFileName);

    private void WriteData(bool enabled)
    {
        var destination = Path.Combine(_profileDirectory, "data", DataFileName);
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        var temporary = destination + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(new { enabled }),
            new UTF8Encoding(false));
        File.Move(temporary, destination, overwrite: true);
    }

    private Settings Load()
    {
        if (!File.Exists(_settingsPath)) return new(false, null);
        try
        {
            return JsonSerializer.Deserialize<Settings>(File.ReadAllText(_settingsPath))
                ?? new(false, null);
        }
        catch (JsonException)
        {
            // A damaged preference must never turn an experimental hook on.
            return new(false, null);
        }
    }

    private void Save(Settings settings)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_settingsPath)!);
        var temporary = _settingsPath + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(settings,
            new JsonSerializerOptions { WriteIndented = true }), new UTF8Encoding(false));
        File.Move(temporary, _settingsPath, overwrite: true);
    }

    private static string Hash(byte[] bytes) =>
        Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

    private sealed record Settings(bool Enabled, string? InstalledScriptSha256);

    // Loaded by UEVR only for Ride-Win64-Shipping. The controller anchors
    // provide UE world transforms; KismetMathLibrary converts them into the
    // first-person arm mesh's component space before writing anim IK targets.
    // This remains experimental until the exact target pair and wrist offsets
    // pass a real headset test on the user's game build.
    public static string Script => """
        -- AVRcade experimental RV There Yet? controller-driven first-person hands.
        -- No gameplay input, grabbing, full-body mesh, or network state is changed.
        local vr = uevr.params.vr
        local enabled = false
        local elapsed = 0
        local failure_count = 0
        local failed_session = false
        local last_pawn = nil
        local last_anim = nil
        local pawn_address = nil
        local anim_address = nil
        local anchors = nil
        local target_fields = nil
        local ik_fields = nil

        local function log(message)
            uevr.params.functions.log_info("[AVRCADE-HANDS] " .. message)
        end

        local function suspend_ik()
            if last_anim ~= nil and ik_fields ~= nil then
                for _, field in ipairs(ik_fields) do
                    pcall(function() last_anim:set_property(field, false) end)
                end
            end
        end

        local function clear_ik()
            suspend_ik()
            last_pawn, last_anim, anchors, target_fields, ik_fields = nil, nil, nil, nil, nil
            pawn_address, anim_address = nil, nil
        end

        local function refresh_setting()
            local data = json.load_file("vrclient-rv-hands.json")
            local wanted = type(data) == "table" and data.enabled == true
            if enabled and not wanted then
                suspend_ik()
                log("disabled; normal hand animation restored")
            end
            enabled = wanted and not failed_session
        end

        local function fields_for(anim)
            local targets = {left = {}, right = {}}
            local flags = {}
            local class = anim:get_class()
            for _ = 1, 6 do
                if class == nil then break end
                local field = class:get_child_properties()
                for _ = 1, 256 do
                    if field == nil then break end
                    local name = field:get_fname():to_string()
                    local lower = name:lower()
                    if lower:find("handltarget", 1, true) then
                        targets.left[#targets.left + 1] = name
                    elseif lower:find("handrtarget", 1, true) then
                        targets.right[#targets.right + 1] = name
                    elseif lower:find("usearmik", 1, true) or lower == "use arm ik" then
                        flags[#flags + 1] = name
                    end
                    field = field:get_next()
                end
                class = class:get_super_struct()
            end
            if #targets.left == 0 or #targets.right == 0 or #flags == 0 then
                error("first-person left/right IK targets or enable flag unavailable")
            end
            return targets, flags
        end

        local function create_anchor(pawn, hand)
            local scene_class = uevr.api:find_uobject("Class /Script/Engine.SceneComponent")
            if scene_class == nil then error("SceneComponent class unavailable") end
            UEVR_UObjectHook.activate()
            local component = uevr.api:add_component_by_class(pawn, scene_class:as_class())
            if component == nil then error("controller anchor could not be created") end
            local state = UEVR_UObjectHook.get_or_add_motion_controller_state(component)
            if state == nil then error("controller anchor state unavailable") end
            state:set_hand(hand)
            state:set_permanant(true)
            return component
        end

        local function initialize(pawn, anim)
            local targets, flags = fields_for(anim)
            local math_class = uevr.api:find_uobject("Class /Script/Engine.KismetMathLibrary")
            if math_class == nil then error("KismetMathLibrary unavailable") end
            local math_library = math_class:as_class():get_class_default_object()
            if math_library == nil then error("KismetMathLibrary defaults unavailable") end
            local left = create_anchor(pawn, 0)
            local right = create_anchor(pawn, 1)
            anchors = {left = left, right = right, math = math_library}
            last_pawn, last_anim, target_fields, ik_fields = pawn, anim, targets, flags
            pawn_address, anim_address = pawn:get_address(), anim:get_address()
            log("first-person IK targets armed for local pawn; controller hands are experimental")
        end

        local function update_hands()
            if not vr.is_hmd_active() then suspend_ik(); return end
            if vr.get_left_controller_index() < 0 or vr.get_right_controller_index() < 0 then
                suspend_ik(); return
            end
            local controller = uevr.api:get_player_controller(0)
            local pawn = uevr.api:get_local_pawn(0)
            if controller == nil or pawn == nil or
               controller:get_class():get_short_name():lower():find("frontend", 1, true) or
               pawn:get_class():get_short_name() ~= "BP_FirstPersonCharacter_C" then
                suspend_ik(); return
            end
            local mesh = pawn:get_property("SK_BoxyRider_FPP_Arms_01")
            if mesh == nil then suspend_ik(); return end
            local anim = mesh:GetAnimInstance()
            if anim == nil or anim:get_class():get_short_name() ~= "BPA_BoxyRider_FPP_Arms_C" then
                suspend_ik(); return
            end
            if pawn:get_address() ~= pawn_address or anim:get_address() ~= anim_address or anchors == nil then
                clear_ik()
                initialize(pawn, anim)
            end
            local mesh_world = mesh:K2_GetComponentToWorld()
            local left_world = anchors.left:K2_GetComponentToWorld()
            local right_world = anchors.right:K2_GetComponentToWorld()
            local left_target = anchors.math:MakeRelativeTransform(left_world, mesh_world)
            local right_target = anchors.math:MakeRelativeTransform(right_world, mesh_world)
            if left_target == nil or right_target == nil then error("relative hand transform unavailable") end
            for _, name in ipairs(target_fields.left) do anim:set_property(name, left_target) end
            for _, name in ipairs(target_fields.right) do anim:set_property(name, right_target) end
            for _, name in ipairs(ik_fields) do anim:set_property(name, true) end
            failure_count = 0
        end

        uevr.sdk.callbacks.on_pre_engine_tick(function(_, delta)
            elapsed = elapsed + delta
            if elapsed >= 1 then
                elapsed = 0
                local ok, err = pcall(refresh_setting)
                if not ok then enabled = false; clear_ik(); log("settings error: " .. tostring(err)) end
            end
            if not enabled then return end
            local ok, err = pcall(update_hands)
            if not ok then
                failure_count = failure_count + 1
                clear_ik()
                if failure_count >= 3 then
                    enabled = false
                    failed_session = true
                    log("stopped after rig/API failures: " .. tostring(err))
                end
            end
        end)

        if uevr.sdk.callbacks.on_script_reset ~= nil then
            uevr.sdk.callbacks.on_script_reset(function() clear_ik() end)
        end
        log("loaded; opt-in setting required")
        """;
}
