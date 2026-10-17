-- AVRcade: local controller-driven Backrooms arms. No input or network changes.
-- Reviewed Steam skeleton: ArmsMesh, Left/RightArm, ForeArm, Hand, finger bones.
local function v(x,y,z) return {x=x,y=y,z=z} end
local function add(a,b) return v(a.x+b.x,a.y+b.y,a.z+b.z) end
local function sub(a,b) return v(a.x-b.x,a.y-b.y,a.z-b.z) end
local function mul(a,s) return v(a.x*s,a.y*s,a.z*s) end
local function dot(a,b) return a.x*b.x+a.y*b.y+a.z*b.z end
local function cross(a,b) return v(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x) end
local function length(a) return math.sqrt(dot(a,a)) end
local function unit(a,fallback)
    local n=length(a); if n<1e-7 then return fallback or v(1,0,0) end
    return mul(a,1/n)
end
local function finite(n) return type(n)=="number" and n==n and math.abs(n)<math.huge end
local function valid_vector(a) return finite(a.x) and finite(a.y) and finite(a.z) end
local function q(x,y,z,w) return {x=x,y=y,z=z,w=w} end
local function conjugate(a) return q(-a.x,-a.y,-a.z,a.w) end
local function qnorm(a)
    local n=math.sqrt(a.x*a.x+a.y*a.y+a.z*a.z+a.w*a.w)
    assert(finite(n) and n>1e-7,"invalid controller/bone rotation")
    return q(a.x/n,a.y/n,a.z/n,a.w/n)
end
local function qmul(a,b)
    return q(a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z)
end
local function rotate(a,b)
    local r=qmul(qmul(a,q(b.x,b.y,b.z,0)),conjugate(a)); return v(r.x,r.y,r.z)
end
local function perpendicular(a)
    return unit(cross(a,math.abs(a.z)<0.9 and v(0,0,1) or v(0,1,0)))
end
local function between(a,b)
    a,b=unit(a),unit(b); local d=math.max(-1,math.min(1,dot(a,b)))
    if d<-0.999999 then local axis=perpendicular(a); return q(axis.x,axis.y,axis.z,0) end
    local c=cross(a,b); return qnorm(q(c.x,c.y,c.z,1+d))
end
-- Rotation whose columns are forward/right/up in the hand bone's local frame.
local function basis(forward,up)
    local x=unit(forward)
    local z=unit(sub(up,mul(x,dot(up,x))),perpendicular(x))
    local y=unit(cross(z,x)); z=cross(x,y)
    local trace=x.x+y.y+z.z
    if trace>0 then
        local s=math.sqrt(trace+1)*2
        return qnorm(q((y.z-z.y)/s,(z.x-x.z)/s,(x.y-y.x)/s,s/4))
    elseif x.x>y.y and x.x>z.z then
        local s=math.sqrt(1+x.x-y.y-z.z)*2
        return qnorm(q(s/4,(y.x+x.y)/s,(z.x+x.z)/s,(y.z-z.y)/s))
    elseif y.y>z.z then
        local s=math.sqrt(1+y.y-x.x-z.z)*2
        return qnorm(q((y.x+x.y)/s,s/4,(z.y+y.z)/s,(z.x-x.z)/s))
    end
    local s=math.sqrt(1+z.z-x.x-y.y)*2
    return qnorm(q((z.x+x.z)/s,(z.y+y.z)/s,s/4,(x.y-y.x)/s))
end
-- Preserve the wrist goal, stretch the arm for long reach, and avoid singular elbows.
local function solve(shoulder,elbow,wrist,goal,pole)
    assert(valid_vector(goal),"nonfinite controller position")
    local a,b=length(sub(elbow,shoulder)),length(sub(wrist,elbow))
    assert(a>0.01 and b>0.01,"invalid arm lengths")
    local delta=sub(goal,shoulder); local d=length(delta)
    local axis=unit(delta,unit(sub(wrist,shoulder)))
    if d>a+b then local scale=d/(a+b); a,b=a*scale,b*scale end
    if d<math.abs(a-b) then
        -- Shorten the longer segment rather than displacing the controller wrist.
        if a>b then a=b+d else b=a+d end
    end
    local bend=sub(sub(elbow,shoulder),mul(axis,dot(sub(elbow,shoulder),axis)))
    if length(bend)<0.001 then bend=sub(pole,mul(axis,dot(pole,axis))) end
    bend=unit(bend,perpendicular(axis))
    local x=d<1e-7 and 0 or (d*d+a*a-b*b)/(2*d)
    local h=math.sqrt(math.max(0,a*a-x*x))
    return add(shoulder,add(mul(axis,x),mul(bend,h))),goal
end

local vr=uevr.params.vr
local state,settings=nil,{enabled=false}
local failures,retry,elapsed=0,0,1
local animation,prepare_body
local status,last_log,log_count="Waiting for local player",nil,0
local DATA="avrcade-backrooms-hands.json"
local function report(message)
    status=message
    if last_log==message then return end
    last_log=message
    if log_count<24 then
        log_count=log_count+1
        uevr.params.functions.log_info("[AVRCADE-BACKROOMS-HANDS] "..message)
    end
    if fs and fs.write then pcall(fs.write,"avrcade-backrooms-hands-status.txt",message.."\n") end
end
local function alive(o) return o~=nil and UEVR_UObjectHook.exists(o) end
local function restore_animation()
    if animation and alive(animation.source) then
        animation.source:set_property("VisibilityBasedAnimTickOption",animation.tick)
    end
    animation=nil
end
local function keep_animation(source)
    if animation and (not alive(animation.source) or animation.source:get_address()~=source:get_address()) then
        restore_animation()
    end
    if not animation then animation={source=source,tick=source:get_property("VisibilityBasedAnimTickOption")} end
    source:set_property("VisibilityBasedAnimTickOption",0)
end
local function restore_body()
    if not state then return end
    for _,entry in pairs(state.body) do
        if alive(entry.object) then
            entry.object:SetOwnerNoSee(entry.owner_no_see)
            if entry.tick~=nil then entry.object:set_property("VisibilityBasedAnimTickOption",entry.tick) end
        end
    end
    state.body={}
end
local function restore_source()
    restore_body()
    if state and state.applied and alive(state.source) then
        state.source:SetVisibility(state.original.visible,false)
        state.source:SetHiddenInGame(state.original.hidden,false)
        state.applied=false
    end
end
local function suspend(keep_ticking)
    if state and alive(state.mesh) then state.mesh:SetVisibility(false,false) end
    restore_source()
    -- Hidden source bones must keep refreshing while a transient bad pose clears.
    -- Otherwise retrying the same frozen pose cannot ever recover.
    if not keep_ticking then restore_animation() end
end
local function cleanup()
    pcall(suspend)
    if not state then return end
    for _,c in ipairs(state.owned) do
        if alive(c.object) then
            if c.anchor then pcall(UEVR_UObjectHook.remove_motion_controller_state,c.object) end
            -- Destroy only our components, with their owning actor as the caller.
            if alive(state.pawn) then pcall(function() c.object:K2_DestroyComponent(state.pawn) end) end
        end
    end
    state=nil
end
local function class(path)
    local c=uevr.api:find_uobject("Class /Script/Engine."..path)
    assert(c~=nil,path.." class unavailable"); return c:as_class()
end
local function initialize(pawn,source,asset)
    state={pawn=pawn,source=source,asset=asset,owned={},offsets={},materials={},body={},applied=false}
    local identity=source:K2_GetComponentToWorld()
    -- The source transform is a real, reflected FTransform; don't assume its byte layout.
    identity.Translation={X=0,Y=0,Z=0}; identity.Rotation={X=0,Y=0,Z=0,W=1}; identity.Scale3D={X=1,Y=1,Z=1}
    local function create(kind,deferred)
        local c=uevr.api:add_component_by_class(pawn,class(kind),deferred)
        assert(c~=nil,"component allocation failed")
        table.insert(state.owned,{object=c,anchor=kind=="SceneComponent"})
        return c
    end
    local function anchor(hand)
        local c=create("SceneComponent",false)
        local tracking=UEVR_UObjectHook.get_or_add_motion_controller_state(c)
        assert(tracking~=nil,"controller anchor unavailable")
        tracking:set_hand(hand)
        tracking:set_permanent(false) -- Never save transient components into the community profile.
        return c
    end
    UEVR_UObjectHook.activate()
    state.left,state.right=anchor(0),anchor(1)
    -- UPoseableMeshComponent's SetSkeletalMesh is not a reflected function on UE4.
    -- Assign before registration, then FinishAddComponent builds its bone container.
    local mesh=create("PoseableMeshComponent",true); state.mesh=mesh
    mesh:set_property("SkeletalMesh",asset)
    mesh:set_property("bVisible",false); mesh:set_property("bHiddenInGame",true)
    pawn:FinishAddComponent(mesh,true,identity)
    mesh:SetCollisionEnabled(0); mesh:SetGenerateOverlapEvents(false)
    mesh:SetOnlyOwnerSee(true); mesh:SetCastShadow(false); mesh:SetBoundsScale(4)
    state.ready=true
    prepare_body()
end
local function position(t)
    local p=t.Translation; local r=v(p.x,p.y,p.z)
    assert(valid_vector(r),"nonfinite bone/controller position"); return r
end
local function rotation(t)
    local r=t.Rotation; return qnorm(q(r.X,r.Y,r.Z,r.W))
end
local function set_transform(t,p,r)
    t.Translation={X=p.x,Y=p.y,Z=p.z}
    t.Rotation={X=r.x,Y=r.y,Z=r.z,W=r.w}
    return t
end
prepare_body=function()
    -- CharacterMesh0 is ACharacter.Mesh; Legs is the game's separate body mesh.
    -- OwnerNoSee affects only this local player's view, without changing collision,
    -- remote players, or visibility flags controlled by the community profile.
    local warmup=false
    for _,property in ipairs({"Mesh","Legs"}) do
        local body=state.pawn:get_property(property)
        local old=state.body[property]
        if old and (not alive(body) or old.object:get_address()~=body:get_address()) then
            if alive(old.object) then
                old.object:SetOwnerNoSee(old.owner_no_see)
                if old.tick~=nil then old.object:set_property("VisibilityBasedAnimTickOption",old.tick) end
            end
            state.body[property]=nil
        end
        if alive(body) and body:get_address()~=state.source:get_address() then
            if not state.body[property] then
                state.body[property]={object=body,owner_no_see=body:get_property("bOwnerNoSee"),
                    tick=body:get_property("VisibilityBasedAnimTickOption")}
                if state.body[property].tick~=nil then warmup=true end
            end
            if state.body[property].tick~=nil then body:set_property("VisibilityBasedAnimTickOption",0) end
        end
    end
    return warmup
end
local function local_body_only()
    for _,entry in pairs(state.body) do
        if alive(entry.object) then entry.object:SetOwnerNoSee(true) end
    end
end
local function tracked_material(material)
    local base=material
    for _=1,8 do
        if not alive(base) then return material end
        local kind=base:get_class():get_short_name()
        if kind=="Material" then break end
        if kind~="MaterialInstanceConstant" and kind~="MaterialInstanceDynamic" then return material end
        base=base:get_property("Parent")
    end
    if not alive(base) or base:get_class():get_short_name()~="Material" or
        base:get_fname():to_string()~="Arms_Mat_FOV" then return material end
    -- The cooked default arm material displaces vertices for flat-screen FOV.
    -- Its world-space counterpart has the same arm textures without that shader.
    local world=uevr.api:find_uobject("Material /Game/Player/Textures/Arms_Mat.Arms_Mat")
    if alive(world) then return world end
    -- The reviewed opaque hazmat material is normally loaded by the local body.
    -- Use it if the texture-matched counterpart isn't resident (no blocking loads).
    world=uevr.api:find_uobject("Material /Game/Player/Textures/Hazmat_Mat.Hazmat_Mat")
    assert(alive(world),"No reviewed world-space arm material is loaded")
    return world
end
-- Cooked Hazmat reference skeleton, relative to Spine2. The game zero-scales
-- Left/RightShoulder to hide its body arms, so those live sockets cannot anchor
-- our rig. Spine2 remains usable. Values come from the owned UE4.27 asset;
-- provenance and both complete skeletons are recorded in arm-recovery artifacts.
local body_shoulders={
    Left={p=v(7.8382473,-15.4605675,-0.0427588),r=q(-0.5410142,0.4546340,0.5437166,0.4527513)},
    Right={p=v(-7.8382473,-15.4614401,-0.0683002),r=q(0.5400935,0.4554180,0.5445838,-0.4520198)},
}
local function scaled(p,s) return v(p.x*s.x,p.y*s.y,p.z*s.z) end
local function body_frame(side)
    for _,property in ipairs({"Legs","Mesh"}) do
        local entry=state.body[property]
        local body=entry and entry.object
        if alive(body) then
            local asset=body:get_property("SkeletalMesh")
            if alive(asset) and asset:get_fname():to_string()=="Hazmat" and body:GetBoneIndex("Spine2")>=0 then
                local t=body:GetSocketTransform("Spine2",0)
                local p,r,s=position(t),rotation(t),t.Scale3D
                assert(valid_vector(s) and s.x>0.01 and s.y>0.01 and s.z>0.01,"body spine pose unavailable")
                local ref=body_shoulders[side]
                local clavicle=add(p,rotate(r,scaled(ref.p,s)))
                local orientation=qnorm(qmul(r,ref.r))
                local upper=add(clavicle,rotate(orientation,scaled(v(0,-15.699735,0),s)))
                return clavicle,orientation,upper
            end
        end
    end
end
local function arm_pose(source,side)
    local upper=source:GetSocketTransform(side.."Arm",0)
    local lower=source:GetSocketTransform(side.."ForeArm",0)
    local hand=source:GetSocketTransform(side.."Hand",0)
    local a,b,c=position(upper),position(lower),position(hand)
    assert(length(sub(b,a))>0.01 and length(sub(c,b))>0.01,"invalid arm lengths")
    rotation(upper);rotation(lower);rotation(hand)
    return {upper=upper,lower=lower,hand=hand}
end
local function update_arm(side,anchor,pole,pose)
    local mesh=state.mesh
    -- CopyPose marks the poseable render data dirty. Read this frame's animation
    -- from the source, rather than its potentially stale poseable bone cache.
    local upper,lower,hand=pose.upper,pose.lower,pose.hand
    local shoulder,elbow,wrist=position(upper),position(lower),position(hand)
    local target=anchor:K2_GetComponentToWorld(); local goal=position(target)
    if not state.offsets[side] then
        local inverse=conjugate(rotation(hand))
        local middle=position(state.source:GetSocketTransform(side.."HandMiddle1",0))
        local thumb=position(state.source:GetSocketTransform(side.."HandThumb1",0))
        state.offsets[side]=conjugate(basis(rotate(inverse,sub(middle,wrist)),rotate(inverse,sub(thumb,wrist))))
    end
    local upper_rotation,lower_rotation=rotation(upper),rotation(lower)
    if state.source:GetBoneIndex(side.."Shoulder")>=0 then
        local clavicle,orientation,body_upper=body_frame(side)
        if clavicle then
            local root=state.source:GetSocketTransform(side.."Shoulder",0)
            local delta=qnorm(qmul(orientation,conjugate(rotation(root))))
            -- Move the whole clavicle and rigidly rotate the animated chain into
            -- the body frame before IK. Source segment lengths and hand scale stay
            -- intact; the wrist goal still comes directly from its controller.
            elbow=add(body_upper,rotate(delta,sub(elbow,shoulder)))
            wrist=add(body_upper,rotate(delta,sub(wrist,shoulder)))
            shoulder=body_upper
            upper_rotation=qnorm(qmul(delta,upper_rotation))
            lower_rotation=qnorm(qmul(delta,lower_rotation))
            mesh:SetBoneTransformByName(side.."Shoulder",set_transform(root,clavicle,orientation),0)
        end
    end
    assert(length(sub(goal,shoulder))<300,"controller outside local arm range")
    local bent,new_wrist=solve(shoulder,elbow,wrist,goal,pole)
    local upper_q=qnorm(qmul(between(sub(elbow,shoulder),sub(bent,shoulder)),upper_rotation))
    local lower_q=qnorm(qmul(between(sub(wrist,elbow),sub(new_wrist,bent)),lower_rotation))
    local hand_q=qnorm(qmul(rotation(target),state.offsets[side]))
    -- Parent first; the child transforms are explicitly in UE world space.
    mesh:SetBoneTransformByName(side.."Arm",set_transform(upper,shoulder,upper_q),0)
    mesh:SetBoneTransformByName(side.."ForeArm",set_transform(lower,bent,lower_q),0)
    mesh:SetBoneTransformByName(side.."Hand",set_transform(hand,new_wrist,hand_q),0)
end
local function update()
    if settings.enabled~=true then retry=0;failures=0;suspend(); return end
    if not vr.is_hmd_active() or not vr.is_using_controllers() or
        vr.get_left_controller_index()<0 or vr.get_right_controller_index()<0 then
        suspend(); report("Waiting for tracked controllers"); return
    end
    if retry>0 then return end
    local pawn=uevr.api:get_local_pawn(0)
    if not alive(pawn) or pawn:get_class():get_short_name()~="BPCharacter_Demo_C" then
        cleanup(); report("Waiting for local gameplay pawn"); return
    end
    local source=pawn:get_property("Arms")
    if not alive(source) then cleanup(); report("Waiting for first-person arms"); return end
    for _,side in ipairs({"Left","Right"}) do
        for _,bone in ipairs({"Arm","ForeArm","Hand","HandMiddle1","HandThumb1"}) do
            if source:GetBoneIndex(side..bone)<0 then
                cleanup(); report("Unsupported arm skeleton; original rendering retained"); return
            end
        end
    end
    local asset=source:get_property("SkeletalMesh")
    if not alive(asset) then cleanup(); return end
    if state and (not alive(state.pawn) or not alive(state.source) or not alive(state.asset) or
        not state.ready or not alive(state.mesh) or not alive(state.left) or not alive(state.right) or
        state.pawn:get_address()~=pawn:get_address() or state.source:get_address()~=source:get_address() or
        state.asset:get_address()~=asset:get_address()) then cleanup() end
    keep_animation(source)
    -- Animation/costume/level transitions can temporarily collapse the bones.
    -- Validate before allocating anything, and keep hidden source animation alive.
    local poses={Left=arm_pose(source,"Left"),Right=arm_pose(source,"Right")}
    if not state then
        initialize(pawn,source,asset)
        report("Controller anchors created; waiting for first tracked frame")
        return -- UObjectHook positions new anchors during the following stereo view update.
    end
    if prepare_body() then return end -- allow hidden body/master-pose bones one refresh
    if not state.applied then
        state.original={visible=source:get_property("bVisible"),hidden=source:get_property("bHiddenInGame")}
        state.applied=true
    end
    local mesh=state.mesh
    mesh:K2_SetWorldTransform(source:K2_GetComponentToWorld(),false,{},false)
    mesh:CopyPoseFromSkeletalComponent(source)
    for i=0,math.min(source:GetNumMaterials(),8)-1 do
        local material=tracked_material(source:GetMaterial(i))
        if material~=state.materials[i] then mesh:SetMaterial(i,material); state.materials[i]=material end
    end
    update_arm("Left",state.left,v(0,-1,-1),poses.Left)
    update_arm("Right",state.right,v(0,1,-1),poses.Right)
    local_body_only()
    -- Commit visibility only after both complete poses have validated successfully.
    source:SetVisibility(false,false); source:SetHiddenInGame(true,false)
    mesh:SetHiddenInGame(false,false); mesh:SetVisibility(true,false)
    failures=0; retry=0; report("Controller hands active")
end

uevr.sdk.callbacks.on_post_engine_tick(function(_,delta)
    elapsed=elapsed+(delta or 0)
    if elapsed>=1 then
        elapsed=0
        local ok,data=pcall(json.load_file,DATA)
        settings=ok and type(data)=="table" and data or {enabled=false}
    end
    retry=math.max(0,retry-math.max(0,delta or 0))
    local ok,err=pcall(update)
    if not ok then
        failures=math.min(failures+1,6)
        retry=math.min(5,0.25*2^(failures-1))
        if state and not state.ready then cleanup() else pcall(function() suspend(true) end) end
        report("Hands suspended; retrying automatically in "..retry.."s: "..tostring(err))
    end
end)
uevr.sdk.callbacks.on_script_reset(cleanup)
uevr.sdk.callbacks.on_draw_ui(function()
    if imgui.tree_node("AVRcade Backrooms hands") then
        imgui.text(status)
        if imgui.button(settings.enabled==true and "Disable controller hands" or "Enable controller hands") then
            local next_settings={enabled=settings.enabled~=true}
            local ok,saved=pcall(json.dump_file,DATA,next_settings)
            if ok and saved==true then
                settings=next_settings
                retry=0; failures=0
            else
                report("Could not save hand preference; current setting retained")
            end
        end
        imgui.tree_pop()
    end
end)

return {solve=solve,between=between,rotate=rotate,basis=basis,qmul=qmul}
