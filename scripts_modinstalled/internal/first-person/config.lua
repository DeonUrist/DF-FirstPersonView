--@ module = true
-- Settings for the first-person view mod.
--
-- Settings are global (not per-world) and live in
-- <DF>/dfhack-config/mods/first_person_view/settings.json

local json = require('json')
local scriptmanager = require('script-manager')

MOD_ID = 'first_person_view'
PLUGIN = 'fpview'

-- key -> {default, kind, min, max, description}
-- entries with plugin=true are passed on to the fpview plugin
SCHEMA = {
    hotkey={default='Ctrl-Shift-F', kind='string',
        desc='DFHack keybinding that toggles the view'},
    fov={default=100, kind='number', min=40, max=150, plugin=true,
        desc='horizontal field of view in degrees'},
    resolution={default=100, kind='number', min=10, max=100, plugin=true,
        desc='rendered resolution as a percentage of the window (lower is faster)'},
    view_distance={default=48, kind='number', min=4, max=160, plugin=true,
        desc='distance in tiles at which things fade into haze'},
    range={default=2, kind='number', min=1, max=4, plugin=true,
        desc='how far terrain is tracked: 1 = one map window around you, 2 = three windows across, 3 = five'},
    levels_up={default=24, kind='number', min=0, max=40, plugin=true,
        desc='how many z-levels above you are drawn'},
    levels_down={default=8, kind='number', min=0, max=24, plugin=true,
        desc='how many z-levels below you are drawn'},
    relative_controls={default=true, kind='boolean', plugin=true,
        desc='movement keys are relative to where you look (up = forward, left/right = turn)'},
    smooth={default=true, kind='boolean', plugin=true,
        desc='animate turning, walking and climbing'},
    compass={default=true, kind='boolean', plugin=true,
        desc='show the heading indicator at the top of the screen'},
    minimap={default=true, kind='boolean', plugin=true,
        desc='show the normal top-down map in the top right corner instead of the buttons there'},
    minimap_ascii={default=false, kind='boolean', plugin=true,
        desc='draw the minimap in text characters (the @ button next to it switches this)'},
    minimap_tiles={default=11, kind='number', min=5, max=61, plugin=true,
        desc='how many tiles across the minimap shows (the + and - buttons next to it change this)'},
    wasd={default=true, kind='boolean', plugin=true,
        desc='w/s walk forward and back, a/d strafe; what those keys normally do moves to the arrow keys'},
    mouse_look={default=true, kind='boolean', plugin=true,
        desc='the mouse turns the camera, with a crosshair in the middle (hold Alt to free the pointer)'},
    combat_menus={default=true, kind='boolean', plugin=true,
        desc='show a paperdoll and compact lists instead of the game\'s "aim for" and "attack with" lists'},
    game_tips={default=true, kind='boolean', plugin=true,
        desc='the game\'s own hover box describes the tile the crosshair is on'},
    body_doll={default=true, kind='boolean', plugin=true,
        desc='show your own paperdoll, with wounds and a blood bar, in place of the portrait'},
    realtime_rate={default=5, kind='number', min=1, max=99, plugin=true,
        desc='game ticks per second while the timer button lets time pass by itself (a wait is 10; the mouse wheel over the timer changes this)'},
    start_enabled={default=false, kind='boolean',
        desc='switch to first person automatically when an adventure is loaded'},
}

ORDER = {'hotkey', 'fov', 'resolution', 'view_distance', 'range', 'levels_up', 'levels_down',
         'relative_controls', 'smooth', 'compass', 'minimap', 'minimap_ascii', 'minimap_tiles', 'wasd', 'mouse_look', 'combat_menus', 'game_tips', 'body_doll',
         'realtime_rate', 'start_enabled'}

local function settings_path()
    return scriptmanager.getModStatePath(MOD_ID) .. 'settings.json'
end

local function coerce(key, value)
    local spec = SCHEMA[key]
    if not spec then return nil, ('unknown setting: %s'):format(tostring(key)) end
    if spec.kind == 'boolean' then
        if type(value) == 'boolean' then return value end
        local s = tostring(value):lower()
        if s == 'true' or s == 'on' or s == 'yes' or s == '1' then return true end
        if s == 'false' or s == 'off' or s == 'no' or s == '0' then return false end
        return nil, ('%s must be true or false'):format(key)
    elseif spec.kind == 'number' then
        local n = tonumber(value)
        if not n then return nil, ('%s must be a number'):format(key) end
        n = math.floor(n + 0.5)
        if n < spec.min or n > spec.max then
            return nil, ('%s must be between %d and %d'):format(key, spec.min, spec.max)
        end
        return n
    else
        local s = tostring(value)
        if #s == 0 then return nil, ('%s must not be empty'):format(key) end
        return s
    end
end

local function load()
    local file = json.open(settings_path())
    local data = {}
    for key, spec in pairs(SCHEMA) do
        local v = spec.default
        local stored = file.data[key]
        if stored ~= nil then
            local ok = coerce(key, stored)
            if ok ~= nil then v = ok end
        end
        data[key] = v
    end
    return file, data
end

-- loaded once per DFHack session; `first-person reload` re-reads the file
file, data = nil, nil

local function ensure_loaded()
    if not data then
        file, data = load()
    end
end

function get(key)
    ensure_loaded()
    return data[key]
end

function set(key, value)
    ensure_loaded()
    local v, err = coerce(key, value)
    if v == nil then return false, err end
    data[key] = v
    file.data = data
    file:write()
    return true
end

function reload()
    file, data = load()
end
