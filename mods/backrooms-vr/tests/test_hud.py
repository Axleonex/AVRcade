"""Exercise the real HUD adapter against UEVR boundaries and authored layouts."""
from pathlib import Path
import unittest
from lupa.lua54 import LuaRuntime

SCRIPT = Path(__file__).resolve().parents[1] / "backrooms_hud.lua"
HARNESS = '''
callbacks,objects,logs,writes,scale_writes={}, {}, {}, {}, {}
hmd,settings,next_address=true,{enabled=true,inset=0.20},100
local struct_identity={}
parse_count,file_load_count=0,0
function clone(t)
    if type(t)~="table" then return t end
    local r={}; for k,v in pairs(t) do r[k]=clone(v) end; return r
end
function object(kind,name)
    next_address=next_address+1
    local o={alive=true,kind=kind,name=name or kind,address=next_address,props={
        RenderTransform={Scale={X=1,Y=1},Translation={X=0,Y=0},Angle=0},RenderTransformPivot={X=.5,Y=.5}}}
    function o:get_address() return self.address end
    function o:get_class() return {get_short_name=function() return self.kind end} end
    function o:get_fname() return {to_string=function() return self.name end} end
    function o:get_property(k) return self.props[k] end
    function o:GetDesiredSize() return setmetatable(clone(self.desired_size or {X=128,Y=128}),struct_identity) end
    function o:SetRenderScale(v)
        assert(getmetatable(v)==struct_identity,"render scale needs native FVector2D")
        if self.ignore_scale then return end
        self.props.RenderTransform.Scale={X=v.X,Y=v.Y};scale_writes[#scale_writes+1]=self
    end
    return o
end
pc=object("PlayerController")
function make_slot(x,y,left,top,width,height,ax,ay)
    local s=object("CanvasPanelSlot")
    s.layout={Anchors={Minimum={X=x,Y=y},Maximum={X=x,Y=y}},
        Offsets={Left=left,Top=top,Right=width,Bottom=height},Alignment={X=ax or 0.5,Y=ay or 0.5}}
    function s:GetLayout()
        if self.fail_read then error("read failed") end
        return setmetatable(clone(self.layout),struct_identity)
    end
    function s:SetLayout(v)
        if self.fail_write then error("write failed") end
        if self.ignore_write then return end
        -- The pinned UEVR binding treats a plain table passed to a const-ref
        -- struct parameter as an output placeholder, leaving input zeroed.
        assert(getmetatable(v)==struct_identity,"plain table skipped as const-ref input")
        self.layout=clone(v); table.insert(writes,self)
    end
    return s
end
function make_hud(path,name,slot)
    local w=object("Overlay",name); w.props.Slot=slot
    local crosshair=object("Image","CrosshairImage");crosshair.props.Slot=make_slot(0.5,0.5,0,0,16,16)
    local canvas=object("CanvasPanel");canvas.children={w,crosshair}
    function canvas:GetChildrenCount() return #self.children end
    function canvas:GetChildAt(i) return self.children[i+1] end
    local tree=object("WidgetTree");tree.props.RootWidget=canvas
    local owner=object("UserWidget");owner.props.WidgetTree=tree;owner.viewport=true;owner.visible=true;owner.pc=pc
    function owner:IsInViewport() return self.viewport end
    function owner:IsVisible() return self.visible end
    function owner:GetOwningPlayer() return self.pc end
    objects[path]=objects[path] or {};table.insert(objects[path],owner)
    return owner,canvas,crosshair
end
local prefix="WidgetBlueprintGeneratedClass /Game/"
sanity_slot=make_slot(1,1,-100,-300,128,128)
hud,canvas,crosshair=make_hud(prefix.."UI/InGame_UI.InGame_UI_C","Sanity",sanity_slot)
voice_slot=make_slot(0,0,50,50,0,0)
voice=make_hud(prefix.."VoiceChat/Widgets/W_VOIPInGame.W_VOIPInGame_C","W_VOIPStatus",voice_slot)
bars_slot=make_slot(0,1,0,0,300,200,0,1)
bars=make_hud(prefix.."HorrorAI/UI/W_MainHUD.W_MainHUD_C","W_ProgressBars",bars_slot)
uevr={api={},params={vr={},functions={}},sdk={callbacks={}}}
function uevr.api:get_player_controller(i) assert(i==0);return pc end
function uevr.api:find_uobject(path) return objects[path] and path or nil end
function uevr.params.vr.is_hmd_active() return hmd end
function uevr.params.functions.log_info(s) table.insert(logs,s) end
function uevr.sdk.callbacks.on_post_engine_tick(fn) callbacks.tick=fn end
function uevr.sdk.callbacks.on_script_reset(fn) callbacks.reset=fn end
UEVR_UObjectHook={exists=function(o) return o.alive end,
    get_objects_by_class=function(c,defaults) assert(defaults==false);return objects[c] or {} end}
fs={read=function()
    if read_error then error("read failed") end
    return settings_raw or (settings and tostring(settings) or "")
end}
json={load_file=function() file_load_count=file_load_count+1;return settings end,
    load_string=function(raw) parse_count=parse_count+1;return settings_raw and nil or settings end}
function tick() callbacks.tick(nil,0.3) end
function near(a,b) assert(math.abs(a-b)<0.0001,tostring(a).." != "..tostring(b)) end
'''


class HudTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        self.lua.execute(HARNESS)
        self.lua.execute(SCRIPT.read_text(encoding="utf-8"))

    def run_lua(self, code):
        self.lua.execute(code)

    def test_authored_edges_move_inward_without_shrinking(self):
        self.run_lua('tick(); near(sanity_slot.layout.Anchors.Minimum.X,0.8); '
                     'near(sanity_slot.layout.Offsets.Left,-60); near(sanity_slot.layout.Offsets.Top,-180); '
                     'assert(sanity_slot.layout.Offsets.Right==128 and sanity_slot.layout.Offsets.Bottom==128); '
                     'near(voice_slot.layout.Anchors.Minimum.X,0.2); near(voice_slot.layout.Offsets.Left,30); '
                     'near(bars_slot.layout.Anchors.Minimum.X,0.2); near(bars_slot.layout.Anchors.Minimum.Y,0.8); '
                     'assert(bars_slot.layout.Alignment.X==0 and bars_slot.layout.Alignment.Y==1); '
                     'assert(bars_slot.layout.Offsets.Right==300)')

    def test_crosshair_and_unknown_widgets_are_untouched(self):
        self.run_lua('unknown=object("Image","UnknownMarker");unknown.props.Slot=make_slot(0,0,0,0,30,30); '
                     'table.insert(canvas.children,unknown);tick(); '
                     'assert(crosshair.props.Slot.layout.Anchors.Minimum.X==0.5); '
                     'assert(unknown.props.Slot.layout.Anchors.Minimum.X==0);assert(#writes==3)')

    def test_repeated_ticks_never_accumulate_inset(self):
        self.run_lua('for i=1,20 do tick() end;near(sanity_slot.layout.Anchors.Minimum.X,0.8);assert(#writes==3)')

    def test_headset_inactive_restores_original(self):
        self.run_lua('tick();hmd=false;tick();assert(sanity_slot.layout.Anchors.Minimum.X==1); '
                     'assert(sanity_slot.layout.Offsets.Top==-300)')

    def test_settings_disable_and_reset_restore_original(self):
        self.run_lua('tick();settings={enabled=false};tick();assert(sanity_slot.layout.Anchors.Minimum.X==1); '
                     'settings={enabled=true,inset=0.1};tick();near(sanity_slot.layout.Anchors.Minimum.X,0.9); '
                     'callbacks.reset();assert(sanity_slot.layout.Anchors.Minimum.X==1)')

    def test_changed_inset_uses_original_not_previous_adjustment(self):
        self.run_lua('tick();settings={inset=0.3};tick();near(sanity_slot.layout.Anchors.Minimum.X,0.7); '
                     'near(sanity_slot.layout.Offsets.Left,-40)')

    def test_foreign_layout_is_preserved_on_reset(self):
        self.run_lua('tick();sanity_slot.layout.Offsets.Left=123;callbacks.reset(); '
                     'assert(sanity_slot.layout.Offsets.Left==123)')

    def test_detached_hidden_and_remote_huds_are_untouched(self):
        self.run_lua('hud.viewport=false;voice.visible=false;bars.pc=object("RemoteController");tick();assert(#writes==0)')

    def test_removing_widget_restores_its_slot(self):
        self.run_lua('tick();canvas.children={crosshair};tick(); '
                     'assert(sanity_slot.layout.Anchors.Minimum.X==1)')

    def test_replaced_slot_is_initialized_and_previous_restored(self):
        self.run_lua('tick();old=sanity_slot;sanity_slot=make_slot(1,1,-100,-300,128,128); '
                     'canvas.children[1].props.Slot=sanity_slot;tick(); '
                     'assert(old.layout.Anchors.Minimum.X==1);near(sanity_slot.layout.Anchors.Minimum.X,0.8)')

    def test_stretched_or_wrong_slots_are_untouched(self):
        self.run_lua('sanity_slot.layout.Anchors.Maximum.X=0.5;voice_slot.kind="OverlaySlot";tick();assert(#writes==1)')

    def test_invalid_settings_keep_last_valid_values(self):
        self.run_lua('tick();settings={inset=0/0};tick();near(sanity_slot.layout.Anchors.Minimum.X,0.8); '
                     'settings={inset=0.9};tick();near(sanity_slot.layout.Anchors.Minimum.X,0.8)')

    def test_destroyed_objects_and_missing_controller_are_safe(self):
        self.run_lua('tick();sanity_slot.alive=false;pc=nil;tick();callbacks.reset()')

    def test_repeated_errors_stop_and_restore_previous_changes(self):
        self.run_lua('voice_slot.fail_read=true;for i=1,10 do tick() end; '
                     'assert(sanity_slot.layout.Anchors.Minimum.X==1);voice_slot.fail_read=false; '
                     'tick();assert(sanity_slot.layout.Anchors.Minimum.X==1);assert(#logs<15)')

    def test_failed_setter_leaves_original_layout(self):
        self.run_lua('sanity_slot.fail_write=true;tick(); '
                     'assert(sanity_slot.layout.Anchors.Minimum.X==1); '
                     'assert(string.find(logs[#logs],"suspended"))')

    def test_unaccepted_write_is_reported(self):
        self.run_lua('sanity_slot.ignore_write=true;tick(); '
                     'assert(sanity_slot.layout.Anchors.Minimum.X==1); '
                     'assert(string.find(logs[#logs],"not accepted"))')

    def test_missing_settings_use_much_more_compact_default_without_json_file_errors(self):
        self.run_lua('settings=nil;tick();near(sanity_slot.layout.Anchors.Minimum.X,0.60); '
                     'near(sanity_slot.layout.Offsets.Left,-20);near(sanity_slot.layout.Offsets.Top,-60); '
                     'assert(sanity_slot.layout.Offsets.Right==128); '
                     'assert(file_load_count==0 and parse_count==0); '
                     'for i=1,8 do tick() end;assert(file_load_count==0 and parse_count==0)')

    def test_unchanged_settings_are_parsed_once_and_malformed_json_keeps_valid_layout(self):
        self.run_lua('for i=1,10 do tick() end;assert(parse_count==1 and file_load_count==0); '
                     'settings_raw="{broken";tick();near(sanity_slot.layout.Anchors.Minimum.X,0.8); '
                     'for i=1,10 do tick() end;assert(parse_count==2)')

    def test_preference_read_failure_keeps_last_valid_layout(self):
        self.run_lua('tick();read_error=true;tick();near(sanity_slot.layout.Anchors.Minimum.X,0.8)')

    def test_default_scales_complete_groups_and_restores_on_exit(self):
        self.run_lua('settings=nil;tick();local w=canvas.children[1];near(w.props.RenderTransform.Scale.X,.85); '
                     'for i=1,20 do tick() end;near(w.props.RenderTransform.Scale.Y,.85); '
                     'hmd=false;tick();near(w.props.RenderTransform.Scale.X,1);near(sanity_slot.layout.Offsets.Left,-100)')

    def test_visual_group_centers_include_asymmetric_slot_alignment(self):
        self.run_lua('settings=nil;tick();near(bars_slot.layout.Offsets.Left,-120);near(bars_slot.layout.Offsets.Top,80); '
                     'near(1920*bars_slot.layout.Anchors.Minimum.X+bars_slot.layout.Offsets.Left+150,798); '
                     'near(1080*bars_slot.layout.Anchors.Minimum.Y+bars_slot.layout.Offsets.Top-100,628)')

    def test_scale_change_uses_original_transform_and_keeps_other_transform_fields(self):
        self.run_lua('local w=canvas.children[1];w.props.RenderTransform.Scale={X=1.2,Y=.8}; '
                     'w.props.RenderTransform.Translation={X=7,Y=9};w.props.RenderTransform.Angle=12; '
                     'settings={inset=.4,scale=.9};tick();near(w.props.RenderTransform.Scale.X,1.08); '
                     'near(w.props.RenderTransform.Scale.Y,.72);settings={inset=.4,scale=1};tick(); '
                     'near(w.props.RenderTransform.Scale.X,1.2);assert(w.props.RenderTransform.Angle==12); '
                     'assert(w.props.RenderTransform.Translation.X==7)')

    def test_foreign_scale_is_preserved_on_reset(self):
        self.run_lua('tick();local w=canvas.children[1];w.props.RenderTransform.Scale={X=1.25,Y=1.25}; '
                     'callbacks.reset();near(w.props.RenderTransform.Scale.X,1.25)')

    def test_invalid_scale_keeps_last_valid_preferences(self):
        self.run_lua('settings={inset=.4,scale=.9};tick();settings={inset=.4,scale=.1};tick(); '
                     'near(canvas.children[1].props.RenderTransform.Scale.X,.9);settings={scale=0/0};tick(); '
                     'near(canvas.children[1].props.RenderTransform.Scale.X,.9)')

    def test_rejected_render_scale_restores_owned_layout(self):
        self.run_lua('canvas.children[1].ignore_scale=true;tick(); '
                     'near(sanity_slot.layout.Anchors.Minimum.X,1);assert(string.find(logs[#logs],"not accepted"))')

    def test_custom_render_pivot_keeps_visual_center_in_central_region(self):
        self.run_lua('local w=canvas.children[1];w.props.RenderTransformPivot={X=0,Y=0}; '
                     'settings={inset=.4,scale=.85};tick(); '
                     'local x=1920*.6+sanity_slot.layout.Offsets.Left-64+.85*64;near(x,1132)')


if __name__ == "__main__":
    unittest.main()
