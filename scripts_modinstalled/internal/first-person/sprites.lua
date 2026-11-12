--@ module = true
-- Reads the game's own tile sprites out of memory and turns them into
-- textures for the first-person renderer.
--
-- Pixels are stored as 0xAABBGGRR integers (the layout DF's surfaces use).
-- Every texture comes in NUM_LEVELS brightness levels so the renderer can
-- fade things with distance without doing colour maths per pixel:
--   sprite.lv[1] is full brightness, sprite.lv[NUM_LEVELS] is nearly dark.
-- In billboard textures a 0 pixel means "transparent".

local enabler = df.global.enabler

NUM_LEVELS = 8
SIZE = 32                  -- DF map sprites are 32x32
local NPIX = SIZE * SIZE

-- colour that distant things fade into
FOG_R, FOG_G, FOG_B = 8, 9, 12

-- SDL_Surface / SDL_PixelFormat offsets (SDL2, 64-bit)
local SURF_FORMAT, SURF_W, SURF_H, SURF_PITCH, SURF_PIXELS = 8, 16, 20, 24, 32
local FMT_BYTESPP, FMT_RMASK, FMT_GMASK, FMT_BMASK, FMT_AMASK = 17, 20, 24, 28, 32

-- how long cached textures are trusted before the source sprite is re-read.
-- creature sprites live in recycled texture slots, so they expire quickly.
local TTL_STATIC = 60
local TTL_DYNAMIC = 3
local MAX_REBUILDS_PER_PASS = 16
local MAX_BILLBOARDS = 600

raws = raws or {}          -- texpos -> {t=time, px=pixels|false}
walls = walls or {}        -- key -> texture
floors = floors or {}      -- key -> texture
bills = bills or {}        -- key -> texture
specials = specials or {}  -- name -> texture
bill_count = bill_count or 0

local now = 0
local rebuilds_left = MAX_REBUILDS_PER_PASS

local function u64(addr) return df.reinterpret_cast('uint64_t', addr).value end
local function i32(addr) return df.reinterpret_cast('int32_t', addr).value end
local function u32(addr) return df.reinterpret_cast('uint32_t', addr).value end
local function u8(addr) return df.reinterpret_cast('uint8_t', addr).value end

function rgb(r, g, b)
    return 0xFF000000 | (b << 16) | (g << 8) | r
end

local function mask_shift(mask)
    if mask == 0 then return nil end
    local s = 0
    while mask & 1 == 0 do
        mask = mask >> 1
        s = s + 1
    end
    return s
end

-- read one game texture; returns an array of SIZE*SIZE 0xAABBGGRR values
-- (true alpha preserved), or nil if the texture is missing or unsupported
local function read_surface(texpos)
    local list = enabler.textures.raws
    if texpos <= 0 or texpos >= #list then return nil end
    local raw = list[texpos]
    if not raw then return nil end
    local _, addr = df.sizeof(raw)
    local w, h = i32(addr + SURF_W), i32(addr + SURF_H)
    if w < 1 or h < 1 or w > 256 or h > 256 then return nil end
    local fmt = u64(addr + SURF_FORMAT)
    if fmt == 0 or u8(fmt + FMT_BYTESPP) ~= 4 then return nil end
    local rs, gs, bs = mask_shift(u32(fmt + FMT_RMASK)), mask_shift(u32(fmt + FMT_GMASK)),
        mask_shift(u32(fmt + FMT_BMASK))
    local as = mask_shift(u32(fmt + FMT_AMASK))
    if not rs or not gs or not bs then return nil end
    local native = rs == 0 and gs == 8 and bs == 16 and as == 24
    local pitch, pixels = i32(addr + SURF_PITCH), u64(addr + SURF_PIXELS)
    if pixels == 0 then return nil end

    local px = {}
    local n = 0
    -- sprites that are not 32x32 are resampled (nearest) to 32x32
    for y = 0, SIZE - 1 do
        local sy = (y * h) // SIZE
        local row = df.reinterpret_cast('uint32_t', pixels + sy * pitch)
        for x = 0, SIZE - 1 do
            local v = row:_displace((x * w) // SIZE).value
            if not native then
                local a = as and ((v >> as) & 255) or 255
                v = (a << 24) | (((v >> bs) & 255) << 16) | (((v >> gs) & 255) << 8) | ((v >> rs) & 255)
            end
            n = n + 1
            px[n] = v
        end
    end
    return px
end

local function same_pixels(a, b)
    if not a or not b then return a == b end
    for i = 1, NPIX do
        if a[i] ~= b[i] then return false end
    end
    return true
end

-- cached read; returns pixels (or nil) and a version number that goes up
-- every time the sprite's pixels are found to have changed
local function get_raw(texpos, ttl)
    local e = raws[texpos]
    if e and now - e.t < ttl then
        return e.px or nil, e.ver
    end
    local px = read_surface(texpos)
    if not e then
        e = {t=now, px=px or false, ver=1}
        raws[texpos] = e
    else
        if not same_pixels(e.px or nil, px) then
            e.px = px or false
            e.ver = e.ver + 1
        end
        e.t = now
    end
    return px, e.ver
end

local function shade_levels(base, n)
    local lv = {base}
    for k = 2, NUM_LEVELS do
        local t = (k - 1) / (NUM_LEVELS - 1)
        local f = math.floor((1 - (t ^ 1.25) * 0.97) * 256)
        local out = {}
        for i = 1, n do
            local c = base[i]
            if c == 0 then
                out[i] = 0
            else
                -- floor division, not >>: the differences can be negative
                local r = FOG_R + (((c & 255) - FOG_R) * f) // 256
                local g = FOG_G + ((((c >> 8) & 255) - FOG_G) * f) // 256
                local b = FOG_B + ((((c >> 16) & 255) - FOG_B) * f) // 256
                out[i] = 0xFF000000 | (b << 16) | (g << 8) | r
            end
        end
        lv[k] = out
    end
    return lv
end

-- alpha-blend src over dst in place (both SIZE*SIZE)
local function blend(dst, src, dst_offset, dst_stride)
    dst_offset = dst_offset or 0
    dst_stride = dst_stride or SIZE
    local i = 0
    for y = 0, SIZE - 1 do
        local d = dst_offset + y * dst_stride
        for x = 1, SIZE do
            i = i + 1
            local s = src[i]
            local sa = s >> 24
            if sa >= 250 then
                dst[d + x] = s
            elseif sa > 8 then
                local o = dst[d + x]
                local oa = o >> 24
                if oa < 8 then
                    dst[d + x] = s
                else
                    local ia = 255 - sa
                    local r = ((s & 255) * sa + (o & 255) * ia) // 255
                    local g = (((s >> 8) & 255) * sa + ((o >> 8) & 255) * ia) // 255
                    local b = (((s >> 16) & 255) * sa + ((o >> 16) & 255) * ia) // 255
                    local a = oa > sa and oa or sa
                    dst[d + x] = (a << 24) | (b << 16) | (g << 8) | r
                end
            end
        end
    end
end

-- make every pixel either fully opaque or 0 (transparent)
local function harden(px, n, threshold)
    for i = 1, n do
        local c = px[i]
        if (c >> 24) >= threshold then
            px[i] = c | 0xFF000000
        else
            px[i] = 0
        end
    end
end

local function average_color(px)
    local r, g, b, n = 0, 0, 0, 0
    for i = 1, NPIX do
        local c = px[i]
        if (c >> 24) >= 128 then
            r = r + (c & 255)
            g = g + ((c >> 8) & 255)
            b = b + ((c >> 16) & 255)
            n = n + 1
        end
    end
    if n == 0 then return nil end
    return rgb(r // n, g // n, b // n), n
end

local function solid(color)
    local px = {}
    for i = 1, NPIX do px[i] = color end
    return px
end

---------------------------------------------------------------------------
-- wall faces
--
-- Top-down wall sprites are mostly transparent: just a strip of block
-- edging along the sides that face open ground. A wall face is built by
-- finding that strip and tiling it over the whole face, which gives a
-- block wall drawn with the game's own art.

local function opaque_at(px, x, y)
    return (px[y * SIZE + x + 1] >> 24) >= 128
end

-- longest run of consecutive lines (rows or columns) that are at least
-- half opaque; returns start, length, opaque pixel count
local function best_run(px, by_rows)
    local best_start, best_len, best_score = nil, 0, 0
    local start, len, score = nil, 0, 0
    for a = 0, SIZE do
        local count = 0
        if a < SIZE then
            for b = 0, SIZE - 1 do
                local x, y = b, a
                if not by_rows then x, y = a, b end
                if opaque_at(px, x, y) then count = count + 1 end
            end
        end
        if count >= SIZE // 2 then
            if not start then start, len, score = a, 0, 0 end
            len = len + 1
            score = score + count
        else
            if start and score > best_score then
                best_start, best_len, best_score = start, len, score
            end
            start = nil
        end
    end
    return best_start, best_len, best_score
end

local function face_from_edging(px)
    local avg = average_color(px)
    if not avg then return nil end

    local rs, rl, rscore = best_run(px, true)
    local cs, cl, cscore = best_run(px, false)
    local strip, strip_h = nil, 0
    if rs and rl >= 3 and rscore >= cscore then
        -- horizontal strip: rows rs .. rs+rl-1
        strip, strip_h = {}, rl
        for y = 0, rl - 1 do
            for x = 0, SIZE - 1 do
                strip[y * SIZE + x + 1] = px[(rs + y) * SIZE + x + 1]
            end
        end
    elseif cs and cl >= 3 then
        -- vertical strip: turn it on its side so courses run horizontally
        strip, strip_h = {}, cl
        for y = 0, cl - 1 do
            for x = 0, SIZE - 1 do
                strip[y * SIZE + x + 1] = px[x * SIZE + (cs + y) + 1]
            end
        end
    else
        -- no clear strip (e.g. a lone corner block): tile the bounding box
        local x0, y0, x1, y1 = SIZE, SIZE, -1, -1
        for y = 0, SIZE - 1 do
            for x = 0, SIZE - 1 do
                if opaque_at(px, x, y) then
                    if x < x0 then x0 = x end
                    if x > x1 then x1 = x end
                    if y < y0 then y0 = y end
                    if y > y1 then y1 = y end
                end
            end
        end
        local bw, bh = x1 - x0 + 1, y1 - y0 + 1
        if bw < 2 or bh < 2 then return solid(avg) end
        local face = {}
        for y = 0, SIZE - 1 do
            for x = 0, SIZE - 1 do
                local c = px[(y0 + y % bh) * SIZE + (x0 + x % bw) + 1]
                if (c >> 24) < 128 then c = avg end
                face[y * SIZE + x + 1] = c | 0xFF000000
            end
        end
        return face
    end

    -- fill gaps in each strip row from the nearest opaque pixel in that row
    for y = 0, strip_h - 1 do
        local base = y * SIZE
        for x = 0, SIZE - 1 do
            if (strip[base + x + 1] >> 24) < 128 then
                local found = avg
                for d = 1, SIZE - 1 do
                    local c = strip[base + ((x + d) % SIZE) + 1]
                    if (c >> 24) >= 128 then found = c break end
                    c = strip[base + ((x - d) % SIZE) + 1]
                    if (c >> 24) >= 128 then found = c break end
                end
                strip[base + x + 1] = found
            end
        end
    end
    -- stack the courses, shifting every other one for a bonded look
    local face = {}
    for y = 0, SIZE - 1 do
        local course = y // strip_h
        local shift = (course % 2 == 1) and (SIZE // 4) or 0
        local sbase = (y % strip_h) * SIZE
        for x = 0, SIZE - 1 do
            face[y * SIZE + x + 1] = strip[sbase + ((x + shift) % SIZE) + 1] | 0xFF000000
        end
    end
    return face
end

local function build_wall(bg, bg2)
    local face
    if bg2 ~= 0 then
        local px = get_raw(bg2, TTL_STATIC)
        if px then face = face_from_edging(px) end
    end
    if not face and bg ~= 0 then
        local px = get_raw(bg, TTL_STATIC)
        if px then
            face = {}
            local avg = average_color(px) or rgb(40, 38, 44)
            for i = 1, NPIX do
                local c = px[i]
                face[i] = (c >> 24) >= 128 and (c | 0xFF000000) or avg
            end
        end
    end
    face = face or solid(rgb(52, 48, 56))
    return {lv=shade_levels(face, NPIX), t=now}
end

function wall(bg, bg2)
    local key = (bg2 << 32) | bg
    local tex = walls[key]
    if tex and (now - tex.t < TTL_STATIC or rebuilds_left <= 0) then return tex end
    if tex then rebuilds_left = rebuilds_left - 1 end
    tex = build_wall(bg, bg2)
    walls[key] = tex
    return tex
end

-- tree trunks: the top-down sprite shows growth rings, which look wrong on
-- the side of a trunk. Smear the sprite's middle rows downwards instead,
-- which gives vertical grain in the tree's own colours.
local function build_bark(bg, bg2)
    local px = (bg2 ~= 0 and get_raw(bg2, TTL_STATIC)) or (bg ~= 0 and get_raw(bg, TTL_STATIC)) or nil
    local avg = px and average_color(px)
    if not avg then return build_wall(bg, bg2) end
    local face = {}
    for y = 0, SIZE - 1 do
        local row = 13 + ((y * 7 + (y >> 2)) % 6)
        for x = 0, SIZE - 1 do
            local c = px[row * SIZE + x + 1]
            face[y * SIZE + x + 1] = (c >> 24) >= 128 and (c | 0xFF000000) or avg
        end
    end
    return {lv=shade_levels(face, NPIX), t=now}
end

function bark(bg, bg2)
    local key = (bg2 << 32) | bg | (1 << 61)
    local tex = walls[key]
    if tex and (now - tex.t < TTL_STATIC or rebuilds_left <= 0) then return tex end
    if tex then rebuilds_left = rebuilds_left - 1 end
    tex = build_bark(bg, bg2)
    walls[key] = tex
    return tex
end

-- a tile-filling building (closed door, floodgate ...): the building's own
-- sprite drawn flat on every face, over the floor it stands on
local function build_flat_wall(bg, tp)
    local face
    local base = bg ~= 0 and get_raw(bg, TTL_STATIC) or nil
    local avg = base and average_color(base) or rgb(52, 48, 56)
    face = {}
    for i = 1, NPIX do
        local c = base and base[i] or avg
        face[i] = (c >> 24) >= 128 and (c | 0xFF000000) or avg
    end
    local px = get_raw(tp, TTL_DYNAMIC)
    if px then
        blend(face, px)
        for i = 1, NPIX do face[i] = face[i] | 0xFF000000 end
    end
    local lv = shade_levels(face, NPIX)
    -- one sprite per wall face instead of repeating like masonry
    lv.stretch = true
    return {lv=lv, t=now}
end

function flat_wall(bg, tp)
    local key = (tp << 32) | bg | (1 << 62)
    local tex = walls[key]
    if tex and (now - tex.t < TTL_DYNAMIC or rebuilds_left <= 0) then return tex end
    if tex then rebuilds_left = rebuilds_left - 1 end
    tex = build_flat_wall(bg, tp)
    walls[key] = tex
    return tex
end

---------------------------------------------------------------------------
-- floors

local function water_pixels()
    local px = {}
    for y = 0, SIZE - 1 do
        for x = 0, SIZE - 1 do
            local wave = math.sin((x + 3 * math.sin(y * 0.55)) * 0.6) + math.sin(y * 0.9 + x * 0.25)
            local l = 0.5 + 0.12 * wave
            px[y * SIZE + x + 1] = rgb(math.floor(30 + 40 * l), math.floor(90 + 70 * l), math.floor(150 + 90 * l))
        end
    end
    return px
end

local function magma_pixels()
    local px = {}
    for y = 0, SIZE - 1 do
        for x = 0, SIZE - 1 do
            local n = math.sin(x * 0.55 + math.sin(y * 0.4) * 2.2) * math.sin(y * 0.6 + math.sin(x * 0.35) * 1.9)
            local l = 0.5 + 0.5 * n
            if l < 0.25 then
                px[y * SIZE + x + 1] = rgb(90, 24, 10)
            else
                px[y * SIZE + x + 1] = rgb(math.floor(200 + 55 * l), math.floor(60 + 150 * l * l), math.floor(10 + 30 * l))
            end
        end
    end
    return px
end

-- liquid: 0 none, 1 water, 2 magma. dim: true for ground seen through a hole
local function build_floor(bg, liquid, dim)
    local px
    if liquid == 1 then
        px = water_pixels()
    elseif liquid == 2 then
        px = magma_pixels()
    else
        local src = bg ~= 0 and get_raw(bg, TTL_STATIC) or nil
        if not src then return nil end
        local avg = average_color(src)
        if not avg then return nil end
        px = {}
        for i = 1, NPIX do
            local c = src[i]
            px[i] = (c >> 24) >= 128 and (c | 0xFF000000) or avg
        end
    end
    if dim then
        for i = 1, NPIX do
            local c = px[i]
            px[i] = 0xFF000000 | (((c >> 16) & 255) * 5 // 8 << 16) | (((c >> 8) & 255) * 5 // 8 << 8) |
                ((c & 255) * 5 // 8)
        end
    end
    return {lv=shade_levels(px, NPIX), t=now}
end

function floor(bg, liquid, dim)
    local key = bg | (liquid << 32) | ((dim and 1 or 0) << 36)
    local tex = floors[key]
    if tex ~= nil then
        if tex == false then return nil end
        if now - tex.t < TTL_STATIC or rebuilds_left <= 0 then return tex end
        rebuilds_left = rebuilds_left - 1
    end
    tex = build_floor(bg, liquid, dim)
    floors[key] = tex or false
    return tex
end

---------------------------------------------------------------------------
-- billboards (creatures, items, plants, furniture ...)
--
-- `pieces` is a flat array {texpos, dx, dy, texpos, dx, dy, ...} where dx/dy
-- are tile offsets from the anchor tile (large creatures are drawn by the
-- game as several tile-sized pieces). Pieces are layered in order.

local function versions(pieces, n)
    local sum = 0
    for i = 1, n, 3 do
        local _, ver = get_raw(pieces[i], TTL_DYNAMIC)
        sum = sum + ver
    end
    return sum
end

local function build_billboard(pieces, n)
    local x0, x1, y0, y1 = 0, 0, 0, 0
    local srcs = {}
    local any = false
    local vsum = versions(pieces, n)
    for i = 1, n, 3 do
        local px = get_raw(pieces[i], TTL_DYNAMIC)
        if px and average_color(px) then
            srcs[#srcs + 1] = {px, pieces[i + 1], pieces[i + 2]}
            local dx, dy = pieces[i + 1], pieces[i + 2]
            if dx < x0 then x0 = dx end
            if dx > x1 then x1 = dx end
            if dy < y0 then y0 = dy end
            if dy > y1 then y1 = dy end
            any = true
        end
    end
    if not any then return {empty=true, t=now, vsum=vsum} end
    local tw, th = x1 - x0 + 1, y1 - y0 + 1
    local w, h = tw * SIZE, th * SIZE
    local canvas = {}
    for i = 1, w * h do canvas[i] = 0 end
    for _, s in ipairs(srcs) do
        blend(canvas, s[1], (s[3] - y0) * SIZE * w + (s[2] - x0) * SIZE, w)
    end
    harden(canvas, w * h, 150)
    return {
        lv=shade_levels(canvas, w * h), t=now, vsum=vsum,
        w=w, h=h,
        -- horizontal position of the anchor tile's centre inside the canvas, in tiles
        anchor=(0 - x0) + 0.5,
        tiles_w=tw, tiles_h=th,
        -- tile rows of the canvas that lie below the anchor tile
        below=y1,
    }
end

function billboard(pieces, n)
    local key
    if n == 3 and pieces[2] == 0 and pieces[3] == 0 then
        key = pieces[1]
    else
        key = table.concat(pieces, ',', 1, n)
    end
    local tex = bills[key]
    if tex then
        tex.used = now
        if now - tex.t < TTL_DYNAMIC or rebuilds_left <= 0 then
            return not tex.empty and tex or nil
        end
        -- expired: only rebuild if one of the source sprites really changed
        if versions(pieces, n) == tex.vsum then
            tex.t = now
            return not tex.empty and tex or nil
        end
        rebuilds_left = rebuilds_left - 1
    else
        bill_count = bill_count + 1
    end
    tex = build_billboard(pieces, n)
    tex.used = now
    bills[key] = tex
    return not tex.empty and tex or nil
end

---------------------------------------------------------------------------
-- misc

function special(name)
    local tex = specials[name]
    if tex then return tex end
    local px
    if name == 'unknown' then
        px = solid(rgb(14, 13, 16))
    elseif name == 'water' then
        px = water_pixels()
    elseif name == 'magma' then
        px = magma_pixels()
    elseif name == 'roof' then
        px = {}
        for y = 0, SIZE - 1 do
            for x = 0, SIZE - 1 do
                local l = 30 + ((x * 7 + y * 13 + (x ~ y) * 3) % 9)
                px[y * SIZE + x + 1] = rgb(l + 4, l, l + 2)
            end
        end
    else
        px = solid(rgb(60, 60, 60))
    end
    tex = {lv=shade_levels(px, NPIX), t=now}
    specials[name] = tex
    return tex
end

-- call before each scene read
function begin_pass(time)
    now = time
    rebuilds_left = MAX_REBUILDS_PER_PASS
    if bill_count > MAX_BILLBOARDS then
        for key, tex in pairs(bills) do
            if now - (tex.used or 0) > 20 then
                bills[key] = nil
                bill_count = bill_count - 1
            end
        end
    end
end

-- change the colour distant things fade into. Brightness levels are baked
-- into every texture, so this throws the built textures away; returns true
-- if the colour really changed (the caller must then rebuild its scene).
function set_fog(r, g, b)
    if r == FOG_R and g == FOG_G and b == FOG_B then return false end
    FOG_R, FOG_G, FOG_B = r, g, b
    walls, floors, bills, specials = {}, {}, {}, {}
    bill_count = 0
    return true
end

-- forget everything (texture numbers are reassigned when a world loads)
function clear()
    raws, walls, floors, bills, specials = {}, {}, {}, {}, {}
    bill_count = 0
end
