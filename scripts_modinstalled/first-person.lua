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

-- one of the game's windows reaches into the corner of the minimap (naming an item):
-- the minimap is off for that while, and its buttons and the gear stay off it too
local function corner_covered()
    return _G.fpview_corner_covered == true
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
-- plugin moves the game's lower row up beside the timer (columns 27 to 50),
-- takes the speed display off the screen (what it says is in the message
-- list) and moves the action bar down right of the portrait; the travel and
-- sleep buttons follow the game's row, from column 51.
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

-- DFHack's logo button would lie over the paperdoll and its bars. While the
-- view is on it goes to the top right corner, at the top of the column of the
-- minimap's buttons (or over the lone gear without the minimap); when the view
-- goes off it goes back to where DFHack's overlay settings put it. Only the
-- widget's place on the screen is changed, not those settings.
local LOGO_WIDGET = 'hotkeys.adventuremenu'
logo_moved = logo_moved or false

local function frame_at(pos, old)
    local frame = {w=old.w, h=old.h}
    if pos.x < 0 then frame.r = -pos.x - 1 else frame.l = pos.x - 1 end
    if pos.y < 0 then frame.b = -pos.y - 1 else frame.t = pos.y - 1 end
    return frame
end

local function place_dfhack_logo()
    local ok, state = pcall(overlay.get_state)
    local entry = ok and state.db[LOGO_WIDGET]
    if not entry or not entry.widget then return end
    local w = entry.widget
    if view_shown() then
        local frame = frame_at({x=(minimap_shown() or corner_covered()) and -30 or -1, y=1}, w.frame)
        if not logo_moved or w.frame.r ~= frame.r or w.frame.t ~= frame.t then
            w.frame = frame
            w:updateLayout()
            logo_moved = true
        end
    elseif logo_moved then
        local config = state.config[LOGO_WIDGET]
        local pos = config and config.pos or w.default_pos
        w.frame = frame_at(pos, w.frame)
        w:updateLayout()
        logo_moved = false
    end
end

-- With the ring (Tab) the camera and the timer are on it, at its top: the
-- buttons are only shown while the first-person view is off (the camera turns
-- it on).
local function on_ring()
    return view_shown() and config.get('radial_menu')
end

-- the timer's number set with the wheel over the ring's timer: kept in the settings
local function store_ring_rate()
    local rate = _G.fpview_realtime_rate
    if type(rate) ~= 'number' then return end
    _G.fpview_realtime_rate = nil
    if rate ~= config.get('realtime_rate') then
        config.set('realtime_rate', rate)
    end
end

-- the travel map: the button has no use there and is not shown
local function traveling()
    local ok, yes = pcall(function()
        local adv = df.global.adventure
        return adv ~= nil and adv.menu == df.ui_advmode_menu.Travel
    end)
    return ok and yes
end

function CameraButton:onRenderBody(dc)
    paint_button(dc, 'button_camera', 4, 3, false, self:getMousePos() ~= nil, camera_chars)
end

-- The press is looked for while drawing, not in onInput: another mod's
-- widget in the same spot (Herald's alert) may take the click before it gets
-- here, and the button would do nothing.
function CameraButton:render(dc)
    place_dfhack_logo()
    store_ring_rate()
    if traveling() or on_ring() then
        self.was_down = df.global.enabler.mouse_lbut_down ~= 0
        return
    end
    local down = df.global.enabler.mouse_lbut_down ~= 0
    if down and not self.was_down and self:getMousePos() then
        dfhack.run_command('first-person', 'toggle')
    end
    self.was_down = down
    CameraButton.super.render(self, dc)
end

function CameraButton:onInput(keys)
    -- a click on the button is ours and goes no further
    if traveling() or on_ring() then return false end
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
    -- with the ring the timer is on it
    visible=function() return view_shown() and not config.get('radial_menu') end,
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
    desc='Adds travel (map) and sleep buttons right of the game\'s row of buttons while the first-person view is shown.',
    default_enabled=true,
    default_pos={x=52, y=-4},
    viewscreens='dungeonmode',
    frame={w=8, h=3},
    version=3,
    -- with the ring (Tab) travel and sleep are on it, and the bottom has no buttons
    visible=function() return view_shown() and not config.get('radial_menu') end,
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
    -- the plugin draws the button's caption over everything (paint_widget_hint):
    -- 1 travel, 2 sleep, and that button's top left cell
    local body = self.frame_body
    if x and body then
        _G.fpview_widget_hint = x < 4 and 1 or 2
        _G.fpview_widget_hint_x = body.x1 + (x < 4 and 0 or 4)
        _G.fpview_widget_hint_y = body.y1
    else
        _G.fpview_widget_hint = 0
    end
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

-- next to the minimap, top to bottom, under the DFHack logo (place_dfhack_logo):
-- the gear opens the settings, "@" draws the minimap in text characters and
-- back, "+" and "-" zoom it
MinimapButtons = defclass(nil, overlay.OverlayWidget)
MinimapButtons.ATTRS{
    desc='Adds buttons next to the first-person minimap: the gear opens the settings, @ switches it between graphics and text characters, + and - zoom it.',
    default_enabled=true,
    default_pos={x=-30, y=4},
    viewscreens='dungeonmode',
    frame={w=4, h=12},
    version=6,
    visible=minimap_shown,
}

function MinimapButtons:init()
    self:addviews{
        framed_icon('settings', 0, 'icon_settings', string.char(15)),
        framed_icon('ascii', 3, 'icon_ascii', '@'),
        framed_icon('zoom_in', 6, 'icon_zoom_in', '+'),
        framed_icon('zoom_out', 9, 'icon_zoom_out', '-'),
    }
end

-- The mod's settings, opened by the gear next to the minimap
SettingsScreen = defclass(nil, gui.ZScreen)
SettingsScreen.ATTRS{
    focus_path='first-person/settings',
}

-- the field of view slider: a stop every 10 degrees
local FOV_MIN, FOV_STEP = config.SCHEMA.fov.min, 10
local FOV_STOPS = (config.SCHEMA.fov.max - FOV_MIN) // FOV_STEP + 1

local function set_from_screen(key, value)
    dfhack.run_command_silent('first-person', 'set', key, tostring(value))
end

-- A number setting typed in: its hotkey (or a click) starts typing, Enter
-- applies, Esc gives up. While typing it takes every key, so digits do not
-- reach the window's other hotkeys. Not typing, it shows the stored value.
NumberField = defclass(nil, widgets.EditField)
NumberField.ATTRS{
    setting=DEFAULT_NIL,
    modal=true,
}

function NumberField:init()
    local spec = config.SCHEMA[self.setting]
    self.on_char = function(ch, text) return ch:find('%d') ~= nil and #text < #tostring(spec.max) end
    self.text_area.on_char = self.on_char
    self.text_area.on_submit = function(text)
        local n = tonumber(text)
        if n then
            set_from_screen(self.setting, math.max(spec.min, math.min(spec.max, math.floor(n))))
        end
        self:setText(tostring(config.get(self.setting)))
    end
    self:setText(tostring(config.get(self.setting)))
end

-- typing replaces the value instead of adding to it
function NumberField:setFocus(focus)
    if focus and not self.focus then self:setText('') end
    return NumberField.super.setFocus(self, focus)
end

function NumberField:render(dc)
    if not self.focus then
        local stored = tostring(config.get(self.setting))
        if self.text ~= stored then self:setText(stored) end
    end
    NumberField.super.render(self, dc)
end

function SettingsScreen:init()
    local function toggle(key, row, hotkey, label)
        return widgets.ToggleHotkeyLabel{
            view_id=key,
            frame={t=row, l=0},
            key=hotkey,
            label=label,
            initial_option=config.get(key),
            on_change=function(on)
                set_from_screen(key, on)
                -- the markers go off with the compass
                if key == 'compass' and not on then
                    self.subviews.compass_markers:setOption(false)
                end
            end,
        }
    end
    local function number(key, row, hotkey, label)
        local spec = config.SCHEMA[key]
        return NumberField{
            view_id=key,
            frame={t=row, l=0, w=48},
            key=hotkey,
            label_text=('%s (%d-%d): '):format(label, spec.min, spec.max),
            setting=key,
        }
    end
    local markers = toggle('compass_markers', 3, 'CUSTOM_M', 'Compass markers')
    markers.enabled = function() return config.get('compass') end
    self:addviews{
        widgets.Window{
            frame={w=56, h=22},
            frame_title='First Person View settings',
            subviews={
                toggle('hardware_renderer', 0, 'CUSTOM_H', 'Hardware renderer'),
                toggle('cursor_hint', 1, 'CUSTOM_C', 'Cursor hint'),
                toggle('compass', 2, 'CUSTOM_O', 'Compass'),
                markers,
                toggle('minimap', 4, 'CUSTOM_I', 'Minimap'),
                toggle('enemy_list', 5, 'CUSTOM_E', 'Enemy list'),
                toggle('auto_attack', 6, 'CUSTOM_A', 'Auto attack'),
                toggle('far_land', 7, 'CUSTOM_L', 'Far land'),
                toggle('visibility_gradient', 8, 'CUSTOM_V', 'Visibility gradient'),
                number('view_distance', 9, 'CUSTOM_D', 'Draw distance, tiles'),
                number('max_height', 10, 'CUSTOM_U', 'Maximum height, levels'),
                number('max_depth', 11, 'CUSTOM_N', 'Maximum depth, levels'),
                number('horizon', 12, 'CUSTOM_Z', 'Horizon, tiles'),
                number('far_trees', 13, 'CUSTOM_T', 'Far trees, tiles'),
                widgets.Label{
                    frame={t=14, l=2},
                    text='Type a number, Enter to apply, Esc to cancel',
                    text_pen=COLOR_GREY,
                },
                widgets.Label{
                    frame={t=16, l=0},
                    text={'FOV: ', {text=function() return tostring(config.get('fov')) end, pen=COLOR_YELLOW}},
                },
                widgets.Slider{
                    frame={t=17, l=0, w=3 * (FOV_STOPS - 1) + 7},
                    num_stops=FOV_STOPS,
                    get_idx_fn=function()
                        return (config.get('fov') - FOV_MIN + FOV_STEP // 2) // FOV_STEP + 1
                    end,
                    on_change=function(idx)
                        set_from_screen('fov', FOV_MIN + (idx - 1) * FOV_STEP)
                    end,
                },
            },
        },
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
        elseif on_button(views.settings) then
            SettingsScreen{}:show()
            return true
        end
    end
    return MinimapButtons.super.onInput(self, keys)
end

-- Without the minimap its buttons go too; the gear alone stays, in the top
-- right corner under the DFHack logo, so the settings (and the minimap) can be reached.
SettingsButton = defclass(nil, overlay.OverlayWidget)
SettingsButton.ATTRS{
    desc='Adds the gear that opens the first-person settings in the top right corner while the minimap is off.',
    default_enabled=true,
    default_pos={x=-1, y=4},
    viewscreens='dungeonmode',
    frame={w=4, h=3},
    version=2,
    visible=function() return view_shown() and not minimap_shown() and not corner_covered() end,
}

function SettingsButton:init()
    self:addviews{framed_icon('settings', 0, 'icon_settings', string.char(15))}
end

function SettingsButton:onInput(keys)
    if keys._MOUSE_L and on_button(self.subviews.settings) then
        SettingsScreen{}:show()
        return true
    end
    return SettingsButton.super.onInput(self, keys)
end

OVERLAY_WIDGETS = {button=CameraButton, timer=TimeButton, travel_sleep=TravelSleepButtons,
    minimap_style=MinimapButtons, settings=SettingsButton}

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

---------------------------------------------------------------------------
-- installing the plugin
--
-- DFHack loads plugins only from <DF>/hack/plugins, never from a mod. The mod
-- (from the Steam Workshop) carries the plugin in its dfhack_plugin folder and
-- copies it there: on the first start, and again when an update of the mod
-- brings a different file. Only at the title screen: loading or unloading the
-- plugin while a world is loaded freezes the game.

local PLUGIN_FILE = 'fpview.plug.dll'

local function read_file(path)
    local f = io.open(path, 'rb')
    if not f then return nil end
    local data = f:read('a')
    f:close()
    return data
end

local function at_title()
    if dfhack.isWorldLoaded() then return false end
    local screen = dfhack.gui.getDFViewscreen(true)
    return screen ~= nil and df.viewscreen_titlest:is_instance(screen)
end

local function install_problem(text)
    dfhack.printerr('First Person View: ' .. text:gsub('\n', ' '))
    require('gui.dialogs').showMessage('First Person View', text, COLOR_LIGHTRED)
end

-- true when done (or nothing to do), false to be tried again later
local function install_plugin()
    local dir = mod_dir()
    if not dir or dfhack.getOSType() ~= 'windows' then return true end
    local shipped = dir .. '/dfhack_plugin/'
    if not dfhack.filesystem.isfile(shipped .. PLUGIN_FILE) then return true end
    if not at_title() then return false end
    local data = read_file(shipped .. PLUGIN_FILE)
    local dest = dfhack.getHackPath() .. '/plugins/' .. PLUGIN_FILE
    if not data or read_file(dest) == data then return true end

    local built_for = (read_file(shipped .. 'dfhack_version.txt') or ''):match('^%s*(.-)%s*$')
    local running = dfhack.getDFHackVersion()
    if built_for ~= '' and built_for ~= running then
        install_problem(('This version of First Person View is made for DFHack %s,\n'
            .. 'but DFHack %s is installed. The first-person view stays off\n'
            .. 'until the mod is updated for this DFHack.'):format(built_for, running))
        return true
    end

    local was_loaded = plugin_available()
    if was_loaded then dfhack.run_command_silent('unload', PLUGIN) end
    local f = io.open(dest, 'wb')
    local written = f and f:write(data) and true
    if f then f:close() end
    dfhack.run_command_silent('load', PLUGIN)
    if not written then
        install_problem(("Could not copy the mod's plugin into\n%s\n"
            .. "Copy %s there from the mod's dfhack_plugin folder by hand\n"
            .. "and restart the game."):format(dest, PLUGIN_FILE))
    elseif not plugin_available() then
        install_problem("The mod's plugin was copied into hack/plugins but DFHack\n"
            .. "could not load it. The DFHack console says why.")
    else
        push_settings()
        print(('First Person View: %s its plugin in hack/plugins.'):format(was_loaded and 'updated' or 'installed'))
    end
    return true
end

-- once per game session, from DFHack's update rather than while the overlay
-- loads this script
local function try_install()
    if plugin_install_done then return end
    local ok, done = pcall(install_plugin)
    if not ok then
        dfhack.printerr('First Person View: installing the plugin failed: ' .. tostring(done))
        done = true
    end
    if done then
        plugin_install_done = true
    else
        dfhack.timeout(100, 'frames', try_install)
    end
end

if not plugin_install_scheduled then
    plugin_install_scheduled = true
    dfhack.timeout(1, 'frames', try_install)
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
    -- no compass, no markers on it
    if key == 'compass' and not config.get('compass') and config.get('compass_markers') then
        set_setting('compass_markers', false)
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
