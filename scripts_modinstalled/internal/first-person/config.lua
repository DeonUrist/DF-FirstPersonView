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
    fov={default=120, kind='number', min=40, max=160, plugin=true,
        desc='horizontal field of view in degrees'},
    resolution={default=100, kind='number', min=10, max=100, plugin=true,
        desc='rendered resolution as a percentage of the window (lower is faster)'},
    view_distance={default=48, kind='number', min=4, max=999, plugin=true,
        desc='draw distance in tiles: the loaded map is drawn this far (beyond it the far land, when on); with the far land off, things also fade into haze up to it'},
    range={default=2, kind='number', min=1, max=4, plugin=true,
        desc='how far terrain is tracked: 1 = one map window around you, 2 = three windows across, 3 = five'},
    max_height={default=24, kind='number', min=0, max=999, plugin=true,
        desc='Maximum height: how many z-levels above you are drawn'},
    model_lines={default=1, kind='number', min=0, max=4, plugin=true,
        desc="dark lines along the edges of furniture, stairs and other models, this many game pixels wide (0: none), for the look of the game's pixel art"},
    max_depth={default=8, kind='number', min=0, max=999, plugin=true,
        desc='Maximum depth: how many z-levels below you are drawn'},
    far_land={default=true, kind='boolean', plugin=true,
        desc='Far land: beyond the loaded map, draw the land out to the horizon from the world map (heights, biomes, sea and lakes, trees), and haze by the horizon instead of the draw distance'},
    horizon={default=20000, kind='number', min=500, max=60000, plugin=true,
        desc='Horizon: how far the far land reaches, in tiles (also the distance at which everything has faded into haze)'},
    far_trees={default=1400, kind='number', min=0, max=1400, plugin=true,
        desc='Far trees: how far trees are drawn as trees beyond the draw distance, in tiles (0: none; at most the nearest 6000, beyond them dense woods are blocks of canopy)'},
    relative_controls={default=true, kind='boolean', plugin=true,
        desc='movement keys are relative to where you look (up = forward, left/right = turn)'},
    smooth={default=true, kind='boolean', plugin=true,
        desc='animate turning, walking and climbing'},
    compass={default=true, kind='boolean', plugin=true,
        desc='show the heading indicator at the top of the screen'},
    compass_markers={default=true, kind='boolean', plugin=true,
        desc='show the nearby places on the compass (off whenever the compass is off)'},
    cursor_hint={default=true, kind='boolean', plugin=true,
        desc='name the creature, item, plant or building under the pointer in a line below it'},
    minimap={default=true, kind='boolean', plugin=true,
        desc='show the normal top-down map in the top right corner'},
    enemy_list={default=true, kind='boolean', plugin=true,
        desc='show the row of pictures of the creatures in a conflict with you (under the minimap, or in the top right corner without it)'},
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
    inventory_panel={default=true, kind='boolean', plugin=true,
        desc='replace the game\'s inventory screen with a panel: your sprite with a square for each body part, what you carry as squares, drag and drop'},
    radial_menu={default=true, kind='boolean', plugin=true,
        desc="take the game's buttons off the bottom of the screen; Tab opens a ring of their actions around the pointer"},
    journal_panel={default=true, kind='boolean', plugin=true,
        desc="replace the game's journal with a panel: both names of everything, where it is and how far, every name a link"},
    game_tips={default=true, kind='boolean', plugin=true,
        desc='the game\'s own hover box describes the tile the crosshair is on'},
    body_doll={default=true, kind='boolean', plugin=true,
        desc='show your own paperdoll, with wounds and a blood bar, in place of the portrait'},
    status_bars={default=true, kind='boolean', plugin=true,
        desc='bars under your paperdoll (bleedout, exhaustion, pain, hunger, thirst, sleepiness) and above it for passing states'},
    any_conflict_discs={default=false, kind='boolean', plugin=true,
        desc='Show discs for any conflict: also mark creatures fighting each other, not only those in a conflict with you'},
    auto_attack={default=false, kind='boolean', plugin=true,
        desc='Auto attack: walking into a creature you are in a conflict with strikes it; off, you step onto its tile instead if the game lets you'},
    realtime_rate={default=5, kind='number', min=1, max=99, plugin=true,
        desc='game ticks per second while the timer button lets time pass by itself (a wait is 10; the mouse wheel over the timer changes this)'},
    hardware_renderer={default=true, kind='boolean', plugin=true,
        desc='draw the view on the graphics card, so you can look straight up or down (off: the software renderer)'},
    start_enabled={default=false, kind='boolean',
        desc='switch to first person automatically when an adventure is loaded'},
}

ORDER = {'hotkey', 'fov', 'resolution', 'view_distance', 'range', 'max_height', 'max_depth', 'far_land', 'horizon', 'far_trees', 'model_lines',
         'relative_controls', 'smooth', 'compass', 'compass_markers', 'cursor_hint', 'minimap', 'enemy_list', 'minimap_ascii', 'minimap_tiles', 'wasd', 'mouse_look', 'combat_menus', 'inventory_panel', 'journal_panel', 'radial_menu', 'game_tips', 'body_doll', 'status_bars', 'any_conflict_discs', 'auto_attack',
         'realtime_rate', 'hardware_renderer', 'start_enabled'}

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

-- settings stored under an older name
local RENAMED = {max_height='levels_up', max_depth='levels_down'}

local function load()
    local file = json.open(settings_path())
    local data = {}
    for key, spec in pairs(SCHEMA) do
        local v = spec.default
        local stored = file.data[key]
        if stored == nil and RENAMED[key] then stored = file.data[RENAMED[key]] end
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
