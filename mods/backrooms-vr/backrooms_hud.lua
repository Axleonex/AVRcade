-- AVRcade: inward placement for reviewed Backrooms gameplay HUD groups.
-- Center complete visual groups and modestly scale their contents. Preserve
-- layout dimensions, visibility, centered sights, interactive panels and menus.
local adapters={
    {path="UI/InGame_UI.InGame_UI_C",widgets={Sanity=true}},
    {path="VoiceChat/Widgets/W_VOIPInGame.W_VOIPInGame_C",widgets={W_VOIPStatus=true}},
    {path="HorrorAI/UI/W_MainHUD.W_MainHUD_C",widgets={W_ProgressBars=true}},
}
local DATA="avrcade-backrooms-hud.json"
local DEFAULT_INSET,DEFAULT_SCALE=0.40,0.85
local settings={enabled=true,inset=DEFAULT_INSET,scale=DEFAULT_SCALE}
local last_preferences=nil
local tracked,elapsed,failures,stopped={},1,0,false
local last_log,log_count=nil,0
local function alive(o) return o~=nil and UEVR_UObjectHook.exists(o) end
local function same(a,b) return alive(a) and alive(b) and a:get_address()==b:get_address() end
local function report(message)
    if message==last_log or log_count>=24 then return end
    last_log=message;log_count=log_count+1
    uevr.params.functions.log_info("[AVRCADE-BACKROOMS-HUD] "..message)
end
local function number(n)
    assert(type(n)=="number" and n==n and math.abs(n)<math.huge,"invalid HUD layout number")
    return n
end
local function vec(p) return {X=number(p.X),Y=number(p.Y)} end
local function layout(slot)
    local d=slot:GetLayout();local a,o=d.Anchors,d.Offsets
    -- Deep copy reflected structs; none of the saved data borrows engine memory.
    return {Anchors={Minimum=vec(a.Minimum),Maximum=vec(a.Maximum)},
        Offsets={Left=number(o.Left),Top=number(o.Top),Right=number(o.Right),Bottom=number(o.Bottom)},
        Alignment=vec(d.Alignment)}
end
local function write_layout(slot,d)
    -- SetLayout(const FAnchorData&) is flagged as an out/reference parameter.
    -- Pinned UEVR skips a plain Lua table there as an output placeholder.
    -- Populate the real reflected FAnchorData returned by GetLayout instead.
    local native=slot:GetLayout()
    native.Anchors=d.Anchors;native.Offsets=d.Offsets;native.Alignment=d.Alignment
    slot:SetLayout(native)
end
local function equal(a,b)
    if type(a)~=type(b) then return false end
    if type(a)=="number" then return math.abs(a-b)<0.0001 end
    if type(a)~="table" then return a==b end
    for k,v in pairs(a) do if not equal(v,b[k]) then return false end end
    for k in pairs(b) do if a[k]==nil then return false end end
    return true
end
local function render_scale(widget) return vec(widget:get_property("RenderTransform").Scale) end
local function write_scale(widget,value)
    -- Use an actual reflected FVector2D for the const-reference setter input.
    local native=widget:GetDesiredSize();native.X=value.X;native.Y=value.Y
    widget:SetRenderScale(native)
end
local function restore(entry)
    local ok,err=pcall(function()
        if alive(entry.slot) and entry.applied and equal(layout(entry.slot),entry.applied) then
            write_layout(entry.slot,entry.original)
        end
        if alive(entry.widget) and entry.applied_scale and equal(render_scale(entry.widget),entry.applied_scale) then
            write_scale(entry.widget,entry.original_scale)
        end
    end)
    if not ok then report("Could not restore retired HUD slot: "..tostring(err)) end
end
local function cleanup()
    for _,entry in pairs(tracked) do restore(entry) end
    tracked={}
end
local function load_settings()
    -- fs.read returns an empty string for a missing file. json.load_file logs
    -- parse errors on that same optional path, even inside pcall.
    local ok,raw=pcall(fs.read,DATA)
    if not ok then report("HUD preferences unreadable; keeping last valid placement");return end
    if raw==last_preferences then return end
    last_preferences=raw
    if raw=="" then settings={enabled=true,inset=DEFAULT_INSET,scale=DEFAULT_SCALE};return end
    local parsed,data=pcall(json.load_string,raw)
    if not parsed or data==nil then report("Invalid HUD preferences; keeping last valid placement");return end
    if type(data)~="table" or (data.enabled~=nil and type(data.enabled)~="boolean") or
        (data.inset~=nil and (type(data.inset)~="number" or data.inset~=data.inset or data.inset<0 or data.inset>0.45)) or
        (data.scale~=nil and (type(data.scale)~="number" or data.scale~=data.scale or data.scale<0.75 or data.scale>1.25)) then
        report("Invalid HUD preferences; inset must be 0 to 0.45 and scale 0.75 to 1.25");return
    end
    settings={enabled=data.enabled~=false,inset=data.inset or DEFAULT_INSET,scale=data.scale or DEFAULT_SCALE}
end
local function compressed(d,widget,original_scale)
    local a,o=d.Anchors,d.Offsets
    -- Stretched slots may contain full-screen effects or interactive layouts.
    if not equal(a.Minimum,a.Maximum) then return nil end
    if a.Minimum.X<0 or a.Minimum.X>1 or a.Minimum.Y<0 or a.Minimum.Y>1 then return nil end
    local scale=1-2*settings.inset
    local x,y=0.5+(a.Minimum.X-0.5)*scale,0.5+(a.Minimum.Y-0.5)*scale
    local pivot=vec(widget:get_property("RenderTransformPivot"))
    local desired=widget:GetDesiredSize()
    local auto=widget:get_property("Slot"):get_property("bAutoSize")
    local width=(auto or o.Right<=0) and number(desired.X) or o.Right
    local height=(auto or o.Bottom<=0) and number(desired.Y) or o.Bottom
    -- Contract the actual visual center, including non-centered alignment and
    -- render pivots. Moving anchors alone leaves wide groups toward the edges.
    local function offset(old,alignment,p,k,size)
        local original_center=p-alignment+k*(0.5-p)
        local scaled_center=p-alignment+k*settings.scale*(0.5-p)
        return old*scale+(scale*original_center-scaled_center)*size
    end
    return {Anchors={Minimum={X=x,Y=y},Maximum={X=x,Y=y}},
        Offsets={Left=offset(o.Left,d.Alignment.X,pivot.X,original_scale.X,width),
            Top=offset(o.Top,d.Alignment.Y,pivot.Y,original_scale.Y,height),Right=o.Right,Bottom=o.Bottom},
        Alignment={X=d.Alignment.X,Y=d.Alignment.Y}}
end
local function adjust(slot,widget,seen,label)
    local id=slot:get_address();local current=layout(slot);local current_scale=render_scale(widget);local entry=tracked[id]
    if entry and not same(entry.widget,widget) then restore(entry);tracked[id]=nil;entry=nil end
    if not entry then entry={slot=slot,widget=widget,original=current,original_scale=current_scale};tracked[id]=entry
    elseif not equal(current,entry.applied) then entry.original=current end
    if entry.applied_scale and not equal(current_scale,entry.applied_scale) then entry.original_scale=current_scale end
    local desired=compressed(entry.original,widget,entry.original_scale)
    if not desired then restore(entry);tracked[id]=nil;return end
    seen[id]=true
    local desired_scale={X=entry.original_scale.X*settings.scale,Y=entry.original_scale.Y*settings.scale}
    if equal(current,desired) and equal(current_scale,desired_scale) then return end
    entry.applied,entry.applied_scale=desired,desired_scale -- Allow cleanup after a rejected setter.
    if not equal(current,desired) then write_layout(slot,desired) end
    if not equal(current_scale,desired_scale) then write_scale(widget,desired_scale) end
    local actual=layout(slot)
    assert(equal(actual,desired),"HUD layout update was not accepted")
    assert(equal(render_scale(widget),desired_scale),"HUD render scale was not accepted")
    entry.applied=actual
    report(string.format("Centered %s at %.3f, %.3f; group scale %.2f",label,
        actual.Anchors.Minimum.X,actual.Anchors.Minimum.Y,settings.scale))
end
local function update()
    load_settings()
    if not settings.enabled or settings.inset==0 or not uevr.params.vr.is_hmd_active() then cleanup();return end
    local pc=uevr.api:get_player_controller(0)
    if not alive(pc) then cleanup();return end
    local seen={}
    for _,adapter in ipairs(adapters) do
        local class=uevr.api:find_uobject("WidgetBlueprintGeneratedClass /Game/"..adapter.path)
        if class then
            for _,owner in ipairs(UEVR_UObjectHook.get_objects_by_class(class,false)) do
                if alive(owner) and owner:IsInViewport() and owner:IsVisible() and same(owner:GetOwningPlayer(),pc) then
                    local tree=owner:get_property("WidgetTree")
                    local root=alive(tree) and tree:get_property("RootWidget") or nil
                    if alive(root) and root:get_class():get_short_name()=="CanvasPanel" then
                        local count=root:GetChildrenCount()
                        assert(count>=0 and count<=64,"unexpected gameplay HUD hierarchy")
                        for i=0,count-1 do
                            local widget=root:GetChildAt(i)
                            if alive(widget) and adapter.widgets[widget:get_fname():to_string()] then
                                local slot=widget:get_property("Slot")
                                if alive(slot) and slot:get_class():get_short_name()=="CanvasPanelSlot" then
                                    adjust(slot,widget,seen,widget:get_fname():to_string())
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    for id,entry in pairs(tracked) do
        if not seen[id] then restore(entry);tracked[id]=nil end
    end
end
uevr.sdk.callbacks.on_post_engine_tick(function(_,delta)
    if stopped then return end
    elapsed=elapsed+(delta or 0)
    if elapsed<0.25 then return end
    elapsed=0
    local ok,err=pcall(update)
    if ok then failures=0;return end
    failures=failures+1;cleanup();stopped=failures>=3
    report((stopped and "Stopped after three errors: " or "HUD adjustment suspended: ")..tostring(err))
end)
uevr.sdk.callbacks.on_script_reset(cleanup)
