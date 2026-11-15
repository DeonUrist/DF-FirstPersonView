// ---------------------------------------------------------------------------
// The tracked journal entries on the game's travel map: an exclamation mark in
// the entry's colour over its square, the name in the small font under the
// square. One that is off the map waits at the map's edge with an arrow
// pointing towards it and its distance under the name, as the compass holds
// the places that are out of its window at its ends.
//
// The travel map's layout, read in the game (2026-11-15, window 1920 x 1080):
// gps->main_map_port holds it, 120 x 68 squares of 16 pixels drawn from the
// renderer's origin (0, 4); the player's army is on the port's (screen_x,
// screen_y), its middle. A square is an embark square (48 tiles: three sites
// lay on their squares), or a third of one (16 tiles, army.pos's own unit) when
// the game zooms in at a site (adventure.site_level_zoom, set by the game on its
// own). The square's size in pixels is taken as the window over the port's
// size, rounded up (the port is as many squares as cover the window); only
// 1920 x 1080 was seen.

struct TravelModel {
    struct Mark {
        double cx = 0, cy = 0;   // on the port, in squares (the middle of its square)
        std::string name, dist;
        uint32_t color = 0;
    };
    int dim_x = 0, dim_y = 0;    // the port
    int you_x = 0, you_y = 0;    // your square
    std::vector<Mark> marks;
};

std::mutex travel_mutex;
std::shared_ptr<const TravelModel> travel_model;
std::atomic<int64_t> travel_seen_ms{0};   // when the game thread last found the travel map ours

int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// render thread: the travel map is up and ours (not a stale answer from before
// the adventure screen stopped drawing: the title screen, a world unloading)
bool travel_live() {
    return travel_ours && steady_ms() - travel_seen_ms.load() < 500;
}

void publish_travel(std::shared_ptr<const TravelModel> m) {
    std::lock_guard<std::mutex> lock(travel_mutex);
    travel_model = std::move(m);
}

std::shared_ptr<const TravelModel> get_travel() {
    std::lock_guard<std::mutex> lock(travel_mutex);
    return travel_model;
}

constexpr int MAX_TRAVEL_MARKS = 32;

// game thread, once a frame after the game has drawn
void travel_frame() {
    const bool ours = travel_map_ours() && gps && gps->main_map_port;
    travel_ours = ours;
    if (ours)
        travel_seen_ms = steady_ms();
    if (!ours) {
        if (get_travel())
            publish_travel(nullptr);
        return;
    }
    // the tracked entries' places (in tiles of the world), worked out again
    // every 30 frames or when the list changes
    struct Place {
        double x, y;
        std::string name, dist;
        uint32_t color;
    };
    static std::vector<Place> places;
    static int frames = 0;
    static uint32_t serial = ~0u;
    load_tracked();
    if (serial != tracked_serial.load() || ++frames >= 30) {
        frames = 0;
        serial = tracked_serial.load();
        places.clear();
        for (size_t i = 0; i < jr.tracked.size() && places.size() < size_t(MAX_TRAVEL_MARKS); ++i) {
            const JRef &r = jr.tracked[i];
            double x, y, rad;
            if (!marker_pos(r, x, y, rad))
                continue;
            places.push_back({x, y, names_of(r).english, where_text(x, y, rad),
                              i < jr.tracked_color.size() ? jr.tracked_color[i] : J_TRACK});
        }
    }
    int sx, sy;
    const df::graphic_map_portst *p = gps->main_map_port;
    if (!travel_smm(sx, sy) || p->dim_x <= 0 || p->dim_y <= 0) {
        publish_travel(nullptr);
        return;
    }
    const double unit = adventure->site_level_zoom ? 16.0 : 48.0;
    const double ux = std::floor((sx * 16.0 + 8) / unit), uy = std::floor((sy * 16.0 + 8) / unit);
    auto m = std::make_shared<TravelModel>();
    m->dim_x = p->dim_x;
    m->dim_y = p->dim_y;
    m->you_x = p->screen_x;
    m->you_y = p->screen_y;
    for (const Place &pl : places) {
        TravelModel::Mark k;
        k.cx = p->screen_x + (std::floor(pl.x / unit) - ux) + 0.5;
        k.cy = p->screen_y + (std::floor(pl.y / unit) - uy) + 0.5;
        k.name = pl.name;
        k.dist = pl.dist;
        k.color = pl.color;
        m->marks.push_back(std::move(k));
    }
    publish_travel(m);
}

// ---------------------------------------------------------------------------
// drawing (render thread)

UiTexture tex_travel[MAX_TRAVEL_MARKS];
std::string tex_travel_keys[MAX_TRAVEL_MARKS];

void destroy_travel_textures() {
    for (int i = 0; i < MAX_TRAVEL_MARKS; ++i) {
        tex_travel[i].destroy();
        tex_travel_keys[i].clear();
    }
}

// The mark as one picture: the exclamation mark (size pixels tall) at the top
// middle, `gap` see-through rows, then the label: the name, and the distance
// under it when given. Returns where the label starts (its top row).
int make_travel_texture(SDL_Renderer *sdl, UiTexture &t, const std::string &name, const std::string &dist,
                        uint32_t color, int size, int gap) {
    const int cw = 8, ch = 12;   // the small font
    const int bang_h = size + 3;  // paint_bang's foot and outline reach a little past `size`
    const int chars = int(std::max(name.size(), dist.size()));
    const int lw = chars * cw + 6, lh = (dist.empty() ? 1 : 2) * ch + 4;
    fpv::Canvas cv;
    cv.resize(std::max(lw, size / 4 + 6), bang_h + gap + lh);
    paint_bang(cv, cv.w / 2, size + 1, size, false, cw, ch, color);
    const int ly = bang_h + gap, lx = (cv.w - lw) / 2;
    cv.fill(lx, ly, lw, lh, 0xB0000000u | (fpv::rgb(12, 12, 16) & 0x00FFFFFFu));
    cv.frame(lx, ly, lw, lh, color, 1);
    panel_text(cv, lx + (lw - int(name.size()) * cw) / 2, ly + 2, name, UI_WHITE, cw, ch);
    if (!dist.empty())
        panel_text(cv, lx + (lw - int(dist.size()) * cw) / 2, ly + 2 + ch, dist, UI_DIM, cw, ch);
    t.upload(sdl, cv.px.data(), cv.w, cv.h);
    return ly;
}

// a filled triangle, row by row
void fill_triangle(SDL_Renderer *sdl, const double (&v)[3][2]) {
    const double y0 = std::min({v[0][1], v[1][1], v[2][1]}), y1 = std::max({v[0][1], v[1][1], v[2][1]});
    for (int y = int(std::floor(y0)); y <= int(std::ceil(y1)); ++y) {
        const double yc = y + 0.5;
        double lo = 1e9, hi = -1e9;
        for (int e = 0; e < 3; ++e) {
            const double *a = v[e], *b = v[(e + 1) % 3];
            if ((yc < a[1]) == (yc < b[1]))
                continue;
            const double x = a[0] + (yc - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        if (hi < lo)
            continue;
        const SDL_Rect r = {int(std::lround(lo)), y, std::max(1, int(std::lround(hi - lo))), 1};
        sdl_render_fill_rect(sdl, &r);
    }
}

// an arrow head with its tip at (x, y) pointing along (ux, uy): black, then the colour inside
void draw_travel_arrow(SDL_Renderer *sdl, double x, double y, double ux, double uy, uint32_t c, int len) {
    const auto tri = [&](double l, double hw, double back) {
        const double bx = x - ux * (l + back), by = y - uy * (l + back);
        const double tx = x - ux * back, ty = y - uy * back;
        const double v[3][2] = {{tx, ty}, {bx - uy * hw, by + ux * hw}, {bx + uy * hw, by - ux * hw}};
        fill_triangle(sdl, v);
    };
    sdl_set_render_draw_color(sdl, 0, 0, 0, 255);
    tri(len + 3, len * 0.6 + 2, -1.5);
    sdl_set_render_draw_color(sdl, Uint8(fpv::px_r(c)), Uint8(fpv::px_g(c)), Uint8(fpv::px_b(c)), 255);
    tri(len, len * 0.6, 0);
}

void draw_travel_marks(SDL_Renderer *sdl, const df::renderer_2d_base *r) {
    const std::shared_ptr<const TravelModel> m = travel_live() ? get_travel() : nullptr;
    Cells c;
    if (!m || m->marks.empty() || !ui_font_ready || !gps || !get_cells(c) || m->dim_x <= 0 || m->dim_y <= 0)
        return;
    const int W = gps->screen_pixel_x, H = gps->screen_pixel_y;
    const int cs = std::max(1, std::max((W + m->dim_x - 1) / m->dim_x, (H + m->dim_y - 1) / m->dim_y));
    const double ox = r->origin_x, oy = r->origin_y;
    // where marks may stand: below the game's top row, above its bottom bar
    const int rx0 = c.ox + c.cw, rx1 = c.ox + (c.dimx - 1) * c.cw;
    const int ry0 = c.oy + 2 * c.ch, ry1 = c.oy + (c.dimy - 4) * c.ch;
    if (rx1 - rx0 < 8 * c.cw || ry1 - ry0 < 8 * c.ch)
        return;
    const double you_x = ox + (m->you_x + 0.5) * cs, you_y = oy + (m->you_y + 0.5) * cs;
    const int size = std::max(14, c.ch * 3 / 2);
    Uint8 r0 = 0, g0 = 0, b0 = 0, a0 = 0;
    sdl_get_render_draw_color(sdl, &r0, &g0, &b0, &a0);
    for (int i = 0; i < int(m->marks.size()) && i < MAX_TRAVEL_MARKS; ++i) {
        const TravelModel::Mark &k = m->marks[i];
        const double px = ox + k.cx * cs, py = oy + k.cy * cs;
        const bool inside = px >= rx0 && px < rx1 && py >= ry0 && py < ry1;
        // on the map the label goes under the square, so the game's own icon there shows
        const int gap = inside ? cs : 2;
        const std::string dist = inside ? std::string() : k.dist;
        const std::string key = k.name + "|" + dist + "|" + std::to_string(k.color) + "|" + std::to_string(gap) + "|" +
                                std::to_string(size);
        if (key != tex_travel_keys[i] || !tex_travel[i].tex) {
            tex_travel_keys[i] = key;
            make_travel_texture(sdl, tex_travel[i], k.name, dist, k.color, size, gap);
        }
        const UiTexture &t = tex_travel[i];
        if (!t.tex)
            continue;
        if (inside) {
            // the exclamation mark's foot on the square's top edge
            t.draw(sdl, int(std::lround(px)) - t.w / 2, int(std::lround(py - cs / 2.0)) - (size + 3));
            continue;
        }
        // held at the edge where the line from you to it leaves the room
        double ux = px - you_x, uy = py - you_y;
        const double len = std::sqrt(ux * ux + uy * uy);
        if (len < 1e-6)
            continue;
        ux /= len;
        uy /= len;
        const int arrow = std::max(8, c.ch * 3 / 4);
        const double m0 = arrow + 4;   // the arrow's tip stays this far inside the room
        double tmax = 1e18;
        if (ux > 1e-9)
            tmax = std::min(tmax, (rx1 - m0 - you_x) / ux);
        if (ux < -1e-9)
            tmax = std::min(tmax, (rx0 + m0 - you_x) / ux);
        if (uy > 1e-9)
            tmax = std::min(tmax, (ry1 - m0 - you_y) / uy);
        if (uy < -1e-9)
            tmax = std::min(tmax, (ry0 + m0 - you_y) / uy);
        tmax = std::max(0.0, tmax);
        const double ax = you_x + ux * tmax, ay = you_y + uy * tmax;
        draw_travel_arrow(sdl, ax, ay, ux, uy, k.color ? k.color : UI_GOLD, arrow);
        // the mark and its label beside the arrow, on the room's side
        const double reach = std::abs(ux) * t.w / 2.0 + std::abs(uy) * t.h / 2.0 + arrow + 4;
        int tx = int(std::lround(ax - ux * reach)) - t.w / 2, ty = int(std::lround(ay - uy * reach)) - t.h / 2;
        tx = std::clamp(tx, rx0, std::max(rx0, rx1 - t.w));
        ty = std::clamp(ty, ry0, std::max(ry0, ry1 - t.h));
        t.draw(sdl, tx, ty);
    }
    sdl_set_render_draw_color(sdl, r0, g0, b0, a0);
}
