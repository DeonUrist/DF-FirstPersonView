--@ module = true
-- Software renderer for a world made of stacked tiles.
--
-- For every screen column a ray walks across the map, tile by tile, front to
-- back. Each map column it crosses contributes
--   * vertical faces where it enters a solid block (drawn as a textured strip)
--   * horizontal surfaces - floors seen from above, ceilings seen from below
--     (drawn by projecting each pixel onto the surface)
-- on any of the captured z-levels. A short list of still-empty pixel ranges
-- per screen column makes nearer things hide farther ones, and the walk stops
-- as soon as the column is full. Billboards (creatures, items, plants) are
-- drawn last against a per-pixel depth buffer.
--
-- Output goes into a flat Lua array `frame` (row-major, 1-based, 0xAABBGGRR).
-- The inner loops use 16.16 fixed-point integers because integer maths and
-- shifts are the fastest thing stock Lua 5.3 can do.

local sprites = reqscript('internal/first-person/sprites')

local NUM_LEVELS = sprites.NUM_LEVELS

LEVEL_H = 1.45   -- height of one z-level, in tile widths
EYE = 0.72       -- eye height above the floor, in tile widths
local QH = LEVEL_H / 4          -- height of a quarter level
MAX_DIST = 200                  -- clamp for surface distances near the horizon
local FAR = 1 << 40
local BIAS = 1 << 44   -- multiple of 65536, larger than any distance * direction

local floor, abs, sin, cos, tan, ceil = math.floor, math.abs, math.sin, math.cos, math.tan, math.ceil

-- per-frame lookup tables, rebuilt when the camera height or output size changes
local tab = {}
local row_dist = {}    -- hq -> row -> 16.16 distance to the surface at that height
local sky_row = {}
-- every span drawn, per screen column, with its depth (16.16): billboards
-- are clipped against these instead of a per-pixel depth buffer
local span_a, span_b, span_d = {}, {}, {}
local col_first, col_last = {}, {}

-- still-empty pixel ranges of the current column: [oa[i], ob[i])
local oa, ob = {}, {}
local no = 0
-- visible parts of the span being drawn
local va, vb = {}, {}

local function lerp(a, b, t) return floor(a + (b - a) * t + 0.5) end

-- take [a, b) out of the open ranges; the parts that were still open end up
-- in va/vb. returns how many parts there are
local function clip(a, b)
    local nv = 0
    local i = 1
    while i <= no do
        local ia, ib = oa[i], ob[i]
        if ib <= a then
            i = i + 1
        elseif ia >= b then
            break
        else
            local s = a > ia and a or ia
            local e = b < ib and b or ib
            nv = nv + 1
            va[nv], vb[nv] = s, e
            if s == ia and e == ib then
                for j = i, no - 1 do
                    oa[j], ob[j] = oa[j + 1], ob[j + 1]
                end
                no = no - 1
            elseif s == ia then
                oa[i] = e
                i = i + 1
            elseif e == ib then
                ob[i] = s
                i = i + 1
            else
                for j = no, i + 1, -1 do
                    oa[j + 1], ob[j + 1] = oa[j], ob[j]
                end
                oa[i + 1], ob[i + 1] = e, ib
                ob[i] = s
                no = no + 1
                i = i + 2
            end
        end
    end
    return nv
end

local function prepare(W, H, S, hz, eye, view_dist, daylight)
    if tab.W ~= W or tab.H ~= H or tab.S ~= S or tab.hz ~= hz or tab.eye ~= eye or tab.view_dist ~= view_dist then
        tab.W, tab.H, tab.S, tab.hz, tab.eye, tab.view_dist = W, H, S, hz, eye, view_dist
        row_dist = {}
        tab.daylight = nil
    end
    if tab.daylight ~= daylight then
        tab.daylight = daylight
        for y = 0, H - 1 do
            -- sky: zenith at the top, haze towards the horizon
            local t = (y + 0.5) / (hz > 1 and hz or 1)
            if t > 1 then t = 1 end
            t = t * t
            local zr, zg, zb_ = 52, 104, 188
            local hr, hg, hb = 158, 196, 226
            local nr, ng, nb = 4, 6, 18
            local r = lerp(lerp(nr, zr, daylight), lerp(nr + 14, hr, daylight), t)
            local g = lerp(lerp(ng, zg, daylight), lerp(ng + 16, hg, daylight), t)
            local b = lerp(lerp(nb, zb_, daylight), lerp(nb + 26, hb, daylight), t)
            sky_row[y] = sprites.rgb(r, g, b)
        end
    end
end

-- per-row distance table for the surface at quarter-height hq
local function build_rows(hq)
    local H, S, hz, eye = tab.H, tab.S, tab.hz, tab.eye
    local dh = eye - hq * QH
    local dist = {}
    for y = 0, H - 1 do
        local dy = y + 0.5 - hz
        local d = MAX_DIST
        if dh * dy > 0 then
            d = dh * S / dy
            if d > MAX_DIST then d = MAX_DIST end
        end
        dist[y] = floor(d * 65536)
    end
    row_dist[hq] = dist
    return dist
end

-- frame:  output array, W*H entries
-- stacks: scene.stacks
-- bills:  list of billboards
-- cam:    x, y = position in map tiles; eye = eye height in tile widths
--         (z * LEVEL_H + EYE); ang = heading in radians (0 = north,
--         clockwise); fov = horizontal field of view in degrees;
--         pitch = vertical shift of the horizon in pixels
-- opts:   view_distance, daylight, map_w, map_h, skip = billboard to leave out
function render(frame, W, H, stacks, bills, cam, opts)
    local P = tan(math.rad(cam.fov) / 2)
    local S = W / (2 * P)
    local hz = H / 2 + (cam.pitch or 0)
    local eye = cam.eye
    local view_dist = opts.view_distance
    prepare(W, H, S, hz, eye, view_dist, opts.daylight)

    local px, py = cam.x, cam.y
    local ang = cam.ang
    local dirx, diry = sin(ang), -cos(ang)
    local rgtx, rgty = cos(ang), sin(ang)
    local planex, planey = rgtx * P, rgty * P
    local map_w, map_h = opts.map_w, opts.map_h

    local lvscale = (NUM_LEVELS - 1) / view_dist
    -- camera position in 16.16, pre-compensated for the bias that keeps the
    -- per-pixel products positive (>> is a logical shift in Lua)
    local cxb, cyb = floor(px * 65536) - (BIAS >> 16), floor(py * 65536) - (BIAS >> 16)
    local fog = sprites.rgb(sprites.FOG_R, sprites.FOG_G, sprites.FOG_B)
    local max_d = view_dist + 4
    local hzi = floor(hz + 0.5)
    if hzi < 0 then hzi = 0 elseif hzi > H then hzi = H end

    local cam_stack = stacks[(floor(py) << 10) | floor(px)]
    local cam_sky = cam_stack and cam_stack.sky or false

    local nsp = 0
    for sx = 0, W - 1 do
        local camx = (2 * (sx + 0.5) / W) - 1
        local rx, ry = dirx + planex * camx, diry + planey * camx
        local rxi, ryi = floor(rx * 65536), floor(ry * 65536)

        local mapx, mapy = floor(px), floor(py)
        local ddx = rx == 0 and 1e30 or abs(1 / rx)
        local ddy = ry == 0 and 1e30 or abs(1 / ry)
        local stepx, stepy, sdx, sdy
        if rx < 0 then
            stepx, sdx = -1, (px - mapx) * ddx
        else
            stepx, sdx = 1, (mapx + 1 - px) * ddx
        end
        if ry < 0 then
            stepy, sdy = -1, (py - mapy) * ddy
        else
            stepy, sdy = 1, (mapy + 1 - py) * ddy
        end

        no = 1
        oa[1], ob[1] = 0, H
        col_first[sx] = nsp + 1
        local d0, side, inv0 = 0, 0, 0
        local col_sky = cam_sky
        local col = sx + 1

        while true do
            local d1 = sdx < sdy and sdx or sdy
            local inv1 = S / d1
            local st = stacks[(mapy << 10) | mapx]
            if st then
                if st.sky then col_sky = true end

                -- vertical faces on the side of the tile the ray enters through
                local nf = st.nf
                if nf > 0 and d0 > 0 then
                    local inv = inv0
                    local f = st.f
                    local texx1
                    for i = 1, nf, 3 do
                        local top = hz - (f[i + 1] * QH - eye) * inv
                        local bot = hz - (f[i] * QH - eye) * inv
                        local a = top <= 0 and 0 or floor(top + 0.5)
                        local b = bot >= H and H or floor(bot + 0.5)
                        if a < b and b > oa[1] and a < ob[no] then
                            local nv = clip(a, b)
                            if nv > 0 then
                                if not texx1 then
                                    local wallx
                                    if side == 0 then wallx = py + d0 * ry else wallx = px + d0 * rx end
                                    wallx = wallx - floor(wallx)
                                    local texx = floor(wallx * 32)
                                    if texx > 31 then texx = 31 end
                                    if (side == 0 and rx < 0) or (side == 1 and ry > 0) then texx = 31 - texx end
                                    texx1 = texx + 1
                                end
                                local l = 1 + floor(d0 * lvscale) + side
                                if l > NUM_LEVELS then l = NUM_LEVELS end
                                local lvs = f[i + 2]
                                local lv = lvs[l]
                                -- masonry repeats every tile width; a door is one sprite per face
                                local span = lvs.stretch and 32 or (f[i + 1] - f[i]) * QH * 32
                                local step = floor(span * 65536 / (bot - top))
                                local voff = floor((ceil(span / 32) * 32 - span) * 65536)
                                local dfix = floor(d0 * 65536)
                                for k = 1, nv do
                                    local s, e = va[k], vb[k]
                                    local tpos = floor((s + 0.5 - top) * step) + voff
                                    local idx = s * W + col
                                    for _ = s, e - 1 do
                                        frame[idx] = lv[((tpos >> 16 & 31) << 5) + texx1]
                                        idx = idx + W
                                        tpos = tpos + step
                                    end
                                    nsp = nsp + 1
                                    span_a[nsp], span_b[nsp], span_d[nsp] = s, e, dfix
                                end
                                if no == 0 then break end
                            end
                        end
                    end
                    if no == 0 then break end
                end

                -- horizontal surfaces between where the ray enters and leaves the tile
                local ns = st.ns
                if ns > 0 then
                    local s_ = st.s
                    for i = 1, ns, 3 do
                        local hq = s_[i]
                        local dh = eye - hq * QH
                        local a, b
                        if s_[i + 2] > 0 then
                            -- seen from above: must be below the eye
                            if dh > 0 then
                                local far = hz + dh * inv1
                                a = far <= 0 and 0 or floor(far + 0.5)
                                if d0 > 0 then
                                    local near = hz + dh * inv0
                                    b = near >= H and H or floor(near + 0.5)
                                else
                                    b = H
                                end
                            end
                        elseif dh < 0 then
                            -- seen from below: must be above the eye
                            local far = hz + dh * inv1
                            b = far >= H and H or floor(far + 0.5)
                            if d0 > 0 then
                                local near = hz + dh * inv0
                                a = near <= 0 and 0 or floor(near + 0.5)
                            else
                                a = 0
                            end
                        end
                        if a and a < b and b > oa[1] and a < ob[no] then
                            local nv = clip(a, b)
                            if nv > 0 then
                                local rdt = row_dist[hq] or build_rows(hq)
                                -- one brightness per tile is plenty; ceilings are a bit darker
                                local l = 1 + floor(d1 * lvscale)
                                if s_[i + 2] < 0 then l = l + 2 end
                                if l > NUM_LEVELS then l = NUM_LEVELS end
                                local pix = s_[i + 1][l]
                                local dfar = floor(d1 * 65536)
                                for k = 1, nv do
                                    local s, e = va[k], vb[k]
                                    local idx = s * W + col
                                    for y = s, e - 1 do
                                        local rd = rdt[y]
                                        frame[idx] = pix[((((cyb + (rd * ryi + BIAS >> 16)) >> 11 & 31) << 5) |
                                            ((cxb + (rd * rxi + BIAS >> 16)) >> 11 & 31)) + 1]
                                        idx = idx + W
                                    end
                                    nsp = nsp + 1
                                    span_a[nsp], span_b[nsp], span_d[nsp] = s, e, dfar
                                end
                                if no == 0 then break end
                            end
                        end
                    end
                    if no == 0 then break end
                end
            end

            -- step into the next tile
            if sdx < sdy then
                sdx = sdx + ddx
                mapx = mapx + stepx
                side = 0
            else
                sdy = sdy + ddy
                mapy = mapy + stepy
                side = 1
            end
            d0 = d1
            inv0 = inv1
            if d0 > max_d or mapx < 0 or mapy < 0 or mapx >= map_w or mapy >= map_h then break end
        end

        -- whatever is still empty is sky above the horizon and haze below it
        for i = 1, no do
            local s, e = oa[i], ob[i]
            local idx = s * W + col
            local sky_end = col_sky and (e < hzi and e or hzi) or s
            for y = s, sky_end - 1 do
                frame[idx] = sky_row[y]
                idx = idx + W
            end
            for _ = (sky_end > s and sky_end or s), e - 1 do
                frame[idx] = fog
                idx = idx + W
            end
            nsp = nsp + 1
            span_a[nsp], span_b[nsp], span_d[nsp] = s, e, FAR
        end
        col_last[sx] = nsp
    end

    -- billboards, far to near
    local list, n = {}, 0
    local skip_x, skip_y, skip_z = opts.skip_x, opts.skip_y, opts.skip_z
    for _, b in ipairs(bills) do
        local relx, rely = b.x - px, b.y - py
        local depth = relx * dirx + rely * diry
        if depth > 0.3 and depth < max_d and not (b.mx == skip_x and b.my == skip_y and b.z == skip_z) then
            b.depth = depth
            b.lat = relx * rgtx + rely * rgty
            n = n + 1
            list[n] = b
        end
    end
    table.sort(list, function(a, b) return a.depth > b.depth end)

    for i = 1, n do
        local b = list[i]
        local tex = b.tex
        local depth = b.depth
        local inv = S / depth
        local left = W / 2 + b.lat * inv - tex.anchor * inv
        local wpx, hpx = tex.tiles_w * inv, tex.tiles_h * inv
        local base_h = (b.z + (b.raise or 0)) * LEVEL_H
        local bottom = hz + (eye - base_h) * inv + tex.below * inv
        local topy = bottom - hpx
        local x0 = left <= 0 and 0 or floor(left + 0.5)
        local x1 = left + wpx >= W and W or floor(left + wpx + 0.5)
        local y0 = topy <= 0 and 0 or floor(topy + 0.5)
        local y1 = bottom >= H and H or floor(bottom + 0.5)
        if x0 < x1 and y0 < y1 then
            local l = 1 + floor(depth * lvscale)
            if l > NUM_LEVELS then l = NUM_LEVELS end
            local lv = tex.lv[l]
            local tw, th = tex.w, tex.h
            local xstep = tw / wpx
            local ystep = floor(th * 65536 / hpx)
            local tpos0 = floor((y0 + 0.5 - topy) * ystep)
            local dfix = floor(depth * 65536)
            for sx = x0, x1 - 1 do
                local tx = floor((sx + 0.5 - left) * xstep)
                if tx < 0 then tx = 0 elseif tx >= tw then tx = tw - 1 end
                local tcol = tx + 1
                -- draw the parts of this column that lie behind the billboard
                for si = col_first[sx], col_last[sx] do
                    if dfix < span_d[si] then
                        local s, e = span_a[si], span_b[si]
                        if s < y0 then s = y0 end
                        if e > y1 then e = y1 end
                        if s < e then
                            local tpos = tpos0 + (s - y0) * ystep
                            local idx = s * W + sx + 1
                            for _ = s, e - 1 do
                                local ty = tpos >> 16
                                if ty >= th then break end
                                local c = lv[ty * tw + tcol]
                                if c ~= 0 then frame[idx] = c end
                                idx = idx + W
                                tpos = tpos + ystep
                            end
                        end
                    end
                end
            end
        end
    end
end
