-- RV There Yet? frontend-only UEVR camera lift. Does not move the pawn or menu.
-- Keep gameplay's saved camera offset intact when leaving the frontend.
local vr = uevr.params.vr
local offset_key = "VR_CameraUpOffset"
local menu_lift = 145 -- Unreal units (centimeters); headset calibration may need adjustment.
local elapsed, frontend, base_offset, applied_offset, disabled = 0, false, nil, nil, false
local last_controller_name = nil
local function log(message) uevr.params.functions.log_info("[VRCLIENT-MENU] " .. message) end

local function update()
    if not vr.is_runtime_ready() then return end
    local controller = uevr.api:get_player_controller(0)
    if controller == nil then return end -- avoid bouncing the camera during a map load
    local name = controller:get_class():get_short_name():lower()
    if name ~= last_controller_name then
        log("Controller class: " .. name)
        last_controller_name = name
    end
    local in_frontend = name:find("frontend", 1, true) ~= nil or
                        name:find("front_end", 1, true) ~= nil
    if in_frontend == frontend then return end

    local current = tonumber(vr:get_mod_value(offset_key))
    if current == nil then
        log("Camera offset unavailable; leaving view unchanged")
        return
    end

    if in_frontend then
        -- A script reload or a profile saved while in the menu can leave our
        -- exact lift in the live value. Do not stack another 145 cm on it.
        base_offset = math.abs(current - menu_lift) < 0.1 and 0 or current
        applied_offset = base_offset + menu_lift
        vr.set_mod_value(offset_key, tostring(applied_offset))
        frontend = true
        log("Raised frontend view by " .. menu_lift .. " cm (" .. name .. ")")
    else
        if frontend and applied_offset ~= nil and math.abs(current - applied_offset) < 0.1 then
            vr.set_mod_value(offset_key, tostring(base_offset))
            log("Restored gameplay camera offset to " .. tostring(base_offset))
        end
        frontend, base_offset, applied_offset = false, nil, nil
    end
end

uevr.sdk.callbacks.on_post_engine_tick(function(_, delta)
    if disabled then return end
    elapsed = elapsed + delta
    if elapsed < 0.25 then return end
    elapsed = 0
    local ok, err = pcall(update)
    if not ok then
        disabled = true
        log("Height adjustment disabled: " .. tostring(err))
    end
end)

log("Frontend-only height adjustment armed")
