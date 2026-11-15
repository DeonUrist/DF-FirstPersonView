// ---------------------------------------------------------------------------
// The world map as the game draws it, one square at a time, from the game's own
// tile pages (data/vanilla/vanilla_world_map/graphics): simulation thread only.
//
// The game fills its world-map grid (gps->main_map_port) only while one of its
// map screens is drawn and keeps no tile per square (read 2026-11-15), so the
// tiles are chosen here: the biome's tile (WORLD_MAP_TILES, in its good, evil and
// savage versions), the forest or mountain picture over it, the river or brook
// across it, and the site's icon. Which of a token's variants the game takes,
// and the blended edges between biomes, are not known: a variant is chosen by
// the square's position and the edges are left out.

struct WmRef {
    std::string page;
    int x = 0, y = 0;
};

// every TILE_GRAPHICS line of the world map's graphics file: token -> variants in order
std::unordered_map<std::string, std::vector<WmRef>> wm_defs;
bool wm_defs_read = false;

void read_wm_defs() {
    if (wm_defs_read)
        return;
    wm_defs_read = true;
    std::ifstream f("data/vanilla/vanilla_world_map/graphics/graphics_world_map.txt");
    std::string line;
    while (std::getline(f, line)) {
        const size_t a = line.find("[TILE_GRAPHICS:");
        if (a == std::string::npos)
            continue;
        const size_t b = line.find(']', a);
        std::vector<std::string> part;
        std::string cur;
        for (size_t i = a + 1; i < (b == std::string::npos ? line.size() : b); ++i) {
            if (line[i] == ':') {
                part.push_back(cur);
                cur.clear();
            } else {
                cur += line[i];
            }
        }
        part.push_back(cur);
        if (part.size() < 5)
            continue;
        WmRef r;
        r.page = part[1];
        r.x = std::atoi(part[2].c_str());
        r.y = std::atoi(part[3].c_str());
        const int variant = part.size() > 5 ? std::max(1, std::atoi(part[5].c_str())) : 1;
        std::vector<WmRef> &v = wm_defs[part[4]];
        if (int(v.size()) < variant)
            v.resize(variant);
        v[variant - 1] = r;
    }
}

// a token's picture; `pick` chooses among its variants
std::shared_ptr<const fpv::Image> wm_tile(const std::string &token, uint32_t pick = 0) {
    read_wm_defs();
    auto it = wm_defs.find(token);
    if (it == wm_defs.end() || it->second.empty())
        return nullptr;
    const WmRef &r = it->second[pick % it->second.size()];
    if (r.page.empty())
        return nullptr;
    return page_image(r.page.c_str(), r.x, r.y);
}

uint32_t square_hash(int x, int y) {
    uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

// "_GOOD", "_EVILSAV" ... as the token names have them
std::string alignment_suffix(const df::region_map_entry &e) {
    const bool savage = e.savagery >= 66;
    if (e.evilness >= 66)
        return savage ? "_EVILSAV" : "_EVIL";
    if (e.evilness < 33)
        return savage ? "_GOODSAV" : "_GOOD";
    return "";
}

// the base tile's token and the overlay's (forest or mountain), by biome
void biome_tokens(int wx, int wy, const df::region_map_entry &e, std::string &base, std::string &over) {
    using B = df::biome_type;
    over.clear();
    const B b = Maps::getBiomeType(int16_t(wx), int16_t(wy));
    switch (b) {
    case B::MOUNTAIN:
        base = "ROCKY_PLAINS";
        if (e.flags.is_set(df::region_map_entry_flags::is_peak))
            over = "MOUNTAIN_PEAK";
        else
            over = e.elevation >= 250 ? "MOUNTAIN_HIGH" : e.elevation >= 200 ? "MOUNTAIN_MID" : "MOUNTAIN_LOW";
        break;
    case B::GLACIER: base = "GLACIER"; break;
    case B::TUNDRA: base = "TUNDRA"; break;
    case B::SWAMP_TEMPERATE_FRESHWATER: case B::SWAMP_TEMPERATE_SALTWATER: case B::SWAMP_TROPICAL_FRESHWATER:
    case B::SWAMP_TROPICAL_SALTWATER: case B::SWAMP_MANGROVE:
        base = "SWAMP";
        break;
    case B::MARSH_TEMPERATE_FRESHWATER: case B::MARSH_TEMPERATE_SALTWATER: case B::MARSH_TROPICAL_FRESHWATER:
    case B::MARSH_TROPICAL_SALTWATER:
        base = "MARSH";
        break;
    case B::FOREST_TAIGA: base = "TUNDRA"; over = "FOREST_TAIGA"; break;
    case B::FOREST_TEMPERATE_CONIFER: base = "GRASSLAND_TEMP"; over = "FOREST_CONIFER_TEMP"; break;
    case B::FOREST_TEMPERATE_BROADLEAF: base = "GRASSLAND_TEMP"; over = "FOREST_BROADLEAF_TEMP"; break;
    case B::FOREST_TROPICAL_CONIFER: base = "GRASSLAND_TROP"; over = "FOREST_CONIFER_TROP"; break;
    case B::FOREST_TROPICAL_DRY_BROADLEAF: base = "GRASSLAND_TROP"; over = "FOREST_BROADLEAF_TROP_DRY"; break;
    case B::FOREST_TROPICAL_MOIST_BROADLEAF: base = "GRASSLAND_TROP"; over = "FOREST_BROADLEAF_TROP_MOIST"; break;
    case B::GRASSLAND_TEMPERATE: base = "GRASSLAND_TEMP"; break;
    case B::SAVANNA_TEMPERATE: base = "SAVANNA_TEMP"; break;
    case B::SHRUBLAND_TEMPERATE: case B::SHRUBLAND_TROPICAL: base = "SHRUBLAND"; break;
    case B::GRASSLAND_TROPICAL: base = "GRASSLAND_TROP"; break;
    case B::SAVANNA_TROPICAL: base = "SAVANNA_TROP"; break;
    case B::DESERT_BADLAND: base = "BADLANDS"; break;
    case B::DESERT_ROCK: base = "ROCKY_PLAINS"; break;
    case B::DESERT_SAND: base = "SAND_DESERT_YELLOW"; break;
    case B::OCEAN_ARCTIC: base = "FROZEN_OCEAN"; break;
    case B::OCEAN_TROPICAL: case B::OCEAN_TEMPERATE: base = e.elevation < 50 ? "OCEAN_DEEP" : "OCEAN"; break;
    default:
        if (ENUM_KEY_STR(biome_type, b).rfind("LAKE_", 0) == 0)
            base = "LAKE";
        else
            base = e.elevation >= 150 ? "ROCKY_HILLS" : "GRASSLAND_TEMP";
        break;
    }
    if (e.flags.is_set(df::region_map_entry_flags::is_lake))
        base = "LAKE";
}

// the site icon's token for a kind of site
std::string site_token(const df::world_site *s) {
    using T = df::world_site_type;
    switch (s->type) {
    case T::PlayerFortress: return "SITE_FORTRESS";
    case T::MountainHalls: return "SITE_MOUNTAINHOME";
    case T::DarkFortress: return "SITE_DARK_FORTRESS_1";
    case T::Cave: return "SITE_CAVE";
    case T::ForestRetreat: return "SITE_FOREST_RETREAT_1";
    case T::Town:
        return s->resident_count >= 600 ? "SITE_CITY_2" : s->resident_count >= 200 ? "SITE_CITY_1" : "SITE_VILLAGE";
    case T::ImportantLocation: return "SITE_SHRINE_TITAN";
    case T::LairShrine: return "SITE_LAIR_BURROW";
    case T::Fortress: return "SITE_CASTLE";
    case T::Camp: return "SITE_CAMP";
    case T::Monument: return "SITE_TOMB";
    default: return "SITE_VILLAGE";
    }
}

// a picture drawn over another of `side` pixels, scaled to fit (nearest)
void wm_over(fpv::Image &dst, const fpv::Image *src) {
    if (!src || !src->ok())
        return;
    const int s = dst.w;
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            const uint32_t c = src->px[size_t(y * src->h / s) * src->w + x * src->w / s];
            const uint32_t a = fpv::px_a(c);
            if (a >= 128)
                dst.px[size_t(y) * s + x] = c | 0xFF000000u;
        }
}

// a picture blended over another of the same side by its own alpha; with `mask`,
// only where the mask is (its alpha times the picture's)
void wm_blend(fpv::Image &dst, const fpv::Image *src, const fpv::Image *mask = nullptr) {
    if (!src || !src->ok() || (mask && !mask->ok()))
        return;
    const int s = dst.w;
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            const uint32_t c = src->px[size_t(y * src->h / s) * src->w + x * src->w / s];
            uint32_t a = fpv::px_a(c);
            if (mask)
                a = a * fpv::px_a(mask->px[size_t(y * mask->h / s) * mask->w + x * mask->w / s]) / 255;
            if (!a)
                continue;
            uint32_t &d = dst.px[size_t(y) * s + x];
            const auto mix = [&](unsigned p, unsigned q) { return (p * a + q * (255 - a)) / 255; };
            d = fpv::rgb(mix(fpv::px_r(c), fpv::px_r(d)), mix(fpv::px_g(c), fpv::px_g(d)), mix(fpv::px_b(c), fpv::px_b(d)));
        }
}

// What a square is for its coast: 0 land, 1 water, 2 deep water; -1 not seen
// (it then casts no edge). Its base tile token comes with it.
int wm_water_kind(int wx, int wy, std::string *base_out = nullptr) {
    if (!tile_discovered(wx, wy))
        return -1;
    const df::region_map_entry &e = world->world_data->region_map[wx][wy];
    std::string base, over;
    biome_tokens(wx, wy, e, base, over);
    if (base_out)
        *base_out = base + alignment_suffix(e);
    if (base == "OCEAN_DEEP")
        return 2;
    return base == "OCEAN" || base == "LAKE" || base == "FROZEN_OCEAN" ? 1 : 0;
}

// The edge shapes the game's tiles are made for (its own numbering, read from
// the game's code by a helper, 2026-11-15): which sides cast onto this square.
// Straight sides, thick corners (two sides), inlets (three), surround (four),
// and thin corners where only a diagonal neighbour casts.
std::vector<std::pair<std::string, int>> edge_shapes(const bool n, const bool s, const bool w, const bool e,
                                                     const bool nw, const bool ne, const bool sw, const bool se) {
    // the side whose neighbour supplies the picture: 0 N, 1 S, 2 W, 3 E
    std::vector<std::pair<std::string, int>> out;
    if (n) {
        if (w && e && s) out.push_back({"SURROUND", 0});
        else if (w && e) out.push_back({"INLET_NWE", 0});
        else if (w && s) out.push_back({"INLET_NSW", 0});
        else if (w) out.push_back({"THICK_CORNER_NW", 0});
        else if (e && s) out.push_back({"INLET_NSE", 0});
        else if (e) out.push_back({"THICK_CORNER_NE", 0});
        else out.push_back({"STRAIGHT_N", 0});
    }
    if (s) {
        if (w) {
            if (!n) out.push_back({e ? "INLET_SWE" : "THICK_CORNER_SW", 1});
        } else if (e) {
            if (!n) out.push_back({"THICK_CORNER_SE", 1});
        } else {
            out.push_back({"STRAIGHT_S", 1});
        }
    }
    if (w && !n && !s)
        out.push_back({"STRAIGHT_W", 2});
    if (e && !n && !s)
        out.push_back({"STRAIGHT_E", 3});
    if (nw && !n && !w) out.push_back({"THIN_CORNER_NW", 4});
    if (ne && !n && !e) out.push_back({"THIN_CORNER_NE", 5});
    if (sw && !s && !w) out.push_back({"THIN_CORNER_SW", 6});
    if (se && !s && !e) out.push_back({"THIN_CORNER_SE", 7});
    return out;
}

// The coast on a water square: the land beside it is cut into it with the
// game's edge shapes (WORLD_EDGE_SHAPE_*, a mask) in that land's own tile, and
// the beach line (WORLD_EDGE_BEACH_LINE_*) drawn along it; deep water gets the
// shallow water's edge (WORLD_EDGE_WATER_*), as the game draws it. The game
// itself draws no land edge into water in vanilla (its land edge sets are not
// defined), so this is the use the edge pictures were made for, not a copy.
void wm_coast(fpv::Image &img, int wx, int wy, int kind) {
    static const int DX[8] = {0, 0, -1, 1, -1, 1, -1, 1}, DY[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
    int nk[8];
    std::string nbase[8];
    for (int d = 0; d < 8; ++d) {
        nk[d] = wm_water_kind(wx + DX[d], wy + DY[d], &nbase[d]);
        if (nk[d] < 0)
            nk[d] = kind;   // not seen: as this square, no edge
    }
    const uint32_t h = square_hash(wx, wy) >> 4;
    const auto variant = [&](int i) { return (h >> (3 * i)) % 5; };
    // land into any water
    {
        bool c[8];
        for (int d = 0; d < 8; ++d)
            c[d] = nk[d] == 0;
        int i = 0;
        for (const auto &[shape, side] : edge_shapes(c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7])) {
            const uint32_t v = variant(i++);
            std::shared_ptr<const fpv::Image> mask = wm_tile("WORLD_EDGE_SHAPE_" + shape, v);
            std::shared_ptr<const fpv::Image> land = wm_tile(nbase[side], square_hash(wx + DX[side], wy + DY[side]));
            if (!land)
                land = wm_tile("GRASSLAND_TEMP", 0);
            if (mask && land)
                wm_blend(img, land.get(), mask.get());
            wm_blend(img, wm_tile("WORLD_EDGE_BEACH_LINE_" + shape, v).get());
        }
    }
    // shallow water into deep
    if (kind == 2) {
        bool c[8];
        for (int d = 0; d < 8; ++d)
            c[d] = nk[d] == 1;
        int i = 0;
        for (const auto &[shape, side] : edge_shapes(c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7])) {
            (void)side;
            wm_blend(img, wm_tile("WORLD_EDGE_WATER_" + shape, variant(i++)).get());
        }
    }
}

// The square's picture, 16 x 16; known sites get their icon. Cached by square;
// the cache is dropped with a new world.
std::unordered_map<int32_t, std::shared_ptr<const fpv::Image>> wm_cache;
std::unordered_map<int32_t, const df::world_site *> wm_site_at;
std::string wm_cache_world;
uint32_t wm_cache_epoch = 0;

void wm_reset_if_new_world() {
    const std::string w = world ? world->cur_savegame.save_dir : std::string();
    const uint32_t e = page_epoch();
    if (w == wm_cache_world && e == wm_cache_epoch)
        return;
    wm_cache_world = w;
    wm_cache_epoch = e;
    wm_cache.clear();
    wm_site_at.clear();
    if (world && world->world_data)
        for (const df::world_site *s : world->world_data->sites)
            if (s)
                wm_site_at[s->pos.y * 65536 + s->pos.x] = s;
}

const uint32_t WM_UNKNOWN = fpv::rgb(14, 14, 18);

std::shared_ptr<const fpv::Image> wm_square(int wx, int wy, bool show_site) {
    if (!in_world(wx, wy))
        return nullptr;
    wm_reset_if_new_world();
    const int32_t key = (wy * 65536 + wx) * 2 + (show_site ? 1 : 0);
    auto it = wm_cache.find(key);
    if (it != wm_cache.end())
        return it->second;
    if (wm_cache.size() > 40000)
        wm_cache.clear();
    auto img = std::make_shared<fpv::Image>();
    img->w = img->h = 16;
    img->px.assign(256, WM_UNKNOWN | 0xFF000000u);
    if (tile_discovered(wx, wy)) {
        const df::region_map_entry &e = world->world_data->region_map[wx][wy];
        const uint32_t h = square_hash(wx, wy);
        const std::string al = alignment_suffix(e);
        std::string base, over;
        biome_tokens(wx, wy, e, base, over);
        std::shared_ptr<const fpv::Image> b = wm_tile(base + al, h);
        if (!b)
            b = wm_tile(base, h);
        wm_over(*img, b.get());
        const int kind = wm_water_kind(wx, wy);
        if (kind > 0)
            wm_coast(*img, wx, wy, kind);
        if (!over.empty()) {
            std::shared_ptr<const fpv::Image> o = wm_tile(over + al, h >> 8);
            if (!o)
                o = wm_tile(over, h >> 8);
            wm_over(*img, o.get());
        }
        if (e.flags.is_set(df::region_map_entry_flags::has_river)) {
            std::string dirs;
            if (e.flags.is_set(df::region_map_entry_flags::river_up)) dirs += "N";
            if (e.flags.is_set(df::region_map_entry_flags::river_down)) dirs += "S";
            if (e.flags.is_set(df::region_map_entry_flags::river_left)) dirs += "W";
            if (e.flags.is_set(df::region_map_entry_flags::river_right)) dirs += "E";
            const bool brook = e.flags.is_set(df::region_map_entry_flags::is_brook);
            std::shared_ptr<const fpv::Image> r = wm_tile((brook ? "BROOK_" : "RIVER_") + (dirs.empty() ? std::string("0") : dirs));
            wm_over(*img, r.get());
        }
        if (show_site) {
            auto s = wm_site_at.find(wy * 65536 + wx);
            if (s != wm_site_at.end())
                wm_over(*img, wm_tile(site_token(s->second)).get());
        }
    }
    wm_cache[key] = img;
    return img;
}
