-- AVRcade Backrooms: disable the flat-screen VHS barrel warp while in VR.
-- Reviewed owned build: BPCharacter_Demo.PP_VCR is a dynamic instance of
-- PP_VCR_3/4; the latter enables Barrel_Distortion_Toggle with intensity 0.2.
-- The game's barrel timeline can rewrite the toggle, so check after game tick.
local PARAM="Barrel_Distortion_Toggle"
local state,failures,stopped=nil,0,false
local last_log,log_count=nil,0
local function alive(o) return o~=nil and UEVR_UObjectHook.exists(o) end
local function same(a,b)
    return alive(a) and alive(b) and a:get_address()==b:get_address()
end
local function report(message)
    if message==last_log or log_count>=20 then return end
    last_log=message; log_count=log_count+1
    uevr.params.functions.log_info("[AVRCADE-BACKROOMS-LENS] "..message)
end
local function value(material)
    local n=material:K2_GetScalarParameterValue(PARAM)
    assert(type(n)=="number" and n==n and math.abs(n)<math.huge,"invalid barrel parameter")
    return n
end
local function cleanup()
    local old=state; state=nil
    if old and alive(old.material) then
        -- Do not overwrite a newer value supplied by the game's own timeline.
        local ok,err=pcall(function()
            if value(old.material)==0 and old.restore~=0 then
                old.material:SetScalarParameterValue(PARAM,old.restore)
            end
        end)
        if not ok then report("Could not restore retired lens instance: "..tostring(err)) end
    end
end
local function reviewed(material)
    if not alive(material) or material:get_class():get_short_name()~="MaterialInstanceDynamic" then return false end
    local parent=material:get_property("Parent")
    for _,name in ipairs({"PP_VCR_3","PP_VCR_4"}) do
        local known=uevr.api:find_uobject("MaterialInstanceConstant /Game/Game/"..name.."."..name)
        if same(parent,known) then return true end
    end
    return false
end
local function update()
    if not uevr.params.vr.is_hmd_active() then cleanup(); return end
    local pawn=uevr.api:get_local_pawn(0)
    if not alive(pawn) or pawn:get_class():get_short_name()~="BPCharacter_Demo_C" then cleanup(); return end
    local material=pawn:get_property("PP_VCR")
    if state and (not same(pawn,state.pawn) or not same(material,state.material)) then cleanup() end
    local fresh=state==nil
    if not state then
        if not reviewed(material) then return end
        local original=value(material)
        state={pawn=pawn,material=material,restore=original}
    end
    local current=value(material)
    if current~=0 then
        state.restore=current
        material:SetScalarParameterValue(PARAM,0)
        assert(value(material)==0,"barrel parameter update was not accepted")
    end
    if fresh then report("Local VHS barrel toggle "..tostring(state.restore).." -> 0 while headset active") end
end
uevr.sdk.callbacks.on_post_engine_tick(function()
    if stopped then return end
    local ok,err=pcall(update)
    if ok then failures=0; return end
    failures=failures+1; cleanup()
    stopped=failures>=3
    report((stopped and "Stopped after three errors: " or "Lens guard suspended: ")..tostring(err))
end)
uevr.sdk.callbacks.on_script_reset(cleanup)
