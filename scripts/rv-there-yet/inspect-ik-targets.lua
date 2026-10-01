-- One-shot, read-only RV rig inspection. No mesh, animation, input, or pose writes.
local elapsed, attempts, captures, finished = 0, 0, 0, false
local function log(message) uevr.params.functions.log_info("[VRCLIENT-IK] " .. message) end

local function scalar(value, name)
    local ok, field = pcall(function() return value:get_property(name) end)
    if not ok or field == nil then
        ok, field = pcall(function() return value[name] end)
    end
    if ok and type(field) == "number" then return field end
    return nil
end

local function vector(value)
    if value == nil then return nil end
    local x = scalar(value, "X") or scalar(value, "x")
    local y = scalar(value, "Y") or scalar(value, "y")
    local z = scalar(value, "Z") or scalar(value, "z")
    if x ~= nil and y ~= nil and z ~= nil then
        return string.format("%.3f,%.3f,%.3f", x, y, z)
    end
    return nil
end

local function describe(value)
    if value == nil then return "nil" end
    if type(value) == "boolean" or type(value) == "number" or type(value) == "string" then
        return type(value) .. "=" .. tostring(value)
    end
    local ok, name = pcall(function() return value:get_full_name() end)
    if ok and name ~= nil then return "object=" .. name end
    ok, name = pcall(function() return value:get_struct():get_full_name() end)
    if ok and name ~= nil then
        local parts = {"struct=" .. name}
        local xyz = vector(value)
        if xyz ~= nil then parts[#parts + 1] = "xyz=" .. xyz end
        for _, key in ipairs({"Translation", "Rotation", "Scale3D"}) do
            local read_ok, field = pcall(function() return value:get_property(key) end)
            if read_ok and field ~= nil then
                local nested = vector(field)
                if nested ~= nil then parts[#parts + 1] = key .. "=" .. nested end
            end
        end
        return table.concat(parts, " ")
    end
    return type(value) .. "=" .. tostring(value)
end

local function inspect(object, label)
    if object == nil then log(label .. " missing"); return end
    log(label .. " " .. object:get_full_name())
    local class = object:get_class()
    for _ = 1, 6 do
        if class == nil then break end
        local field = class:get_child_properties()
        for _ = 1, 256 do
            if field == nil then break end
            local name = field:get_fname():to_string()
            local lower = name:lower()
            if (lower:find("hand", 1, true) or lower:find("arm", 1, true) or
                lower:find("ik", 1, true)) and not lower:find("animgraphnode", 1, true) then
                local ok_type, kind = pcall(function() return field:get_class():get_name() end)
                local ok_value, value = pcall(function() return object:get_property(name) end)
                log(label .. " field=" .. name .. " type=" .. (ok_type and kind or "unknown") ..
                    " value=" .. (ok_value and describe(value) or "unreadable:" .. tostring(value)))
            end
            field = field:get_next()
        end
        class = class:get_super_struct()
    end
end

local function capture()
    local controller = uevr.api:get_player_controller(0)
    if controller == nil or controller:get_class():get_short_name():lower():find("frontend", 1, true) then
        return false
    end
    local pawn = uevr.api:get_local_pawn(0)
    if pawn == nil or pawn:get_class():get_short_name() ~= "BP_FirstPersonCharacter_C" then
        return false
    end
    log("BEGIN solo gameplay read-only capture " .. tostring(captures + 1))
    inspect(pawn, "pawn")
    for _, mesh_name in ipairs({"FirstPersonMesh", "SK_BoxyRider_FPP_Arms_01", "CharacterMesh0"}) do
        local ok_mesh, mesh = pcall(function() return pawn:get_property(mesh_name) end)
        if ok_mesh and mesh ~= nil then
            local ok_anim, anim = pcall(function() return mesh:GetAnimInstance() end)
            if ok_anim then inspect(anim, mesh_name .. " animation") end
        end
    end
    local vr = uevr.params.vr
    log("controller indices left=" .. tostring(vr.get_left_controller_index()) ..
        " right=" .. tostring(vr.get_right_controller_index()) ..
        " hmd-active=" .. tostring(vr.is_hmd_active()))
    for _, device in ipairs({
        {"hmd", vr.get_hmd_index(), vr.get_pose},
        -- This pinned nightly exposes get_pose but not the newer get_grip_pose.
        {"left-controller", vr.get_left_controller_index(), vr.get_pose},
        {"right-controller", vr.get_right_controller_index(), vr.get_pose}
    }) do
        if device[2] ~= nil and device[2] >= 0 then
            local ok, value = pcall(function()
                local position = UEVR_Vector3f.new()
                local rotation = UEVR_Quaternionf.new()
                device[3](device[2], position, rotation)
                return string.format("position=%.3f,%.3f,%.3f rotation=%.3f,%.3f,%.3f,%.3f",
                    position.x, position.y, position.z,
                    rotation.x, rotation.y, rotation.z, rotation.w)
            end)
            log(device[1] .. " " .. (ok and value or "unavailable:" .. tostring(value)))
        end
    end
    log("END capture; no game state modified")
    return true
end

uevr.sdk.callbacks.on_post_engine_tick(function(_, delta)
    if finished then return end
    elapsed = elapsed + delta
    if elapsed < 5 then return end
    elapsed = 0
    attempts = attempts + 1
    local ok, captured = pcall(capture)
    if not ok then
        log("Capture stopped: " .. tostring(captured))
        finished = true
    elseif captured then
        captures = captures + 1
        finished = captures >= 2
    elseif attempts >= 120 then
        log("Timed out waiting for gameplay pawn")
        finished = true
    end
end)

log("Read-only IK target probe armed")
