"""Run the real keyboard Lua against a UEVR/ImGui harness (requires lupa)."""
from pathlib import Path
import unittest
from lupa.lua54 import LuaRuntime

SCRIPT = Path(__file__).resolve().parents[1] / "backrooms_names.lua"


class KeyboardTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        self.lua.execute('''
            events, owners, settings, drawn, clicked = {}, {}, {}, {}, nil
            buttons_drawn = {}
            settings.FrameworkConfig_AlwaysShowCursor = "false"
            settings.VR_MovementOrientation = "1"
            settings.VR_SnapTurn = "true"
            focused_field, drawing_ui, transport = nil, false, "ok"
            uevr = {api = {}, sdk = {callbacks = {}}, params = {vr = {}, functions = {}}}
            function uevr.params.vr.set_mod_value(k, v) settings[k] = v end
            function uevr.params.vr:get_mod_value(k) return settings[k] or "" end
            function uevr.params.functions.is_drawing_ui() return drawing_ui end
            function uevr.api:find_uobject(name) return name end
            function uevr.api:dispatch_custom_event(event, text)
                table.insert(events, {event, text})
                local response=transport_actions and transport_actions[event] or transport
                if response ~= "missing" then lua_event("avrcade.keyboard.result", response) end
            end
            function uevr.sdk.callbacks.on_pre_engine_tick(fn) tick = fn end
            function uevr.sdk.callbacks.on_xinput_get_state(fn) xinput = fn end
            function uevr.sdk.callbacks.on_lua_event(fn) lua_event = fn end
            function uevr.sdk.callbacks.on_frame(fn) frame = fn end
            function uevr.sdk.callbacks.on_script_reset(fn) reset = fn end
            UEVR_UObjectHook = {
                activate = function() end,
                exists = function(obj) return obj.alive end,
                get_objects_by_class = function(class, defaults)
                    assert(defaults == false); return owners[class] or {}
                end,
            }
            function make_field(focus)
                local f = {alive = true, visible = true, IsReadOnly = false, IsPassword = false}
                function f:IsVisible() return self.visible end
                function f:HasKeyboardFocus() return focused_field == self end
                function f:HasAnyUserFocus() return focused_field == self end
                function f:SetKeyboardFocus() focused_field = self end
                if focus then f:SetKeyboardFocus() end
                return f
            end
            function make_owner(kind, focus)
                local o = {alive = true, visible = true, EditableTextBox_Name = make_field(focus), Button_Yes = make_field(false)}
                function o:IsVisible() return self.visible end
                local path = "WidgetBlueprintGeneratedClass /Game/MainMenu/Widgets/MainMenu/" .. kind .. "." .. kind .. "_C"
                owners[path] = owners[path] or {}; table.insert(owners[path], o)
                owner = o; return o
            end
            function make_lobby(attached)
                local wrapper = {alive = true, visible = true, EditableText_143 = make_field(false)}
                function wrapper:IsVisible() return self.visible end
                local o = {alive = true, visible = true, attached = attached, Textfield_SaveGame = wrapper}
                function o:IsVisible() return self.visible end
                function o:IsInViewport() return self.attached end
                local path = "WidgetBlueprintGeneratedClass /Game/NebulaLobbyUI/UI/UI_Lobby/UI_Menu_Lobby.UI_Menu_Lobby_C"
                owners[path] = owners[path] or {}; table.insert(owners[path], o)
                lobby = o; return o
            end
            function make_modal(attached,two_buttons)
                local m={alive=true,visible=true,attached=attached,Button1=make_field(false),Button2=make_field(false)}
                m.Button2.visible=two_buttons or false
                function m:IsVisible() return self.visible end
                function m:IsInViewport() return self.attached end
                function m:GetParent() return self.parent end
                function m:get_class() return {find_function=function() if self.handler_missing then return nil end; return true end} end
                function m.Button1:GetIsEnabled() return self.enabled~=false end
                m["BndEvt__W_FancyModal_Button1_K2Node_ComponentBoundEvent_0_OnClick__DelegateSignature"]=function(self)
                    assert(self==m); self.clicks=(self.clicks or 0)+1; self.attached=false
                end
                owners["WidgetBlueprintGeneratedClass /Game/UI/Messages/W_FancyModal.W_FancyModal_C"]={m}
                modal=m; return m
            end
            function poll(buttons, x, y, index)
                local state = {Gamepad = {wButtons = buttons or 0, sThumbLX = x or 0, sThumbLY = y or 0,
                    sThumbRX = 0, sThumbRY = 0, bLeftTrigger = 0, bRightTrigger = 0}}
                xinput(nil, index or 0, state); return state.Gamepad
            end
            function press(buttons) poll(0); tick(nil, 0.016); poll(buttons); tick(nil, 0.016); poll(0); tick(nil, 0.016) end
            function settle() for i=1,16 do tick(nil, 0.016) end end
            imgui = {}
            function imgui.load_font(path, size) assert(path == nil and size == 26); return 1 end
            function imgui.get_display_size() return {x = 1920, y = 1080} end
            function imgui.set_next_window_pos(p, cond, pivot) assert(p[1] == 960 and pivot[1] == 0.5) end
            function imgui.set_next_window_size(size, cond) assert(size[1] == 760 and (size[2] == 480 or size[2]==120)) end
            function imgui.begin_window(name, open, flags)
                assert(settings.FrameworkConfig_AlwaysShowCursor == "true")
                if name=="AVRcade keyboard" then windows = (windows or 0) + 1 end; return true
            end
            function imgui.end_window() end
            function imgui.push_font(f) end
            function imgui.pop_font() end
            function imgui.text(t) table.insert(drawn, t) end
            function imgui.same_line() end
            function imgui.push_style_color(index, color) assert(index == 21) end
            function imgui.pop_style_color(count) end
            function imgui.button(label, size)
                table.insert(buttons_drawn, {label=label,width=size[1],height=size[2]})
                local key=label:match("^(.-)##")
                if clicked and (key==clicked or clicked=="Done" and key=="DONE - Right B") then clicked = nil; return true end
                return false
            end
            function click(key) clicked = key; frame(); tick(nil, 0.016) end
            function event_count(suffix)
                local count = 0
                for _, event in ipairs(events) do if event[1] == "avrcade.keyboard." .. suffix then count = count + 1 end end
                return count
            end
            function text_entered()
                local text = ""
                for _, event in ipairs(events) do if event[1] == "avrcade.keyboard.text" then text = text .. event[2] end end
                return text
            end
        ''')
        self.lua.execute(SCRIPT.read_text(encoding="utf-8"))

    def run_lua(self, code):
        self.lua.execute(code)

    def open(self):
        self.run_lua('make_owner("W_CreateServer", true); tick(nil, 0.25); frame()')

    def test_opens_without_autofill_and_restores_settings(self):
        self.open()
        self.run_lua('''
            assert(windows == 1 and event_count("text") == 0)
            assert(settings.UI_FollowView == "false" and settings.UI_Framework_FollowView == "false")
            assert(settings.VR_DecoupledPitchUIAdjust == "false")
            assert(settings.VR_MovementOrientation == "0" and settings.VR_SnapTurn == "false")
            press(0x4000)
            settle()
            assert(settings.FrameworkConfig_AlwaysShowCursor == "false") -- Hint appears on the next idle scan.
            assert(settings.VR_MovementOrientation == "1" and settings.VR_SnapTurn == "true")
            assert(focused_field == owner.Button_Yes)
        ''')

    def test_controller_letters_shift_digits_and_editing(self):
        self.open()
        self.run_lua('''
            press(0x1000); press(0x8000); press(0x1000)
            press(1); press(0x1000)
            assert(text_entered() == "qQ1")
            press(0x2000); assert(event_count("backspace") == 1)
            click("Space"); click("Clear")
            assert(text_entered() == "qQ1 " and event_count("clear") == 1)
        ''')

    def test_existing_text_not_replaced(self):
        self.run_lua('make_owner("W_NewGame", true); owner.SaveName = "ExistingSave"; tick(nil, 0.25); frame()')
        self.run_lua('assert(windows == 1 and owner.SaveName == "ExistingSave" and event_count("clear") == 0)')

    def test_current_lobby_opens_without_focus_and_ignores_detached_copy(self):
        self.run_lua('''
            make_lobby(false); stale = lobby; make_lobby(true)
            tick(nil, 0.25); frame()
            assert(windows == 1 and focused_field == lobby.Textfield_SaveGame.EditableText_143)
            assert(focused_field ~= stale.Textfield_SaveGame.EditableText_143)
        ''')

    def test_current_lobby_keyboard_keeps_working_after_pointer_focus_loss(self):
        self.run_lua('''
            make_lobby(true); tick(nil, 0.25); frame()
            focused_field = nil; clicked = "q"; frame(); tick(nil, 0.016)
            assert(text_entered() == "q" and settings.FrameworkConfig_AlwaysShowCursor == "true")
            press(0x1000); assert(text_entered() == "qq")
        ''')

    def test_current_lobby_done_stays_closed_until_prompt_leaves(self):
        self.run_lua('''
            make_lobby(true); tick(nil, 0.25); frame()
            focused_field = nil; click("Done"); settle(); tick(nil, 0.25)
            count = windows; frame(); assert(windows == count)
            assert(settings.FrameworkConfig_AlwaysShowCursor == "true") -- Reopen hint remains available.
            lobby.Textfield_SaveGame.visible = false; tick(nil, 0.25)
            lobby.Textfield_SaveGame.visible = true; tick(nil, 0.25); frame()
            assert(windows == count + 1)
        ''')

    def test_current_lobby_invalid_prompt_does_not_open(self):
        for condition in ["lobby.attached = false", "lobby.visible = false", "lobby.Textfield_SaveGame.visible = false", "lobby.Textfield_SaveGame.EditableText_143.IsReadOnly = true"]:
            with self.subTest(condition=condition):
                self.setUp()
                self.run_lua(f'make_lobby(true); {condition}; tick(nil, 0.25); frame(); assert(windows == nil)')

    def test_hidden_dead_readonly_unfocused_fields_do_not_open(self):
        for condition in ["owner.visible = false", "owner.alive = false", "owner.EditableTextBox_Name.visible = false", "owner.EditableTextBox_Name.alive = false", "owner.EditableTextBox_Name.IsReadOnly = true", "focused_field = nil"]:
            with self.subTest(condition=condition):
                self.setUp()
                self.run_lua(f'make_owner("W_CreateServer", true); {condition}; tick(nil, 0.25); frame(); assert(windows == nil)')

    def test_generic_focused_fields_and_password_mask(self):
        for kind in ["EditableTextBox", "EditableText", "MultiLineEditableTextBox", "MultiLineEditableText"]:
            with self.subTest(kind=kind):
                self.setUp()
                self.run_lua(f'f = make_field(true); f.IsPassword = true; owners["Class /Script/UMG.{kind}"] = {{f}}; tick(nil, 0.25); press(0x1000); frame(); assert(windows == 1); assert(drawn[1]:find("Keys sent: *", 1, true))')

    def test_dialog_hidden_or_transport_lost_close_overlay(self):
        for condition in ["owner.visible = false", "owner.EditableTextBox_Name.alive = false", "drawing_ui = true", 'transport = "unfocused"']:
            with self.subTest(condition=condition):
                self.setUp(); self.open()
                self.run_lua(f'{condition}; tick(nil, 0.016); frame(); assert(windows == 1 and settings.FrameworkConfig_AlwaysShowCursor == "false")')

    def test_selection_press_is_not_typed_and_close_button_is_drained(self):
        self.run_lua('''
            poll(0x1000); make_owner("W_CreateServer", true); tick(nil, 0.25)
            assert(poll(0x1000).wButtons == 0); tick(nil, 0.016)
            assert(event_count("text") == 0)
            poll(0); poll(0x4000); tick(nil, 0.016); settle()
            assert(poll(0x4000).wButtons == 0)
            poll(0); assert(poll(0x1000).wButtons == 0x1000)
        ''')

    def test_input_consumed_only_while_active_and_reset_restores(self):
        self.run_lua('assert(poll(0x1000, 20000, 20000).wButtons == 0x1000)')
        self.open()
        self.run_lua('''
            p = poll(8, 20000, 20000); assert(p.wButtons == 0 and p.sThumbLX == 0 and p.sThumbLY == 0)
            reset(); assert(settings.VR_SnapTurn == "true" and settings.FrameworkConfig_AlwaysShowCursor == "false")
            poll(0); assert(poll(8).wButtons == 8)
        ''')

    def test_done_suppresses_reopen_until_focus_changes(self):
        self.run_lua('''
            f = make_field(true); owners["Class /Script/UMG.EditableTextBox"] = {f}; tick(nil, 0.25); frame()
            press(0x4000); settle(); tick(nil, 0.25); frame(); assert(windows == 1)
            focused_field = nil; tick(nil, 0.25); f:SetKeyboardFocus(); tick(nil, 0.25); frame(); assert(windows == 2)
        ''')

    def test_manual_shortcut_only_resolves_one_visible_name_dialog(self):
        self.run_lua('''
            make_owner("W_CreateServer", false); tick(nil,0.25); press(0x8000); frame(); assert(windows == 1)
            reset(); focused_field = nil; make_owner("W_NewGame", false)
            tick(nil,0.25); press(0x8000); frame(); assert(windows == 1)
        ''')

    def test_unavailable_transport_never_opens_or_sends_text(self):
        for state in ["missing", "unfocused"]:
            with self.subTest(state=state):
                self.setUp()
                self.run_lua(f'transport = "{state}"; make_owner("W_CreateServer", true); tick(nil, 0.25); frame(); assert(windows == nil and event_count("text") == 0)')

    def test_first_controller_with_input_drives_keyboard(self):
        self.open()
        self.run_lua('''
            poll(0, 0, 0, 0); poll(0x1000, 0, 0, 1); tick(nil, 0.016)
            assert(text_entered() == "q")
            poll(0, 0, 0, 1); poll(0x2000, 0, 0, 1); tick(nil, 0.016)
            assert(event_count("backspace") == 1)
        ''')

    def test_navigation_repeats_and_wraps_rows(self):
        self.open()
        self.run_lua('''
            press(4); press(0x1000); assert(text_entered() == "p")
            poll(8); tick(nil, 0.016); poll(8); tick(nil, 0.1)
            poll(0); press(0x1000); assert(text_entered() == "pq")
            press(2); press(2); press(2); press(0x1000) -- Shift on action row
            assert(text_entered() == "pq")
        ''')

    def test_generic_pointer_focus_loss_does_not_discard_clicked_key(self):
        self.open()
        self.run_lua('focused_field=nil; click("q"); assert(text_entered()=="q"); assert(settings.FrameworkConfig_AlwaysShowCursor=="true")')

    def test_done_waits_for_pending_characters_and_commits_before_focus_handoff(self):
        self.open()
        self.run_lua('''clicked="q"; frame(); clicked="Done"; frame(); tick(nil,0.016)
            assert(focused_field==owner.EditableTextBox_Name and text_entered()=="q")
            settle(); assert(event_count("commit")==1 and focused_field==owner.Button_Yes)''')

    def test_done_is_bottom_right_grid_button_and_action_row_fits(self):
        self.open()
        self.run_lua('''
            local last=buttons_drawn[#buttons_drawn]
            assert(last.label=="DONE - Right B##5:5" and last.width>=190)
            local width,count=0,0
            for _,button in ipairs(buttons_drawn) do
                assert(not button.label:find("##finish",1,true))
                if button.label:find("##5:",1,true) then
                    width=width+button.width;count=count+1
                end
            end
            assert(count==5 and width+(count-1)*8<=744)
        ''')

    def test_bottom_right_done_can_be_selected_with_navigation_and_right_a(self):
        self.open()
        self.run_lua('''
            press(0x1000) -- q
            press(2);press(2);press(2) -- Bottom row, Shift.
            press(4) -- Wrap left to bottom-right Done.
            press(0x1000);settle()
            assert(text_entered()=="q" and event_count("commit")==1)
            assert(focused_field==owner.Button_Yes)
        ''')

    def test_delivery_waits_for_actual_keyboard_focus(self):
        self.open()
        self.run_lua('''local field=owner.EditableTextBox_Name
            function field:SetKeyboardFocus() end
            focused_field=nil; clicked="q"; frame(); tick(nil,0.016)
            assert(text_entered()=="")
            focused_field=field; tick(nil,0.016); assert(text_entered()=="q")''')

    def test_enter_is_finish_and_failed_commit_keeps_keyboard_open(self):
        self.open()
        self.run_lua('''lua_event("avrcade.keyboard.finish", ""); transport_actions={["avrcade.keyboard.commit"]="failed"}; settle()
            assert(settings.FrameworkConfig_AlwaysShowCursor=="true")
            transport_actions=nil; lua_event("avrcade.keyboard.finish", ""); settle()
            assert(event_count("commit")>0)''')

    def test_suppressed_prompt_has_visible_reopen_button(self):
        self.run_lua('''make_lobby(true); tick(nil,0.25); press(0x4000); settle(); tick(nil,0.25)
            frame(); assert(table.concat(drawn," "):find("Left upper",1,true))
            clicked="Open keyboard"; frame(); tick(nil,0.016)
            assert(focused_field==lobby.Textfield_SaveGame.EditableText_143)''')

    def test_switching_to_another_text_field_does_not_reclaim_the_old_target(self):
        self.open()
        self.run_lua('''other=make_field(true); owners["Class /Script/UMG.EditableText"]={other}
            tick(nil,0.016); click("q"); assert(focused_field==other and text_entered()=="q")''')

    def test_focus_failure_can_close_without_sending_or_committing(self):
        self.open()
        self.run_lua('''function owner.EditableTextBox_Name:SetKeyboardFocus() end
            focused_field=nil; clicked="q"; frame(); clicked="Done"; frame()
            tick(nil,0.016); assert(event_count("text")==0 and event_count("commit")==0)
            assert(event_count("end")==1)''')

    def test_failed_focus_during_open_ends_native_enter_capture(self):
        self.run_lua('''make_owner("W_CreateServer",true)
            function owner.EditableTextBox_Name:SetKeyboardFocus() error("no Slate widget") end
            tick(nil,0.25); assert(event_count("end")==1)''')

    def test_physical_right_b_finishes_from_another_controller_index(self):
        self.open()
        self.run_lua('''poll(0x1000,0,0,0); tick(nil,0.016); poll(0,0,0,0)
            poll(0x4000,0,0,1); settle()
            assert(event_count("commit")==1 and event_count("backspace")==0)
            assert(poll(0x4000,0,0,1).wButtons==0)
            poll(0,0,0,1); assert(poll(0x1000,0,0,1).wButtons==0x1000)''')

    def test_settings_chord_disabled_without_touching_camera_alignment(self):
        self.run_lua('''settings.VR_CameraRightOffset="0.000000"; tick(nil,0.25)
            assert(settings.FrameworkConfig_EnableL3R3Toggle=="false")
            assert(settings.VR_CameraRightOffset=="0.000000")''')

    def test_single_button_modal_accepts_right_a_or_right_b_only_on_press(self):
        for button in ["0x1000","0x4000"]:
            self.setUp()
            self.run_lua(f'''make_modal(true,false); tick(nil,0.25); assert(modal.clicks==nil)
                poll({button}); tick(nil,0.016); assert(modal.clicks==1)
                tick(nil,0.25); assert(poll({button}).wButtons==0); assert(modal.clicks==1)''')

    def test_popup_interrupts_keyboard_without_sending_to_obscured_field(self):
        self.run_lua('''make_lobby(true); tick(nil,0.25); make_modal(true,false); tick(nil,0.016)
            press(0x1000); assert(event_count("text")==0 and modal.clicks==1)
            tick(nil,0.25); press(0x1000); assert(text_entered()=="q")''')

    def test_choice_or_detached_or_disabled_modal_is_not_acknowledged(self):
        for condition in ["modal.attached=false","modal.visible=false","modal.Button2.visible=true","modal.Button1.enabled=false","modal.handler_missing=true"]:
            self.setUp()
            self.run_lua(f'make_modal(true,false); {condition}; tick(nil,0.25); press(0x1000); assert(modal.clicks==nil)')

    def test_modal_click_is_bound_to_the_popup_the_user_saw(self):
        self.run_lua('''make_modal(true,false); tick(nil,0.25); poll(0x1000)
            make_modal(true,false); tick(nil,0.016); assert(modal.clicks==nil)''')

    def test_nested_popup_attachment_and_failed_acknowledgement_fallback(self):
        self.run_lua('''make_modal(false,false)
            modal.parent={alive=true,is_a=function() return true end,IsInViewport=function() return true end}
            tick(nil,0.25); press(0x1000); assert(modal.clicks==1)
            make_modal(true,false)
            modal["BndEvt__W_FancyModal_Button1_K2Node_ComponentBoundEvent_0_OnClick__DelegateSignature"]=function() error("Unavailable") end
            tick(nil,0.25); press(0x1000); tick(nil,0.016)
            assert(poll(0x1000).wButtons==0x1000)''')

    def test_popup_reflection_error_does_not_capture_game_input(self):
        self.run_lua('''make_modal(true,false)
            function modal.Button1:GetIsEnabled() error("layout changed") end
            tick(nil,0.25); assert(poll(0x1000).wButtons==0x1000)''')


if __name__ == "__main__":
    unittest.main()
