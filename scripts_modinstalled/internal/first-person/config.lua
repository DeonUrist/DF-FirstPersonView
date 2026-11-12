--@ module = true
-- Settings for the first-person view mod.
--
-- Settings are global (not per-world) and live in
-- <DF>/dfhack-config/mods/first_person_view/settings.json

local json = require('json')
local scriptmanager = require('script-manager')

MOD_ID = 'first_person_view'
COMMAND = 'first-person'

-- key -> {default, kind, min, max, description}
SCHEMA = {
    hotkey={default='Ctrl-Shift-F', kind='string',
        desc='DFHack keybinding that toggles the view'},
    fov={default=80, kind='number', min=40, max=120,
        desc='horizontal field of view in degrees'},
    quality={default=16, kind='number', min=4, max=32,
        desc='rendered pixels per map tile (higher = sharper and slower)'},
    max_width={default=800, kind='number', min=160, max=1920,
        desc='upper limit for the rendered image width in pixels'},
    view_distance={default=18, kind='number', min=4, max=64,
        desc='distance in tiles at which things fade into darkness'},
    levels_up={default=4, kind='number', min=0, max=12,
        desc='how many z-levels above you are drawn'},
    levels_down={default=3, kind='number', min=0, max=12,
        desc='how many z-levels below you are drawn'},
    relative_controls={default=true, kind='boolean',
        desc='movement keys are relative to where you look (up = forward, left/right = turn)'},
    smooth={default=true, kind='boolean',
        desc='animate turning and walking'},
    fast_motion={default=true, kind='boolean',
        desc='draw at half resolution while the view is moving, for smoother motion'},
    compass={default=true, kind='boolean',
        desc='show the heading indicator at the top of the screen'},
    start_enabled={default=false, kind='boolean',
        desc='switch to first person automatically when an adventure is loaded'},
}

ORDER = {'hotkey', 'fov', 'quality', 'max_width', 'view_distance', 'levels_up', 'levels_down',
         'relative_controls', 'smooth', 'fast_motion', 'compass', 'start_enabled'}

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

function all()
    ensure_loaded()
    return data
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
