-- AVRcade: world-anchored menu rooms, with normal stereo head tracking.
-- UEVR HEAD aim recenters its reference every frame. BP_MenuActor's fixed camera
-- ignores that control rotation, cancelling scene yaw and rotating the UI anchor.
-- Suspend aim updates for the title camera or an active local menu only.
local vr=uevr.params.vr
local MENU_CLASS="BlueprintGeneratedClass /Game/MainMenu/BP_MenuActor.BP_MenuActor_C"
local menu_classes={}
for _,path in ipairs({
    "SettingsMenu/Widgets/Settings/WB_SettingsPanel_Theme2",
    "NebulaLobbyUI/UI/UI_Lobby/UI_Menu_Lobby", "NebulaLobbyUI/UI/UI_ModeSelection/UI_Menu_ModeSelection",
    "NebulaLobbyUI/UI/UI_Programm/UI_Menu_Programm", "NebulaLobbyUI/UI/UI_Evaluation/UI_Menu_Evaluation",
    "UI/Lobby/Lobby_Menu", "UI/Lobby/Lobby_MapSelection", "UI/Lobby/Lobby_MapSelection_2",
    "UI/Lobby/Lobby_JoinWithCode_Modal", "UI/Lobby/PlayerList/W_PlayerBlockList",
    "UI/Online/W_ReportPlayer_Modal",
    "UI/Messages/W_FancyModal", "UI/Messages/W_FancyRichChoiceModal", "UI/Messages/W_FancyRichMessageModal",
    "MainMenu/Widgets/W_Confirmation", "MainMenu/Widgets/W_FancyConfirmationModal",
    "MainMenu/Widgets/W_ControllerDisconnectNotification",
}) do
    local name=path:match("([^/]+)$")
    menu_classes["WidgetBlueprintGeneratedClass /Game/"..path.."."..name.."_C"]=true
end
for _,name in ipairs({"W_MainMenu","W_CreateServer","W_JoinServer","W_NewGame","W_Map_Selection",
    "W_MainMenu_ServerList","W_AccountPicker","W_EULA","W_Error","W_ErrorModal","W_Kicked"}) do
    menu_classes["WidgetBlueprintGeneratedClass /Game/MainMenu/Widgets/MainMenu/"..name.."."..name.."_C"]=true
end
local INVENTORY_CLASS="WidgetBlueprintGeneratedClass /Game/NewInventorySystem/Inventory/UI/UI_Inventory.UI_Inventory_C"
menu_classes[INVENTORY_CLASS]=true
local widget_class,user_widget_class
local state,failures,stereo_failures,stopped=nil,0,0,false
local function alive(o) return o~=nil and UEVR_UObjectHook.exists(o) end
local function same(a,b) return alive(a) and alive(b) and a:get_address()==b:get_address() end
local function report(s) uevr.params.functions.log_info("[AVRCADE-BACKROOMS-MENU] "..s) end
local function attached_visible(widget,pc)
    -- Child widgets can remain Visible after removal or under a hidden host.
    -- Walk panel parents and WidgetTree ownership to a visible viewport root.
    local current=widget
    for _=1,32 do
        if not alive(current) then return false end
        if current:is_a(widget_class) then
            if not current:IsVisible() then return false end
            if current:is_a(user_widget_class) then
                local owner=current:GetOwningPlayer()
                if alive(owner) and not same(owner,pc) then return false end
                if current:IsInViewport() then return true end
            end
            local parent=current:GetParent()
            if alive(parent) then
                if parent:get_class():get_short_name()=="WidgetSwitcher" and not same(parent:GetActiveWidget(),current) then
                    return false
                end
                current=parent
            else current=current:get_outer() end
        else current=current:get_outer() end
    end
    return false -- Also rejects malformed/cyclic ownership chains.
end
local function foreground_menu(pc)
    if not alive(widget_class) then widget_class=uevr.api:find_uobject("Class /Script/UMG.Widget") end
    if not alive(user_widget_class) then user_widget_class=uevr.api:find_uobject("Class /Script/UMG.UserWidget") end
    if not alive(widget_class) or not alive(user_widget_class) then return false end
    -- UObjectHook indexes subclasses. One native UserWidget query catches newly
    -- created screens immediately without polling every asset class by name.
    for _,owner in ipairs(UEVR_UObjectHook.get_objects_by_class(user_widget_class,false)) do
        if alive(owner) and owner:IsVisible() then
            local kind=owner:get_class():get_full_name()
            -- UI_Inventory also owns the always-visible hotbar/sanity HUD.
            -- Cursor visibility alone does not mean its interactive menu is open.
            local inventory_active=kind~=INVENTORY_CLASS or owner:HasAnyUserFocus() or owner:HasUserFocusedDescendants(pc)
            if menu_classes[kind] and inventory_active and
                same(owner:GetOwningPlayer(),pc) and attached_visible(owner,pc) then return kind end
        end
    end
    return false
end
local function release()
    local previous=state;state=nil
    if previous and previous.owns_aim and not vr.is_aim_allowed() then
        vr.set_aim_allowed(true)
    end
    if previous then report("Menu lock released") end
end
local function valid(n) return type(n)=="number" and n==n and math.abs(n)<10000 end
local function pose()
    local p,q=UEVR_Vector3f.new(),UEVR_Quaternionf.new()
    vr.get_pose(vr.get_hmd_index(),p,q)
    if not (valid(p.x) and valid(p.y) and valid(p.z) and
        valid(q.x) and valid(q.y) and valid(q.z) and valid(q.w)) then return end
    local norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w
    if norm<0.5 or norm>1.5 then return end
    return p
end
local function update()
    if not vr.is_runtime_ready() or not vr.is_hmd_active() then release();return end
    local pc=uevr.api:get_player_controller(0)
    local pawn=uevr.api:get_local_pawn(0)
    if not alive(pc) then release();return end
    local target=pc:GetViewTarget()
    local title=alive(pawn) and pawn:get_class():get_full_name()==MENU_CLASS and same(target,pawn)
    local reason=title and "title-camera" or foreground_menu(pc)
    if not alive(target) or not reason then release();return end
    if state and same(state.pc,pc) and same(state.target,target) then
        if state.reason~=reason then state.reason=reason;report("Menu context: "..reason) end
        -- UObjectHook resets this shared temporary flag after every eye pass.
        if vr.is_aim_allowed() then vr.set_aim_allowed(false) end
        return
    end
    release()
    local p=pose()
    if not p then return end
    state={pc=pc,target=target,reason=reason,owns_aim=vr.is_aim_allowed()}
    if state.owns_aim then vr.set_aim_allowed(false) end
    -- One placement on entry/reconnect. Ordinary head turns never move this anchor.
    vr.set_standing_origin(p)
    vr.recenter_view()
    state.rotation=UEVR_Quaternionf.new()
    vr.get_rotation_offset(state.rotation)
    report("Menu room anchored ["..reason.."]; natural head tracking active, aim recentering suspended")
end
uevr.sdk.callbacks.on_pre_engine_tick(function()
    if stopped then return end
    local ok,err=pcall(update)
    if ok then failures=0;return end
    pcall(release)
    failures=failures+1
    report("Menu camera check failed: "..tostring(err))
    if failures>=3 then stopped=true;report("Menu helper suspended after repeated errors") end
end)
uevr.sdk.callbacks.on_pre_calculate_stereo_view_offset(function()
    if stopped or not state then return end
    local ok,err=pcall(function()
        if not vr.is_runtime_ready() or not vr.is_hmd_active() then release();return end
        -- Hold the captured reference, never the current headset orientation.
        -- Reassert for BOTH eyes: native post-eye processing re-enables aiming,
        -- and the right-eye roomscale pass would otherwise move the title pawn.
        if vr.is_aim_allowed() then vr.set_aim_allowed(false) end
        vr.set_rotation_offset(state.rotation)
    end)
    if ok then stereo_failures=0;return end
    pcall(release);stereo_failures=stereo_failures+1
    report("Menu stereo anchor failed: "..tostring(err))
    if stereo_failures>=3 then stopped=true end
end)
uevr.sdk.callbacks.on_script_reset(function()
    local ok,err=pcall(release)
    if not ok then report("Could not restore aiming: "..tostring(err)) end
    failures,stereo_failures,stopped=0,0,false
end)
