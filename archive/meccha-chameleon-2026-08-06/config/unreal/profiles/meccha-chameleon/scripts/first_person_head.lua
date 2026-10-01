-- first_person_head.lua — first-person head-bone camera pin, RELEASED in paint mode.
--
-- Normal play : the VR camera is pinned to the character's head bone (first person)
--               every frame, so you see through the chameleon's eyes.
-- Paint mode  : the pin is RELEASED — the game's own paint camera frames the canvas
--               and you paint like the flat game (user directive 2026-07-13). Head
--               tracking still works; the canvas just stops riding your head.
--
-- TWO values are discovered from the RUNNING game and PRINTED to the UEVR console
-- so you can read them in-headset, then lock them in here:
--   1. HEAD BONE NAME  -> set as CANDIDATES[1] once printed.
--   2. PAINT-MODE SIGNAL -> set PAINT.property (which pawn/controller field turns
--      true / equals a value while painting). Until it is set, is_paint_mode()
--      returns false and the pin stays ON always (today's behavior) — no regression.
--
-- UNVERIFIED: gated on MC injection becoming stable (crash isolation ongoing).
-- Nightly-only feature (Lua API); this game is already on the nightly channel.

local api = uevr.api

-- ── 1. Head bone ─────────────────────────────────────────────────────────────
local CANDIDATES = { "head", "Head", "HEAD", "head_01", "Bip001-Head", "b_head" }

local UP_OFFSET = 8.0        -- cm above the bone pivot (eyes sit above the head bone)
local FORWARD_OFFSET = 12.0  -- cm forward along view yaw, to clear the head model

-- ── 2. Paint-mode signal (fill in after in-headset discovery) ────────────────
-- PAINT.on       : where the flag lives — "pawn" or "controller".
-- PAINT.property : the field name that indicates painting (e.g. "bIsPainting" or
--                  a mode enum like "CurrentState"). nil = never paint mode ->
--                  the pin stays on always (safe default, no regression).
-- PAINT.equals   : nil  -> truthy test (true / non-zero counts as painting);
--                  value -> painting only when the field == this value (e.g. 3).
-- How to find it: inject, open UEVR's UObjectHook browser, inspect the local pawn
-- (and its Controller), start painting, and watch which boolean/enum flips. Or set
-- a best guess in PAINT.property and turn PROBE_STATE on to have that field's value
-- printed to the console whenever it changes.
local PAINT = {
    on = "pawn",
    property = nil,
    equals = nil,
}

local PROBE_STATE = false  -- true => print PAINT.property's value each time it changes

-- ── runtime state ────────────────────────────────────────────────────────────
local head_name = nil
local searched_mesh = nil
local last_probe = nil

local function find_head_name(mesh)
    for _, name in ipairs(CANDIDATES) do
        local ok, exists = pcall(function() return mesh:DoesSocketExist(name) end)
        if ok and exists then return name end
    end
    -- fallback: scan the whole skeleton for anything containing "head"
    local ok, num = pcall(function() return mesh:GetNumBones() end)
    if ok and num ~= nil then
        for i = 0, num - 1 do
            local ok2, bone = pcall(function() return mesh:GetBoneName(i):to_string() end)
            if ok2 and bone ~= nil and bone:lower():find("head", 1, true) then
                return bone
            end
        end
    end
    return nil
end

local function get_mesh(pawn)
    local mesh = pawn.Mesh
    if mesh == nil then
        local ok, m = pcall(function()
            return pawn:GetComponentByClass(
                api:find_uobject("Class /Script/Engine.SkeletalMeshComponent"))
        end)
        if ok then mesh = m end
    end
    return mesh
end

-- The object the paint-mode flag lives on (the pawn, or its controller).
local function get_state_object(pawn)
    if PAINT.on == "controller" then
        local ok, c = pcall(function() return pawn.Controller end)
        if ok then return c end
        return nil
    end
    return pawn
end

-- True while the game is in paint mode. Returns false (pin stays on) until
-- PAINT.property is configured — so an unconfigured profile behaves exactly like
-- the always-on first-person pin.
local function is_paint_mode(pawn)
    if PAINT.property == nil then return false end
    local obj = get_state_object(pawn)
    if obj == nil then return false end

    local ok, value = pcall(function() return obj[PAINT.property] end)
    if not ok then return false end

    if PROBE_STATE and value ~= last_probe then
        last_probe = value
        print("[first_person_head] " .. PAINT.property .. " = " .. tostring(value))
    end

    if PAINT.equals ~= nil then
        return value == PAINT.equals
    end
    -- truthy test: booleans and non-zero numbers count as "painting"
    if type(value) == "boolean" then return value end
    if type(value) == "number" then return value ~= 0 end
    return false
end

uevr.sdk.callbacks.on_early_calculate_stereo_view_offset(
    function(device, view_index, world_to_meters, position, rotation, is_double)
        local pawn = api:get_local_pawn(0)
        if pawn == nil then return end

        -- Paint mode: release the pin. Leaving position/rotation untouched lets the
        -- game's paint camera + normal head tracking present the canvas, like vanilla.
        if is_paint_mode(pawn) then return end

        local mesh = get_mesh(pawn)
        if mesh == nil then return end

        if mesh ~= searched_mesh then
            searched_mesh = mesh
            head_name = find_head_name(mesh)
            print("[first_person_head] head bone: " .. tostring(head_name))
        end
        if head_name == nil then return end

        local ok, pos = pcall(function() return mesh:GetSocketLocation(head_name) end)
        if not ok or pos == nil then return end

        -- rotation is a rotator (x=pitch, y=yaw, z=roll) in degrees
        local yaw = math.rad(rotation.y)
        position.x = pos.x + math.cos(yaw) * FORWARD_OFFSET
        position.y = pos.y + math.sin(yaw) * FORWARD_OFFSET
        position.z = pos.z + UP_OFFSET
    end)
