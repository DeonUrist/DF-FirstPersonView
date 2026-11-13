-- First-person view for adventure mode
--@ module = true

--[====[
first-person
============

Tags: adventure | graphics | interface

Shows adventure mode from a first-person perspective.

The map area is replaced with a 3D view of your surroundings, drawn with the
sprites the game itself uses for the tiles, plants, items and creatures around
you, across several z-levels. The rest of the adventure mode interface keeps
working on top of it. Press the hotkey again to return to the normal view.

The drawing is done by the ``fpview`` plugin (``hack/plugins/fpview.plug.dll``),
which has to be installed alongside this script. This script provides the
hotkey and remembers your settings.

Usage
-----

    first-person [toggle]
    first-person on|off
    first-person status
    first-person hotkey [<key>]
    first-person set <setting> <value>
    first-person realtime [on|off|toggle]
    first-person reload

Examples
--------

``first-person``
    Toggle the first-person view. Bound to Ctrl-Shift-F by default.
``first-person hotkey Alt-V``
    Toggle with Alt-V instead. The choice is remembered.
``first-person set fov 90``
    Use a wider field of view.
``first-person set resolution 50``
    Draw at half the window resolution (faster on slow machines).
``first-person status``
    Show whether the view is on and list every setting with its value.
``first-person realtime``
    Let time pass by itself (``realtime_rate`` game ticks a second), or stop it.
    The same as the timer button. Space also stops it.
``first-person reload``
    Read the settings and the art in the mod's ``media`` folder again.

Controls
--------

With ``relative_controls`` on (the default), movement keys are relative to
the direction you are looking while the view is active: up moves forward,
down moves backward, left and right turn by 45 degrees, and the diagonal keys
step diagonally without turning. All other keys behave as usual.

With ``relative_controls`` off, keys keep their normal compass meaning and
the view turns to face the direction you last moved in.
]====]

local config = reqscript('internal/first-person/config')

local GLOBAL_KEY = 'first-person'
local TOGGLE_COMMAND = 'first-person toggle'
local PLUGIN = config.PLUGIN

registered_hotkey = registered_hotkey or nil

local gui = require('gui')
local overlay = require('plugins.overlay')
local widgets = require('gui.widgets')

-- the plugin keeps these globals up to date while it runs
local function minimap_shown()
    return _G.fpview_minimap_shown == true
end

local function view_shown()
    return _G.fpview_view_shown == true
end

local function realtime_on()
    return _G.fpview_realtime_on == true
end

---------------------------------------------------------------------------
-- interface buttons, framed like the game's own panels

-- where the mod is installed, and its art folder
function mod_dir()
    local path = dfhack.findScript('first-person')
    if not path then return nil end
    path = path:gsub('\\', '/')
    return (path:gsub('/scripts_modinstalled/first%-person%.lua$', ''))
end

function media_dir()
    local dir = mod_dir()
    return dir and (dir .. '/media') or nil
end

-- a 16 x 12 picture from the art folder as two interface tiles; nil if it is not there
icon_handles = icon_handles or {}
local function icon_tile(name, half)
    local handles = icon_handles[name]
    if handles == nil then
        handles = false
        local dir = media_dir()
        local file = dir and (dir .. '/' .. name .. '.png')
        if file and dfhack.filesystem.exists(file) then
            local ok, loaded = pcall(dfhack.textures.loadTileset, file, 8, 12, true)
            if ok and loaded and #loaded >= 2 then handles = loaded end
        end
        icon_handles[name] = handles
    end
    if not handles then return nil end
    return dfhack.textures.getTexposByHandle(handles[half])
end

-- DFHack keeps every picture it has loaded, by file name, until the game is
-- closed: art installed or edited while the game runs would never show (the
-- camera button kept its old look that way). So each version of a picture is
-- loaded from its own copy, named after the file's modification time.
local function art_copy(file, name, mtime)
    local dir = require('script-manager').getModStatePath(config.MOD_ID) .. 'art'
    if not dfhack.filesystem.mkdir_recursive(dir) then return file end
    mtime = math.floor(mtime)
    local copy = ('%s/%s-%d.png'):format(dir, name, mtime)
    if dfhack.filesystem.isfile(copy) then return copy end
    local src = io.open(file, 'rb')
    if not src then return file end
    local data = src:read('*a')
    src:close()
    local dst = io.open(copy, 'wb')
    if not dst then return file end
    dst:write(data)
    dst:close()
    -- older copies of the same picture are of no more use
    for _, other in ipairs(dfhack.filesystem.listdir(dir) or {}) do
        if other ~= ('%s-%d.png'):format(name, mtime) and other:match('^' .. name:gsub('%W', '%%%0') .. '%-%d+%.png$') then
            os.remove(dir .. '/' .. other)
        end
    end
    return copy
end

-- a whole button picture from the art folder, cut into w x h interface
-- cells (8 x 12 pixels each); nil if it is missing or not that size. The
-- file is looked at again every few seconds, so new art shows by itself.
button_pictures = button_pictures or {}
local function button_tiles(name, w, h)
    local entry = button_pictures[name]
    local now = dfhack.getTickCount()
    if not entry or now - entry.checked > 3000 then
        local dir = media_dir()
        local file = dir and (dir .. '/' .. name .. '.png')
        local mtime = file and dfhack.filesystem.isfile(file) and dfhack.filesystem.mtime(file) or nil
        if not entry or entry.mtime ~= mtime then
            entry = {mtime=mtime, handles=false}
            if mtime then
                local ok, loaded = pcall(dfhack.textures.loadTileset, art_copy(file, name, mtime), 8, 12, true)
                if ok and loaded and #loaded == w * h then entry.handles = loaded end
            end
            button_pictures[name] = entry
        end
        entry.checked = now
    end
    return entry.handles or nil
end

-- Paints a button the size of the game's square ones: the picture if there
-- is one, else fallback(dc, pen) draws it in text characters.
local function paint_button(dc, name, w, h, lit, hover, fallback)
    local handles = button_tiles(name, w, h)
    if handles then
        for y = 0, h - 1 do
            for x = 0, w - 1 do
                dc:seek(x, y):tile(nil, dfhack.textures.getTexposByHandle(handles[y * w + x + 1]))
            end
        end
        return
    end
    for y = 0, h - 1 do
        dc:seek(0, y):string((' '):rep(w), {fg=COLOR_BLACK, bg=COLOR_BLACK})
    end
    fallback(dc, {fg=(hover or lit) and COLOR_YELLOW or COLOR_WHITE, bg=COLOR_BLACK})
end

-- rows of characters (numbers are code page 437 codes) drawn from the top left
local function char_rows(dc, rows, pen)
    for y, row in ipairs(rows) do
        for x, ch in ipairs(row) do
            dc:seek(x - 1, y - 1):char(type(ch) == 'number' and string.char(ch) or ch, pen)
        end
    end
end

-- the fallbacks: a camera, a map, a sleeper's "Zz", each in 4 x 3 cells.
-- The camera is solid blocks with a ring for the lens (9), no letters.
local function camera_chars(dc, pen)
    char_rows(dc, {{' ', 220, 220, ' '}, {219, 9, 219, 219}, {223, 223, 223, 223}}, pen)
end

local function map_chars(dc, pen)
    char_rows(dc, {{218, 196, 196, 191}, {179, '~', 30, 179}, {192, 196, 196, 217}}, pen)
end

local function sleep_chars(dc, pen)
    char_rows(dc, {{' ', ' ', 'z', ' '}, {' ', 'Z', ' ', ' '}, {'Z', ' ', ' ', ' '}}, pen)
end

-- Mouse wheel notches over a widget since it was last drawn (up is
-- positive). The plugin counts the wheel itself, as with Alt held (to free the
-- pointer from mouse look) the game makes no key of it.
local function wheel_moved(widget)
    local total = _G.fpview_wheel
    if type(total) ~= 'number' then return 0 end
    local now = dfhack.getTickCount()
    local seen, at = widget.wheel_seen, widget.wheel_at
    widget.wheel_seen, widget.wheel_at = total, now
    -- not drawn for a while: the wheel was turned somewhere else
    if not seen or now - at > 500 then return 0 end
    return total - seen
end

-- the wheel as keys, for when the plugin does not count it: 1 up, -1 down
local function wheel_key(keys)
    if keys.CONTEXT_SCROLL_UP or keys.CONTEXT_SCROLL_PAGEUP then return 1 end
    if keys.CONTEXT_SCROLL_DOWN or keys.CONTEXT_SCROLL_PAGEDOWN then return -1 end
    return 0
end

local function plugin_counts_wheel()
    return type(_G.fpview_wheel) == 'number'
end

-- a framed button showing a picture from the art folder (or a character if there is none)
local function framed_icon(id, top, icon, fallback)
    local panel
    local function pen() return panel:getMousePos() and COLOR_YELLOW or COLOR_WHITE end
    local has_icon = icon_tile(icon, 1) ~= nil
    local text
    if has_icon then
        text = {{tile=function() return icon_tile(icon, 1) end}, {tile=function() return icon_tile(icon, 2) end}}
    else
        text = {{text=' ' .. fallback, pen=pen}}
    end
    panel = widgets.Panel{
        view_id=id,
        frame={t=top, l=0, w=4, h=3},
        frame_style=gui.FRAME_INTERIOR_MEDIUM,
        frame_background=gui.CLEAR_PEN,
        subviews={widgets.Label{frame={t=0, l=0}, text=text}},
    }
    return panel
end

-- true if the pointer is on the button, its frame included
local function on_button(panel)
    local x, y = dfhack.screen.getMousePos()
    local r = panel.frame_rect
    local p = panel.frame_parent_rect
    if not x or not r or not p then return false end
    x, y = x - p.x1, y - p.y1
    return x >= r.x1 and x <= r.x2 and y >= r.y1 and y <= r.y2
end

-- The game's square buttons beside the portrait sit 4 interface cells apart
-- (32 pixels at 8 x 12 cells), each picture a little narrower than its 4
-- cells. "View your companions" takes columns 12 to 15 of the upper row, the
-- row of buttons under it columns 12 to 35, then the speed display ("Walk")
-- columns 36 to about 45 (measured on a screenshot of the game). Ours take
-- 4 x 3 cells (32 x 36 pixels): the camera in the next slot, column 16
-- (x=17), the timer after it (columns 20 to 26). While the view shows, the
-- plugin moves the game's lower row and the speed display up beside the timer
-- (columns 27 to 60) and the action bar down right of the portrait; the
-- travel and sleep buttons follow the speed display, from column 61.
--
-- Each widget class is made anew (defclass(nil, ...)) every time this file is
-- loaded. DFHack keeps a script's globals when the file is reloaded, and
-- defclass on an existing class keeps the methods the new file no longer
-- has: an old CameraButton:init kept adding the framed " Camera " caption
-- (cut to "C") over the camera picture.

-- A camera, right of "View your companions": switches between the normal and
-- the first-person view
CameraButton = defclass(nil, overlay.OverlayWidget)
CameraButton.ATTRS{
    desc='Adds a camera button next to "View your companions" that switches between the normal and the first-person view.',
    default_enabled=true,
    default_pos={x=17, y=-4},
    viewscreens='dungeonmode',
    frame={w=4, h=3},
    version=7,
}

function CameraButton:onRenderBody(dc)
    paint_button(dc, 'button_camera', 4, 3, false, self:getMousePos() ~= nil, camera_chars)
end

-- The press is looked for while drawing, not in onInput: another mod's
-- widget in the same spot (Herald's alert) may take the click before it gets
-- here, and the button would do nothing.
function CameraButton:render(dc)
    local down = df.global.enabler.mouse_lbut_down ~= 0
    if down and not self.was_down and self:getMousePos() then
        dfhack.run_command('first-person', 'toggle')
    end
    self.was_down = down
    CameraButton.super.render(self, dc)
end

function CameraButton:onInput(keys)
    -- a click on the button is ours and goes no further
    if keys._MOUSE_L and self:getMousePos() then
        return true
    end
    return CameraButton.super.onInput(self, keys)
end

-- the timer's number one wheel notch further: by 1 up to 10, then by 5
local function next_rate(rate, dir)
    if dir > 0 then
        rate = rate < 10 and rate + 1 or rate + 5
    else
        rate = rate > 10 and math.max(10, rate - 5) or rate - 1
    end
    return math.max(1, math.min(99, rate))
end

local function wheel_rate(notches)
    local rate = config.get('realtime_rate')
    for _ = 1, math.abs(notches) do
        rate = next_rate(rate, notches)
    end
    if rate ~= config.get('realtime_rate') then
        dfhack.run_command_silent('first-person', 'set', 'realtime_rate', tostring(rate))
    end
end

local function timer_chars(dc, pen)
    char_rows(dc, {{' ', 220, ' '}, {'(', '/', ')'}, {' ', 223, ' '}}, pen)
end

-- A timer right of the camera, with the number of game ticks a second beside
-- it: a click lets time pass by itself (or stops it), the mouse wheel over it
-- changes the number. Only there while the first-person view is.
TimeButton = defclass(nil, overlay.OverlayWidget)
TimeButton.ATTRS{
    desc='Adds a timer button next to the camera button that lets time pass by itself; the mouse wheel over it sets how fast.',
    default_enabled=true,
    default_pos={x=21, y=-4},
    viewscreens='dungeonmode',
    frame={w=7, h=3},
    version=2,
    visible=view_shown,
}

function TimeButton:onRenderBody(dc)
    local on = realtime_on()
    local hover = self:getMousePos() ~= nil
    paint_button(dc, on and 'button_timer_on' or 'button_timer', 7, 3, on, hover, timer_chars)
    dc:seek(4, 1):string(('%2d'):format(config.get('realtime_rate')),
        {fg=(on or hover) and COLOR_YELLOW or COLOR_WHITE, bg=COLOR_BLACK})
end

-- pressed: see CameraButton:render
function TimeButton:render(dc)
    local down = df.global.enabler.mouse_lbut_down ~= 0
    local over = self:getMousePos() ~= nil
    if down and not self.was_down and over then
        dfhack.run_command_silent('first-person', 'realtime', 'toggle')
    end
    self.was_down = down
    local notches = wheel_moved(self)
    if notches ~= 0 and over then
        wheel_rate(notches)
    end
    TimeButton.super.render(self, dc)
end

function TimeButton:onInput(keys)
    if not self:getMousePos() then
        return TimeButton.super.onInput(self, keys)
    end
    if keys._MOUSE_L then
        return true
    end
    -- the wheel as keys never reaches the game here; the plugin's count is
    -- used when there is one, so a notch is not taken twice
    local dir = wheel_key(keys)
    if dir ~= 0 then
        if not plugin_counts_wheel() then wheel_rate(dir) end
        return true
    end
    return TimeButton.super.onInput(self, keys)
end

-- Travel (the world map) and sleep, as the game's own buttons in the bottom
-- right corner do; those are hidden while the first-person view shows, as
-- the corner holds the message list.
TravelSleepButtons = defclass(nil, overlay.OverlayWidget)
TravelSleepButtons.ATTRS{
    desc='Adds travel (map) and sleep buttons right of the speed display while the first-person view is shown.',
    default_enabled=true,
    default_pos={x=62, y=-4},
    viewscreens='dungeonmode',
    frame={w=8, h=3},
    version=2,
    visible=view_shown,
}

-- Paints the game's own 4 x 3 button picture that starts at tile col, row of
-- its INTERFACE_BITS_ADVENTURE page (graphics_interface.txt); false if the
-- game has no such picture loaded (no graphics), and nothing is painted.
local function paint_game_button(dc, col, row)
    local tiles = {}
    for y = 0, 2 do
        for x = 0, 3 do
            local tile = dfhack.screen.findGraphicsTile('INTERFACE_BITS_ADVENTURE', col + x, row + y)
            if not tile then return false end
            tiles[y * 4 + x + 1] = tile
        end
    end
    for y = 0, 2 do
        for x = 0, 3 do
            dc:seek(x, y):tile(nil, tiles[y * 4 + x + 1])
        end
    end
    return true
end

-- the game's ADVENTURE_TRAVEL and ADVENTURE_SLEEP pictures; our art (or
-- characters) only if the game has none
function TravelSleepButtons:onRenderBody(dc)
    local x = self:getMousePos()
    local travel, sleep = dc:viewport(0, 0, 4, 3), dc:viewport(4, 0, 4, 3)
    if not paint_game_button(travel, 20, 3) then
        paint_button(travel, 'button_travel', 4, 3, false, x ~= nil and x < 4, map_chars)
    end
    if not paint_game_button(sleep, 24, 3) then
        paint_button(sleep, 'button_sleep', 4, 3, false, x ~= nil and x >= 4, sleep_chars)
    end
end

function TravelSleepButtons:onInput(keys)
    local x = self:getMousePos()
    if not x then
        return TravelSleepButtons.super.onInput(self, keys)
    end
    if keys._MOUSE_L then
        -- the click is ours: the game must not also take it for one on the
        -- (hidden) map under the button
        df.global.enabler.mouse_lbut = 0
        local key = x < 4 and 'A_TRAVEL' or 'A_SLEEP'
        -- sent from outside the game's input handling, which this is part of
        dfhack.timeout(1, 'frames', function()
            if view_shown() then
                gui.simulateInput(dfhack.gui.getDFViewscreen(true), key)
            end
        end)
        return true
    end
    if keys._MOUSE_R or wheel_key(keys) ~= 0 then
        return true
    end
    return TravelSleepButtons.super.onInput(self, keys)
end

-- (the message list in the bottom right corner is drawn by the plugin)

-- next to the minimap, top to bottom: "@" draws it in text characters and
-- back, "+" and "-" zoom it
MinimapButtons = defclass(nil, overlay.OverlayWidget)
MinimapButtons.ATTRS{
    desc='Adds buttons next to the first-person minimap: @ switches it between graphics and text characters, + and - zoom it.',
    default_enabled=true,
    default_pos={x=-30, y=3},
    viewscreens='dungeonmode',
    frame={w=4, h=9},
    version=3,
    visible=minimap_shown,
}

function MinimapButtons:init()
    self:addviews{
        framed_icon('ascii', 0, 'icon_ascii', '@'),
        framed_icon('zoom_in', 3, 'icon_zoom_in', '+'),
        framed_icon('zoom_out', 6, 'icon_zoom_out', '-'),
    }
end

-- tiles added or taken per zoom click: bigger steps the farther out
local function zoom_step(tiles)
    if tiles < 15 then return 2 elseif tiles < 31 then return 4 end
    return 6
end

function MinimapButtons:onInput(keys)
    if keys._MOUSE_L then
        local views = self.subviews
        if on_button(views.ascii) then
            dfhack.run_command_silent('first-person', 'set', 'minimap_ascii', tostring(not config.get('minimap_ascii')))
            return true
        elseif on_button(views.zoom_in) then
            dfhack.run_command_silent('first-person', 'set', 'minimap_tiles',
                tostring(math.max(5, config.get('minimap_tiles') - zoom_step(config.get('minimap_tiles') - 1))))
            return true
        elseif on_button(views.zoom_out) then
            dfhack.run_command_silent('first-person', 'set', 'minimap_tiles',
                tostring(math.min(61, config.get('minimap_tiles') + zoom_step(config.get('minimap_tiles')))))
            return true
        end
    end
    return MinimapButtons.super.onInput(self, keys)
end

OVERLAY_WIDGETS = {button=CameraButton, timer=TimeButton, travel_sleep=TravelSleepButtons,
    minimap_style=MinimapButtons}

---------------------------------------------------------------------------
-- talking to the plugin

local function plugin(...)
    local output, status = dfhack.run_command_silent(PLUGIN, ...)
    return status == CR_OK, output
end

function plugin_available()
    return (plugin('status'))
end

local function require_plugin()
    if not plugin_available() then
        qerror(('The %s plugin is not loaded. Copy fpview.plug.dll into <DF>/hack/plugins '
            .. '(it must be built for your exact DFHack version) and restart the game.'):format(PLUGIN))
    end
end

-- hand every stored setting, and the art folder, to the plugin
function push_settings()
    if not plugin_available() then return false end
    local media = media_dir()
    if media then plugin('media', media) end
    for _, key in ipairs(config.ORDER) do
        if config.SCHEMA[key].plugin then
            plugin('set', key, tostring(config.get(key)))
        end
    end
    return true
end

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
-- lifecycle

dfhack.onStateChange[GLOBAL_KEY] = function(sc)
    if sc == SC_MAP_LOADED then
        push_settings()
        if config.get('start_enabled') and dfhack.world.getAdventurer() then
            plugin('on')
        end
    end
end

apply_hotkey()
push_settings()

if dfhack_flags.module then
    return
end

---------------------------------------------------------------------------
-- command line

local function print_status()
    require_plugin()
    local _, output = plugin('status')
    -- the plugin reports state and timing; settings are listed from here
    for line in output:gmatch('[^\n]+') do
        if not line:find('^  fov ') and not line:find('^  relative_controls ') then
            print(line)
        end
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
    if config.SCHEMA[key].plugin then
        plugin('set', key, tostring(config.get(key)))
    end
    print(('%s = %s'):format(key, tostring(config.get(key))))
end

local args = {...}
local cmd = args[1] or 'toggle'

if cmd == 'toggle' or cmd == 'on' or cmd == 'off' then
    require_plugin()
    push_settings()
    local ok, output = plugin(cmd)
    if not ok then qerror((output:gsub('%s+$', ''))) end
    print((output:gsub('%s+$', '')))
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
elseif cmd == 'realtime' then
    require_plugin()
    local ok, output = plugin('realtime', args[2] or 'toggle')
    if not ok then qerror((output:gsub('%s+$', ''))) end
    print((output:gsub('%s+$', '')))
elseif cmd == 'reload' then
    config.reload()
    apply_hotkey()
    push_settings()
    icon_handles = {}
    button_pictures = {}
    if media_dir() then plugin('media', media_dir(), 'reload') end
    print('Settings and art reloaded.')
else
    print(dfhack.script_help())
end
