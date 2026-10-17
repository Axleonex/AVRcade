-- AVRcade controller keyboard. Kept at the old helper path to replace autofill.
-- UObject access and text delivery stay on the engine thread; ImGui only draws.
local vr = uevr.params.vr
local rows = {}
for _, line in ipairs({"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-_."}) do
    local row = {}
    for i = 1, #line do row[#row + 1] = line:sub(i, i) end
    rows[#rows + 1] = row
end
rows[5] = {"Shift", "Space", "Backspace", "Clear", "Done"}
local action_widths = {Shift=100, Space=140, Backspace=160, Clear=100, Done=200}
local known = {
    {class = "WidgetBlueprintGeneratedClass /Game/NebulaLobbyUI/UI/UI_Lobby/UI_Menu_Lobby.UI_Menu_Lobby_C", label = "Save game name", automatic = true},
    {class = "WidgetBlueprintGeneratedClass /Game/MainMenu/Widgets/MainMenu/W_NewGame.W_NewGame_C", label = "Save name"},
    {class = "WidgetBlueprintGeneratedClass /Game/MainMenu/Widgets/MainMenu/W_CreateServer.W_CreateServer_C", label = "Lobby name"},
}
local editable = {"EditableTextBox", "EditableText", "MultiLineEditableTextBox", "MultiLineEditableText"}
local active, suppressed, suppressed_prompt, result, font
local temporary, commands, previous = {}, {}, {}
local input_index, drain_index
local input = {buttons = 0, x = 0, y = 0}
local input_age, scan_elapsed, repeat_elapsed = 0, 0, 0
local last_direction, row, column = nil, 2, 1
local uppercase, manual, applied, warned = false, false, false, false
local typed, message = "", ""
local finishing, hint_target, hint_cursor
local modal_target, modal_confirm, resume_after_modal
local modal_failed
local drain_indices={}
local MODAL_ACCEPT="BndEvt__W_FancyModal_Button1_K2Node_ComponentBoundEvent_0_OnClick__DelegateSignature"
local diagnostics, last_diagnostic = {}, nil
local function diagnostic(text)
    if text == last_diagnostic then return end
    last_diagnostic = text
    diagnostics[#diagnostics + 1] = text
    if #diagnostics > 24 then table.remove(diagnostics, 1) end
    -- Record state/errors only; never persist the user's text.
    if fs and fs.write then pcall(fs.write, "avrcade-keyboard-status.txt", table.concat(diagnostics, "\n")) end
end
diagnostic("Keyboard script loaded")

uevr.sdk.callbacks.on_lua_event(function(event, data)
    if event == "avrcade.keyboard.result" then result = data end
    if event == "avrcade.keyboard.finish" and active then commands[#commands + 1] = "Done" end
end)
local function send(action, value)
    result = nil
    uevr.api:dispatch_custom_event("avrcade.keyboard." .. action, value or "")
    if result == "ok" then if action~="begin" then message="" end; return true end
    message = result == "unfocused" and "Focus the game window to type or clear text." or "Key was not sent. Check the game field and try again."
    diagnostic("Transport: " .. tostring(result))
    return false
end
local function visible(field)
    return field and UEVR_UObjectHook.exists(field) and field:IsVisible() and not field.IsReadOnly
end
local function focused(field)
    return visible(field) and (field:HasKeyboardFocus() or field:HasAnyUserFocus())
end
local function prompt_visible(target)
    if not visible(target.field) then return false end
    if target.owner and (not UEVR_UObjectHook.exists(target.owner) or not target.owner:IsVisible()) then return false end
    if target.automatic then
        return target.owner:IsInViewport() and UEVR_UObjectHook.exists(target.wrapper) and target.wrapper:IsVisible()
    end
    return true
end
local function attached(widget)
    if widget:IsInViewport() then return true end
    local parent=widget:GetParent()
    local user_widget=uevr.api:find_uobject("Class /Script/UMG.UserWidget")
    for _=1,12 do
        if not parent or not UEVR_UObjectHook.exists(parent) then return false end
        if user_widget and parent:is_a(user_widget) and parent:IsInViewport() then return true end
        parent=parent:get_outer()
    end
    return false
end
local function find_modal()
    local candidates={}
    for _,name in ipairs({"W_FancyModal","W_FancyRichMessageModal"}) do
        local path="WidgetBlueprintGeneratedClass /Game/UI/Messages/"..name.."."..name.."_C"
        local class=uevr.api:find_uobject(path)
        if class then
            for _,owner in ipairs(UEVR_UObjectHook.get_objects_by_class(class,false)) do
                if UEVR_UObjectHook.exists(owner) and owner:IsVisible() and attached(owner) then
                    local first,second=owner.Button1,owner.Button2
                    local accept=owner~=modal_failed and visible(first) and first:GetIsEnabled() and not visible(second) and
                        owner:get_class():find_function(MODAL_ACCEPT)~=nil
                    candidates[#candidates+1]={owner=owner,accept=accept,modal=true}
                end
            end
        end
    end
    if #candidates==1 then return candidates[1] end
    if #candidates>1 then return {modal=true,accept=false} end
end
local function clear_hint()
    hint_target=nil
    if hint_cursor then vr.set_mod_value("FrameworkConfig_AlwaysShowCursor",hint_cursor); hint_cursor=nil end
end
local function close_keyboard()
    uevr.api:dispatch_custom_event("avrcade.keyboard.end", "")
    if not active then return end
    suppressed = active.field
    suppressed_prompt = active.automatic and active or nil
    active, commands, finishing = nil, {}, nil
    drain_index, input_index = input_index, nil
    for key, value in pairs(temporary) do vr.set_mod_value(key, value) end
    temporary = {}
    print("[AVRcade Keyboard] Closed")
    diagnostic("Keyboard closed")
end
local function open_keyboard(target)
    if not send("begin") then return end
    clear_hint()
    target.field:SetKeyboardFocus()
    target.password = target.field.IsPassword == true
    message=""
    active, suppressed, suppressed_prompt = target, nil, nil
    row, column, uppercase, typed, commands, finishing = 2, 1, false, "", {}, nil
    last_direction, repeat_elapsed, input_age = nil, 0, 1
    input = {buttons = 0, x = 0, y = 0}
    for key, value in pairs({FrameworkConfig_AlwaysShowCursor = "true", VR_MovementOrientation = "0", VR_SnapTurn = "false"}) do
        local old = vr:get_mod_value(key)
        if old and old ~= "" then temporary[key] = old; vr.set_mod_value(key, value) end
    end
    print("[AVRcade Keyboard] Opened: " .. target.label)
    diagnostic("Keyboard opened: " .. target.label)
end
local function find_target(allow_visible, keyboard_only)
    local function selected(field)
        return visible(field) and (keyboard_only and field:HasKeyboardFocus() or not keyboard_only and focused(field))
    end
    local candidates = {}
    for _, target in ipairs(known) do
        local class = uevr.api:find_uobject(target.class)
        if class then
            for _, owner in ipairs(UEVR_UObjectHook.get_objects_by_class(class, false)) do
                if UEVR_UObjectHook.exists(owner) and owner:IsVisible() then
                    local wrapper = target.automatic and owner.Textfield_SaveGame or nil
                    local field
                    if target.automatic then
                        if wrapper and UEVR_UObjectHook.exists(wrapper) then field = wrapper.EditableText_143 end
                    else field = owner.EditableTextBox_Name end
                    local candidate = {field = field, owner = owner, wrapper = wrapper, label = target.label, automatic = target.automatic}
                    if prompt_visible(candidate) then
                        if selected(candidate.field) then return candidate end
                        candidates[#candidates + 1] = candidate
                    end
                end
            end
        end
    end
    for _, name in ipairs(editable) do
        local class = uevr.api:find_uobject("Class /Script/UMG." .. name)
        if class then
            for _, field in ipairs(UEVR_UObjectHook.get_objects_by_class(class, false)) do
                if selected(field) then return {field = field, label = "Text entry"} end
            end
        end
    end
    for _, candidate in ipairs(candidates) do
        if candidate.automatic then return candidate end
    end
    if allow_visible and #candidates == 1 then return candidates[1] end
end
local function choose(key)
    if not active then return end
    if key == "Done" then
        finishing={elapsed=0,ticks=0,committed=false}
        commands={}
    elseif key == "Shift" then uppercase = not uppercase
    else
        active.field:SetKeyboardFocus()
        if key == "Backspace" then
            if send("backspace") then typed = typed:sub(1, -2) end
        elseif key == "Clear" then
            if send("clear") then typed = "" end
        else
            local ch = key == "Space" and " " or (uppercase and key:upper() or key)
            if send("text", ch) then typed = (typed .. ch):sub(-64) end
        end
    end
end
uevr.sdk.callbacks.on_xinput_get_state(function(retval, index, state)
    local gamepad = state.Gamepad
    local buttons = gamepad.wButtons
    local old = previous[index] or 0
    previous[index] = buttons
    local pressed = buttons & (~old)
    -- UEVR default VR mapping: right A=0x1000, right B=0x4000,
    -- left lower=0x2000, left upper=0x8000 (not Xbox face-button labels).
    local shortcut = not active and not modal_target and hint_target and (pressed & 0x8000)~=0
    if shortcut then manual = true end
    if modal_target and modal_target.accept and (pressed & 0x5000)~=0 then modal_confirm=modal_target.owner end
    if active then
        -- Finish works on every polled controller, even if another index owns navigation.
        if (pressed & 0x4000)~=0 and not finishing then commands[#commands+1]="Done" end
        if input_index == nil and (buttons ~= 0 or math.abs(gamepad.sThumbLX) > 8000 or math.abs(gamepad.sThumbLY) > 8000) then input_index = index end
        if input_index == index then
            input = {buttons = buttons, x = gamepad.sThumbLX, y = gamepad.sThumbLY}
            input_age = 0
            if (pressed & 0x2000) ~= 0 then commands[#commands + 1] = "Backspace"
            elseif (pressed & 0x8000) ~= 0 then commands[#commands + 1] = "Shift"
            elseif (pressed & 0x1000) ~= 0 then commands[#commands + 1] = "Selected" end
        end
    end
    if active or (modal_target and modal_target.accept) then drain_indices[index]=true end
    local draining = drain_indices[index] or drain_index==index
    if not active and not (modal_target and modal_target.accept) and draining and buttons == 0 and
        math.abs(gamepad.sThumbLX) < 8000 and math.abs(gamepad.sThumbLY) < 8000 and
        math.abs(gamepad.sThumbRX) < 8000 and math.abs(gamepad.sThumbRY) < 8000 and
        gamepad.bLeftTrigger==0 and gamepad.bRightTrigger==0 then
        drain_indices[index], drain_index, draining = nil, nil, false
    end
    if active or (modal_target and modal_target.accept) or draining or manual or shortcut then
        gamepad.wButtons, gamepad.bLeftTrigger, gamepad.bRightTrigger = 0, 0, 0
        gamepad.sThumbLX, gamepad.sThumbLY, gamepad.sThumbRX, gamepad.sThumbRY = 0, 0, 0, 0
    end
end)
local function engine_tick(delta)
    if not applied then
        for key, value in pairs({UI_Size = "1.000000", UI_Distance = "1.500000", UI_FollowView = "false", UI_Framework_FollowView = "false", VR_DecoupledPitchUIAdjust = "false", FrameworkConfig_EnableL3R3Toggle="false"}) do vr.set_mod_value(key, value) end
        applied = true
    end
    if uevr.params.functions.is_drawing_ui() then
        modal_target,modal_confirm,resume_after_modal=nil,nil,nil; close_keyboard(); clear_hint(); return
    end
    local popup=find_modal()
    if popup then
        if active then resume_after_modal=true; close_keyboard() end
        clear_hint(); modal_target=popup
        hint_target=popup; hint_cursor=vr:get_mod_value("FrameworkConfig_AlwaysShowCursor")
        vr.set_mod_value("FrameworkConfig_AlwaysShowCursor","true")
        if modal_confirm then
            local requested=modal_confirm; modal_confirm=nil
            if popup.accept and popup.owner==requested then
                local ok=pcall(function() popup.owner[MODAL_ACCEPT](popup.owner) end)
                if ok then diagnostic("Message popup acknowledged")
                else modal_failed=popup.owner; popup.accept=false; diagnostic("Use the game's popup button; custom acknowledgement unavailable") end
            end
        end
        return
    elseif modal_target then
        modal_target,modal_confirm,modal_failed=nil,nil,nil; clear_hint()
        if resume_after_modal then suppressed,suppressed_prompt=nil,nil; manual=true end
        resume_after_modal=false
    end
    if active then
        if not prompt_visible(active) or uevr.params.functions.is_drawing_ui() or not send("begin") then close_keyboard(); return end
        -- Every target can lose Slate focus when the overlay is clicked. Wait for
        -- real keyboard focus, rather than treating controller user focus as enough.
        if not active.field:HasKeyboardFocus() then
            local selected=find_target(false,true)
            if selected and selected.field~=active.field and selected.field:HasKeyboardFocus() then
                close_keyboard(); open_keyboard(selected); return
            end
            active.field:SetKeyboardFocus()
        end
        if not active.field:HasKeyboardFocus() then
            for _,key in ipairs(commands) do
                if key=="Done" then message="The field did not accept focus. Unsent keys were discarded."; close_keyboard(); return end
            end
            message="Waiting for the game text field to accept focus."; return
        end
        if finishing then
            finishing.elapsed=finishing.elapsed+delta; finishing.ticks=finishing.ticks+1
            if finishing.elapsed>=0.1 and finishing.ticks>=2 then
                if not finishing.committed then
                    if not send("commit") then finishing=nil; return end
                    finishing={elapsed=0,ticks=0,committed=true}
                else
                    local owner=active.owner
                    close_keyboard()
                    if owner and UEVR_UObjectHook.exists(owner) and owner.Button_Yes and UEVR_UObjectHook.exists(owner.Button_Yes) then owner.Button_Yes:SetKeyboardFocus() end
                end
            end
            return
        end
        input_age = input_age + delta
        local direction
        if input_age < 0.3 then
            if (input.buttons & 1) ~= 0 then direction = "up"
            elseif (input.buttons & 2) ~= 0 then direction = "down"
            elseif (input.buttons & 4) ~= 0 then direction = "left"
            elseif (input.buttons & 8) ~= 0 then direction = "right"
            elseif math.max(math.abs(input.x), math.abs(input.y)) > 16000 then
                if math.abs(input.x) > math.abs(input.y) then direction = input.x > 0 and "right" or "left"
                else direction = input.y > 0 and "up" or "down" end
            end
        end
        repeat_elapsed = repeat_elapsed - delta
        if direction and (direction ~= last_direction or repeat_elapsed <= 0) then
            if direction == "up" then row = (row - 2) % #rows + 1
            elseif direction == "down" then row = row % #rows + 1
            elseif direction == "left" then column = (column - 2) % #rows[row] + 1
            else column = column % #rows[row] + 1 end
            column = math.min(column, #rows[row])
            repeat_elapsed = direction ~= last_direction and 0.35 or 0.12
        end
        last_direction = direction
        -- Deliver one action per engine frame. Done must not steal focus from
        -- characters still in Unreal's deferred Windows/Slate message processing.
        local key=table.remove(commands,1)
        if key then
            choose(key == "Selected" and rows[row][column] or key)
        end
    else
        scan_elapsed = scan_elapsed + delta
        if scan_elapsed < 0.2 and not manual then return end
        scan_elapsed = 0
        if suppressed_prompt then
            if not prompt_visible(suppressed_prompt) then suppressed, suppressed_prompt = nil, nil end
        elseif suppressed and not focused(suppressed) then suppressed = nil end
        local target = find_target(manual)
        if target and (manual or target.field ~= suppressed) and not uevr.params.functions.is_drawing_ui() then open_keyboard(target) end
        if not active then
            clear_hint()
            local available=target or find_target(true)
            if available and available.owner and not uevr.params.functions.is_drawing_ui() then
                hint_target=available
                hint_cursor=vr:get_mod_value("FrameworkConfig_AlwaysShowCursor")
                vr.set_mod_value("FrameworkConfig_AlwaysShowCursor","true")
            end
        end
        manual = false
    end
end
UEVR_UObjectHook.activate()
uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    local ok, err = pcall(engine_tick, delta)
    if not ok then
        close_keyboard()
        clear_hint(); modal_target,modal_confirm,resume_after_modal=nil,nil,nil
        if not warned then print("[AVRcade Keyboard] Error: " .. tostring(err)); diagnostic("Error: " .. tostring(err)); warned = true end
    end
end)
uevr.sdk.callbacks.on_script_reset(function() close_keyboard(); clear_hint(); modal_target,modal_confirm,resume_after_modal,modal_failed=nil,nil,nil,nil end)
uevr.sdk.callbacks.on_frame(function()
    if not active then
        if not hint_target or uevr.params.functions.is_drawing_ui() then return end
        local display=imgui.get_display_size()
        imgui.set_next_window_pos({display.x/2,display.y*0.18},1,{0.5,0.5})
        imgui.set_next_window_size({760,120},1)
        if imgui.begin_window("AVRcade text entry",true,2|4|32|256) then
            if hint_target.modal then
                imgui.text(hint_target.accept and "Message popup: Right A or Right B = OK" or "Choose an option in the game's popup.")
                if hint_target.accept and imgui.button("OK - close popup##modal",{300,46}) then modal_confirm=hint_target.owner end
            else
                imgui.text("Left upper button opens keyboard (Quest Y / Index left B).")
                if imgui.button("Open keyboard##reopen",{220,46}) then manual=true end
            end
            if message~="" then imgui.text(message) end
        end
        imgui.end_window()
        return
    end
    font = font or imgui.load_font(nil, 26)
    local display = imgui.get_display_size()
    imgui.set_next_window_pos({display.x / 2, display.y / 2}, 1, {0.5, 0.5})
    imgui.set_next_window_size({760, 480}, 1)
    if font then imgui.push_font(font) end
    local opened = imgui.begin_window("AVRcade keyboard", true, 2 | 4 | 32 | 256)
    if opened then
        local preview = active.password and string.rep("*", #typed) or typed
        imgui.text(active.label .. "  |  Keys sent: " .. preview:sub(-24))
        imgui.text("Left stick: choose key | Right A: type | Right B: Done")
        imgui.text("Left lower: erase | Left upper: shift")
        if finishing then imgui.text("Finishing text entry...") end
        for r, keys in ipairs(rows) do
            for c, key in ipairs(keys) do
                if c > 1 then imgui.same_line() end
                local selected = r == row and c == column
                if selected then imgui.push_style_color(21, 0xFF458735) end
                local label = key=="Done" and "DONE - Right B" or uppercase and #key == 1 and key:upper() or key
                local width=r==5 and action_widths[key] or 66
                if imgui.button(label .. "##" .. r .. ":" .. c, {width, 46}) and not finishing then commands[#commands + 1] = key end
                if selected then imgui.pop_style_color(1) end
            end
        end
        if message ~= "" then imgui.text(message) end
    else commands[#commands + 1] = "Done" end
    imgui.end_window()
    if font then imgui.pop_font() end
end)
