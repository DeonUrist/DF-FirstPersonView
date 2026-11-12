-- First-person view for adventure mode
--@ module = true

--[====[
first-person
============

Tags: adventure | graphics | interface

Renders adventure mode from a first-person perspective.

The map area is replaced with a ray-cast 3D view of the z-level you are on,
drawn with the sprites the game itself uses for the tiles, plants, items and
creatures around you. The rest of the adventure mode interface keeps working
on top of it. Press the hotkey again to return to the normal view.

Usage
-----

    first-person [toggle]
    first-person on|off
    first-person status
    first-person hotkey [<key>]
    first-person set <setting> <value>

Examples
--------

``first-person``
    Toggle the first-person view. Bound to Ctrl-Shift-F by default.
``first-person hotkey Alt-V``
    Toggle with Alt-V instead. The choice is remembered.
``first-person set fov 90``
    Use a wider field of view.
``first-person status``
    Show whether the view is on and list every setting with its value.

Controls
--------

With ``relative_controls`` on (the default), movement keys are relative to
the direction you are looking while the view is active: up moves forward,
down moves backward, left and right turn by 45 degrees, and the diagonal keys
step diagonally without turning. All other keys behave as usual.

With ``relative_controls`` off, keys keep their normal compass meaning and
the view turns to face the direction you last moved in.
]====]

local gui = require('gui')
local overlay = require('plugins.overlay')

local config = reqscript('internal/first-person/config')
local fbuf = reqscript('internal/first-person/framebuffer')
local sprites = reqscript('internal/first-person/sprites')
local scene_mod = reqscript('internal/first-person/scene')
local raycaster = reqscript('internal/first-person/raycaster')

local GLOBAL_KEY = 'first-person'
local TOGGLE_COMMAND = 'first-person toggle'

local DIR_NAMES = {[0]='N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'}
local DIR_DX = {[0]=0, 1, 1, 1, 0, -1, -1, -1}
local DIR_DY = {[0]=-1, -1, 0, 1, 1, 1, 0, -1}

local MOVE_SPEED = 9          -- tiles per second when gliding to a new tile
local TURN_SPEED = 7.5        -- radians per second
local CLIMB_SPEED = 7         -- tile widths per second when changing height
local SCENE_INTERVAL_MS = 120
local SCENE_INTERVAL_MOVING_MS = 60

state = state or {
    active=false,
    heading=0,      -- 0..7, index into DIR_NAMES
}
registered_hotkey = registered_hotkey or nil
frame = frame or {}

---------------------------------------------------------------------------
-- hotkey

local function hotkey_spec(key)
    if key:find('@', 1, true) then return key end
    return key .. '@dungeonmode'
end

-- (re)register the toggle keybinding from the settings
function apply_hotkey()
    local want = config.get('hotkey')
    if registered_hotkey == want then return true end
    if registered_hotkey then
        dfhack.hotkey.removeKeybind(hotkey_spec(registered_hotkey), true, TOGGLE_COMMAND)
        registered_hotkey = nil
    end
    if dfhack.hotkey.addKeybind(hotkey_spec(want), TOGGLE_COMMAND) then
        registered_hotkey = want
        return true
    end
    return false
end

---------------------------------------------------------------------------
-- on / off

local function get_adventurer()
    return dfhack.world.getAdventurer()
end

function set_active(on)
    if on then
        if not get_adventurer() then
            return false, 'the first-person view only works while you control an adventurer'
        end
        state.active = true
        state.snap = true
        state.dirty = true
        state.error = nil
    else
        state.active = false
    end
    return true
end

function isActive()
    return state.active
end

---------------------------------------------------------------------------
-- per-frame update

local function daylight()
    -- 1200 ticks per day; fade around dawn (5-7h) and dusk (18-20h)
    local hour = (df.global.cur_year_tick % 1200) / 50
    local function ramp(a, b, x)
        if x <= a then return 0 end
        if x >= b then return 1 end
        return (x - a) / (b - a)
    end
    local d = math.min(ramp(5, 7, hour), 1 - ramp(18, 20, hour))
    return math.floor(d * 16 + 0.5) / 16
end

local function in_default_adventure_ui()
    local default_menu = df.ui_advmode_menu and df.ui_advmode_menu.Default or 0
    if df.global.adventure.menu ~= default_menu then return false end
    return dfhack.gui.matchFocusString('dungeonmode/Default') or
        dfhack.gui.matchFocusString('dungeonmode/ADVENTURE_ARENA')
end

local function angle_diff(to, from)
    local d = (to - from) % (2 * math.pi)
    if d > math.pi then d = d - 2 * math.pi end
    return d
end

local function draw_compass()
    local h = state.heading
    local text = (' %s  %s [%s] %s  %s '):format(
        DIR_NAMES[(h + 6) % 8], DIR_NAMES[(h + 7) % 8], DIR_NAMES[h],
        DIR_NAMES[(h + 1) % 8], DIR_NAMES[(h + 2) % 8])
    local sw = dfhack.screen.getWindowSize()
    local x = (sw - #text) // 2
    dfhack.screen.paintString({fg=COLOR_WHITE, bg=COLOR_BLACK}, x, 0, text)
end

local function update()
    local adv = get_adventurer()
    if not adv then
        state.active = false
        return
    end
    -- menus that need the map (look, aim, travel ...) get the normal view back
    local default_menu = df.ui_advmode_menu and df.ui_advmode_menu.Default or 0
    if df.global.adventure.menu ~= default_menu then
        state.snap = true
        return
    end

    local vp = df.global.gps.main_viewport
    local cols, rows = vp.dim_x, vp.dim_y
    if cols < 1 or rows < 1 or cols > scene_mod.MAX_COLS then return end

    local cfg = config.all()

    local now = dfhack.getTickCount()
    local dt = now - (state.last_ms or now)
    state.last_ms = now
    if dt < 0 then dt = 0 elseif dt > 250 then dt = 250 end

    -- follow the adventurer
    local ax, ay, az = adv.pos.x, adv.pos.y, adv.pos.z
    local snap = state.snap
    state.snap = false
    local need_scene, full_scene = false, snap or not state.ax
    if state.ax ~= ax or state.ay ~= ay or state.az ~= az then
        if state.ax then
            local dx, dy = ax - state.ax, ay - state.ay
            if math.abs(az - state.az) > 1 or math.abs(dx) > 1 or math.abs(dy) > 1 then
                snap = true
            elseif not cfg.relative_controls then
                for i = 0, 7 do
                    if DIR_DX[i] == dx and DIR_DY[i] == dy then
                        state.heading = i
                        break
                    end
                end
            end
            if az ~= state.az then full_scene = true end
        else
            snap = true
        end
        state.ax, state.ay, state.az = ax, ay, az
        need_scene = true
    end

    -- distance haze: pale by day under the sky, dark at night and underground
    local light = daylight()
    local des = dfhack.maps.getTileFlags(ax, ay, az)
    local haze = (des and des.subterranean) and 0 or math.floor(light * 4 + 0.5) / 4
    if sprites.set_fog(math.floor(8 + 136 * haze), math.floor(9 + 165 * haze), math.floor(12 + 190 * haze)) then
        scene_mod.reset()
        full_scene = true
        state.dirty = true
    end

    -- refresh the scene every so often, and right away when something moved
    local moving = state.moving
    local interval = moving and SCENE_INTERVAL_MOVING_MS or SCENE_INTERVAL_MS
    if need_scene or full_scene or now - (state.scene_ms or 0) >= interval or now < (state.scene_ms or 0) then
        if scene_mod.capture(now / 1000, az, cfg.levels_down, cfg.levels_up, full_scene) then
            state.dirty = true
        end
        state.bills = scene_mod.billboards(az, cfg.levels_down, cfg.levels_up)
        state.scene_ms = now
    end

    -- where the camera wants to be
    local tx, ty = ax + 0.5, ay + 0.5
    local tang = state.heading * math.pi / 4
    local teye = az * raycaster.LEVEL_H + raycaster.EYE
    if scene_mod.ground_at(ax, ay, az) == 'ramp' then
        teye = teye + raycaster.LEVEL_H / 2
    end
    if snap or not cfg.smooth or not state.camx or not state.eye then
        state.camx, state.camy, state.ang, state.eye = tx, ty, tang, teye
    else
        local step = MOVE_SPEED * dt / 1000
        local dx, dy = tx - state.camx, ty - state.camy
        local dist = math.sqrt(dx * dx + dy * dy)
        if dist <= step then
            state.camx, state.camy = tx, ty
        else
            state.camx = state.camx + dx / dist * step
            state.camy = state.camy + dy / dist * step
        end
        local da = angle_diff(tang, state.ang)
        local astep = TURN_SPEED * dt / 1000
        if math.abs(da) <= astep then
            state.ang = tang
        else
            state.ang = state.ang + (da > 0 and astep or -astep)
        end
        local de = teye - state.eye
        local estep = CLIMB_SPEED * dt / 1000
        if math.abs(de) <= estep then
            state.eye = teye
        else
            state.eye = state.eye + (de > 0 and estep or -estep)
        end
    end
    state.moving = state.camx ~= tx or state.camy ~= ty or state.ang ~= tang or state.eye ~= teye

    -- full resolution when standing still, half while the camera is moving
    local K = math.min(cfg.quality, cfg.max_width // cols)
    if state.moving and cfg.fast_motion then K = K // 2 end
    if K < 2 then K = 2 end
    local pool, err = fbuf.ensure(cols, rows, K)
    if not pool then
        error(err)
    end

    local r = state.rendered or {}
    if state.dirty or r.K ~= K or r.camx ~= state.camx or r.camy ~= state.camy or r.ang ~= state.ang or
            r.eye ~= state.eye or r.gen ~= fbuf.generation or r.fov ~= cfg.fov or
            r.view ~= cfg.view_distance or r.light ~= light then
        local map_w, map_h = dfhack.maps.getTileSize()
        local t0 = os.clock()
        raycaster.render(frame, pool.W, pool.H, scene_mod.stacks, state.bills or {},
            {x=state.camx, y=state.camy, eye=state.eye, ang=state.ang, fov=cfg.fov, pitch=0},
            {view_distance=cfg.view_distance, daylight=light, map_w=map_w, map_h=map_h,
             skip_x=ax, skip_y=ay, skip_z=az})
        local t1 = os.clock()
        fbuf.flush(pool, frame)
        state.render_ms = (t1 - t0) * 1000
        state.flush_ms = (os.clock() - t1) * 1000
        state.frames = (state.frames or 0) + 1
        state.dirty = false
        r.camx, r.camy, r.ang, r.eye = state.camx, state.camy, state.ang, state.eye
        r.gen, r.fov, r.view, r.light, r.K = fbuf.generation, cfg.fov, cfg.view_distance, light, K
        state.rendered = r
    end

    fbuf.present(pool)
    if cfg.compass then draw_compass() end
end

---------------------------------------------------------------------------
-- input

-- relative offset (in 45 degree steps) for each movement key
local MOVE_KEYS = {
    {'A_MOVE_N', 0}, {'A_MOVE_NE', 1}, {'A_MOVE_SE', 3}, {'A_MOVE_S', 4},
    {'A_MOVE_SW', 5}, {'A_MOVE_NW', 7},
}
local TURN_KEYS = {
    {'A_MOVE_W', -1}, {'A_MOVE_E', 1},
}
-- "move and climb/descend" variants keep their vertical part
local VERTICAL_KEYS = {}
for _, suffix in ipairs{'_UP', '_DOWN'} do
    for i = 0, 7 do
        table.insert(VERTICAL_KEYS, {'A_MOVE_' .. DIR_NAMES[i] .. suffix, i, suffix})
    end
end

local function send_key(key)
    local scr = dfhack.gui.getDFViewscreen(true)
    state.passthrough = true
    local ok, err = pcall(gui.simulateInput, scr, key)
    state.passthrough = false
    if not ok then dfhack.printerr(('first-person: %s'):format(tostring(err))) end
end

local function handle_input(keys)
    if not state.active or state.passthrough then return false end
    if not config.get('relative_controls') then return false end
    if not in_default_adventure_ui() then return false end

    for _, k in ipairs(TURN_KEYS) do
        if keys[k[1]] then
            state.heading = (state.heading + k[2]) % 8
            return true
        end
    end
    for _, k in ipairs(MOVE_KEYS) do
        if keys[k[1]] then
            send_key('A_MOVE_' .. DIR_NAMES[(state.heading + k[2]) % 8])
            return true
        end
    end
    for _, k in ipairs(VERTICAL_KEYS) do
        if keys[k[1]] then
            send_key('A_MOVE_' .. DIR_NAMES[(state.heading + k[2]) % 8] .. k[3])
            return true
        end
    end
    return false
end

---------------------------------------------------------------------------
-- overlay

FirstPersonOverlay = defclass(FirstPersonOverlay, overlay.OverlayWidget)
FirstPersonOverlay.ATTRS{
    desc='Draws adventure mode in first person when toggled with the first-person command.',
    default_enabled=true,
    fullscreen=true,
    viewscreens='dungeonmode',
    version=1,
}

function FirstPersonOverlay:render()
    -- capturing other z-levels makes the game redraw, which lands back here
    if not state.active or scene_mod.capturing then return end
    local ok, err = xpcall(update, debug.traceback)
    if not ok then
        -- never leave a broken renderer running every frame
        state.active = false
        state.error = tostring(err)
        dfhack.printerr('first-person: view switched off after an error:')
        dfhack.printerr(state.error)
    end
end

function FirstPersonOverlay:onInput(keys)
    return handle_input(keys)
end

OVERLAY_WIDGETS = {view=FirstPersonOverlay}

---------------------------------------------------------------------------
-- lifecycle

dfhack.onStateChange[GLOBAL_KEY] = function(sc)
    if sc == SC_MAP_UNLOADED or sc == SC_WORLD_UNLOADED then
        state.active = false
        state.bills = nil
        state.ax = nil
        -- texture numbers are reassigned when another world loads
        sprites.clear()
        scene_mod.reset()
        fbuf.release()
    elseif sc == SC_MAP_LOADED then
        sprites.clear()
        scene_mod.reset()
        state.bills = nil
        state.ax = nil
        if config.get('start_enabled') and get_adventurer() then
            set_active(true)
        end
    end
end

apply_hotkey()

if dfhack_flags.module then
    return
end

---------------------------------------------------------------------------
-- command line

local function print_status()
    print(('First-person view is %s.'):format(state.active and 'ON' or 'off'))
    if state.error then
        print('Last error: ' .. state.error)
    end
    if state.frames then
        print(('Frames drawn: %d (last frame: %.1f ms render, %.1f ms upload)')
            :format(state.frames, state.render_ms or 0, state.flush_ms or 0))
    end
    print()
    print('Settings (change with "first-person set <setting> <value>"):')
    for _, key in ipairs(config.ORDER) do
        print(('  %-18s %-14s %s'):format(key, tostring(config.get(key)), config.SCHEMA[key].desc))
    end
end

local function set_setting(key, value)
    if not key or value == nil then
        qerror('usage: first-person set <setting> <value>')
    end
    local previous = config.get(key)
    local ok, err = config.set(key, value)
    if not ok then qerror(err) end
    if key == 'hotkey' and not apply_hotkey() then
        config.set('hotkey', previous)
        apply_hotkey()
        qerror(('"%s" is not a valid key; keeping %s'):format(tostring(value), tostring(previous)))
    end
    state.dirty = true
    print(('%s = %s'):format(key, tostring(config.get(key))))
end

local args = {...}
local cmd = args[1] or 'toggle'

if cmd == 'toggle' or cmd == 'on' or cmd == 'off' then
    local on = cmd == 'on' or (cmd == 'toggle' and not state.active)
    local ok, err = set_active(on)
    if not ok then qerror(err) end
    print(('First-person view %s.'):format(state.active and 'on' or 'off'))
elseif cmd == 'status' then
    print_status()
elseif cmd == 'hotkey' then
    if args[2] then
        set_setting('hotkey', args[2])
    else
        print(('Hotkey: %s'):format(config.get('hotkey')))
    end
elseif cmd == 'set' then
    set_setting(args[2], args[3])
elseif cmd == 'reload' then
    config.reload()
    apply_hotkey()
    state.dirty = true
    print('Settings reloaded.')
else
    print(dfhack.script_help())
end
