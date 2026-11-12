--@ module = true
-- Framebuffer made of map-viewport tiles.
--
-- DFHack can register textures at runtime, but DF's renderer keeps a GPU copy
-- of every texture it has drawn, so creating fresh textures each frame would
-- leak. Instead we allocate one small texture per map-viewport cell once,
-- then for every frame:
--   1. copy pixels straight into the SDL surfaces behind those textures, and
--   2. ask the renderer to drop its cached GPU copy (clean_cached_tile), which
--      is the same thing the game does when it rebuilds a creature's sprite.
-- The textures are written to the topmost map layer, so the game's own UI
-- still draws on top of the first-person image.
--
-- Textures are never deleted: dfhack.textures.deleteHandle() reads a surface
-- after freeing it and crashed the game when called for a batch of tiles
-- (DFHack 53.17-r1). Tiles are kept per size and reused instead; the set only
-- grows when the map view needs more tiles than ever before.

local enabler = df.global.enabler
local gps = df.global.gps

-- SDL_Surface field offsets (SDL2, 64-bit)
local SURF_PITCH = 24
local SURF_PIXELS = 32

-- texture_fullid key the renderer uses for map-viewport tiles:
-- white foreground, black background, flag = transparent_background
local KEY_FLAG = 1

local GROW_ROW = 64          -- tiles per row in a freshly allocated sheet

stores = stores or {}         -- K -> {handles={...}}, every tile ever made
views = views or {}           -- K -> pool last used at that tile size
pool = pool or nil            -- the view currently in use
strbuf = strbuf or nil        -- std::string used as a staging buffer
generation = generation or 0  -- bumped whenever the pool changes shape

local function u64(addr)
    return df.reinterpret_cast('uint64_t', addr).value
end

local function i32(addr)
    return df.reinterpret_cast('int32_t', addr).value
end

local function surface_addr(texpos)
    local raws = enabler.textures.raws
    if not texpos or texpos <= 0 or texpos >= #raws then return nil end
    local raw = raws[texpos]
    if not raw then return nil end
    local _, addr = df.sizeof(raw)
    return addr
end

local function staging()
    if not strbuf then
        strbuf = df.new('string')
    end
    local _, addr = df.sizeof(strbuf)
    return strbuf, addr
end

-- make sure at least `count` tiles of size K exist
local function grow(K, count)
    local store = stores[K]
    if not store then
        store = {handles={}}
        stores[K] = store
    end
    local missing = count - #store.handles
    if missing <= 0 then return store end
    local sheet_cols = math.min(GROW_ROW, missing)
    local sheet_rows = (missing + sheet_cols - 1) // sheet_cols
    local handles = dfhack.textures.createTileset({}, sheet_cols * K, sheet_rows * K, K, K)
    if not handles or #handles < missing then
        return nil
    end
    for _, h in ipairs(handles) do
        table.insert(store.handles, h)
    end
    return store
end

-- (re)read texpos numbers and pixel pointers for every tile in the pool.
-- texpos values change whenever the game reloads its textures.
local function resolve(p)
    local texpos, pix, surf = {}, {}, {}
    local handles = p.store.handles
    for i = 1, p.count do
        local tp = dfhack.textures.getTexposByHandle(handles[i])
        local addr = surface_addr(tp)
        if not addr then return false end
        texpos[i] = tp
        surf[i] = addr
        pix[i] = u64(addr + SURF_PIXELS)
    end
    p.texpos, p.pix, p.surf = texpos, pix, surf
    p.pitch = i32(surf[1] + SURF_PITCH)
    return true
end

local function valid(p)
    local n = p.count
    return surface_addr(p.texpos[1]) == p.surf[1] and
        surface_addr(p.texpos[n]) == p.surf[n]
end

-- returns a pool of cols*rows tiles with K x K pixels each, or nil, error
function ensure(cols, rows, K)
    local cached = views[K]
    if cached and cached.cols == cols and cached.rows == rows then
        if valid(cached) or resolve(cached) then
            pool = cached
            return cached
        end
    end
    pool = nil

    local count = cols * rows
    local store = grow(K, count)
    if not store then
        return nil, 'could not allocate framebuffer textures'
    end
    local W, H = cols * K, rows * K
    local p = {
        cols=cols, rows=rows, K=K, W=W, H=H,
        count=count, store=store,
        fmt='<' .. ('I4'):rep(W),
    }
    if not resolve(p) then
        return nil, 'could not resolve framebuffer textures'
    end
    pool = p
    views[K] = p
    generation = generation + 1
    return p
end

-- forget the current view (its textures stay allocated for reuse)
function release()
    pool = nil
    views = {}
end

-- copy a frame (array of W*H 0xAABBGGRR integers, row-major, 1-based) into
-- the tile surfaces and invalidate the renderer's cached copies
function flush(p, frame)
    local W, H, K, cols = p.W, p.H, p.K, p.cols
    local fmt, pix, pitch = p.fmt, p.pix, p.pitch
    local pack, unpack = string.pack, table.unpack
    local memmove = dfhack.internal.memmove
    local str, str_addr = staging()
    local strptr = df.reinterpret_cast('uint64_t', str_addr)
    local rowbytes = K * 4

    for y = 0, H - 1 do
        local a = y * W
        -- one string per framebuffer row, staged through a std::string so
        -- its bytes have an address we can memmove from
        str.value = pack(fmt, unpack(frame, a + 1, a + W))
        local src = strptr.value
        local tbase = (y // K) * cols
        local off = (y % K) * pitch
        for tx = 1, cols do
            memmove(pix[tbase + tx] + off, src, rowbytes)
            src = src + rowbytes
        end
    end

    local r = enabler.renderer
    local texpos = p.texpos
    for i = 1, p.count do
        r:clean_cached_tile(texpos[i], 1, 1, 1, 0, 0, 0, KEY_FLAG)
    end
end

-- write the pool's tiles into the top layer of the map viewport.
-- must be called every frame: the game clears that layer when it redraws.
function present(p)
    local vp = gps.main_viewport
    if vp.dim_x ~= p.cols or vp.dim_y ~= p.rows then return false end
    local arr = vp.screentexpos_interface
    local dim_y = vp.dim_y
    local texpos = p.texpos
    local cols = p.cols
    for y = 0, p.rows - 1 do
        local base = y * cols + 1
        for x = 0, cols - 1 do
            arr[x * dim_y + y] = texpos[base + x]
        end
    end
    return true
end
