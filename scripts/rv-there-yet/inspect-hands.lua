-- Developer-only read-only capture. Not shipped/installed by the normal launcher.
-- Temporarily place in Ride-Win64-Shipping/scripts, enter solo gameplay, then remove.
-- Logs names of the local pawn's meshes, bones, and animation properties only.
-- No transforms, input, camera, saves, networking, or animation state are changed.
local elapsed, attempts, finished = 0, 0, false
local function emit(message) uevr.params.functions.log_info("[VRCLIENT-HANDS] " .. message) end

local function properties(object, label)
    if object == nil then return end
    emit(label .. " " .. object:get_full_name())
    local class = object:get_class()
    for depth = 1, 6 do
        if class == nil then break end
        emit("class " .. class:get_full_name())
        local field = class:get_child_properties()
        for index = 1, 192 do
            if field == nil then break end
            local name = field:get_fname():to_string()
            if name:lower():match("mesh") or name:lower():match("hand") or
               name:lower():match("arm") or name:lower():match("anim") or
               name:lower():match("ik") or name:lower():match("rig") then
                emit("property " .. name)
            end
            field = field:get_next()
        end
        class = class:get_super_struct()
    end
end

local function belongs_to(object, pawn)
    local outer = object
    for _ = 1, 10 do
        if outer == nil then return false end
        if outer:get_address() == pawn:get_address() then return true end
        outer = outer:get_outer()
    end
    return false
end

local function capture()
    local pawn = uevr.api:get_local_pawn(0)
    if pawn == nil then return false end
    local controller = uevr.api:get_player_controller(0)
    if controller ~= nil and controller:get_class():get_short_name():lower():match("frontend") then
        return false -- a menu preview is not the gameplay rig
    end
    local mesh_class = uevr.api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
    if mesh_class == nil then return false end
    emit("BEGIN capture; verify pawn is gameplay character, not menu preview")
    if controller ~= nil then properties(controller, "controller") end
    properties(pawn, "pawn")
    -- Actor.GetComponentsByClass is not reflected in this shipping build.
    -- UClass matching plus outer ownership is a read-only way to find its meshes.
    local meshes = mesh_class:as_class():get_objects_matching(false)
    local owned = 0
    for _, mesh in ipairs(meshes or {}) do
        if owned >= 16 then break end
        if belongs_to(mesh, pawn) then
            owned = owned + 1
            emit("mesh " .. mesh:get_full_name())
            local ok_count, count = pcall(function() return mesh:GetNumBones() end)
            if ok_count and type(count) == "number" then
                emit("bone-count " .. tostring(count))
                for bone = 0, math.min(count, 256) - 1 do
                    local ok_name, name = pcall(function() return mesh:GetBoneName(bone):to_string() end)
                    if ok_name then emit("bone " .. tostring(bone) .. " " .. name) end
                end
            else
                emit("bone enumeration unavailable: " .. tostring(count))
            end
            local ok_anim, anim = pcall(function() return mesh:GetAnimInstance() end)
            if ok_anim and anim ~= nil then properties(anim, "animation") end
        end
    end
    emit("owned skeletal meshes " .. tostring(owned))
    emit("END capture; no game state modified")
    return true
end

uevr.sdk.callbacks.on_post_engine_tick(function(engine, delta)
    if finished then return end
    elapsed = elapsed + delta
    if elapsed < 5 then return end
    elapsed = 0
    attempts = attempts + 1
    local ok, captured = pcall(capture)
    if not ok then
        emit("capture error: " .. tostring(captured))
        finished = true -- fail closed; no repeated errors each frame
    elseif captured then
        finished = true
    elseif attempts >= 120 then
        emit("Timed out without a local skeletal pawn; reload scripts once in gameplay")
        finished = true
    end
end)

emit("Read-only probe armed; waiting for local skeletal pawn (up to 10 minutes)")
