"""Exercise the shipped Lua driver, including pose math and cleanup, without a headset."""
from pathlib import Path
import math
import random
import unittest
from lupa.lua54 import LuaRuntime

SCRIPT = Path(__file__).resolve().parents[1] / "backrooms_hands.lua"

HARNESS = r'''
callbacks, created, removed, logs = {}, {}, {}, {}
settings = {enabled=true}; hmd, controllers, missing_bone = true, true, false
left_index, right_index, fail_at, fail_pose = 1, 2, nil, false
next_address = 100
local transform_meta
function copy(t)
    if type(t) ~= "table" then return t end
    if getmetatable(t)==transform_meta then
        local r=transform(0,0,0); for k,v in pairs(t) do r[k]=copy(v) end; return r
    end
    local r = {}; for k,v in pairs(t) do r[k]=copy(v) end; return r
end
transform_meta={
    __index=function(t,k) return rawget(t,"data")[k] end,
    __newindex=function(t,k,val)
        if (k=="Translation" or k=="Scale3D") and val.X then val={x=val.X,y=val.Y,z=val.Z} end
        rawget(t,"data")[k]=val
    end,
    __pairs=function(t) return next,rawget(t,"data"),nil end,
}
function transform(x,y,z)
    return setmetatable({data={Translation={x=x,y=y,z=z},Rotation={X=0,Y=0,Z=0,W=1},Scale3D={x=1,y=1,z=1}}},transform_meta)
end
function object(kind)
    next_address=next_address+1
    local o={alive=true, kind=kind, address=next_address, props={}}
    function o:get_address() return self.address end
    function o:get_class() return {get_short_name=function() return self.kind end} end
    function o:get_fname() return {to_string=function() return self.name or self.kind end} end
    function o:get_property(k) return self.props[k] end
    function o:set_property(k,v) self.props[k]=v end
    return o
end
asset=object("SkeletalMesh")
game_material=object("Arms_Mat_FOV")
game_material.kind="Material"; game_material.name="Arms_Mat_FOV"
world_material=object("Arms_Mat")
hazmat_material=object("Hazmat_Mat")
body=object("SkeletalMeshComponent"); body.props.bOwnerNoSee=false
legs=object("SkeletalMeshComponent"); legs.props.bOwnerNoSee=true
remote_body=object("SkeletalMeshComponent"); remote_body.props.bOwnerNoSee=false
function body:SetOwnerNoSee(value) self.props.bOwnerNoSee=value end
function legs:SetOwnerNoSee(value) self.props.bOwnerNoSee=value end
function remote_body:SetOwnerNoSee(value) self.props.bOwnerNoSee=value end
source=object("SkeletalMeshComponent")
source.props={SkeletalMesh=asset,VisibilityBasedAnimTickOption=3,bVisible=false,bHiddenInGame=true}
source.bones={}
for _,side in ipairs({"Left","Right"}) do
    local y=side=="Left" and -15 or 15
    source.bones[side.."Arm"]=transform(100,y,160)
    source.bones[side.."ForeArm"]=transform(125,y,155)
    source.bones[side.."Hand"]=transform(145,y,150)
    source.bones[side.."HandMiddle1"]=transform(148,y,150)
    source.bones[side.."HandThumb1"]=transform(146,y,152)
    source.bones[side.."HandIndex1"]=transform(148,y,151)
end
function source:GetBoneIndex(name) if missing_bone and name=="LeftHand" then return -1 end; return self.bones[name] and 1 or -1 end
function source:K2_GetComponentToWorld() return transform(100,0,100) end
function source:GetNumMaterials() return 1 end
function source:GetMaterial(i) return game_material end
function source:SetVisibility(v,children) assert(children==false); self.props.bVisible=v end
function source:SetHiddenInGame(v,children) assert(children==false); self.props.bHiddenInGame=v end
function source:GetSocketTransform(name,space) assert(space==0); return copy(self.bones[name]) end
function hazmat_body()
    legs.props.SkeletalMesh=object("SkeletalMesh"); legs.props.SkeletalMesh.name="Hazmat"
    legs.props.VisibilityBasedAnimTickOption=3
    body.props.VisibilityBasedAnimTickOption=2
    spine=transform(70,0,140);spine.Scale3D={X=.9,Y=.9,Z=.9}
    function legs:GetBoneIndex(name) return name=="Spine2" and 3 or -1 end
    function legs:GetSocketTransform(name,space)
        assert(name=="Spine2" and space==0,"Hidden shoulder sockets must not anchor the rig")
        return copy(spine)
    end
    for _,side in ipairs({"Left","Right"}) do
        source.bones[side.."Shoulder"]=transform(90,side=="Left" and -8 or 8,160)
    end
end
function make_pawn()
    local p=object("BPCharacter_Demo_C"); p.props.Arms=source; p.props.Mesh=body; p.props.Legs=legs
    function p:FinishAddComponent(c,manual,t)
        assert(manual==true and c.deferred and t.Scale3D.x==1)
        if c.kind=="PoseableMeshComponent" then assert(c.props.SkeletalMesh==asset) end
        c.finished=true
    end
    return p
end
pawn=make_pawn()
uevr={api={},params={vr={},functions={}},sdk={callbacks={}}}
function uevr.api:get_local_pawn(i) assert(i==0); return pawn end
function uevr.api:find_uobject(path)
    if path:match("^Material ") then
        if path:match("Arms_Mat%.Arms_Mat$") then return world_material end
        if path:match("Hazmat_Mat%.Hazmat_Mat$") then return hazmat_material end
        return nil
    end
    local kind=path:match("%.([^%.]+)$")
    return {as_class=function(self) return self end,kind=kind}
end
function uevr.api:add_component_by_class(owner,class,deferred)
    if fail_at==#created+1 then error("allocation failure") end
    local c=object(class.kind); c.owner=owner; c.deferred=deferred; c.visible=false
    table.insert(created,c)
    function c:K2_GetComponentToWorld()
        if self.hand==0 then return copy(left_pose or transform(145,-25,145)) end
        return copy(right_pose or transform(145,25,145))
    end
    function c:K2_SetWorldTransform(t,sweep,hit,teleport)
        assert(not sweep and not teleport); self.world=copy(t)
    end
    function c:CopyPoseFromSkeletalComponent(s)
        assert(self.finished and s==source)
        if fail_pose then error("pose failure") end
        self.bones=copy(s.bones)
    end
    function c:GetBoneTransformByName(name,space) error("poseable cache may be stale after CopyPose") end
    function c:SetBoneTransformByName(name,t,space) assert(space==0); self.bones[name]=copy(t) end
    function c:SetVisibility(v,children) assert(children==false); self.visible=v end
    function c:SetHiddenInGame(v,children) assert(children==false); self.hidden=v end
    function c:SetCollisionEnabled(v) assert(v==0); self.no_collision=true end
    function c:SetGenerateOverlapEvents(v) assert(v==false) end
    function c:SetOnlyOwnerSee(v) assert(v==true); self.owner_only=true end
    function c:SetCastShadow(v) assert(v==false) end
    function c:SetBoundsScale(v) assert(v>=1) end
    function c:SetMaterial(i,m) self.material=m end
    function c:K2_DestroyComponent(owner) assert(owner==self.owner); self.alive=false end
    return c
end
UEVR_UObjectHook={activate=function() end,exists=function(o) return o.alive==true end}
function UEVR_UObjectHook.get_or_add_motion_controller_state(c)
    return {set_hand=function(_,h) c.hand=h end,set_permanent=function(_,p) assert(p==false) end}
end
function UEVR_UObjectHook.remove_motion_controller_state(c) table.insert(removed,c) end
function uevr.params.vr.is_hmd_active() return hmd end
function uevr.params.vr.is_using_controllers() return controllers end
function uevr.params.vr.get_left_controller_index() return left_index end
function uevr.params.vr.get_right_controller_index() return right_index end
function uevr.params.functions.log_info(s) table.insert(logs,s) end
function uevr.sdk.callbacks.on_post_engine_tick(f) callbacks.tick=f end
function uevr.sdk.callbacks.on_script_reset(f) callbacks.reset=f end
function uevr.sdk.callbacks.on_draw_ui(f) callbacks.ui=f end
json={load_file=function() return settings end,dump_file=function(_,s) settings=s; return true end}
fs={write=function() end}
imgui={tree_node=function() return false end}
function tick(delta) callbacks.tick(nil,delta or 0.016) end
function start() tick(); tick() end
'''


class HandsTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(HARNESS)
        self.math = self.lua.execute(SCRIPT.read_text(encoding="utf-8"))

    def run_lua(self, text):
        self.lua.execute(text)

    def test_independent_wrist_positions_and_rotations(self):
        self.run_lua('''
            left_pose=transform(135,-35,130)
            right_pose=transform(140,35,165); right_pose.Rotation={X=0,Y=0,Z=0.70710678,W=0.70710678}
            start()
            assert(#created==3)
            assert(created[3].bones.LeftHand.Translation.y==-35)
            assert(created[3].bones.RightHand.Translation.z==165)
            assert(math.abs(created[3].bones.RightHand.Rotation.Z-0.70710678)<0.001)
            assert(created[3].visible and created[3].owner_only and created[3].no_collision)
            assert(source.props.bVisible==false and source.props.bHiddenInGame==true)
        ''')

    def test_openvr_device_indices_do_not_get_used_as_hand_ids(self):
        self.run_lua('left_index=7; right_index=13; tick(); assert(created[1].hand==0 and created[2].hand==1)')

    def test_does_not_accumulate_components_on_frames(self):
        self.run_lua('for i=1,100 do tick() end; assert(#created==3)')

    def test_controller_loss_restores_source_and_can_resume(self):
        self.run_lua('''start(); controllers=false; tick()
            assert(source.props.VisibilityBasedAnimTickOption==3 and not created[3].visible)
            controllers=true; tick(); assert(created[3].visible and #created==3)''')

    def test_headset_inactive_and_negative_device_indices_suspend(self):
        self.run_lua('start(); hmd=false; tick(); assert(not created[3].visible); hmd=true; left_index=-1; tick(); assert(not created[3].visible)')

    def test_disable_restores_exact_original_source_visibility(self):
        self.run_lua('''source.props.bVisible=true; source.props.bHiddenInGame=false
            start(); settings.enabled=false; tick(1.1)
            assert(source.props.bVisible and not source.props.bHiddenInGame)
            assert(source.props.VisibilityBasedAnimTickOption==3 and not created[3].visible)''')

    def test_reset_removes_only_owned_hooks_and_components(self):
        self.run_lua('''start(); callbacks.reset(); assert(#removed==2)
            for _,c in ipairs(created) do assert(not c.alive) end
            assert(source.alive and source.props.VisibilityBasedAnimTickOption==3)''')

    def test_pawn_replacement_destroys_old_components(self):
        self.run_lua('start(); pawn=make_pawn(); tick(); assert(#created==6); for i=1,3 do assert(not created[i].alive) end')

    def test_frontend_remote_and_missing_bone_are_not_modified(self):
        self.run_lua('''pawn.kind="Frontend_C"; tick(); assert(#created==0)
            pawn=make_pawn(); missing_bone=true; tick(); assert(#created==0)''')

    def test_partial_initialization_cleanup_and_bounded_retry(self):
        self.run_lua('''fail_at=2; tick(); assert(not created[1].alive)
            assert(source.props.VisibilityBasedAnimTickOption==3)
            fail_at=nil; tick(.1); assert(#created==1)
            tick(.2); tick(); assert(#created==4 and created[4].visible)''')

    def test_nonfinite_pose_cannot_reach_renderer(self):
        self.run_lua('start(); left_pose=transform(0/0,0,0); tick(); assert(not created[3].visible)')

    def test_failed_pose_restores_visibility_keeps_animation_and_recovers(self):
        self.run_lua('''source.props.bVisible=true;source.props.bHiddenInGame=false
            start(); fail_pose=true; for i=1,600 do tick() end
            assert(#created==3 and not created[3].visible)
            assert(source.props.bVisible and not source.props.bHiddenInGame)
            assert(source.props.VisibilityBasedAnimTickOption==0)
            fail_pose=false; tick(6); tick(); assert(created[3].visible)
            callbacks.reset();assert(source.props.VisibilityBasedAnimTickOption==3)''')

    def test_new_anchors_do_not_render_untracked_initial_transforms(self):
        self.run_lua('tick(); assert(not created[3].visible); assert(source.props.VisibilityBasedAnimTickOption==0); tick(); assert(created[3].visible)')

    def test_collapsed_pose_reuses_healthy_rig_and_recovers(self):
        self.run_lua('''start();local old=copy(source.bones.LeftForeArm)
            source.bones.LeftForeArm=copy(source.bones.LeftArm)
            for i=1,120 do tick() end;assert(#created==3 and not created[3].visible)
            source.bones.LeftForeArm=old;tick(6);tick()
            assert(#created==3 and created[3].visible)''')

    def test_invalid_startup_pose_refreshes_without_allocating_and_reset_restores_tick(self):
        self.run_lua('''local old=copy(source.bones.LeftForeArm)
            source.bones.LeftForeArm=copy(source.bones.LeftArm);tick()
            assert(#created==0 and source.props.VisibilityBasedAnimTickOption==0)
            callbacks.reset();assert(source.props.VisibilityBasedAnimTickOption==3)
            source.bones.LeftForeArm=old;tick(6);tick();assert(created[3].visible)''')

    def test_allocation_errors_have_bounded_cooldown_and_automatic_recovery(self):
        self.run_lua('''local allocate=uevr.api.add_component_by_class;local attempts=0;local down=true
            function uevr.api:add_component_by_class(...)
                attempts=attempts+1;if down then return nil end;return allocate(self,...)
            end
            for i=1,600 do tick() end
            assert(attempts>=3 and attempts<20 and #created==0)
            down=false;tick(6);tick();assert(created[3].visible)''')

    def test_long_session_repeated_pose_failures_do_not_leak_or_stop(self):
        self.run_lua('''start();for round=1,50 do
            for i=1,200 do tick() end
            fail_pose=true;tick();fail_pose=false;tick(6);tick()
            assert(#created==3 and created[3].visible)
            end''')

    def test_body_clavicles_move_out_of_viewmodel_frame_while_wrists_remain_exact(self):
        self.run_lua('''hazmat_body();start()
            local p=created[3].bones.LeftShoulder.Translation
            assert(math.abs(p.x-(70+7.8382473*.9))<.001)
            assert(math.abs(p.y-(-15.4605675*.9))<.001)
            assert(created[3].bones.LeftHand.Translation.x==145 and created[3].bones.LeftHand.Translation.y==-25)
            assert(created[3].bones.RightHand.Translation.y==25)
            assert(legs.props.VisibilityBasedAnimTickOption==0 and body.props.VisibilityBasedAnimTickOption==0)
            settings.enabled=false;tick(1.1)
            assert(legs.props.VisibilityBasedAnimTickOption==3 and body.props.VisibilityBasedAnimTickOption==2)''')

    def test_body_frame_follows_body_yaw_translation_and_scale(self):
        self.run_lua('''hazmat_body();spine=transform(20,30,140)
            spine.Rotation={X=0,Y=0,Z=math.sqrt(.5),W=math.sqrt(.5)}
            spine.Scale3D={X=.8,Y=.8,Z=.8};start()
            local p=created[3].bones.LeftShoulder.Translation
            assert(math.abs(p.x-(20+15.4605675*.8))<.001)
            assert(math.abs(p.y-(30+7.8382473*.8))<.001)
            assert(created[3].bones.LeftHand.Translation.x==145)''')

    def test_unreviewed_body_mesh_does_not_receive_hazmat_calibration(self):
        self.run_lua('''hazmat_body();legs.props.SkeletalMesh.name="OtherCostume";start()
            assert(created[3].bones.LeftShoulder.Translation.x==90)
            assert(created[3].bones.LeftHand.Translation.x==145)''')

    def test_disable_or_tracking_loss_during_cooldown_restores_animation_immediately(self):
        self.run_lua('''start();fail_pose=true;tick();controllers=false;tick()
            assert(source.props.VisibilityBasedAnimTickOption==3)
            controllers=true;settings.enabled=false;tick(1.1)
            assert(source.props.VisibilityBasedAnimTickOption==3 and not created[3].visible)''')

    def test_costume_change_rebuilds_the_driver(self):
        self.run_lua('start(); asset=object("SkeletalMesh"); source.props.SkeletalMesh=asset; tick(); tick(); assert(#created==6 and not created[3].alive and created[6].visible)')

    def test_reachable_arm_lengths_are_preserved(self):
        v=lambda x,y,z:self.lua.table_from(dict(x=x,y=y,z=z))
        for goal in [(30,10,-20),(0,0,-30),(-20,15,10),(45,0,0)]:
            e,w=self.math.solve(v(0,0,0),v(25,0,0),v(50,0,0),v(*goal),v(0,0,-1))
            self.assertAlmostEqual(sum(e[k]**2 for k in ("x","y","z")),625,places=6)
            self.assertAlmostEqual(sum((w[k]-e[k])**2 for k in ("x","y","z")),625,places=6)

    def test_missing_settings_never_enables_driver(self):
        self.run_lua('settings=nil; tick(); assert(#created==0)')

    def test_fov_warp_is_not_copied_to_tracked_mesh(self):
        self.run_lua('start(); assert(created[3].material==world_material)')

    def test_fov_material_instances_are_resolved_with_bounded_parent_walk(self):
        self.run_lua('''local base=game_material
            game_material=object("MaterialInstanceDynamic"); game_material.props.Parent=base
            start(); assert(created[3].material==world_material)
            game_material.props.Parent=game_material; tick(); assert(created[3].material==game_material)''')

    def test_local_torso_hidden_without_changing_remote_body_or_collision(self):
        self.run_lua('start(); assert(body.props.bOwnerNoSee and legs.props.bOwnerNoSee); assert(not remote_body.props.bOwnerNoSee)')

    def test_body_visibility_restored_after_disable_reset_and_failure(self):
        self.run_lua('''start(); settings.enabled=false; tick(1.1)
            assert(not body.props.bOwnerNoSee and legs.props.bOwnerNoSee)
            settings.enabled=true; tick(1.1); callbacks.reset()
            assert(not body.props.bOwnerNoSee and legs.props.bOwnerNoSee)
            start(); fail_pose=true; tick()
            assert(not body.props.bOwnerNoSee and legs.props.bOwnerNoSee)''')

    def test_body_component_replacement_restores_previous_component(self):
        self.run_lua('''start(); local previous=body
            body=object("SkeletalMeshComponent"); body.props.bOwnerNoSee=false
            function body:SetOwnerNoSee(v) self.props.bOwnerNoSee=v end
            pawn.props.Mesh=body; tick()
            assert(not previous.props.bOwnerNoSee and body.props.bOwnerNoSee)''')

    def test_material_fallback_and_unreviewed_material_preservation(self):
        self.run_lua('''world_material=nil; start(); assert(created[3].material==hazmat_material)
            game_material=object("CustomCostume"); tick(); assert(created[3].material==game_material)''')

    def test_no_warped_mesh_if_safe_material_is_unavailable(self):
        self.run_lua('world_material=nil; hazmat_material=nil; start(); assert(not created[3].visible); assert(not body.props.bOwnerNoSee)')

    def test_nonboolean_setting_does_not_enable_driver(self):
        self.run_lua('settings={enabled="false"}; tick(); assert(#created==0)')

    def test_failed_preference_write_keeps_current_setting(self):
        self.run_lua('''start()
            imgui={tree_node=function() return true end,text=function() end,
                button=function() return true end,tree_pop=function() end}
            json.dump_file=function() return false end
            callbacks.ui(); tick(); assert(created[3].visible)
            json.dump_file=function(_,s) settings=s; return true end
            callbacks.ui(); tick(); assert(not created[3].visible)''')

    def test_stretched_and_folded_arm_math_stays_finite(self):
        random.seed(531)
        v = lambda x,y,z: self.lua.table_from(dict(x=x,y=y,z=z))
        for _ in range(300):
            goal=[random.uniform(-160,160) for _ in range(3)]
            elbow, wrist = self.math.solve(v(0,0,0),v(25,0,-5),v(45,0,-10),v(*goal),v(0,0,-1))
            self.assertTrue(all(math.isfinite(elbow[k]) for k in ("x","y","z")))
            self.assertTrue(all(math.isfinite(wrist[k]) for k in ("x","y","z")))
            self.assertAlmostEqual(sum((wrist[k]-goal[i])**2 for i,k in enumerate(("x","y","z"))),0,places=6)

    def test_folded_reach_and_opposite_quaternion(self):
        v=lambda x,y,z:self.lua.table_from(dict(x=x,y=y,z=z))
        q=self.math.between(v(1,0,0),v(-1,0,0))
        r=self.math.rotate(q,v(1,0,0))
        self.assertAlmostEqual(r.x,-1,places=6)
        e,w=self.math.solve(v(0,0,0),v(25,0,0),v(50,0,0),v(0,0,0),v(0,0,-1))
        self.assertAlmostEqual(e.z,-25,places=6)
        self.assertEqual(w.x,0)


if __name__ == "__main__":
    unittest.main()
