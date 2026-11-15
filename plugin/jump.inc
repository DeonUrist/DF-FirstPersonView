// Jumping (the game's J, ADVENTURE_JUMP). The game takes the jump's target as
// a tile on your own level, also one over a pit (that is how one jumps down).
// In the view a click lands on whatever is seen, often the ground a level or
// more below, so while the game's jump is open the tiles it accepts are drawn
// as squares on your level's floor height, and a click is taken where the
// pointer's ray crosses that height (jump_pick), even in the air.
//
// Which tiles, read from the code of Dwarf Fortress 53.17 (2026-11-16): the
// jump key's handler (0x1408b6741 on) tries every tile up to 5 away across and
// along on your level, not your own, through two tests:
// - 0x140e9caa0: inside the map and a flag of the tile set (taken here as not
//   hidden; what the flag is was not traced).
// - 0x140e9c800: without momentum (unit.job.move_momentum_dir 0 or
//   unit.job.gait_buildup <= 0) exactly two tiles away; with it, by the speed
//   1000000 / (delay + 100) of the game's move-delay function (0x14134d520):
//   under 2000 the same, else 2 to 3 tiles (2500: 4, 3000: 5) and only ahead
//   in the direction of the momentum (bits 1/2: y -1/+1, 4/8: x +1/-1).
// The move delay here is the gait's own (full_speed, from start_speed while the
// gait builds up), as the start of the game's function has it; the rest of that
// function (attributes, weight, wounds and more) is left out, so a running
// jump may be shown a tile further than the game allows. Walls are left out
// (nothing to land on; whether the game offers them was not checked).

static_assert(offsetof(df::unit, job) + offsetof(df::unit::T_job, move_momentum_dir) == 0x4c4 &&
                  offsetof(df::unit, job) + offsetof(df::unit::T_job, gait_buildup) == 0x4c8,
              "the jump rules were read for these fields at these places");

struct JumpSpots {
    bool on = false;
    int16_t z = 0;
    float h = 0;                                     // the level's floor, in tile widths
    std::vector<std::pair<int16_t, int16_t>> tiles;  // where the game lets you jump
    int16_t cx = -1, cy = -1;                        // the game's own jump cursor, if on that level
};

std::mutex jump_mutex;
JumpSpots jump_spots;
// the camera of the frame on screen, in the pixels it was made at
fpv::Camera jump_cam;
int jump_cam_w = 0, jump_cam_h = 0;
bool jump_cam_ok = false;

// how far a running jump reaches (0: a standing jump, two tiles exactly)
int jump_reach(df::unit *u) {
    if (!u->job.move_momentum_dir || u->job.gait_buildup <= 0)
        return 0;
    int delay = 900;   // the game's own value when there is no gait
    if (u->body.body_plan) {
        const df::gait_type type = u->flags1.bits.on_ground ? df::gait_type::CRAWL : df::gait_type::WALK;
        const auto &gaits = u->body.body_plan->gait_info[type];
        const int32_t i = u->enemy.gait_index[type];
        if (i >= 0 && i < int(gaits.size()) && gaits[i]) {
            const df::gait_info &g = *gaits[i];
            if (g.buildup_time > 0 && u->job.gait_buildup < g.buildup_time)
                delay = (u->job.gait_buildup * g.full_speed + (g.buildup_time - u->job.gait_buildup) * g.start_speed) /
                        g.buildup_time;
            else
                delay = g.full_speed;
        }
    }
    const int speed = 1000000 / std::max(1, delay + 100);
    return speed >= 3000 ? 5 : speed >= 2500 ? 4 : speed >= 2000 ? 3 : 0;
}

bool jump_allowed(df::unit *u, int reach, int x, int y) {
    const int dx = x - u->pos.x, dy = y - u->pos.y, adx = std::abs(dx), ady = std::abs(dy);
    if (reach == 0)
        return std::max(adx, ady) == 2;
    if ((adx <= 1 && ady <= 1) || adx > reach || ady > reach)
        return false;
    const uint8_t b = u->job.move_momentum_dir;
    const int my = (b & 2) ? 1 : (b & 1) ? -1 : 0;
    const int mx = (b & 8) ? -1 : (b & 4) ? 1 : 0;
    if (mx != 0) {
        if (dx * mx < 0)
            return false;
        return my == 0 ? ady <= adx : dy * my >= 0;
    }
    if (my == 0)
        return false;
    return dy * my >= 0 && ady >= adx;
}

// game thread, every captured frame
void jump_update(df::unit *adv) {
    JumpSpots s;
    if (adv && game && game->main_interface.adventure.jump.open) {
        s.on = true;
        s.z = adv->pos.z;
        s.h = adv->pos.z * LEVEL_H;
        const int reach = jump_reach(adv);
        for (int x = adv->pos.x - 5; x <= adv->pos.x + 5; ++x)
            for (int y = adv->pos.y - 5; y <= adv->pos.y + 5; ++y) {
                const df::coord pos(int16_t(x), int16_t(y), adv->pos.z);
                if (pos == adv->pos || !Maps::isValidTilePos(pos) || !jump_allowed(adv, reach, x, y))
                    continue;
                const df::tile_designation *des = Maps::getTileDesignation(pos);
                const df::tiletype *tt = Maps::getTileType(pos);
                if (!des || des->bits.hidden || !tt ||
                    tileShapeBasic(tileShape(*tt)) == df::tiletype_shape_basic::Wall)
                    continue;
                s.tiles.emplace_back(int16_t(x), int16_t(y));
            }
        const df::coord &c = game->main_interface.adventure.jump.cursor;
        if (c.z == s.z) {
            s.cx = c.x;
            s.cy = c.y;
        }
        radial_load_icons();   // the jump pointer is the ring's jump picture
    }
    std::lock_guard<std::mutex> lock(jump_mutex);
    jump_spots = std::move(s);
}

bool jump_shown() {
    std::lock_guard<std::mutex> lock(jump_mutex);
    return jump_spots.on;
}

// render thread: the frame just drawn, for jump_pick and the squares
void jump_note_camera(const fpv::Camera &cam, int w, int h) {
    std::lock_guard<std::mutex> lock(jump_mutex);
    jump_cam = cam;
    jump_cam_w = w;
    jump_cam_h = h;
    jump_cam_ok = true;
}

// The tile the jump goes to for a window pixel: where the ray through it
// crosses your level's floor height, if the game lets you jump there.
fpv::Pick jump_pick(int px, int py) {
    fpv::Pick p;
    const int ow = view_out_w.load(), oh = view_out_h.load();
    if (ow <= 0 || oh <= 0 || px < 0 || py < 0 || px >= ow || py >= oh)
        return p;
    std::lock_guard<std::mutex> lock(jump_mutex);
    if (!jump_spots.on || !jump_cam_ok || jump_cam_w <= 0 || jump_cam_h <= 0)
        return p;
    const fpv::View3D v = fpv::View3D::of(jump_cam, jump_cam_w, jump_cam_h);
    const float sx = (px + 0.5f) * jump_cam_w / ow, sy = (py + 0.5f) * jump_cam_h / oh;
    const float a = (sx - v.cx) / v.S, b = (v.cy - sy) / v.S;
    const float Dx = v.fx + v.rx * a + v.ux * b, Dy = v.fy + v.ry * a + v.uy * b, Dz = v.fz + v.uz * b;
    if (Dz > -1e-4f)
        return p;   // level or up: never reaches the floor
    const float t = (jump_spots.h - v.eye) / Dz;
    if (t <= 0)
        return p;
    const int tx = int(std::floor(v.px + Dx * t)), ty = int(std::floor(v.py + Dy * t));
    for (const auto &tile : jump_spots.tiles)
        if (tile.first == tx && tile.second == ty) {
            p.x = int16_t(tx);
            p.y = int16_t(ty);
            p.z = jump_spots.z;
            return p;
        }
    return p;
}

// render thread, over the view and under the interface: a square on every
// tile the jump can go to, the one at the pointer (window pixels px, py; else
// the game's cursor) lit up
void draw_jump_squares(SDL_Renderer *sdl, int out_w, int out_h, int px, int py) {
    if (!gpu_api.render_geometry)
        return;
    int hx = -1, hy = -1;
    if (px >= 0 && !over_interface(px, py)) {
        const fpv::Pick hp = jump_pick(px, py);
        hx = hp.x;
        hy = hp.y;
    }
    JumpSpots s;
    fpv::Camera cam;
    int cw = 0, ch = 0;
    {
        std::lock_guard<std::mutex> lock(jump_mutex);
        if (!jump_spots.on || !jump_cam_ok || jump_spots.tiles.empty())
            return;
        s = jump_spots;
        cam = jump_cam;
        cw = jump_cam_w;
        ch = jump_cam_h;
    }
    if (cw <= 0 || ch <= 0)
        return;
    if (hx < 0) {
        hx = s.cx;
        hy = s.cy;
    }
    const fpv::View3D v = fpv::View3D::of(cam, cw, ch);
    const float kx = float(out_w) / cw, ky = float(out_h) / ch;
    const float h = s.h + 0.01f;
    std::vector<SDL_Vertex> vert;
    std::vector<int> idx;
    vert.reserve(s.tiles.size() * 12);
    idx.reserve(s.tiles.size() * 30);
    const auto vertex = [&](float sx, float sy, SDL_Color c) {
        SDL_Vertex out;
        out.position = SDL_FPoint{sx * kx, sy * ky};
        out.color = c;
        out.tex_coord = SDL_FPoint{0, 0};
        vert.push_back(out);
    };
    // corners of a square inset by e, in order around it
    const auto corners = [&](int x, int y, float e, float (&sx)[4], float (&sy)[4]) {
        const float wx[4] = {x + e, x + 1 - e, x + 1 - e, x + e};
        const float wy[4] = {y + e, y + e, y + 1 - e, y + 1 - e};
        for (int i = 0; i < 4; ++i) {
            float d;
            if (!v.project(wx[i], wy[i], h, sx[i], sy[i], d))
                return false;
        }
        return true;
    };
    for (const auto &tile : s.tiles) {
        float ox[4], oy[4], ix[4], iy[4];
        if (!corners(tile.first, tile.second, 0.06f, ox, oy) || !corners(tile.first, tile.second, 0.14f, ix, iy))
            continue;
        const bool lit = tile.first == hx && tile.second == hy;
        const SDL_Color fill = lit ? SDL_Color{255, 236, 150, 110} : SDL_Color{255, 220, 110, 40};
        const SDL_Color edge = lit ? SDL_Color{255, 255, 210, 255} : SDL_Color{255, 206, 80, 200};
        const int base = int(vert.size());
        for (int i = 0; i < 4; ++i)
            vertex(ix[i], iy[i], fill);
        for (int i = 0; i < 4; ++i)
            vertex(ox[i], oy[i], edge);
        for (int i = 0; i < 4; ++i)
            vertex(ix[i], iy[i], edge);
        const int quad[6] = {0, 1, 2, 0, 2, 3};
        for (int k : quad)
            idx.push_back(base + k);
        // the frame: four bands between the outer and the inner corners
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            const int o0 = base + 4 + i, o1 = base + 4 + j, i0 = base + 8 + i, i1 = base + 8 + j;
            const int band[6] = {o0, o1, i1, o0, i1, i0};
            for (int k : band)
                idx.push_back(k);
        }
    }
    if (idx.empty())
        return;
    SDL_BlendMode bm;
    sdl_get_render_draw_blend_mode(sdl, &bm);
    sdl_set_render_draw_blend_mode(sdl, SDL_BLENDMODE_BLEND);
    gpu_api.render_geometry(sdl, nullptr, vert.data(), int(vert.size()), idx.data(), int(idx.size()));
    sdl_set_render_draw_blend_mode(sdl, bm);
}

// The ring's jump picture as a pointer, about as tall as the game shows it on
// its button (three cells), its middle on the spot. Render thread.
SDL_Cursor *jump_cursor(int cell_h) {
    static SDL_Cursor *made = nullptr;
    static std::shared_ptr<const fpv::Image> made_from;
    static int made_h = 0;
    std::shared_ptr<const fpv::Image> icon;
    {
        std::lock_guard<std::mutex> lock(radial_icon_mutex);
        for (int i = 0; i < RADIAL_COUNT && i < int(radial_icons.size()); ++i)
            if (std::strcmp(RADIAL_ACTIONS[i].id, "jump") == 0)
                icon = radial_icons[size_t(i)];
    }
    if (!icon || !icon->ok())
        return nullptr;
    const int th = std::clamp(cell_h * 3, 16, 96);
    if (made && made_from == icon && made_h == th)
        return made;
    const int tw = std::max(1, icon->w * th / icon->h);
    SDL_Surface *surface = sdl_create_rgb_surface_with_format(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return made;
    // each pixel the average of the picture's pixels it covers (by alpha)
    for (int y = 0; y < th; ++y) {
        auto row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(surface->pixels) + y * surface->pitch);
        const int y0 = y * icon->h / th, y1 = std::max(y0 + 1, (y + 1) * icon->h / th);
        for (int x = 0; x < tw; ++x) {
            const int x0 = x * icon->w / tw, x1 = std::max(x0 + 1, (x + 1) * icon->w / tw);
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = y0; sy < y1 && sy < icon->h; ++sy)
                for (int sx = x0; sx < x1 && sx < icon->w; ++sx) {
                    const uint32_t c = icon->px[size_t(sy) * icon->w + sx];
                    const uint32_t ca = uint32_t(fpv::px_a(c));
                    r += uint32_t(fpv::px_r(c)) * ca;
                    g += uint32_t(fpv::px_g(c)) * ca;
                    b += uint32_t(fpv::px_b(c)) * ca;
                    a += ca;
                    ++n;
                }
            row[x] = a ? (r / a) | (g / a) << 8 | (b / a) << 16 | (a / std::max(1u, n)) << 24 : 0;
        }
    }
    SDL_Cursor *cursor = sdl_create_color_cursor(surface, tw / 2, th / 2);
    sdl_free_surface(surface);
    if (cursor) {
        made = cursor;   // the one before is left alone: SDL may still show it
        made_from = icon;
        made_h = th;
    }
    return made;
}
