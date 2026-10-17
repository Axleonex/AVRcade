"""Run the shipped lens guard in Lua; mock only UEVR/Unreal boundaries."""
from pathlib import Path
import unittest
from lupa.lua54 import LuaRuntime

SCRIPT = Path(__file__).resolve().parents[1] / "backrooms_lens.lua"
HARNESS = '''
callbacks, logs, writes, lookup = {}, {}, {}, {}
hmd, next_address = true, 100
function object(kind)
    next_address=next_address+1
    local o={alive=true,kind=kind,address=next_address,props={}}
    function o:get_address() return self.address end
    function o:get_class() return {get_short_name=function() return self.kind end} end
    function o:get_property(k) return self.props[k] end
    return o
end
function make_material(parent,value)
    local m=object("MaterialInstanceDynamic"); m.props.Parent=parent
    m.values={Barrel_Distortion_Toggle=value,Barrel_Intensity=0.2,Sanity=0.75}
    function m:K2_GetScalarParameterValue(k)
        if self.fail_read then error("read failed") end
        return self.values[k]
    end
    function m:SetScalarParameterValue(k,v)
        if self.fail_write then error("write failed") end
        if self.ignore_write then return end
        table.insert(writes,{self,k,v}); self.values[k]=v
    end
    return m
end
base3=object("MaterialInstanceConstant"); base4=object("MaterialInstanceConstant")
lookup["MaterialInstanceConstant /Game/Game/PP_VCR_3.PP_VCR_3"]=base3
lookup["MaterialInstanceConstant /Game/Game/PP_VCR_4.PP_VCR_4"]=base4
material=make_material(base4,1)
pawn=object("BPCharacter_Demo_C"); pawn.props.PP_VCR=material
remote=make_material(base4,1)
uevr={api={},params={vr={},functions={}},sdk={callbacks={}}}
function uevr.api:get_local_pawn(i) assert(i==0); return pawn end
function uevr.api:find_uobject(name) return lookup[name] end
function uevr.params.vr.is_hmd_active() return hmd end
function uevr.params.functions.log_info(s) table.insert(logs,s) end
function uevr.sdk.callbacks.on_post_engine_tick(fn) callbacks.tick=fn end
function uevr.sdk.callbacks.on_script_reset(fn) callbacks.reset=fn end
UEVR_UObjectHook={exists=function(o) return o.alive end}
'''


class LensTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        self.lua.execute(HARNESS)
        self.lua.execute(SCRIPT.read_text(encoding="utf-8"))

    def run_lua(self, code):
        self.lua.execute(code)

    def test_removes_only_local_barrel_warp(self):
        self.run_lua('callbacks.tick(); assert(material.values.Barrel_Distortion_Toggle==0); '
                     'assert(material.values.Barrel_Intensity==0.2 and material.values.Sanity==0.75); '
                     'assert(remote.values.Barrel_Distortion_Toggle==1); assert(#writes==1)')

    def test_game_timeline_cannot_reenable_warp(self):
        self.run_lua('callbacks.tick(); material.values.Barrel_Distortion_Toggle=0.4; '
                     'callbacks.tick(); assert(material.values.Barrel_Distortion_Toggle==0); '
                     'callbacks.reset(); assert(material.values.Barrel_Distortion_Toggle==0.4)')

    def test_does_not_write_every_frame_when_already_zero(self):
        self.run_lua('callbacks.tick(); callbacks.tick(); assert(#writes==1)')

    def test_reset_restores_previous_value(self):
        self.run_lua('callbacks.tick(); callbacks.reset(); '
                     'assert(material.values.Barrel_Distortion_Toggle==1)')

    def test_hmd_inactive_restores_flat_screen_effect(self):
        self.run_lua('hmd=false; callbacks.tick(); assert(#writes==0); hmd=true; callbacks.tick(); '
                     'hmd=false; callbacks.tick(); assert(material.values.Barrel_Distortion_Toggle==1)')

    def test_replaced_material_restores_old_and_guards_new(self):
        self.run_lua('callbacks.tick(); old=material; material=make_material(base3,0.5); '
                     'pawn.props.PP_VCR=material; callbacks.tick(); '
                     'assert(old.values.Barrel_Distortion_Toggle==1); '
                     'assert(material.values.Barrel_Distortion_Toggle==0)')

    def test_missing_and_destroyed_objects_are_safe(self):
        self.run_lua('pawn=nil; callbacks.tick(); assert(#writes==0)')
        self.run_lua('pawn=object("BPCharacter_Demo_C"); pawn.props.PP_VCR=material; '
                     'callbacks.tick(); material.alive=false; callbacks.reset()')

    def test_unsupported_pawn_and_material_are_untouched(self):
        self.run_lua('pawn.kind="OtherGamePawn"; callbacks.tick(); assert(#writes==0); '
                     'pawn.kind="BPCharacter_Demo_C"; material.props.Parent=object("Material"); '
                     'callbacks.tick(); assert(#writes==0)')

    def test_shared_constant_is_never_modified(self):
        self.run_lua('pawn.props.PP_VCR=base4; callbacks.tick(); assert(#writes==0)')

    def test_read_error_and_nonfinite_value_do_not_write(self):
        self.run_lua('material.fail_read=true; callbacks.tick(); assert(#writes==0); '
                     'material.fail_read=false; material.values.Barrel_Distortion_Toggle=0/0; '
                     'callbacks.tick(); assert(#writes==0)')

    def test_error_loop_is_bounded(self):
        self.run_lua('material.fail_read=true; for i=1,20 do callbacks.tick() end; '
                     'assert(#logs<8 and #writes==0); material.fail_read=false; '
                     'callbacks.tick(); assert(#writes==0)')

    def test_failed_write_leaves_game_effect_and_reports_failure(self):
        self.run_lua('material.fail_write=true; callbacks.tick(); '
                     'assert(material.values.Barrel_Distortion_Toggle==1 and #writes==0); '
                     'assert(string.find(logs[#logs],"suspended"))')

    def test_unaccepted_update_is_reported(self):
        self.run_lua('material.ignore_write=true; callbacks.tick(); '
                     'assert(material.values.Barrel_Distortion_Toggle==1); '
                     'assert(string.find(logs[#logs],"not accepted"))')

    def test_reset_preserves_newer_game_value(self):
        self.run_lua('callbacks.tick(); material.values.Barrel_Distortion_Toggle=0.7; '
                     'callbacks.reset(); assert(material.values.Barrel_Distortion_Toggle==0.7)')


if __name__ == "__main__":
    unittest.main()
