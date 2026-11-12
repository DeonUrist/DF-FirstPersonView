--@ module = true
-- Builds the multi-level scene the first-person renderer draws.
--
-- The game works out which sprite belongs on every visible map tile and
-- stores them per layer (floor, wall/plant, items, creatures, ...) in the map
-- viewport. Those layers only ever hold one z-level, so to see several levels
-- we do what scrolling with the mouse wheel does: point the view at another
-- level, ask the game to draw it, read the layers, and put the view back.
--
-- What is read is combined with the tile shapes from the map and kept in a
-- cache keyed by absolute map position, so terrain stays known after it
-- scrolls off screen:
--   levels[z]  per-level tile data (wall textures, floor textures, ...)
--   stacks[g]  per map column, the faces and surfaces the renderer can see
--   bills      everything that stands on a tile (creatures, items, plants)
--
-- Cell key:  g = (y << 10) | x   (absolute map tile coordinates)
-- Heights are in quarter levels:  hq = 4 * z + q

local sprites = reqscript('internal/first-person/sprites')

KEY_SHIFT = 10
MAX_COLS = 255

local CLASS_WALL, CLASS_FLOOR, CLASS_OPEN, CLASS_RAMP = 1, 2, 3, 4
local tt_class = {}
local tt_tree = {}

local function classify(tt)
    local c = tt_class[tt]
    if c then return c, tt_tree[tt] end
    local attrs = df.tiletype.attrs[tt]
    local shape = attrs.shape
    local basic = df.tiletype_shape.attrs[shape].basic_shape
    local B = df.tiletype_shape_basic
    if basic == B.Wall then
        c = CLASS_WALL
    elseif basic == B.Ramp then
        c = CLASS_RAMP
    elseif shape == df.tiletype_shape.TWIG then
        c = CLASS_FLOOR      -- leaves: drawn as a thin canopy layer
    elseif basic == B.Open or basic == B.None then
        c = CLASS_OPEN
    else
        c = CLASS_FLOOR
    end
    tt_class[tt] = c
    local mat = attrs.material
    tt_tree[tt] = (mat == df.tiletype_material.TREE or mat == df.tiletype_material.MUSHROOM) and true or false
    return c, tt_tree[tt]
end

levels = levels or {}       -- z -> level data
stacks = stacks or {}       -- g -> column description for the renderer
capturing = false           -- true while we make the game redraw other levels
dirty_cols = dirty_cols or {}
rr_index = rr_index or 0    -- round-robin position for background levels
anchor = anchor or nil      -- where the cache was last wiped

local function new_level()
    return {
        solid={},    -- g -> texture levels of a full-height block
        half={},     -- g -> texture levels of a half-height block (ramps)
        flr={},      -- g -> texture levels of the floor surface
        top={},      -- g -> texture levels of a raised surface, with .topq = quarter height
        topq={},
        roofed={},   -- g -> true if there is something overhead
        sky={},      -- g -> true if open to the sky
        bills={},    -- list of billboards on this level
        bsum=0,
    }
end

function reset()
    levels, stacks, dirty_cols = {}, {}, {}
    rr_index = 0
    anchor = nil
end

local pieces = {}   -- scratch buffer for billboard pieces

-- read the level the map viewport currently shows into the cache.
-- returns true if anything changed
local function read_level(z, now)
    local vp = df.global.gps.main_viewport
    local cols, rows = vp.dim_x, vp.dim_y
    local wx, wy = df.global.window_x, df.global.window_y
    local BLANK = df.global.gps.black_background_texpos[0]

    local L_bg, L_bg2 = vp.screentexpos_background, vp.screentexpos_background_two
    local L_b1, L_b2 = vp.screentexpos_building_one, vp.screentexpos_building_two
    local L_item, L_vehicle, L_vermin = vp.screentexpos_item, vp.screentexpos_vehicle, vp.screentexpos_vermin
    local L_c, L_lc, L_rc = vp.screentexpos, vp.screentexpos_left_creature, vp.screentexpos_right_creature
    local L_uc, L_ulc, L_urc = vp.screentexpos_up_creature, vp.screentexpos_upleft_creature,
        vp.screentexpos_upright_creature
    local L_proj, L_sign = vp.screentexpos_projectile, vp.screentexpos_signpost

    local getTileType, getTileFlags = dfhack.maps.getTileType, dfhack.maps.getTileFlags
    local OCC_IMPASSABLE = df.tile_building_occ.Impassable
    local OCC_DYNAMIC = df.tile_building_occ.Dynamic

    local unknown = sprites.special('unknown').lv

    local L = levels[z]
    if not L then
        L = new_level()
        levels[z] = L
    end
    local Lsolid, Lhalf, Lflr, Ltop, Ltopq, Lroofed, Lsky = L.solid, L.half, L.flr, L.top, L.topq, L.roofed, L.sky
    local bills, nbills, bsum = {}, 0, 0
    local changed = false
    local dirty = dirty_cols

    for cy = 0, rows - 1 do
        local my = wy + cy
        local gbase = my << KEY_SHIFT
        for cx = 0, cols - 1 do
            local idx = cx * rows + cy
            local mx = wx + cx
            local g = gbase | mx
            local solid, half, flr, top, topq, roofed, sky

            local tt = (mx >= 0 and my >= 0) and getTileType(mx, my, z) or nil
            if tt then
                local bg, bg2 = L_bg[idx], L_bg2[idx]
                local class, tree = classify(tt)
                local blank = (bg == 0 or bg == BLANK) and bg2 == 0
                if class == CLASS_WALL then
                    if blank then
                        solid = unknown
                    elseif tree then
                        solid = sprites.bark(bg, bg2).lv
                    else
                        solid = sprites.wall(bg, bg2).lv
                    end
                elseif class == CLASS_OPEN then
                    -- air; only flying things could be here, and only the
                    -- level we stand on is worth the lookups
                    local v = L_c[idx]
                    if v ~= 0 then
                        pieces[1], pieces[2], pieces[3] = v, 0, 0
                        local tex = sprites.billboard(pieces, 3)
                        if tex then
                            nbills = nbills + 1
                            bills[nbills] = {x=mx + 0.5, y=my + 0.5, z=z, tex=tex, mx=mx, my=my}
                            bsum = bsum + v + idx + tex.vsum
                        end
                    end
                else
                    local b1, b2 = L_b1[idx], L_b2[idx]
                    if not (blank and b1 == 0 and b2 == 0 and L_c[idx] == 0 and L_item[idx] == 0) then
                        local des, occ = getTileFlags(mx, my, z)
                        local occb = occ.building
                        if (b1 ~= 0 or b2 ~= 0) and (occb == OCC_IMPASSABLE or occb == OCC_DYNAMIC) then
                            -- a building that fills the tile (closed door, floodgate, ...)
                            solid = sprites.flat_wall(bg, b1 ~= 0 and b1 or b2).lv
                        else
                            local flow = des.flow_size
                            local ftx
                            if flow >= 2 then
                                local lt = des.liquid_type
                                local wtx = sprites.special((lt == true or lt == 1) and 'magma' or 'water').lv
                                if flow >= 4 then
                                    top, topq = wtx, 3
                                    ftx = sprites.floor(bg, 0, false)
                                    if ftx then flr = ftx.lv end
                                else
                                    flr = wtx
                                end
                            else
                                ftx = sprites.floor(bg, 0, false)
                                if ftx then flr = ftx.lv end
                            end
                            if class == CLASS_RAMP then
                                -- half-height step with the ramp sprite on top
                                half = sprites.wall(bg, 0).lv
                                top, topq = flr or half, 2
                                flr = nil
                            end
                            if des.outside then sky = true else roofed = true end

                            -- things standing on the tile
                            local n = 0
                            if bg2 ~= 0 then
                                pieces[1], pieces[2], pieces[3] = bg2, 0, 0
                                n = 3
                            end
                            if b1 ~= 0 then
                                pieces[n + 1], pieces[n + 2], pieces[n + 3] = b1, 0, 0
                                n = n + 3
                            end
                            local v = L_item[idx]
                            if v ~= 0 then
                                pieces[n + 1], pieces[n + 2], pieces[n + 3] = v, 0, 0
                                n = n + 3
                            end
                            v = L_vehicle[idx]
                            if v ~= 0 then
                                pieces[n + 1], pieces[n + 2], pieces[n + 3] = v, 0, 0
                                n = n + 3
                            end
                            v = L_vermin[idx]
                            if v ~= 0 then
                                pieces[n + 1], pieces[n + 2], pieces[n + 3] = v, 0, 0
                                n = n + 3
                            end
                            v = L_c[idx]
                            if v ~= 0 then
                                pieces[n + 1], pieces[n + 2], pieces[n + 3] = v, 0, 0
                                n = n + 3
                                -- large creatures spill into the neighbouring tiles
                                local p
                                if cx > 0 then
                                    p = L_lc[idx - rows]
                                    if p ~= 0 then
                                        pieces[n + 1], pieces[n + 2], pieces[n + 3] = p, -1, 0
                                        n = n + 3
                                    end
                                end
                                if cx < cols - 1 then
                                    p = L_rc[idx + rows]
                                    if p ~= 0 then
                                        pieces[n + 1], pieces[n + 2], pieces[n + 3] = p, 1, 0
                                        n = n + 3
                                    end
                                end
                                if cy > 0 then
                                    p = L_uc[idx - 1]
                                    if p ~= 0 then
                                        pieces[n + 1], pieces[n + 2], pieces[n + 3] = p, 0, -1
                                        n = n + 3
                                    end
                                    if cx > 0 then
                                        p = L_ulc[idx - rows - 1]
                                        if p ~= 0 then
                                            pieces[n + 1], pieces[n + 2], pieces[n + 3] = p, -1, -1
                                            n = n + 3
                                        end
                                    end
                                    if cx < cols - 1 then
                                        p = L_urc[idx + rows - 1]
                                        if p ~= 0 then
                                            pieces[n + 1], pieces[n + 2], pieces[n + 3] = p, 1, -1
                                            n = n + 3
                                        end
                                    end
                                end
                            end
                            if b2 ~= 0 then
                                pieces[n + 1], pieces[n + 2], pieces[n + 3] = b2, 0, 0
                                n = n + 3
                            end
                            v = L_proj[idx]
                            if v ~= 0 then
                                pieces[n + 1], pieces[n + 2], pieces[n + 3] = v, 0, 0
                                n = n + 3
                            end
                            v = L_sign[idx]
                            if v ~= 0 then
                                pieces[n + 1], pieces[n + 2], pieces[n + 3] = v, 0, 0
                                n = n + 3
                            end
                            if n > 0 then
                                local tex = sprites.billboard(pieces, n)
                                if tex then
                                    nbills = nbills + 1
                                    bills[nbills] = {x=mx + 0.5, y=my + 0.5, z=z, tex=tex, mx=mx, my=my,
                                        raise=topq and topq / 4 or 0}
                                    for i = 1, n, 3 do
                                        bsum = (bsum * 31 + pieces[i]) & 0xFFFFFFFFFFFF
                                    end
                                    bsum = bsum + idx + tex.vsum
                                end
                            end
                        end
                    end
                end
            end

            if Lsolid[g] ~= solid then
                -- whether the neighbours' faces can be seen depends on this tile
                dirty[g - 1], dirty[g + 1], dirty[g - 1024], dirty[g + 1024] = true, true, true, true
            end
            if Lsolid[g] ~= solid or Lhalf[g] ~= half or Lflr[g] ~= flr or Ltop[g] ~= top or
                    Ltopq[g] ~= topq or Lroofed[g] ~= roofed or Lsky[g] ~= sky then
                Lsolid[g], Lhalf[g], Lflr[g], Ltop[g], Ltopq[g], Lroofed[g], Lsky[g] =
                    solid, half, flr, top, topq, roofed, sky
                dirty[g] = true
                changed = true
            end
        end
    end

    if bsum ~= L.bsum or nbills ~= #L.bills then changed = true end
    L.bills, L.bsum = bills, bsum
    L.stamp = now
    return changed
end

-- rebuild the renderer's description of one map column from the level data
local function build_stack(g, zlo, zhi)
    local roof = sprites.special('roof').lv
    local faces, nf = {}, 0     -- {hq0, hq1, tex} triples
    local surfs, ns = {}, 0     -- {hq, tex, dir} triples; dir 1 = seen from above, -1 = from below
    local sky = false
    local run_tex, run_h0, run_h1

    for z = zlo, zhi do
        local L = levels[z]
        if L then
            local below, above = levels[z - 1], levels[z + 1]
            local Ls = L.solid
            local s = Ls[g]
            if s then
                -- a block walled in on all four sides can never show a face;
                -- leaving those out is what keeps solid ground cheap to cross
                local buried = Ls[g - 1] and Ls[g + 1] and Ls[g - 1024] and Ls[g + 1024]
                if not buried and run_tex == s and run_h1 == 4 * z and not s.stretch then
                    run_h1 = 4 * z + 4
                else
                    if run_tex then
                        faces[nf + 1], faces[nf + 2], faces[nf + 3] = run_h0, run_h1, run_tex
                        nf = nf + 3
                        run_tex = nil
                    end
                    if not buried then
                        run_tex, run_h0, run_h1 = s, 4 * z, 4 * z + 4
                    end
                end
                -- underside of an overhang
                if below and not below.solid[g] and (below.flr[g] or below.top[g] or below.sky[g] or below.roofed[g]) then
                    surfs[ns + 1], surfs[ns + 2], surfs[ns + 3] = 4 * z, s, -1
                    ns = ns + 3
                end
                -- top of a block that has no known floor above it
                if not above or not (above.solid[g] or above.flr[g] or above.half[g]) then
                    surfs[ns + 1], surfs[ns + 2], surfs[ns + 3] = 4 * z + 4, s, 1
                    ns = ns + 3
                end
            else
                local f = L.flr[g]
                if f then
                    surfs[ns + 1], surfs[ns + 2], surfs[ns + 3] = 4 * z, f, 1
                    ns = ns + 3
                    if below and not below.solid[g] then
                        surfs[ns + 1], surfs[ns + 2], surfs[ns + 3] = 4 * z, f, -1
                        ns = ns + 3
                    end
                end
                local h = L.half[g]
                if h then
                    faces[nf + 1], faces[nf + 2], faces[nf + 3] = 4 * z, 4 * z + 2, h
                    nf = nf + 3
                end
                local t = L.top[g]
                if t then
                    surfs[ns + 1], surfs[ns + 2], surfs[ns + 3] = 4 * z + L.topq[g], t, 1
                    ns = ns + 3
                end
                if L.roofed[g] and not (above and (above.flr[g] or above.solid[g])) then
                    surfs[ns + 1], surfs[ns + 2], surfs[ns + 3] = 4 * z + 4, roof, -1
                    ns = ns + 3
                end
                if L.sky[g] then sky = true end
            end
        end
    end
    if run_tex then
        faces[nf + 1], faces[nf + 2], faces[nf + 3] = run_h0, run_h1, run_tex
        nf = nf + 3
    end
    if nf == 0 and ns == 0 and not sky then return nil end
    return {f=faces, nf=nf, s=surfs, ns=ns, sky=sky}
end

-- Capture the levels around cam_z. The level on screen is always re-read;
-- with `full` every level in range is, otherwise one more per call in turn.
-- Returns true if the scene changed.
function capture(now, cam_z, levels_down, levels_up, full)
    local vp = df.global.gps.main_viewport
    if vp.dim_x < 1 or vp.dim_y < 1 or vp.dim_x > MAX_COLS then return false end
    local _, _, map_z = dfhack.maps.getTileSize()
    local zlo = math.max(0, cam_z - levels_down)
    local zhi = math.min(map_z - 1, cam_z + levels_up)

    -- forget far-away terrain now and then so the cache cannot grow forever
    local wx, wy = df.global.window_x, df.global.window_y
    if not anchor or math.abs(wx - anchor.x) > 96 or math.abs(wy - anchor.y) > 96 then
        reset()
        anchor = {x=wx, y=wy}
        full = true
    end

    sprites.begin_pass(now)
    local todo = {}
    if full then
        for z = zlo, zhi do todo[#todo + 1] = z end
    else
        todo[1] = cam_z
        local n = zhi - zlo
        if n > 0 then
            -- next background level, skipping the camera level
            for _ = 1, n + 1 do
                rr_index = (rr_index + 1) % (n + 1)
                local z = zlo + rr_index
                if z ~= cam_z then
                    todo[2] = z
                    break
                end
            end
        end
    end

    local changed = false
    local scr = dfhack.gui.getDFViewscreen(true)
    local shown_z = df.global.window_z
    local on_screen = shown_z
    capturing = true
    local ok, err = pcall(function()
        for _, z in ipairs(todo) do
            if z ~= on_screen then
                df.global.window_z = z
                scr:render(dfhack.getTickCount())
                on_screen = z
            end
            if read_level(z, now) then changed = true end
        end
    end)
    -- always put the view back, even after an error
    if on_screen ~= shown_z then
        df.global.window_z = shown_z
        pcall(function() scr:render(dfhack.getTickCount()) end)
    end
    capturing = false
    if not ok then error(err, 0) end

    for g in pairs(dirty_cols) do
        stacks[g] = build_stack(g, zlo, zhi)
        dirty_cols[g] = nil
    end
    return changed
end

-- every billboard on the captured levels
function billboards(cam_z, levels_down, levels_up)
    local list, n = {}, 0
    for z = cam_z - levels_down, cam_z + levels_up do
        local L = levels[z]
        if L then
            for _, b in ipairs(L.bills) do
                n = n + 1
                list[n] = b
            end
        end
    end
    return list
end

-- what kind of ground is at this position: 'ramp', 'floor' or nil
function ground_at(x, y, z)
    local L = levels[z]
    if not L then return nil end
    local g = (y << KEY_SHIFT) | x
    if L.half[g] then return 'ramp' end
    if L.flr[g] then return 'floor' end
    return nil
end
