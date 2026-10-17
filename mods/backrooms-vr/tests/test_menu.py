"""Real Lua helper plus the pinned UEVR head-aim/reference-frame behavior."""
from pathlib import Path
import unittest
from lupa.lua54 import LuaRuntime

SCRIPT = Path(__file__).resolve().parents[1] / "backrooms_menu.lua"


class MenuTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        self.lua.execute('''
            active,ready,allowed,head_yaw,offset=true,true,true,0,0
            centers,origins,aim_writes,settings=0,{}, {},{}
            saved_method,position=1,{x=1,y=1.6,z=3}
            logs={}; drawing=false;menu_objects={};menu_classes={};next_id=400
            uevr={params={vr={},functions={}},api={},sdk={callbacks={}}}
            vr=uevr.params.vr
            function vr.is_runtime_ready() return ready end
            function vr.is_hmd_active() return active end
            function vr.is_aim_allowed() return allowed end
            function vr.set_aim_allowed(v) allowed=v;aim_writes[#aim_writes+1]=v end
            function vr.get_aim_method() return allowed and saved_method or 0 end
            function vr.get_mod_value(k) return settings[k] end
            function vr.set_mod_value(k,v) settings[k]=v end
            function vr.get_hmd_index() return 0 end
            UEVR_Vector3f={new=function() return {} end}
            UEVR_Quaternionf={new=function() return {} end}
            function vr.get_pose(index,p,q)
                assert(index==0);p.x=position.x;p.y=position.y;p.z=position.z
                q.x=0;q.y=math.sin(math.rad(head_yaw)/2);q.z=0;q.w=math.cos(math.rad(head_yaw)/2)
            end
            function vr.set_standing_origin(p) origins[#origins+1]={x=p.x,y=p.y,z=p.z} end
            function vr.recenter_view() centers=centers+1;offset=-head_yaw end
            function vr.get_rotation_offset(q) q.x=offset;q.y=0;q.z=0;q.w=1 end
            function vr.set_rotation_offset(q) offset=q.x end
            function uevr.params.functions.log_info(s) logs[#logs+1]=s end
            function uevr.params.functions.is_drawing_ui() return drawing end
            local function object(id,full)
                local o={alive=true,get_address=function() return id end,get_full_name=function() return full end}
                function o:is_a(c) return self.widget and (c.native_kind=='Widget' or c.native_kind=='UserWidget' and self.kind=='UserWidget') end
                function o:get_outer() return self.outer end
                return o
            end
            widget_class=object(90,'Class /Script/UMG.Widget');widget_class.native_kind='Widget'
            user_widget_class=object(91,'Class /Script/UMG.UserWidget');user_widget_class.native_kind='UserWidget'
            camera_class=object(100,'BlueprintGeneratedClass /Game/MainMenu/BP_MenuActor.BP_MenuActor_C')
            other_class=object(101,'BlueprintGeneratedClass /Game/Character/BPCharacter_Demo.BPCharacter_Demo_C')
            pawn=object(200,'BP_MenuActor_C /Game/Maps/MainMenuMap.MainMenuMap.PersistentLevel.CameraActor_Blueprint')
            function pawn:get_class() return camera_class end
            target=pawn;local_pawn=pawn
            pc=object(300,'PlayerController local');pc.Pawn=pawn
            function pc:GetViewTarget() return target end
            function uevr.api:get_player_controller(i) assert(i==0);return pc end
            function uevr.api:get_local_pawn(i) assert(i==0);return local_pawn end
            function uevr.api:find_uobject(path)
                if path=='BlueprintGeneratedClass /Game/MainMenu/BP_MenuActor.BP_MenuActor_C' then return camera_class end
                if path=='Class /Script/UMG.Widget' then return widget_class end
                if path=='Class /Script/UMG.UserWidget' then return user_widget_class end
                return menu_classes[path]
            end
            UEVR_UObjectHook={exists=function(o) return o.alive end,get_objects_by_class=function(c,defaults)
                assert(not defaults)
                if c==user_widget_class then
                    local all={};for _,list in pairs(menu_objects) do for _,w in ipairs(list) do all[#all+1]=w end end;return all
                end
                return menu_objects[c] or {}
            end}
            function menu(path)
                local n=path:match('([^/]+)$');local full='WidgetBlueprintGeneratedClass /Game/'..path..'.'..n..'_C'
                local c=menu_classes[full]
                if not c then next_id=next_id+1;c=object(next_id,full);menu_classes[full]=c;menu_objects[c]={} end
                next_id=next_id+1;local w=object(next_id,full..' instance')
                w.widget=true;w.kind='UserWidget';w.visible=true;w.viewport=true;w.owner=pc
                function w:IsVisible() return self.visible end
                function w:IsInViewport() return self.viewport end
                function w:GetOwningPlayer() return self.owner end
                function w:HasAnyUserFocus() return self.focused==true end
                function w:HasUserFocusedDescendants(player) assert(player==pc);return self.child_focused==true end
                function w:GetParent() return self.parent end
                function w:get_class() return {get_short_name=function() return self.kind end,get_full_name=function() return full end} end
                function w:GetActiveWidget() return self.selected end
                table.insert(menu_objects[c],w);return w
            end
            function gameplay() function pawn:get_class() return other_class end end
            function uevr.sdk.callbacks.on_pre_engine_tick(fn) tick=fn end
            function uevr.sdk.callbacks.on_pre_calculate_stereo_view_offset(fn) stereo=fn end
            function uevr.sdk.callbacks.on_draw_ui(fn) draw=fn end
            function uevr.sdk.callbacks.on_script_reset(fn) reset=fn end
            imgui={text=function() end,button=function() return false end}
            function frame(yaw,dt)
                head_yaw=yaw or head_yaw;tick(nil,dt or 0.02)
                -- HEAD aim recenters the reference while BP_MenuActor ignores control rotation.
                if allowed and saved_method==1 then offset=-head_yaw end
                scene_yaw=head_yaw+offset;panel_yaw=-offset
            end
            function native_frame(yaw)
                head_yaw=yaw;tick(nil,.02)
                if allowed and saved_method==1 then offset=-head_yaw end
                for eye=0,1 do
                    if stereo then stereo(nil,eye,100,{}, {},false) end
                    if eye==0 then left_view=head_yaw+offset else right_view=head_yaw+offset end
                    if eye==1 and allowed then pawn_moves=(pawn_moves or 0)+1 end
                    -- UObjectHook::on_post_calculate_stereo_view_offset, actual native reset.
                    allowed=true
                end
                panel_yaw=-offset
            end
        ''')
        self.lua.execute(SCRIPT.read_text(encoding="utf-8"))

    def check(self, code):
        self.lua.execute(code)

    def test_room_and_panel_stay_anchored_when_head_turns(self):
        self.check('frame(10,1);frame(45);assert(math.abs(scene_yaw-35)<.001);assert(panel_yaw==10);frame(-60);assert(scene_yaw==-70);assert(panel_yaw==10)')

    def test_no_continuous_recentering_or_origin_drift(self):
        self.check('frame(20,1);position.x=9;for i=1,120 do frame(i*.5) end;assert(origins[1].x==1);assert(centers==1);assert(#origins==1)')

    def test_gameplay_restores_aim_before_camera_update(self):
        self.check('frame(0,1);assert(not allowed);target=nil;frame(40);assert(allowed);assert(scene_yaw==0)')

    def test_missing_or_other_local_pawn_does_not_take_ownership(self):
        self.check('local_pawn=nil;frame(0,1);assert(allowed);local_pawn=pawn;function pawn:get_class() return other_class end;frame(0,1);assert(allowed);assert(centers==0)')

    def test_other_view_target_does_not_lock_gameplay(self):
        self.check('target={alive=true,get_address=function() return 201 end};frame(0,1);assert(allowed);assert(centers==0)')

    def test_removed_menu_widget_cannot_keep_gameplay_locked(self):
        self.check('frame(0,1);assert(not allowed);local_pawn=nil;frame(0);assert(allowed)')

    def test_framework_settings_alone_never_lock_gameplay(self):
        self.check('local_pawn=nil;drawing=true;frame(0,1);assert(allowed);assert(centers==0)')

    def test_reentry_captures_new_anchor(self):
        self.check('frame(10,1);local_pawn=nil;frame(30);local_pawn=pawn;frame(50,1);assert(not allowed);assert(panel_yaw==50);assert(centers==2)')

    def test_headset_disconnect_restores_and_reconnect_reanchors(self):
        self.check('frame(0,1);active=false;frame(10);assert(allowed);active=true;frame(30,1);assert(not allowed);assert(panel_yaw==30);assert(centers==2)')

    def test_inactive_or_unready_headset_does_not_change_pose(self):
        self.check('active=false;frame(0,1);assert(allowed);active=true;ready=false;frame(0,1);assert(allowed);assert(centers==0)')

    def test_reset_restores_owned_aim_override(self):
        self.check('frame(0,1);assert(not allowed);reset();assert(allowed)')

    def test_preexisting_aim_suspension_is_preserved(self):
        self.check('allowed=false;frame(0,1);local_pawn=nil;frame(0);reset();assert(not allowed);assert(#aim_writes==0)')

    def test_gameplay_aim_preference_is_never_written(self):
        self.check('saved_method=3;frame(0,1);local_pawn=nil;frame(0);assert(saved_method==3);assert(vr.get_aim_method()==3);assert(settings.VR_AimMethod==nil);assert(settings.VR_DecoupledPitch==nil);assert(settings.VR_CameraRightOffset==nil)')

    def test_already_game_aim_keeps_natural_tracking(self):
        self.check('saved_method=0;frame(10,1);frame(40);assert(scene_yaw==30);local_pawn=nil;frame(40);assert(allowed)')

    def test_invalid_pose_does_not_mutate_and_recovers(self):
        self.check('position.x=0/0;frame(0,1);assert(allowed);position.x=2;frame(30,1);assert(not allowed);assert(centers==1)')

    def test_zero_quaternion_is_rejected(self):
        self.check('function vr.get_pose(i,p,q) p.x=1;p.y=2;p.z=3;q.x=0;q.y=0;q.z=0;q.w=0 end;frame(0,1);assert(allowed);assert(centers==0)')

    def test_dead_controller_restores_owned_override(self):
        self.check('frame(0,1);pc.alive=false;frame(10);assert(allowed)')

    def test_error_restores_override_instead_of_leaving_gameplay_disabled(self):
        self.check('frame(0,1);function pc:GetViewTarget() error("level transition") end;frame(10);assert(allowed)')

    def test_native_per_eye_flag_reset_cannot_unlock_scene_or_move_room(self):
        self.check('native_frame(10);native_frame(45);assert(left_view==35 and right_view==35); '
                   'assert(panel_yaw==10);assert((pawn_moves or 0)==0);assert(centers==1); '
                   'native_frame(-60);assert(left_view==-70 and right_view==-70);assert(panel_yaw==10)')

    def test_native_timeout_recenter_cannot_replace_captured_anchor(self):
        self.check('frame(10,1);offset=-50;assert(stereo);stereo(nil,0,100,{}, {},false);assert(offset==-10)')

    def test_each_eye_uses_native_tracking_without_rewriting_camera_or_projection(self):
        self.check('frame(10,1);local untouched=setmetatable({}, {__newindex=function() error("camera override") end}); '
                   'assert(stereo);for eye=0,1 do allowed=true;stereo(nil,eye,100,untouched,untouched,false);assert(not allowed) end; '
                   'assert(settings.VR_2DScreenMode==nil);assert(settings.VR_AimMethod==nil)')

    def test_stereo_guard_releases_after_leaving_title(self):
        self.check('frame(0,1);target=nil;frame(30);offset=17;assert(stereo);stereo(nil,0,100,{}, {},false);assert(offset==17 and allowed)')

    def test_pause_settings_menu_anchors_gameplay_room_until_closed(self):
        self.check('gameplay();local m=menu("SettingsMenu/Widgets/Settings/WB_SettingsPanel_Theme2"); '
                   'native_frame(10);native_frame(45);assert(left_view==35 and right_view==35 and panel_yaw==10); '
                   'assert((pawn_moves or 0)==0);m.viewport=false;frame(45);assert(allowed)')

    def test_lobby_forms_and_modal_transitions_keep_one_anchor(self):
        self.check('gameplay();local lobby=menu("NebulaLobbyUI/UI/UI_Lobby/UI_Menu_Lobby");frame(10,1); '
                   'lobby.viewport=false;local settings=menu("SettingsMenu/Widgets/Settings/WB_SettingsPanel_Theme2"); '
                   'frame(40);assert(not allowed and centers==1);settings.viewport=false; '
                   'local modal=menu("UI/Messages/W_FancyRichMessageModal");frame(70);assert(centers==1); '
                   'modal.viewport=false;frame(70);assert(allowed)')

    def test_hidden_detached_and_remote_menus_never_lock_gameplay(self):
        self.check('gameplay();local m=menu("SettingsMenu/Widgets/Settings/WB_SettingsPanel_Theme2"); '
                   'm.visible=false;frame(0,1);assert(allowed);m.visible=true;m.viewport=false;frame(0);assert(allowed); '
                   'm.viewport=true;m.owner={alive=true,get_address=function() return 900 end};frame(0);assert(allowed and centers==0)')

    def test_nested_modal_requires_visible_viewport_ancestry(self):
        self.check('gameplay();local host=menu("unreviewed/Host");local modal=menu("UI/Messages/W_FancyModal"); '
                   'modal.viewport=false;modal.parent=host;host.visible=false;frame(0,1);assert(allowed); '
                   'host.visible=true;frame(10);assert(not allowed);host.viewport=false;frame(30);assert(allowed)')

    def test_widget_tree_ownership_bridge_and_inactive_switcher(self):
        self.check('gameplay();local host=menu("unreviewed/Host");host.kind="WidgetSwitcher";host.viewport=false; '
                   'local root=menu("unreviewed/Root");host.parent=root; '
                   'local m=menu("UI/Messages/W_FancyModal");m.viewport=false;m.parent=host; '
                   'host.selected={alive=true,get_address=function() return 999 end};frame(0,1);assert(allowed); '
                   'host.selected=m;frame(10);assert(not allowed);host.visible=false;frame(30);assert(allowed)')

    def test_inventory_hotbar_never_locks_gameplay_even_when_cursor_is_visible(self):
        self.check('gameplay();local m=menu("NewInventorySystem/Inventory/UI/UI_Inventory"); '
                   'pc.bShowMouseCursor=false;frame(0,1);assert(allowed);pc.bShowMouseCursor=true;frame(10); '
                   'assert(allowed);m.focused=true;frame(10);assert(not allowed); '
                   'm.focused=false;frame(30);assert(allowed)')

    def test_inventory_child_focus_locks_until_input_returns_to_gameplay(self):
        self.check('gameplay();local m=menu("NewInventorySystem/Inventory/UI/UI_Inventory"); '
                   'm.child_focused=true;frame(10,1);assert(not allowed);m.child_focused=false; '
                   'frame(40);assert(allowed);assert(settings.VR_AimMethod==nil)')

    def test_gameplay_hud_is_not_a_menu(self):
        self.check('gameplay();menu("UI/InGame_UI");menu("VoiceChat/Widgets/W_VOIPInGame"); '
                   'menu("UI/Scoreboard_UI");menu("UI/Online/W_VOIP_PlayerList"); '
                   'pc.bShowMouseCursor=true;frame(0,1);assert(allowed and centers==0)')

    def test_menu_supports_separate_view_target_and_absent_pawn(self):
        self.check('gameplay();menu("UI/Lobby/Lobby_Menu");local_pawn=nil; '
                   'frame(20,1);frame(50);assert(not allowed and scene_yaw==30)')

    def test_reopen_places_new_anchor_while_tabs_never_recenter(self):
        self.check('gameplay();local m=menu("SettingsMenu/Widgets/Settings/WB_SettingsPanel_Theme2"); '
                   'frame(10,1);frame(40);assert(centers==1);m.visible=false;frame(40); '
                   'm.visible=true;frame(70);assert(centers==2 and panel_yaw==70)')

    def test_same_room_menu_does_not_reanchor_when_pawn_temporarily_disappears(self):
        self.check('gameplay();menu("SettingsMenu/Widgets/Settings/WB_SettingsPanel_Theme2"); '
                   'frame(10,1);local_pawn=nil;frame(40);assert(centers==1 and panel_yaw==10)')

    def test_widget_tree_bridge_and_cycles_are_checked(self):
        self.check('gameplay();local host=menu("unreviewed/Host");local m=menu("UI/Messages/W_FancyModal"); '
                   'm.viewport=false;m.outer={alive=true,is_a=function() return false end,get_outer=function() return host end}; '
                   'frame(10,1);assert(not allowed);host.visible=false;frame(30);assert(allowed); '
                   'm.outer=nil;m.parent=m;frame(30);assert(allowed)')

    def test_foreign_viewport_host_cannot_lock_local_child(self):
        self.check('gameplay();local host=menu("unreviewed/Host");local m=menu("UI/Messages/W_FancyModal"); '
                   'm.viewport=false;m.parent=host;host.owner={alive=true,get_address=function() return 999 end}; '
                   'frame(10,1);assert(allowed and centers==0)')


if __name__ == "__main__":
    unittest.main()
