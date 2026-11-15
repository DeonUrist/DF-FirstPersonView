// ---------------------------------------------------------------------------
// The journal panel: the mod's own journal in place of the game's (setting
// journal_panel). It takes the same room as the inventory panel.
//
// The game's journal names a place in the world's language in one list and in
// English in another, says next to nothing about a site, and a quest does not
// say where to go. This one reads the world directly: every entry carries both
// its names (search matches either), how far and which way it lies, and what is
// known about it, with every name in its text a link to that name's own entry.
// An entry can be tracked: it is then marked on the compass.
//
// What the adventurer knows (read from the game, 2026-11-15): the game's Quests
// list held the agreements with the adventurer's figure as a party (an "Offer
// Service" one with a lord) while `known_info.known_agreement_id` was empty, so
// quests are found by party. `heard_of_stid` was empty too; a site counts as
// known when its world tile is `discovered` on the world map or it was heard of.
// Places are the named locations (abstract buildings) of known sites; rumours
// are `known_info.rumor_info.events`; the bestiary is `creature_knowledge`;
// artifacts are those named by a rumour or quest or held by a known figure.
// That this matches what the game's own lists hold is a guess.
//
// Threads: the model is built on the game thread (opening, every click or key)
// and drawn by the render thread, as the inventory panel's is.

enum { JT_QUESTS, JT_RUMORS, JT_SITES, JT_PLACES, JT_PEOPLE, JT_CIVS, JT_REGIONS, JT_ARTIFACTS, JT_BEASTS, JT_COUNT };
const char *const JT_NAMES[JT_COUNT] = {"Quests", "Rumours", "Sites", "Places", "People",
                                        "Civilizations", "Regions", "Artifacts", "Bestiary"};
enum { JS_NEAR, JS_ENGLISH, JS_NATIVE, JS_COUNT };
const char *const JS_NAMES[JS_COUNT] = {"nearest", "name", "native name"};

int jk_tab(int kind) {
    switch (kind) {
    case JK_QUEST: return JT_QUESTS;
    case JK_RUMOR: return JT_RUMORS;
    case JK_SITE: return JT_SITES;
    case JK_PLACE: return JT_PLACES;
    case JK_PERSON: return JT_PEOPLE;
    case JK_CIV: return JT_CIVS;
    case JK_REGION: return JT_REGIONS;
    case JK_ARTIFACT: return JT_ARTIFACTS;
    case JK_BEAST: return JT_BEASTS;
    default: return -1;
    }
}

struct JRef {
    int kind = JK_NONE;
    int32_t id = -1, sub = -1;  // a place: id is its site, sub the location
    bool ok() const { return kind != JK_NONE; }
    bool operator==(const JRef &o) const { return kind == o.kind && id == o.id && sub == o.sub; }
    bool operator!=(const JRef &o) const { return !(*this == o); }
};

JRef jr_ref(int kind, int32_t id, int32_t sub = -1) {
    JRef r;
    r.kind = kind;
    r.id = id;
    r.sub = sub;
    return r;
}

const uint32_t J_LINK = fpv::rgb(110, 185, 255), J_HEAD = fpv::rgb(230, 175, 70);
const uint32_t J_TRACK = UI_YELLOW, J_UNKNOWN = fpv::rgb(110, 112, 120);

struct JSpan {
    std::string text;
    uint32_t color = UI_WHITE;
    JRef link;
    std::string tip;  // a link: its other name and what it is
};
struct JLine {
    std::vector<JSpan> spans;
    bool head = false;  // a heading: a gap above, in gold
    int indent = 0;
};
struct JRow {
    JRef ref;
    std::string english, native, kind, where;
    double dist = -1;        // tiles from the adventurer; -1 not known
    bool placed = false;     // where it is is known (it can be shown on the compass)
    std::string hay;         // what the search looks in, lower case
};
// a small piece of the world map around the entry and the adventurer
struct JMap {
    int n = 0;                       // world map squares across and down
    std::vector<std::shared_ptr<const fpv::Image>> tiles;  // n * n, the game's world map pictures (16 x 16)
    int tx = -1, ty = -1, ax = -1, ay = -1;  // the entry and the adventurer, in squares of the piece
    std::vector<std::pair<int, int>> sites;  // known sites in it
};
struct JDetail {
    JRef ref;
    std::string english, native, kind, where;
    bool placed = false;
    JMap map;
    std::vector<JLine> lines;
};
struct JData {
    std::vector<JRow> rows[JT_COUNT];
};
// the Map view: a window of world map squares and what is marked on it
struct JMapMark {
    double x = 0, y = 0;  // world map squares (fractions within one)
    JRef ref;
    std::string name;
    uint32_t color = 0;
};
// a tracked region's squares are tinted in its colour; a known site can be pointed at
struct JMapArea {
    JRef ref;
    std::string name;
    uint32_t color = 0;
};
struct JMapSite {
    int x = 0, y = 0;  // its world map square
    int32_t id = -1;
    std::string name;
};
struct JMapView {
    int x0 = 0, y0 = 0, cols = 0, rows = 0;  // the squares shown, from the top left one
    int zoom = 16;                           // pixels a square
    int off_x = 0, off_y = 0;                // where square x0, y0 begins in the map's rectangle
    std::vector<std::shared_ptr<const fpv::Image>> tiles;  // cols * rows
    std::vector<JMapMark> marks;
    std::vector<JMapArea> areas;
    std::vector<int16_t> area_of;   // cols * rows: the area a square is in, -1 none
    std::vector<JMapSite> sites;
    double you_x = -1, you_y = -1;
    std::shared_ptr<const fpv::Image> you_icon;
};
struct JModel {
    std::shared_ptr<const JData> data;
    int tab = JT_QUESTS, sort = JS_NEAR;
    std::string filter;
    std::vector<int> shown;  // rows of the tab that pass the search, in order
    int sel_row = -1;        // the selected entry's place in `shown`
    JDetail detail;
    std::vector<JRef> tracked;
    std::vector<uint32_t> tracked_color;  // each tracked entry's marker colour
    bool back = false, fwd = false;
    uint64_t version = 0;
    int view = 0;   // 0 the journal, 1 the map
    JMapView mapv;
    bool is_tracked(const JRef &r) const { return std::find(tracked.begin(), tracked.end(), r) != tracked.end(); }
    uint32_t color_of(const JRef &r) const {
        for (size_t i = 0; i < tracked.size() && i < tracked_color.size(); ++i)
            if (tracked[i] == r)
                return tracked_color[i];
        return 0;
    }
};

std::mutex jr_mutex;
std::shared_ptr<const JModel> jr_model;
std::atomic<bool> jr_up{false};          // the panel is shown
std::atomic<int> jr_list_scroll{0}, jr_det_scroll{0};  // pixels

std::shared_ptr<const JModel> get_jr() {
    std::lock_guard<std::mutex> lock(jr_mutex);
    return jr_model;
}

// game thread only
struct JState {
    std::shared_ptr<const JData> data;
    int tab = JT_QUESTS, sort = JS_NEAR;
    std::string filter;
    JRef sel;
    std::vector<JRef> history;
    int hpos = -1;
    uint64_t version = 0;
    bool want = false;        // open
    bool game_wanted = false; // the game's own journal was asked for from the panel
    std::vector<JRef> tracked;
    std::vector<uint32_t> tracked_color;  // a colour per tracked entry, kept with it
    std::string tracked_save; // the save the tracked list belongs to
    bool tracked_loaded = false;
    int view = 0;             // 0 the journal, 1 the map
    double map_cx = -1, map_cy = -1;  // the map's middle, world map squares
    int map_zoom = 16;
    bool map_press = false, map_moved = false;
    int press_x = 0, press_y = 0;
    double press_cx = 0, press_cy = 0;
    JRef press_mark;
    int bar_press = 0;        // a scrollbar held: 1 the list's, 2 the page's
    int bar_grab = 0;         // where on its handle it was taken (pixels from the handle's top)
} jr;

// ---------------------------------------------------------------------------
// reading the world

std::string jname(const df::language_name &n, bool english) {
    if (!n.has_name)
        return "";
    return Translation::translateName(&n, english);
}

// "MountainHalls" / "INN_TAVERN" -> "mountain halls" / "inn tavern"
std::string pretty_key(const char *key) {
    std::string s;
    if (!key)
        return s;
    for (const char *p = key; *p; ++p) {
        const char c = *p;
        if (c == '_') {
            s += ' ';
        } else if (c >= 'A' && c <= 'Z') {
            const bool camel = p != key && p[-1] >= 'a' && p[-1] <= 'z';
            if (camel)
                s += ' ';
            s += char(c - 'A' + 'a');
        } else {
            s += c;
        }
    }
    return s;
}

std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}

std::string capital(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
        s[0] = char(s[0] - 'a' + 'A');
    return s;
}

const char *dir8(double dx, double dy) {
    static const char *const D[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const double a = std::atan2(dx, -dy);
    const int k = int(std::lround(a / (3.14159265358979 / 4))) & 7;
    return D[k];
}

// While travelling the adventurer's unit is gone (the game puts the local map
// away; World::getAdventurer is null, read 2026-11-15). Its place is then its
// army's on the travel map, in steps of 16 tiles (army.pos, abs_smm), or where
// the travel map was opened until the first step there (travel_not_moved).
bool travel_smm(int &x, int &y) {
    if (!adventure || adventure->menu != df::ui_advmode_menu::Travel)
        return false;
    const df::army *a = adventure->travel_not_moved ? nullptr : df::army::find(adventure->player_army_id);
    x = a ? a->pos.x : adventure->travel_origin_x;
    y = a ? a->pos.y : adventure->travel_origin_y;
    return x >= 0 && y >= 0;
}

// Where the adventurer is, in tiles of the whole world.
bool adv_tile(double &gx, double &gy) {
    df::unit *adv = World::getAdventurer();
    if (!adv) {
        int sx, sy;
        if (!travel_smm(sx, sy))
            return false;
        gx = sx * 16.0 + 8;
        gy = sy * 16.0 + 8;
        return true;
    }
    if (!world)
        return false;
    gx = world->map.region_x * 48.0 + adv->pos.x + 0.5;
    gy = world->map.region_y * 48.0 + adv->pos.y + 0.5;
    return true;
}

// "here", or how far and which way, as the compass's labels say it
std::string where_text(double tx, double ty, double radius, double *dist_out = nullptr) {
    double gx, gy;
    if (!adv_tile(gx, gy))
        return "";
    const double dx = tx - gx, dy = ty - gy, d = std::sqrt(dx * dx + dy * dy);
    if (dist_out)
        *dist_out = std::max(0.0, d - radius);
    if (d <= radius)
        return "here";
    return distance_text(float(d - radius)) + " " + dir8(dx, dy);
}

bool site_centre(const df::world_site *s, double &x, double &y, double &r) {
    if (!s)
        return false;
    if (s->global_max_x >= s->global_min_x && s->global_min_x >= 0) {
        x = (s->global_min_x + s->global_max_x + 1) * 24.0;
        y = (s->global_min_y + s->global_max_y + 1) * 24.0;
        r = (s->global_max_x - s->global_min_x + 1) * 24.0;
    } else {
        x = s->pos.x * 768.0 + 384;
        y = s->pos.y * 768.0 + 384;
        r = 384;
    }
    return true;
}

bool in_world(int wx, int wy) {
    if (!world || !world->world_data || !world->world_data->region_map)
        return false;
    const auto *wd = world->world_data;
    return wx >= 0 && wy >= 0 && wx < wd->world_width && wy < wd->world_height;
}

bool tile_discovered(int wx, int wy) {
    return in_world(wx, wy) &&
           world->world_data->region_map[wx][wy].flags.is_set(df::region_map_entry_flags::discovered);
}

df::world_region *region_at(int wx, int wy) {
    if (!in_world(wx, wy))
        return nullptr;
    const auto *wd = world->world_data;
    const int32_t id = wd->region_map[wx][wy].region_id;
    if (id < 0 || size_t(id) >= wd->regions.size())
        return nullptr;
    return wd->regions[id];
}

df::world_region *region_by_id(int32_t id) {
    if (!world || !world->world_data || id < 0 || size_t(id) >= world->world_data->regions.size())
        return nullptr;
    return world->world_data->regions[id];
}

// the discovered square of a region nearest the adventurer (any square, if `any`)
bool region_near(const df::world_region *g, double &x, double &y, bool any = false) {
    if (!g)
        return false;
    double gx = 0, gy = 0, best = -1;
    adv_tile(gx, gy);
    for (size_t k = 0; k < g->region_coords.x.size() && k < g->region_coords.y.size(); ++k) {
        const int wx = g->region_coords.x[k], wy = g->region_coords.y[k];
        if (!any && !tile_discovered(wx, wy))
            continue;
        const double cx = wx * 768.0 + 384, cy = wy * 768.0 + 384;
        const double dd = (cx - gx) * (cx - gx) + (cy - gy) * (cy - gy);
        if (best < 0 || dd < best) {
            best = dd;
            x = cx;
            y = cy;
        }
    }
    return best >= 0;
}

#include "wmap.inc"

df::abstract_building *place_of(df::world_site *s, int32_t id) {
    if (!s)
        return nullptr;
    for (df::abstract_building *b : s->buildings)
        if (b && b->id == id)
            return b;
    return nullptr;
}

df::creature_raw *creature_of(int race) {
    if (!world || race < 0 || size_t(race) >= world->raws.creatures.all.size())
        return nullptr;
    return world->raws.creatures.all[race];
}

std::string creature_name(int race) {
    df::creature_raw *c = creature_of(race);
    return c ? c->name[0] : "";
}

// the adventurer's figure; while travelling (no unit) through its nemesis record
df::historical_figure *adv_figure() {
    if (df::unit *adv = World::getAdventurer())
        return df::historical_figure::find(adv->hist_figure_id);
    df::nemesis_record *n = adventure ? df::nemesis_record::find(adventure->player_id) : nullptr;
    return n ? n->figure : nullptr;
}

df::knowledge_profilest *my_knowledge() {
    df::historical_figure *me = adv_figure();
    return me && me->info ? me->info->known_info : nullptr;
}

df::entity_event *rumor_at(int32_t i) {
    df::knowledge_profilest *k = my_knowledge();
    if (!k || i < 0 || size_t(i) >= k->rumor_info.events.size())
        return nullptr;
    return k->rumor_info.events[i];
}

// What a rumour is about: a title and its parts, each a role and an entry.
struct RumorParts {
    std::string title;
    std::vector<std::pair<std::string, JRef>> parts;
    void add(const char *role, int kind, int32_t id, int32_t sub = -1) {
        if (id >= 0)
            parts.push_back({role, jr_ref(kind, id, sub)});
    }
};

RumorParts rumor_parts(const df::entity_event *ev) {
    RumorParts p;
    using T = df::entity_event_type;
    const auto &d = ev->data;
    p.title = capital(pretty_key(ENUM_KEY_STR(entity_event_type, ev->type).c_str()));
    switch (ev->type) {
    case T::invasion:
        p.title = "Invasion";
        p.add("of", JK_SITE, d.invasion.site_id);
        p.add("by", JK_CIV, d.invasion.entity_id);
        p.add("led by", JK_PERSON, d.invasion.attack_leader_hf);
        p.add("commanded by", JK_PERSON, d.invasion.attack_commander_hf);
        break;
    case T::abduction:
        p.title = "Abduction";
        p.add("of", JK_PERSON, d.abduction.histfig_id);
        p.add("by", JK_PERSON, d.abduction.abductor_id);
        p.add("at", JK_SITE, d.abduction.site_id);
        break;
    case T::occupation:
        p.title = "Occupation";
        p.add("of", JK_SITE, d.occupation.site_id);
        p.add("by", JK_CIV, d.occupation.entity_id);
        p.add("led by", JK_PERSON, d.occupation.leader_hf);
        p.add("driving out", JK_CIV, d.occupation.evicted_entity_id);
        break;
    case T::beast:
        p.title = "Beast";
        p.add("", JK_PERSON, d.beast.histfig_id);
        p.add("at", JK_SITE, d.beast.site_id);
        p.add("in", JK_REGION, d.beast.region_id);
        break;
    case T::group:
        p.title = "Group present";
        p.add("", JK_CIV, d.group.entity_id);
        p.add("at", JK_SITE, d.group.site_id);
        break;
    case T::harass:
        p.title = "Harassment";
        p.add("of", JK_SITE, d.harass.site_id);
        p.add("by", JK_CIV, d.harass.entity_id);
        p.add("led by", JK_PERSON, d.harass.army_leader_hf_id);
        break;
    case T::flee:
        p.title = "Refugees";
        p.add("", JK_CIV, d.flee.refugee_entity_id);
        p.add("from", JK_SITE, d.flee.from_site_id);
        p.add("fleeing", JK_CIV, d.flee.army_entity_id);
        p.add("led by", JK_PERSON, d.flee.army_leader_hf_id);
        break;
    case T::abandon:
        p.title = "Site abandoned";
        p.add("", JK_SITE, d.abandon.site_id);
        p.add("by", JK_CIV, d.abandon.entity_id);
        p.add("of", JK_CIV, d.abandon.parent_entity_id);
        break;
    case T::reclaimed:
        p.title = "Site reclaimed";
        p.add("", JK_SITE, d.reclaimed.site_id);
        p.add("by", JK_CIV, d.reclaimed.reclaimer_entity_id);
        p.add("for", JK_CIV, d.reclaimed.behalf_entity_id);
        p.add("led by", JK_PERSON, d.reclaimed.leader_hf);
        break;
    case T::founded:
        p.title = "Site founded";
        p.add("", JK_SITE, d.founded.site_id);
        p.add("by", JK_CIV, d.founded.entity_id);
        p.add("of", JK_CIV, d.founded.parent_entity_id);
        p.add("led by", JK_PERSON, d.founded.leader_hf);
        break;
    case T::reclaiming:
        p.title = "Army leaving to reclaim";
        p.add("", JK_SITE, d.reclaiming.site_id);
        p.add("sent by", JK_CIV, d.reclaiming.entity_id);
        p.add("led by", JK_PERSON, d.reclaiming.civ_leader_hf);
        p.add("commanded by", JK_PERSON, d.reclaiming.civ_commander_hf);
        break;
    case T::founding:
        p.title = "Settlers leaving";
        p.add("for", JK_REGION, d.founding.region_id);
        p.add("sent by", JK_CIV, d.founding.entity_id);
        p.add("led by", JK_PERSON, d.founding.civ_leader_hf);
        p.add("commanded by", JK_PERSON, d.founding.civ_commander_hf);
        break;
    case T::leave:
        p.title = "Occupiers leaving";
        p.add("", JK_SITE, d.leave.site_id);
        p.add("", JK_CIV, d.leave.entity_id);
        break;
    case T::insurrection:
        p.title = "Insurrection";
        p.add("at", JK_SITE, d.insurrection.site_id);
        p.add("against", JK_CIV, d.insurrection.entity_id);
        break;
    case T::insurrection_end:
        p.title = "Insurrection ended";
        p.add("at", JK_SITE, d.insurrection_end.site_id);
        p.add("against", JK_CIV, d.insurrection_end.entity_id);
        break;
    case T::succession:
        p.title = "New position holder";
        p.add("", JK_PERSON, d.succession.histfig_id);
        p.add("after", JK_PERSON, d.succession.former_histfig_id);
        p.add("of", JK_CIV, d.succession.entity_id);
        break;
    case T::claim:
        p.title = "Site claimed";
        p.add("", JK_SITE, d.claim.site_id);
        p.add("by", JK_CIV, d.claim.entity_id);
        p.add("for", JK_PERSON, d.claim.histfig_id);
        break;
    case T::accept_tribute_offer:
    case T::refuse_tribute_offer:
    case T::accept_tribute_demand:
    case T::refuse_tribute_demand:
    case T::accept_peace_offer:
    case T::refuse_peace_offer:
    case T::cease_tribute_offer:
        // the same layout for all of these (one union)
        p.add("", JK_CIV, d.accept_tribute_offer.entity1_id);
        p.add("", JK_PERSON, d.accept_tribute_offer.histfig1_id);
        p.add("and", JK_CIV, d.accept_tribute_offer.entity2_id);
        p.add("", JK_PERSON, d.accept_tribute_offer.histfig2_id);
        break;
    case T::artifact_in_site:
    case T::artifact_not_in_site:
        p.title = ev->type == T::artifact_in_site ? "Artifact at a site" : "Artifact no longer at a site";
        p.add("", JK_ARTIFACT, d.artifact_in_site.artifact_id);
        p.add("at", JK_SITE, d.artifact_in_site.site_id);
        if (d.artifact_in_site.structure_id >= 0)
            p.add("in", JK_PLACE, d.artifact_in_site.site_id, d.artifact_in_site.structure_id);
        break;
    case T::artifact_in_subregion:
    case T::artifact_not_in_subregion:
        p.title = ev->type == T::artifact_in_subregion ? "Artifact in a region" : "Artifact no longer in a region";
        p.add("", JK_ARTIFACT, d.artifact_in_subregion.artifact_id);
        p.add("in", JK_REGION, d.artifact_in_subregion.subregion_id);
        break;
    case T::artifact_in_feature_layer:
    case T::artifact_not_in_feature_layer:
        p.title = "Artifact underground";
        p.add("", JK_ARTIFACT, d.artifact_in_feature_layer.artifact_id);
        break;
    case T::artifact_in_inventory:
    case T::artifact_not_in_inventory:
        p.title = ev->type == T::artifact_in_inventory ? "Artifact carried" : "Artifact no longer carried";
        p.add("", JK_ARTIFACT, d.artifact_in_inventory.artifact_id);
        p.add("by", JK_PERSON, d.artifact_in_inventory.hist_figure_id);
        break;
    case T::artifact_was_destroyed:
        p.title = "Artifact destroyed";
        p.add("", JK_ARTIFACT, d.artifact_destroyed.artifact_id);
        break;
    default:
        break;
    }
    return p;
}

// a creature kind the adventurer knows of: everything known, merged over its castes
struct BeastKnown {
    int race = -1;
    bool heard = false, local = false, recent = false;
    std::vector<int32_t> sites, regions;
};

std::vector<BeastKnown> known_beasts() {
    std::vector<BeastKnown> out;
    df::knowledge_profilest *k = my_knowledge();
    if (!k || !world)
        return out;
    const auto &lc = world->raws.creatures.list_creature;
    for (const df::creature_knowledgest *c : k->creature_knowledge) {
        if (!c || c->combined_caste_id < 0 || size_t(c->combined_caste_id) >= lc.size())
            continue;
        const int race = lc[c->combined_caste_id];
        auto it = std::find_if(out.begin(), out.end(), [&](const BeastKnown &b) { return b.race == race; });
        if (it == out.end()) {
            out.emplace_back();
            it = out.end() - 1;
            it->race = race;
        }
        it->heard = it->heard || c->flags.bits.heard_of;
        it->local = it->local || c->flags.bits.old_local;
        it->recent = it->recent || c->flags.bits.recent_encounter;
        for (int32_t s : c->site)
            if (std::find(it->sites.begin(), it->sites.end(), s) == it->sites.end())
                it->sites.push_back(s);
        for (int32_t r : c->region)
            if (std::find(it->regions.begin(), it->regions.end(), r) == it->regions.end())
                it->regions.push_back(r);
    }
    return out;
}

// both names of anything that can be linked to, and what it is
struct JNames {
    std::string english, native, kind;
};

std::string quest_title(const df::agreement *a);

JNames names_of(const JRef &r) {
    JNames n;
    switch (r.kind) {
    case JK_QUEST:
        if (df::agreement *a = df::agreement::find(r.id)) {
            n.english = quest_title(a);
            n.kind = "quest";
        }
        break;
    case JK_RUMOR:
        if (df::entity_event *ev = rumor_at(r.id)) {
            n.english = rumor_parts(ev).title;
            n.kind = "rumour, year " + std::to_string(ev->year);
        }
        break;
    case JK_SITE:
        if (df::world_site *s = df::world_site::find(r.id)) {
            n.english = jname(s->name, true);
            n.native = jname(s->name, false);
            n.kind = site_kind(s->type);
        }
        break;
    case JK_PLACE:
        if (df::abstract_building *b = place_of(df::world_site::find(r.id), r.sub)) {
            if (df::language_name *nm = b->getName()) {
                n.english = jname(*nm, true);
                n.native = jname(*nm, false);
            }
            n.kind = pretty_key(ENUM_KEY_STR(abstract_building_type, b->getType()).c_str());
        }
        break;
    case JK_PERSON:
        if (df::historical_figure *hf = df::historical_figure::find(r.id)) {
            n.english = jname(hf->name, true);
            n.native = jname(hf->name, false);
            n.kind = creature_name(hf->race);
            if (n.english.empty())
                n.english = "an unnamed " + n.kind;
        }
        break;
    case JK_CIV:
        if (df::historical_entity *e = df::historical_entity::find(r.id)) {
            n.english = jname(e->name, true);
            n.native = jname(e->name, false);
            n.kind = pretty_key(ENUM_KEY_STR(historical_entity_type, e->type).c_str());
            const std::string race = creature_name(e->race);
            if (!race.empty())
                n.kind = race + " " + n.kind;
        }
        break;
    case JK_REGION:
        if (df::world_region *g = region_by_id(r.id)) {
            n.english = jname(g->name, true);
            n.native = jname(g->name, false);
            n.kind = pretty_key(ENUM_KEY_STR(world_region_type, g->type).c_str());
        }
        break;
    case JK_ARTIFACT:
        if (df::artifact_record *a = df::artifact_record::find(r.id)) {
            n.english = jname(a->name, true);
            n.native = jname(a->name, false);
            n.kind = "artifact";
        }
        break;
    case JK_BEAST:
        if (df::creature_raw *c = creature_of(r.id)) {
            n.english = c->name[0];
            n.native = c->name[1] == c->name[0] ? "" : c->name[1];
            n.kind = "creature";
        }
        break;
    default:
        break;
    }
    if (n.english.empty())
        n.english = n.native;
    return n;
}

JSpan jtext(const std::string &s, uint32_t color = UI_WHITE) {
    JSpan sp;
    sp.text = s;
    sp.color = color;
    return sp;
}

JSpan jlink(const JRef &r) {
    const JNames n = names_of(r);
    JSpan sp;
    sp.text = n.english.empty() ? "(unnamed)" : n.english;
    sp.color = J_LINK;
    sp.link = r;
    sp.tip = (n.native.empty() || n.native == n.english ? "" : n.native + " - ") + n.kind;
    return sp;
}

struct JText {
    std::vector<JLine> &lines;
    void head(const std::string &s) {
        JLine l;
        l.head = true;
        l.spans.push_back(jtext(s, J_HEAD));
        lines.push_back(std::move(l));
    }
    JLine &line(int indent = 0) {
        lines.emplace_back();
        lines.back().indent = indent;
        return lines.back();
    }
    void say(const std::string &s, int indent = 0) { line(indent).spans.push_back(jtext(s)); }
};

// Where a figure is, in world tiles: 1 in the loaded area, 2 at a site (`site`
// set), 3 last seen in the wild, 0 not known (or dead).
int person_pos(df::historical_figure *hf, double &x, double &y, double &r, int32_t *site = nullptr) {
    if (!hf || hf->died_year >= 0)
        return 0;
    if (hf->unit_id >= 0) {
        df::unit *u = df::unit::find(hf->unit_id);
        if (u && Units::isActive(u) && world) {
            x = world->map.region_x * 48.0 + u->pos.x + 0.5;
            y = world->map.region_y * 48.0 + u->pos.y + 0.5;
            r = 0;
            return 1;
        }
    }
    const df::state_profilest *w = hf->info ? hf->info->whereabouts : nullptr;
    if (w && w->site_id >= 0)
        if (df::world_site *s = df::world_site::find(w->site_id)) {
            site_centre(s, x, y, r);
            if (site)
                *site = s->id;
            return 2;
        }
    if (w && w->abs_smm_x >= 0) {
        x = w->abs_smm_x * 16.0 + 8;
        y = w->abs_smm_y * 16.0 + 8;
        r = 0;
        return 3;
    }
    return 0;
}

// the same, as words with links
std::vector<JSpan> person_where(df::historical_figure *hf) {
    std::vector<JSpan> out;
    if (!hf)
        return out;
    if (hf->died_year >= 0) {
        out.push_back(jtext("dead (year " + std::to_string(hf->died_year) + ")", UI_DIM));
        return out;
    }
    double x, y, r;
    int32_t site = -1;
    switch (person_pos(hf, x, y, r, &site)) {
    case 1: out.push_back(jtext("nearby, " + where_text(x, y, r))); return out;
    case 2:
        out.push_back(jtext("at "));
        out.push_back(jlink(jr_ref(JK_SITE, site)));
        out.push_back(jtext(", " + where_text(x, y, r)));
        return out;
    case 3: out.push_back(jtext("last known in the wild, " + where_text(x, y, r))); return out;
    default: break;
    }
    const df::state_profilest *w = hf->info ? hf->info->whereabouts : nullptr;
    if (w && w->subregion_id >= 0) {
        out.push_back(jtext("somewhere in "));
        out.push_back(jlink(jr_ref(JK_REGION, w->subregion_id)));
        return out;
    }
    out.push_back(jtext("whereabouts not known", UI_DIM));
    return out;
}

bool party_has(const df::agreement_party *p, int32_t hf) {
    return p && std::find(p->histfig_ids.begin(), p->histfig_ids.end(), hf) != p->histfig_ids.end();
}

const df::agreement_party *party(const df::agreement *a, int32_t id) {
    for (const df::agreement_party *p : a->parties)
        if (p && p->id == id)
            return p;
    return nullptr;
}

std::vector<df::agreement *> my_agreements() {
    std::vector<df::agreement *> out;
    df::historical_figure *me = adv_figure();
    if (!me || !world)
        return out;
    std::unordered_set<int32_t> known;
    if (df::knowledge_profilest *k = my_knowledge())
        for (int32_t id : k->known_agreement_id)
            known.insert(id);
    for (df::agreement *a : world->agreements.all) {
        if (!a)
            continue;
        bool mine = known.count(a->id) > 0;
        for (const df::agreement_party *p : a->parties)
            mine = mine || party_has(p, me->id);
        if (mine)
            out.push_back(a);
    }
    return out;
}

// what a quest asks, in a few words
std::string quest_title(const df::agreement *a) {
    if (a->details.empty() || !a->details[0])
        return "Agreement";
    const df::agreement_details *d = a->details[0];
    using T = df::agreement_details_type;
    switch (d->type) {
    case T::JoinParty: return "Join a party";
    case T::Residency: return "Residency";
    case T::Citizenship: return "Citizenship";
    case T::Parley: return "Arrange a parley";
    case T::Location:
        return d->data.Location ? "Build a " + pretty_key(ENUM_KEY_STR(abstract_building_type, d->data.Location->type).c_str())
                                : "Build a location";
    case T::OfferService: return "Offer of service";
    case T::RetrieveArtifact: {
        if (d->data.RetrieveArtifact)
            if (df::artifact_record *ar = df::artifact_record::find(d->data.RetrieveArtifact->artifact))
                return "Retrieve " + jname(ar->name, true);
        return "Retrieve an artifact";
    }
    case T::PlotAssassination: return "Assassination";
    case T::PlotAbduct: return "Abduction";
    case T::PlotStealArtifact: return "Steal an artifact";
    default: return capital(pretty_key(ENUM_KEY_STR(agreement_details_type, d->type).c_str()));
    }
}

// The figures, groups, sites and artifacts a quest is about.
struct QuestGoals {
    std::vector<int32_t> people, civs, sites, artifacts;
};

void add_unique(std::vector<int32_t> &v, int32_t id) {
    if (id >= 0 && std::find(v.begin(), v.end(), id) == v.end())
        v.push_back(id);
}

QuestGoals quest_goals(const df::agreement *a) {
    QuestGoals g;
    df::historical_figure *me = adv_figure();
    for (const df::agreement_party *p : a->parties) {
        if (!p)
            continue;
        for (int32_t h : p->histfig_ids)
            if (!me || h != me->id)
                add_unique(g.people, h);
        for (int32_t e : p->entity_ids)
            add_unique(g.civs, e);
    }
    using T = df::agreement_details_type;
    for (const df::agreement_details *d : a->details) {
        if (!d)
            continue;
        switch (d->type) {
        case T::JoinParty: if (d->data.JoinParty) add_unique(g.sites, d->data.JoinParty->site); break;
        case T::Residency: if (d->data.Residency) add_unique(g.sites, d->data.Residency->site); break;
        case T::Citizenship: if (d->data.Citizenship) add_unique(g.sites, d->data.Citizenship->site); break;
        case T::Parley: if (d->data.Parley) add_unique(g.sites, d->data.Parley->site); break;
        case T::Location: if (d->data.Location) add_unique(g.sites, d->data.Location->site); break;
        case T::OfferService: if (d->data.OfferService) add_unique(g.civs, d->data.OfferService->served_entity); break;
        case T::RetrieveArtifact:
            if (d->data.RetrieveArtifact) {
                add_unique(g.artifacts, d->data.RetrieveArtifact->artifact);
                add_unique(g.sites, d->data.RetrieveArtifact->artifact_site);
                add_unique(g.civs, d->data.RetrieveArtifact->receiving_entity);
            }
            break;
        case T::PlotAssassination: if (d->data.PlotAssassination) add_unique(g.people, d->data.PlotAssassination->target_id); break;
        case T::PlotAbduct: if (d->data.PlotAbduct) add_unique(g.people, d->data.PlotAbduct->target_id); break;
        case T::PlotStealArtifact: if (d->data.PlotStealArtifact) add_unique(g.artifacts, d->data.PlotStealArtifact->artifact_id); break;
        default: break;
        }
    }
    return g;
}

// the sites of a group, nearest first
std::vector<std::pair<double, df::world_site *>> civ_sites(int32_t civ) {
    std::vector<std::pair<double, df::world_site *>> out;
    if (!world || !world->world_data)
        return out;
    for (df::world_site *s : world->world_data->sites) {
        if (!s || (s->cur_owner_id != civ && s->civ_id != civ))
            continue;
        double x, y, r, d = 0;
        site_centre(s, x, y, r);
        where_text(x, y, r, &d);
        out.push_back({d, s});
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.first < b.first; });
    return out;
}

std::unordered_set<int32_t> heard_sites() {
    std::unordered_set<int32_t> h;
    df::historical_figure *me = adv_figure();
    if (df::knowledge_profilest *k = my_knowledge()) {
        for (int32_t id : k->heard_of_stid)
            h.insert(id);
        if (const df::site_reputation_info *kl = k->known_locations)
            for (const df::site_reputation_report *r : kl->reports)
                if (r)
                    h.insert(r->site_id);
    }
    if (me)
        for (const df::histfig_site_link *l : me->site_links)
            if (l)
                h.insert(l->site);
    return h;
}

bool site_known(const df::world_site *s, const std::unordered_set<int32_t> &heard) {
    return s && (heard.count(s->id) || tile_discovered(s->pos.x, s->pos.y));
}

// who holds a position in a group: position name, figure
std::vector<std::pair<std::string, int32_t>> leaders_of(const df::historical_entity *e) {
    std::vector<std::pair<std::string, int32_t>> out;
    if (!e)
        return out;
    for (const df::entity_position_assignment *a : e->positions.assignments) {
        if (!a || a->histfig < 0)
            continue;
        std::string pos;
        for (const df::entity_position *p : e->positions.own)
            if (p && p->id == a->position_id)
                pos = p->name[0];
        out.push_back({pos.empty() ? std::string("position") : pos, a->histfig});
    }
    return out;
}

// Where an entry is, in world tiles (the middle and a radius): the place to go
// for it. False when that is not known.
bool entry_pos(const JRef &r, double &x, double &y, double &rad, int depth = 0) {
    if (depth > 2 || !world || !world->world_data)
        return false;
    rad = 0;
    switch (r.kind) {
    case JK_SITE:
    case JK_PLACE: return site_centre(df::world_site::find(r.id), x, y, rad);
    case JK_PERSON: return person_pos(df::historical_figure::find(r.id), x, y, rad) != 0;
    case JK_CIV: {
        const std::unordered_set<int32_t> heard = heard_sites();
        for (const auto &[d, s] : civ_sites(r.id))
            if (site_known(s, heard))
                return site_centre(s, x, y, rad);
        return false;
    }
    case JK_REGION:
        if (region_near(region_by_id(r.id), x, y)) {
            rad = 384;
            return true;
        }
        return false;
    case JK_ARTIFACT: {
        df::artifact_record *a = df::artifact_record::find(r.id);
        if (!a)
            return false;
        const int32_t holder = a->holder_hf >= 0 ? a->holder_hf : a->owner_hf;
        if (holder >= 0 && person_pos(df::historical_figure::find(holder), x, y, rad))
            return true;
        const int32_t site = a->storage_site >= 0 ? a->storage_site : a->site;
        if (site >= 0)
            return site_centre(df::world_site::find(site), x, y, rad);
        return false;
    }
    case JK_QUEST: {
        df::agreement *a = df::agreement::find(r.id);
        if (!a)
            return false;
        const QuestGoals g = quest_goals(a);
        for (int32_t s : g.sites)
            if (site_centre(df::world_site::find(s), x, y, rad))
                return true;
        for (int32_t h : g.people)
            if (person_pos(df::historical_figure::find(h), x, y, rad))
                return true;
        for (int32_t s : g.artifacts)
            if (entry_pos(jr_ref(JK_ARTIFACT, s), x, y, rad, depth + 1))
                return true;
        for (int32_t e : g.civs)
            if (entry_pos(jr_ref(JK_CIV, e), x, y, rad, depth + 1))
                return true;
        return false;
    }
    case JK_RUMOR: {
        df::entity_event *ev = rumor_at(r.id);
        if (!ev)
            return false;
        // a site of the rumour first, then the rest in order
        const RumorParts p = rumor_parts(ev);
        for (const auto &[role, e] : p.parts)
            if (e.kind == JK_SITE && entry_pos(e, x, y, rad, depth + 1))
                return true;
        for (const auto &[role, e] : p.parts)
            if (e.kind != JK_SITE && e.kind != JK_CIV && entry_pos(e, x, y, rad, depth + 1))
                return true;
        return false;
    }
    case JK_BEAST: {
        double gx = 0, gy = 0, best = -1;
        adv_tile(gx, gy);
        for (const BeastKnown &b : known_beasts()) {
            if (b.race != r.id)
                continue;
            const auto consider = [&](double cx, double cy, double cr) {
                const double d = std::hypot(cx - gx, cy - gy) - cr;
                if (best < 0 || d < best) {
                    best = d;
                    x = cx;
                    y = cy;
                    rad = cr;
                }
            };
            for (int32_t s : b.sites) {
                double cx, cy, cr;
                if (site_centre(df::world_site::find(s), cx, cy, cr))
                    consider(cx, cy, cr);
            }
            for (int32_t g : b.regions) {
                double cx, cy;
                if (region_near(region_by_id(g), cx, cy, true))
                    consider(cx, cy, 384);
            }
        }
        return best >= 0;
    }
    default: return false;
    }
}

JRow make_row(const JRef &r) {
    JRow row;
    row.ref = r;
    const JNames n = names_of(r);
    row.english = n.english;
    row.native = n.native == n.english ? "" : n.native;
    row.kind = n.kind;
    double x, y, rad;
    if (entry_pos(r, x, y, rad)) {
        row.placed = true;
        row.where = where_text(x, y, rad, &row.dist);
    }
    row.hay = lower(n.english + " | " + n.native + " | " + n.kind);
    return row;
}

std::shared_ptr<JData> read_journal() {
    auto d = std::make_shared<JData>();
    if (!world || !world->world_data)
        return d;
    df::historical_figure *me = adv_figure();
    df::knowledge_profilest *know = my_knowledge();
    const std::unordered_set<int32_t> heard = heard_sites();
    std::unordered_set<int32_t> people, civs, artifacts;
    std::unordered_map<int32_t, std::string> post;  // a leader's position, for the People list

    for (df::agreement *a : my_agreements()) {
        const QuestGoals g = quest_goals(a);
        JRow row = make_row(jr_ref(JK_QUEST, a->id));
        row.kind = a->flags.bits.convicted_accepted ? "concluded" : "open";
        if (!g.people.empty()) {
            const JNames n = names_of(jr_ref(JK_PERSON, g.people[0]));
            row.native = "with " + n.english + (n.native.empty() || n.native == n.english ? "" : " (" + n.native + ")");
        } else if (!g.civs.empty()) {
            row.native = "with " + names_of(jr_ref(JK_CIV, g.civs[0])).english;
        }
        row.hay = lower(row.english + " | " + row.native + " | " + row.kind);
        d->rows[JT_QUESTS].push_back(row);
        for (int32_t h : g.people)
            people.insert(h);
        for (int32_t e : g.civs)
            civs.insert(e);
        for (int32_t e : g.artifacts)
            artifacts.insert(e);
    }

    if (know)
        for (size_t i = 0; i < know->rumor_info.events.size(); ++i) {
            df::entity_event *ev = know->rumor_info.events[i];
            if (!ev)
                continue;
            const RumorParts p = rumor_parts(ev);
            JRow row = make_row(jr_ref(JK_RUMOR, int32_t(i)));
            std::string about, about_native;
            for (const auto &[role, e] : p.parts) {
                if (e.kind == JK_PERSON)
                    people.insert(e.id);
                if (e.kind == JK_CIV)
                    civs.insert(e.id);
                if (e.kind == JK_ARTIFACT)
                    artifacts.insert(e.id);
                if (about.empty()) {
                    const JNames n = names_of(e);
                    about = n.english;
                    about_native = n.native;
                }
            }
            row.english = p.title + (about.empty() ? "" : ": " + about);
            row.native = about_native == about ? "" : about_native;
            row.hay = lower(row.english + " | " + row.native + " | " + row.kind);
            d->rows[JT_RUMORS].push_back(row);
        }

    for (df::world_site *s : world->world_data->sites) {
        if (!site_known(s, heard))
            continue;
        const JRow site_row = make_row(jr_ref(JK_SITE, s->id));
        d->rows[JT_SITES].push_back(site_row);
        if (s->cur_owner_id >= 0)
            civs.insert(s->cur_owner_id);
        if (s->civ_id >= 0)
            civs.insert(s->civ_id);
        for (const auto &[pos, h] : leaders_of(df::historical_entity::find(s->cur_owner_id))) {
            people.insert(h);
            post.emplace(h, pos);
        }
        for (df::abstract_building *b : s->buildings) {
            df::language_name *nm = b ? b->getName() : nullptr;
            if (!nm || !nm->has_name)
                continue;
            JRow row = make_row(jr_ref(JK_PLACE, s->id, b->id));
            row.native += (row.native.empty() ? "" : " - ") + site_row.english;
            row.hay += " | " + lower(site_row.english + " | " + site_row.native);
            d->rows[JT_PLACES].push_back(row);
        }
    }

    if (me) {
        for (const df::histfig_hf_link *l : me->histfig_links)
            if (l)
                people.insert(l->target_hf);
        for (const df::histfig_entity_link *l : me->entity_links)
            if (l)
                civs.insert(l->entity_id);
    }
    for (int32_t e : civs)
        for (const auto &[pos, h] : leaders_of(df::historical_entity::find(e))) {
            people.insert(h);
            post.emplace(h, pos);
        }
    if (me)
        people.erase(me->id);
    for (int32_t h : people) {
        df::historical_figure *hf = df::historical_figure::find(h);
        if (!hf)
            continue;
        JRow row = make_row(jr_ref(JK_PERSON, h));
        auto it = post.find(h);
        if (it != post.end())
            row.kind += ", " + it->second;
        if (hf->died_year >= 0)
            row.kind += ", dead";
        row.hay = lower(row.english + " | " + row.native + " | " + row.kind);
        d->rows[JT_PEOPLE].push_back(row);
    }
    // artifacts held by the people known
    for (const df::artifact_record *a : world->artifacts.all)
        if (a && ((a->holder_hf >= 0 && people.count(a->holder_hf)) || (a->owner_hf >= 0 && people.count(a->owner_hf))))
            artifacts.insert(a->id);
    for (int32_t e : civs)
        if (df::historical_entity::find(e))
            d->rows[JT_CIVS].push_back(make_row(jr_ref(JK_CIV, e)));

    const auto &regions = world->world_data->regions;
    for (size_t i = 0; i < regions.size(); ++i) {
        double x, y;
        if (regions[i] && region_near(regions[i], x, y))
            d->rows[JT_REGIONS].push_back(make_row(jr_ref(JK_REGION, int32_t(i))));
    }
    for (int32_t a : artifacts)
        if (df::artifact_record::find(a))
            d->rows[JT_ARTIFACTS].push_back(make_row(jr_ref(JK_ARTIFACT, a)));
    for (const BeastKnown &b : known_beasts()) {
        JRow row = make_row(jr_ref(JK_BEAST, b.race));
        row.kind = b.recent ? "met lately" : b.local ? "lives around here" : "heard of";
        d->rows[JT_BEASTS].push_back(row);
    }
    return d;
}

// ---------------------------------------------------------------------------
// one entry's page

void quest_detail(df::agreement *a, JText &t, JDetail &D) {
    const QuestGoals g = quest_goals(a);
    D.kind = a->flags.bits.convicted_accepted ? "quest, concluded" : "quest";
    df::historical_figure *me = adv_figure();
    t.head("Where to go");
    bool any = false;
    for (int32_t s : g.sites) {
        double x, y, r;
        if (!site_centre(df::world_site::find(s), x, y, r))
            continue;
        JLine &l = t.line(1);
        l.spans.push_back(jlink(jr_ref(JK_SITE, s)));
        l.spans.push_back(jtext(", " + where_text(x, y, r)));
        any = true;
    }
    for (int32_t h : g.people) {
        df::historical_figure *hf = df::historical_figure::find(h);
        if (!hf)
            continue;
        JLine &l = t.line(1);
        l.spans.push_back(jlink(jr_ref(JK_PERSON, h)));
        l.spans.push_back(jtext(": "));
        for (const JSpan &s : person_where(hf))
            l.spans.push_back(s);
        any = true;
    }
    for (int32_t e : g.artifacts) {
        JLine &l = t.line(1);
        l.spans.push_back(jlink(jr_ref(JK_ARTIFACT, e)));
        double x, y, r;
        l.spans.push_back(jtext(entry_pos(jr_ref(JK_ARTIFACT, e), x, y, r) ? ": " + where_text(x, y, r) : ": not known"));
        any = true;
    }
    for (int32_t e : g.civs) {
        const auto sites = civ_sites(e);
        JLine &l = t.line(1);
        l.spans.push_back(jlink(jr_ref(JK_CIV, e)));
        if (sites.empty()) {
            l.spans.push_back(jtext(": no sites known"));
        } else {
            double x, y, r;
            site_centre(sites[0].second, x, y, r);
            l.spans.push_back(jtext(": nearest of its sites "));
            l.spans.push_back(jlink(jr_ref(JK_SITE, sites[0].second->id)));
            l.spans.push_back(jtext(", " + where_text(x, y, r)));
        }
        any = true;
    }
    if (!any)
        t.say("Nothing in it has a known place.", 1);

    t.head("Parties");
    for (const df::agreement_party *p : a->parties) {
        if (!p)
            continue;
        JLine &l = t.line(1);
        bool first = true;
        for (int32_t h : p->histfig_ids) {
            if (!first)
                l.spans.push_back(jtext(", "));
            first = false;
            l.spans.push_back(me && h == me->id ? jtext("you") : jlink(jr_ref(JK_PERSON, h)));
        }
        for (int32_t e : p->entity_ids) {
            if (!first)
                l.spans.push_back(jtext(", "));
            first = false;
            l.spans.push_back(jlink(jr_ref(JK_CIV, e)));
        }
        if (first)
            l.spans.push_back(jtext("(nobody)", UI_DIM));
    }

    using T = df::agreement_details_type;
    t.head("Terms");
    for (const df::agreement_details *d : a->details) {
        if (!d)
            continue;
        t.line(1).spans.push_back(jtext(capital(pretty_key(ENUM_KEY_STR(agreement_details_type, d->type).c_str())) +
                                        ", agreed in year " + std::to_string(d->year)));
        if (d->type == T::OfferService && d->data.OfferService) {
            const auto *o = d->data.OfferService;
            JLine &m = t.line(2);
            m.spans.push_back(jtext("serving "));
            m.spans.push_back(jlink(jr_ref(JK_CIV, o->served_entity)));
            if (const df::agreement_party *rq = party(a, o->requesting_party)) {
                m.spans.push_back(jtext(", asked by "));
                bool first = true;
                for (int32_t h : rq->histfig_ids) {
                    if (!first)
                        m.spans.push_back(jtext(", "));
                    first = false;
                    m.spans.push_back(me && h == me->id ? jtext("you") : jlink(jr_ref(JK_PERSON, h)));
                }
            }
        } else if (d->type == T::RetrieveArtifact && d->data.RetrieveArtifact) {
            const auto *o = d->data.RetrieveArtifact;
            JLine &m = t.line(2);
            m.spans.push_back(jtext("bring "));
            m.spans.push_back(jlink(jr_ref(JK_ARTIFACT, o->artifact)));
            m.spans.push_back(jtext(" to "));
            m.spans.push_back(jlink(jr_ref(JK_CIV, o->receiving_entity)));
        }
    }
    if (a->smm_x >= 0) {
        t.head("Made");
        t.say("where it was agreed: " + where_text(a->smm_x * 16.0 + 8, a->smm_y * 16.0 + 8, 0), 1);
    }
}

void rumor_detail(df::entity_event *ev, JText &t) {
    const RumorParts p = rumor_parts(ev);
    t.head("What is said");
    JLine &l = t.line(1);
    l.spans.push_back(jtext(p.title));
    for (const auto &[role, e] : p.parts) {
        l.spans.push_back(jtext(role.empty() ? ", " : " " + role + " "));
        l.spans.push_back(jlink(e));
    }
    t.say("happened in year " + std::to_string(ev->year), 1);
    bool any = false;
    for (const auto &[role, e] : p.parts) {
        double x, y, r;
        if (!entry_pos(e, x, y, r))
            continue;
        if (!any)
            t.head("Where");
        any = true;
        JLine &w = t.line(1);
        w.spans.push_back(jlink(e));
        w.spans.push_back(jtext(", " + where_text(x, y, r)));
    }
}

void site_detail(df::world_site *s, JText &t) {
    double x, y, r;
    site_centre(s, x, y, r);
    t.head("Where");
    {
        JLine &l = t.line(1);
        l.spans.push_back(jtext(where_text(x, y, r)));
        if (df::world_region *g = region_at(s->pos.x, s->pos.y)) {
            l.spans.push_back(jtext(", in "));
            l.spans.push_back(jlink(jr_ref(JK_REGION, g->index)));
        }
        t.say("world map square " + std::to_string(s->pos.x) + ", " + std::to_string(s->pos.y), 1);
    }
    t.head("Held by");
    if (s->cur_owner_id >= 0)
        t.line(1).spans.push_back(jlink(jr_ref(JK_CIV, s->cur_owner_id)));
    if (s->civ_id >= 0 && s->civ_id != s->cur_owner_id) {
        JLine &l = t.line(1);
        l.spans.push_back(jtext("of "));
        l.spans.push_back(jlink(jr_ref(JK_CIV, s->civ_id)));
    }
    if (s->cur_owner_id < 0 && s->civ_id < 0)
        t.say("nobody", 1);
    for (const auto &[pos, h] : leaders_of(df::historical_entity::find(s->cur_owner_id))) {
        JLine &l = t.line(1);
        l.spans.push_back(jtext(capital(pos) + ": "));
        l.spans.push_back(jlink(jr_ref(JK_PERSON, h)));
    }
    bool places = false;
    for (df::abstract_building *b : s->buildings) {
        df::language_name *nm = b ? b->getName() : nullptr;
        if (!nm || !nm->has_name)
            continue;
        if (!places)
            t.head("Places here");
        places = true;
        JLine &l = t.line(1);
        l.spans.push_back(jlink(jr_ref(JK_PLACE, s->id, b->id)));
        l.spans.push_back(jtext(", " + pretty_key(ENUM_KEY_STR(abstract_building_type, b->getType()).c_str()), UI_DIM));
    }
    bool quests = false;
    for (df::agreement *a : my_agreements()) {
        const QuestGoals g = quest_goals(a);
        if (std::find(g.sites.begin(), g.sites.end(), s->id) == g.sites.end())
            continue;
        if (!quests)
            t.head("Your quests here");
        quests = true;
        t.line(1).spans.push_back(jlink(jr_ref(JK_QUEST, a->id)));
    }
}

void place_detail(df::world_site *s, df::abstract_building *b, JText &t) {
    double x, y, r;
    site_centre(s, x, y, r);
    t.head("Where");
    JLine &l = t.line(1);
    l.spans.push_back(jtext("in "));
    l.spans.push_back(jlink(jr_ref(JK_SITE, s->id)));
    l.spans.push_back(jtext(", " + where_text(x, y, r)));
    if (b->parent_building_id >= 0) {
        JLine &p = t.line(1);
        p.spans.push_back(jtext("part of "));
        p.spans.push_back(jlink(jr_ref(JK_PLACE, s->id, b->parent_building_id)));
    }
    if (!b->inhabitants.empty())
        t.say(std::to_string(b->inhabitants.size()) + " known to live or work here", 1);
    if (b->getReligionID() >= 0) {
        t.head("Run by");
        t.line(1).spans.push_back(jlink(jr_ref(JK_CIV, b->getReligionID())));
    }
}

void person_detail(df::historical_figure *hf, JText &t, JDetail &D) {
    std::string kind = creature_name(hf->race);
    if (int(hf->sex) == 0)
        kind = "female " + kind;
    else if (int(hf->sex) == 1)
        kind = "male " + kind;
    D.kind = kind;
    t.head("Where");
    {
        JLine &l = t.line(1);
        for (const JSpan &s : person_where(hf))
            l.spans.push_back(s);
    }
    t.head("About");
    if (hf->born_year >= 0)
        t.say("born in year " + std::to_string(hf->born_year), 1);
    for (df::histfig_entity_link *l : hf->entity_links) {
        if (!l)
            continue;
        JLine &line = t.line(1);
        line.spans.push_back(jtext(capital(pretty_key(ENUM_KEY_STR(histfig_entity_link_type, l->getType()).c_str())) + " of "));
        line.spans.push_back(jlink(jr_ref(JK_CIV, l->entity_id)));
        for (const auto &[pos, h] : leaders_of(df::historical_entity::find(l->entity_id)))
            if (h == hf->id)
                line.spans.push_back(jtext(", " + pos));
    }
    int n = 0;
    for (df::histfig_hf_link *l : hf->histfig_links) {
        if (!l || l->target_hf < 0)
            continue;
        if (n == 0)
            t.head("Relations");
        if (++n > 24) {
            t.say("and " + std::to_string(hf->histfig_links.size() - 24) + " more", 1);
            break;
        }
        JLine &line = t.line(1);
        line.spans.push_back(jtext(capital(pretty_key(ENUM_KEY_STR(histfig_hf_link_type, l->getType()).c_str())) + ": "));
        line.spans.push_back(jlink(jr_ref(JK_PERSON, l->target_hf)));
    }
    bool arts = false;
    for (const df::artifact_record *a : world->artifacts.all) {
        if (!a || (a->holder_hf != hf->id && a->owner_hf != hf->id))
            continue;
        if (!arts)
            t.head("Artifacts");
        arts = true;
        t.line(1).spans.push_back(jlink(jr_ref(JK_ARTIFACT, a->id)));
    }
    bool quests = false;
    for (df::agreement *a : my_agreements()) {
        bool in = false;
        for (const df::agreement_party *p : a->parties)
            in = in || party_has(p, hf->id);
        if (!in)
            continue;
        if (!quests)
            t.head("Your quests with them");
        quests = true;
        t.line(1).spans.push_back(jlink(jr_ref(JK_QUEST, a->id)));
    }
}

void civ_detail(df::historical_entity *e, JText &t) {
    const auto sites = civ_sites(e->id);
    const std::unordered_set<int32_t> heard = heard_sites();
    t.head("Sites");
    int unknown = 0, shown = 0;
    for (const auto &[dist, s] : sites) {
        if (!site_known(s, heard)) {
            ++unknown;
            continue;
        }
        if (++shown > 40)
            continue;
        double x, y, r;
        site_centre(s, x, y, r);
        JLine &l = t.line(1);
        l.spans.push_back(jlink(jr_ref(JK_SITE, s->id)));
        l.spans.push_back(jtext(", " + site_kind(s->type) + ", " + where_text(x, y, r), UI_DIM));
    }
    if (shown > 40)
        t.say("and " + std::to_string(shown - 40) + " more", 1);
    if (shown == 0)
        t.say("none you know of", 1);
    if (unknown)
        t.say(std::to_string(unknown) + " more you have not found or heard of", 1);
    const auto leaders = leaders_of(e);
    if (!leaders.empty()) {
        t.head("Positions");
        int n = 0;
        for (const auto &[pos, h] : leaders) {
            if (++n > 30)
                break;
            JLine &l = t.line(1);
            l.spans.push_back(jtext(capital(pos) + ": "));
            l.spans.push_back(jlink(jr_ref(JK_PERSON, h)));
        }
    }
}

void region_detail(df::world_region *g, JText &t) {
    t.head("Size");
    t.say(std::to_string(g->region_coords.x.size()) + " squares of the world map", 1);
    const std::unordered_set<int32_t> heard = heard_sites();
    bool any = false;
    for (df::world_site *s : world->world_data->sites) {
        if (!s || !site_known(s, heard))
            continue;
        df::world_region *at = region_at(s->pos.x, s->pos.y);
        if (!at || at->index != g->index)
            continue;
        if (!any)
            t.head("Sites in it");
        any = true;
        double x, y, r;
        site_centre(s, x, y, r);
        JLine &l = t.line(1);
        l.spans.push_back(jlink(jr_ref(JK_SITE, s->id)));
        l.spans.push_back(jtext(", " + site_kind(s->type) + ", " + where_text(x, y, r), UI_DIM));
    }
}

void artifact_detail(df::artifact_record *a, JText &t) {
    t.head("Last known");
    const int32_t holder = a->holder_hf >= 0 ? a->holder_hf : a->owner_hf;
    if (holder >= 0) {
        JLine &l = t.line(1);
        l.spans.push_back(jtext("held by "));
        l.spans.push_back(jlink(jr_ref(JK_PERSON, holder)));
    }
    const int32_t site = a->storage_site >= 0 ? a->storage_site : a->site;
    if (df::world_site *s = df::world_site::find(site)) {
        double x, y, r;
        site_centre(s, x, y, r);
        JLine &l = t.line(1);
        l.spans.push_back(jtext("at "));
        l.spans.push_back(jlink(jr_ref(JK_SITE, site)));
        l.spans.push_back(jtext(", " + where_text(x, y, r)));
    }
    const int32_t region = a->loss_region >= 0 ? a->loss_region : a->subregion;
    if (region >= 0) {
        JLine &l = t.line(1);
        l.spans.push_back(jtext("in "));
        l.spans.push_back(jlink(jr_ref(JK_REGION, region)));
    }
    if (holder < 0 && site < 0 && region < 0)
        t.say("nowhere known", 1);
    if (a->item) {
        std::string desc;
        a->item->getItemDescription(&desc, 0);
        t.head("What it is");
        t.say(desc, 1);
    }
}

void beast_detail(int race, JText &t) {
    df::creature_raw *c = creature_of(race);
    if (!c)
        return;
    for (const df::caste_raw *k : c->caste)
        if (k && !k->description.empty()) {
            t.head("Description");
            t.say(k->description, 1);
            break;
        }
    for (const BeastKnown &b : known_beasts()) {
        if (b.race != race)
            continue;
        t.head("What you know");
        if (b.recent)
            t.say("met lately", 1);
        if (b.local)
            t.say("known to live around here", 1);
        if (b.heard)
            t.say("heard of", 1);
        if (!b.sites.empty() || !b.regions.empty())
            t.head("Known to be at");
        for (int32_t s : b.sites) {
            double x, y, r;
            if (!site_centre(df::world_site::find(s), x, y, r))
                continue;
            JLine &l = t.line(1);
            l.spans.push_back(jlink(jr_ref(JK_SITE, s)));
            l.spans.push_back(jtext(", " + where_text(x, y, r), UI_DIM));
        }
        for (int32_t g : b.regions) {
            JLine &l = t.line(1);
            l.spans.push_back(jlink(jr_ref(JK_REGION, g)));
            double x, y;
            if (region_near(region_by_id(g), x, y, true))
                l.spans.push_back(jtext(", " + where_text(x, y, 384), UI_DIM));
        }
    }
}

// the piece of the world map that holds the entry and the adventurer
JMap read_map(double tx, double ty) {
    JMap m;
    double gx, gy;
    if (!adv_tile(gx, gy) || !world || !world->world_data)
        return m;
    const int ex = int(tx / 768), ey = int(ty / 768), ax = int(gx / 768), ay = int(gy / 768);
    const int span = std::max(std::abs(ex - ax), std::abs(ey - ay));
    m.n = std::clamp(span + 9, 13, 129);
    const int x0 = (ex + ax) / 2 - m.n / 2, y0 = (ey + ay) / 2 - m.n / 2;
    m.tiles.resize(size_t(m.n) * m.n);
    for (int y = 0; y < m.n; ++y)
        for (int x = 0; x < m.n; ++x)
            m.tiles[size_t(y) * m.n + x] = wm_square(x0 + x, y0 + y, true);
    m.tx = ex - x0;
    m.ty = ey - y0;
    m.ax = ax - x0;
    m.ay = ay - y0;
    return m;
}

JDetail read_detail(const JRef &r) {
    JDetail D;
    D.ref = r;
    if (!r.ok() || !world || !world->world_data)
        return D;
    const JNames n = names_of(r);
    D.english = n.english;
    D.native = n.native == n.english ? "" : n.native;
    D.kind = n.kind;
    double x, y, rad;
    D.placed = entry_pos(r, x, y, rad);
    if (D.placed) {
        D.where = where_text(x, y, rad);
        D.map = read_map(x, y);
    }
    JText t{D.lines};
    switch (r.kind) {
    case JK_QUEST:
        if (df::agreement *a = df::agreement::find(r.id))
            quest_detail(a, t, D);
        break;
    case JK_RUMOR:
        if (df::entity_event *ev = rumor_at(r.id))
            rumor_detail(ev, t);
        break;
    case JK_SITE:
        if (df::world_site *s = df::world_site::find(r.id))
            site_detail(s, t);
        break;
    case JK_PLACE: {
        df::world_site *s = df::world_site::find(r.id);
        if (df::abstract_building *b = place_of(s, r.sub))
            place_detail(s, b, t);
        break;
    }
    case JK_PERSON:
        if (df::historical_figure *hf = df::historical_figure::find(r.id))
            person_detail(hf, t, D);
        break;
    case JK_CIV:
        if (df::historical_entity *e = df::historical_entity::find(r.id))
            civ_detail(e, t);
        break;
    case JK_REGION:
        if (df::world_region *g = region_by_id(r.id))
            region_detail(g, t);
        break;
    case JK_ARTIFACT:
        if (df::artifact_record *a = df::artifact_record::find(r.id))
            artifact_detail(a, t);
        break;
    case JK_BEAST: beast_detail(r.id, t); break;
    default: break;
    }
    return D;
}

// ---------------------------------------------------------------------------
// tracking: entries marked on the compass, kept per save in a file

const char *const TRACK_FILE = "dfhack-config/fpview-tracked.txt";

// Marker colours: a new marker takes one at random among those no other marker
// has (any, once all are taken), so markers look different from each other.
const uint32_t MARKER_COLOURS[] = {
    fpv::rgb(255, 196, 0),   fpv::rgb(255, 92, 72),   fpv::rgb(80, 200, 255),  fpv::rgb(120, 230, 90),
    fpv::rgb(240, 110, 230), fpv::rgb(255, 150, 40),  fpv::rgb(150, 120, 255), fpv::rgb(60, 230, 190),
    fpv::rgb(255, 240, 120), fpv::rgb(230, 60, 120),  fpv::rgb(110, 160, 255), fpv::rgb(200, 255, 80),
    fpv::rgb(255, 130, 170), fpv::rgb(80, 255, 140),  fpv::rgb(190, 90, 255),  fpv::rgb(255, 210, 160),
    fpv::rgb(0, 190, 210),   fpv::rgb(230, 230, 230), fpv::rgb(210, 160, 60),  fpv::rgb(140, 255, 230),
    fpv::rgb(255, 70, 30),   fpv::rgb(170, 210, 120), fpv::rgb(120, 120, 255), fpv::rgb(255, 180, 255),
};
constexpr int MARKER_COUNT = int(sizeof(MARKER_COLOURS) / sizeof(MARKER_COLOURS[0]));

uint32_t marker_colour(int k) { return MARKER_COLOURS[((k % MARKER_COUNT) + MARKER_COUNT) % MARKER_COUNT]; }

uint32_t new_marker_colour(const std::vector<uint32_t> &used) {
    static std::mt19937 rng{std::random_device{}()};
    std::vector<uint32_t> free;
    for (uint32_t c : MARKER_COLOURS)
        if (std::find(used.begin(), used.end(), c) == used.end())
            free.push_back(c);
    if (free.empty())
        return marker_colour(int(rng() % MARKER_COUNT));
    return free[rng() % free.size()];
}

// A region's middle: the discovered square nearest the mean of its discovered
// squares (a region need not hold its own mean). False if none is discovered.
bool region_centre(const df::world_region *g, double &x, double &y) {
    if (!g)
        return false;
    double sx = 0, sy = 0;
    int n = 0;
    for (size_t k = 0; k < g->region_coords.x.size() && k < g->region_coords.y.size(); ++k)
        if (tile_discovered(g->region_coords.x[k], g->region_coords.y[k])) {
            sx += g->region_coords.x[k];
            sy += g->region_coords.y[k];
            ++n;
        }
    if (!n)
        return false;
    sx /= n;
    sy /= n;
    double best = -1;
    for (size_t k = 0; k < g->region_coords.x.size() && k < g->region_coords.y.size(); ++k) {
        const int wx = g->region_coords.x[k], wy = g->region_coords.y[k];
        if (!tile_discovered(wx, wy))
            continue;
        const double d = (wx - sx) * (wx - sx) + (wy - sy) * (wy - sy);
        if (best < 0 || d < best) {
            best = d;
            x = wx * 768.0 + 384;
            y = wy * 768.0 + 384;
        }
    }
    return true;
}

// where a tracked entry's marker goes: a region's middle, else where the entry is
bool marker_pos(const JRef &r, double &x, double &y, double &rad) {
    if (r.kind == JK_REGION) {
        rad = 384;
        return region_centre(region_by_id(r.id), x, y);
    }
    return entry_pos(r, x, y, rad);
}

std::string save_name() {
    return world ? world->cur_savegame.save_dir : std::string();
}

void load_tracked() {
    const std::string save = save_name();
    if (jr.tracked_loaded && save == jr.tracked_save)
        return;
    jr.tracked_loaded = true;
    jr.tracked_save = save;
    jr.tracked.clear();
    jr.tracked_color.clear();
    std::ifstream f(TRACK_FILE);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string s;
        int kind = 0;
        int32_t id = -1, sub = -1;
        uint32_t color = 0;
        if (!(in >> s >> kind >> id >> sub) || s != save)
            continue;
        if (!(in >> std::hex >> color))
            color = 0;
        jr.tracked.push_back(jr_ref(kind, id, sub));
        jr.tracked_color.push_back(color ? color : marker_colour(int(jr.tracked.size()) * 7));
    }
}

void save_tracked() {
    std::vector<std::string> keep;
    {
        std::ifstream f(TRACK_FILE);
        std::string line;
        while (std::getline(f, line))
            if (!line.empty() && line.substr(0, line.find(' ')) != jr.tracked_save)
                keep.push_back(line);
    }
    std::ofstream f(TRACK_FILE, std::ios::trunc);
    for (const std::string &l : keep)
        f << l << "\n";
    for (size_t i = 0; i < jr.tracked.size(); ++i) {
        const JRef &r = jr.tracked[i];
        f << jr.tracked_save << " " << r.kind << " " << r.id << " " << r.sub << " " << std::hex
          << (i < jr.tracked_color.size() ? jr.tracked_color[i] : 0u) << std::dec << "\n";
    }
}

std::atomic<uint32_t> tracked_serial{0};  // changes when the tracked list does

void toggle_tracked(const JRef &r) {
    if (!r.ok() || save_name().empty())
        return;
    load_tracked();
    auto it = std::find(jr.tracked.begin(), jr.tracked.end(), r);
    if (it == jr.tracked.end()) {
        jr.tracked.push_back(r);
        jr.tracked_color.push_back(new_marker_colour(jr.tracked_color));
    } else {
        const size_t i = size_t(it - jr.tracked.begin());
        jr.tracked.erase(it);
        if (i < jr.tracked_color.size())
            jr.tracked_color.erase(jr.tracked_color.begin() + i);
    }
    save_tracked();
    ++tracked_serial;
}

// game thread, from the compass's step: the tracked entries whose place is
// known, put first on the compass (worked out again every 30 frames)
void add_tracked_marks(std::vector<CompassMark> &marks) {
    if (!castle_game_icon_tried) {
        castle_game_icon_tried = true;
        set_castle_game_icon(wm_tile("SITE_CASTLE"));
    }
    static std::vector<CompassMark> cache;
    static int frames = 0;
    static uint32_t serial = ~0u;
    load_tracked();
    if (serial != tracked_serial.load() || ++frames >= 30) {
        frames = 0;
        serial = tracked_serial.load();
        cache.clear();
        double gx, gy;
        if (adv_tile(gx, gy))
            for (size_t i = 0; i < jr.tracked.size(); ++i) {
                const JRef &r = jr.tracked[i];
                double x, y, rad;
                if (!marker_pos(r, x, y, rad))
                    continue;
                CompassMark m;
                m.bearing = float(std::atan2(x - gx, -(y - gy)));
                m.what = names_of(r).english;
                m.dist = where_text(x, y, rad);
                m.color = i < jr.tracked_color.size() ? jr.tracked_color[i] : J_TRACK;
                m.glyph = 251;  // the font's check mark
                m.jkind = r.kind;
                m.jid = r.id;
                m.jsub = r.sub;
                if (r.kind == JK_SITE)
                    m.site = r.id;
                // a person in the loaded area: above or below your level
                if (r.kind == JK_PERSON)
                    if (df::historical_figure *hf = df::historical_figure::find(r.id))
                        if (df::unit *u = hf->unit_id >= 0 ? df::unit::find(hf->unit_id) : nullptr)
                            if (df::unit *adv = World::getAdventurer(); adv && Units::isActive(u)) {
                                const int dz = u->pos.z - adv->pos.z;
                                m.level = dz > 0 ? 1 : dz < 0 ? -1 : 0;
                            }
                cache.push_back(m);
            }
    }
    if (cache.empty())
        return;
    // a place the game lists too is shown once, as tracked
    for (const CompassMark &t : cache)
        if (t.site >= 0)
            marks.erase(std::remove_if(marks.begin(), marks.end(),
                                       [&](const CompassMark &m) { return m.jkind == JK_NONE && m.site == t.site; }),
                        marks.end());
    marks.insert(marks.begin(), cache.begin(), cache.end());
    if (int(marks.size()) > MAX_MARKS)
        marks.resize(MAX_MARKS);
}

// ---------------------------------------------------------------------------
// the model

// The map's rectangle within the panel's body (pixels from the body's top left),
// shared by the model (how many squares) and the layout.
// Inside the game's frame: 3 cells from the sides, 3 rows from the top and
// bottom; the title row, a row of buttons (2 rows tall), then the map, and a
// hint line above the bottom edge.
void map_area(int bw, int bh, int cw, int ch, int &x, int &y, int &w, int &h) {
    const int pad = 3 * cw;
    x = pad;
    y = 2 * ch + ch / 2 + 2 + 3 * ch + 6;
    w = bw - 2 * pad;
    h = bh - 4 * ch - ch / 2 - y;
}

const int MAP_ZOOMS[] = {8, 12, 16, 24, 32, 48};

void read_map_view(JMapView &v) {
    Cells c;
    int bx, by, bw, bh;
    if (!get_cells(c) || !panel_room(c, bx, by, bw, bh) || !world || !world->world_data)
        return;
    int mx, my, mw, mh;
    map_area(bw, bh, c.cw, c.ch, mx, my, mw, mh);
    double gx, gy;
    if (adv_tile(gx, gy)) {
        v.you_x = gx / 768.0;
        v.you_y = gy / 768.0;
    }
    if (jr.map_cx < 0) {
        jr.map_cx = v.you_x;
        jr.map_cy = v.you_y;
    }
    const auto *wd = world->world_data;
    jr.map_cx = std::clamp(jr.map_cx, 0.0, double(wd->world_width));
    jr.map_cy = std::clamp(jr.map_cy, 0.0, double(wd->world_height));
    v.zoom = jr.map_zoom;
    // the square under the rectangle's top left corner, and how far into it
    const double left = jr.map_cx - mw / 2.0 / v.zoom, top = jr.map_cy - mh / 2.0 / v.zoom;
    v.x0 = int(std::floor(left));
    v.y0 = int(std::floor(top));
    v.off_x = -int((left - v.x0) * v.zoom);
    v.off_y = -int((top - v.y0) * v.zoom);
    v.cols = (mw - v.off_x) / v.zoom + 2;
    v.rows = (mh - v.off_y) / v.zoom + 2;
    v.tiles.resize(size_t(v.cols) * v.rows);
    for (int y = 0; y < v.rows; ++y)
        for (int x = 0; x < v.cols; ++x)
            v.tiles[size_t(y) * v.cols + x] = wm_square(v.x0 + x, v.y0 + y, true);
    v.area_of.assign(size_t(v.cols) * v.rows, -1);
    for (size_t i = 0; i < jr.tracked.size(); ++i) {
        const JRef &r = jr.tracked[i];
        const uint32_t color = i < jr.tracked_color.size() ? jr.tracked_color[i] : J_TRACK;
        double x, y, rad;
        if (marker_pos(r, x, y, rad)) {
            JMapMark k;
            k.x = x / 768.0;
            k.y = y / 768.0;
            k.ref = r;
            k.name = names_of(r).english;
            k.color = color;
            v.marks.push_back(k);
        }
        // a tracked region: its known squares in the window, in its colour
        if (r.kind == JK_REGION)
            if (const df::world_region *g = region_by_id(r.id)) {
                JMapArea a;
                a.ref = r;
                a.name = names_of(r).english;
                a.color = color;
                const int16_t idx = int16_t(v.areas.size());
                bool any = false;
                for (size_t k = 0; k < g->region_coords.x.size() && k < g->region_coords.y.size(); ++k) {
                    const int cx = g->region_coords.x[k] - v.x0, cy = g->region_coords.y[k] - v.y0;
                    if (cx < 0 || cy < 0 || cx >= v.cols || cy >= v.rows ||
                        !tile_discovered(g->region_coords.x[k], g->region_coords.y[k]))
                        continue;
                    v.area_of[size_t(cy) * v.cols + cx] = idx;
                    any = true;
                }
                if (any)
                    v.areas.push_back(a);
            }
    }
    // the known sites in the window, to be named and clicked
    const std::unordered_set<int32_t> heard = heard_sites();
    for (const df::world_site *s : wd->sites) {
        if (!s || s->pos.x < v.x0 || s->pos.y < v.y0 || s->pos.x >= v.x0 + v.cols || s->pos.y >= v.y0 + v.rows ||
            !site_known(s, heard))
            continue;
        JMapSite k;
        k.x = s->pos.x;
        k.y = s->pos.y;
        k.id = s->id;
        const JNames n = names_of(jr_ref(JK_SITE, s->id));
        k.name = n.english + (n.native.empty() || n.native == n.english ? "" : " / " + n.native) + " - " + n.kind;
        v.sites.push_back(k);
    }
    v.you_icon = wm_tile("MAP_ADV_YOU_TRAVEL");
}

// game thread: the model for what the state says now
void jr_publish() {
    auto m = std::make_shared<JModel>();
    if (!jr.data)
        jr.data = read_journal();
    load_tracked();
    m->data = jr.data;
    m->tab = jr.tab;
    m->sort = jr.sort;
    m->filter = jr.filter;
    m->tracked = jr.tracked;
    m->tracked_color = jr.tracked_color;
    const std::vector<JRow> &rows = jr.data->rows[jr.tab];
    const std::string f = lower(jr.filter);
    for (size_t i = 0; i < rows.size(); ++i)
        if (f.empty() || rows[i].hay.find(f) != std::string::npos)
            m->shown.push_back(int(i));
    const int sort = jr.sort;
    std::stable_sort(m->shown.begin(), m->shown.end(), [&](int a, int b) {
        const JRow &x = rows[a], &y = rows[b];
        if (sort == JS_NEAR) {
            if ((x.dist < 0) != (y.dist < 0))
                return x.dist >= 0;
            if (x.dist != y.dist)
                return x.dist < y.dist;
            return lower(x.english) < lower(y.english);
        }
        const std::string &p = sort == JS_NATIVE ? (x.native.empty() ? x.english : x.native) : x.english;
        const std::string &q = sort == JS_NATIVE ? (y.native.empty() ? y.english : y.native) : y.english;
        return lower(p) < lower(q);
    });
    for (size_t k = 0; k < m->shown.size(); ++k)
        if (rows[m->shown[k]].ref == jr.sel)
            m->sel_row = int(k);
    m->view = jr.view;
    if (jr.view == 1)
        read_map_view(m->mapv);
    else
        m->detail = read_detail(jr.sel);
    m->back = jr.hpos > 0;
    m->fwd = jr.hpos >= 0 && jr.hpos + 1 < int(jr.history.size());
    m->version = ++jr.version;
    std::lock_guard<std::mutex> lock(jr_mutex);
    jr_model = std::move(m);
}

// game thread: show an entry (from a link, a row, or a place clicked elsewhere)
void jr_go(const JRef &r, bool remember = true) {
    if (!r.ok())
        return;
    const int tab = jk_tab(r.kind);
    if (tab >= 0 && tab != jr.tab) {
        jr.tab = tab;
        jr.filter.clear();
        jr_list_scroll = 0;
    }
    jr.sel = r;
    jr.view = 0;
    if (remember) {
        if (jr.hpos + 1 < int(jr.history.size()))
            jr.history.resize(jr.hpos + 1);
        if (jr.history.empty() || jr.history.back() != r)
            jr.history.push_back(r);
        if (jr.history.size() > 100)
            jr.history.erase(jr.history.begin());
        jr.hpos = int(jr.history.size()) - 1;
    }
    jr_det_scroll = 0;
    jr_publish();
}

// game thread: open the panel, on a tab and maybe at an entry
void jr_open(int tab, const JRef &at) {
    jr.data = read_journal();
    jr.want = true;
    jr.filter.clear();
    if (tab >= 0)
        jr.tab = tab;
    jr_list_scroll = 0;
    jr_det_scroll = 0;
    if (at.ok()) {
        jr_go(at);
        return;
    }
    // the first (nearest) entry of the tab
    jr.sel = JRef();
    jr_publish();
    const std::shared_ptr<const JModel> m = get_jr();
    if (m && !m->shown.empty())
        jr_go(m->data->rows[m->tab][m->shown[0]].ref);
}

void jr_close() {
    jr.want = false;
    jr.bar_press = 0;
    jr_up = false;
    cover_x1 = -1;
}

// ---------------------------------------------------------------------------
// where everything goes (pixels from the panel's top left), for drawing and clicking

enum { JB_NONE, JB_PANEL, JB_TAB, JB_ROW, JB_LINK, JB_SEARCH, JB_SORT, JB_BACK, JB_FWD, JB_CLOSE, JB_GAME, JB_TRACK,
       JB_LEAF, JB_MAP, JB_MARK, JB_CENTRE, JB_ZOOM_IN, JB_ZOOM_OUT, JB_PAGEMAP, JB_MSITE, JB_MAREA, JB_LBAR, JB_DBAR };

// A scrollbar as paint_game_scrollbar draws it: an end cell at each end, the
// handle on the track between. Its handle's top (from the track's top) and length.
void jr_handle(int h, int content, int scroll, int ch, int &at, int &len) {
    const int track = h - 2 * ch;
    len = std::max(2 * ch, track * h / std::max(1, content));
    at = (track - len) * scroll / std::max(1, content - h);
}

// the scroll that puts the handle's top at `top` pixels down the track
int jr_scroll_for(int h, int content, int top, int ch) {
    int at, len;
    jr_handle(h, content, 0, ch, at, len);
    const int span = std::max(1, h - 2 * ch - len);
    return std::clamp(top * std::max(0, content - h) / span, 0, std::max(0, content - h));
}

struct JBox {
    int kind = JB_NONE;
    int a = -1;
    uint32_t color = 0;
    JRef link;
    std::string tip;
    int x = 0, y = 0, w = 0, h = 0;
    bool inside(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct JPiece {
    int x = 0, y = 0;
    std::string text;
    uint32_t color = UI_WHITE;
    int link = -1;  // which link of the page (JBox::a of its boxes)
};

struct JLayout {
    int x = 0, y = 0, w = 0, h = 0, cw = 8, ch = 12, pad = 16;
    int title_y = 0, tabs_y = 0, bar_y = 0, hint_y = 0;
    int list_x = 0, list_y = 0, list_w = 0, list_h = 0, row_h = 0, list_content = 0, list_scroll = 0;
    int det_x = 0, det_y = 0, det_w = 0, det_h = 0, det_content = 0, det_scroll = 0;
    int box = 12;                  // a track box's side
    int map_y = -1, map_s = 0;     // the page's map, scrolled (map_y -1: none)
    JBox page_track;               // the page's track box, scrolled
    int view = 0;
    int mx = 0, my = 0, mw = 0, mh = 0;  // the Map view's rectangle
    std::vector<JBox> marks;       // its marks
    std::vector<JBox> sites;       // its known sites
    const JMapView *mv = nullptr;  // for the areas under a point
    std::vector<JBox> boxes;       // tabs and buttons
    std::vector<JBox> links;       // the links in the page, scrolled
    std::vector<JPiece> pieces;

    bool in_det(int px, int py) const { return px >= det_x && px < det_x + det_w && py >= det_y && py < det_y + det_h; }
    JBox at(int wx, int wy) const {
        const int px = wx - x, py = wy - y;
        const int leaf = leaf_tab_at(wx, wy, x, y, cw, ch);
        if (leaf >= 0) {
            JBox b;
            b.kind = JB_LEAF;
            b.a = leaf;
            return b;
        }
        for (const JBox &b : boxes)
            if (b.inside(px, py))
                return b;
        if (view == 1) {
            // what is drawn highest is found first: marks, then sites, then a tracked region's area
            for (size_t i = marks.size(); i-- > 0;)
                if (marks[i].inside(px, py))
                    return marks[i];
            if (px >= mx && py >= my && px < mx + mw && py < my + mh) {
                for (size_t i = sites.size(); i-- > 0;)
                    if (sites[i].inside(px, py))
                        return sites[i];
                JBox b;
                b.kind = JB_MAP;
                if (mv && mv->zoom > 0) {
                    const int cx = (px - mx - mv->off_x) / mv->zoom, cy = (py - my - mv->off_y) / mv->zoom;
                    if (px - mx - mv->off_x >= 0 && py - my - mv->off_y >= 0 && cx < mv->cols && cy < mv->rows) {
                        const int a = mv->area_of[size_t(cy) * mv->cols + cx];
                        if (a >= 0 && size_t(a) < mv->areas.size()) {
                            b.kind = JB_MAREA;
                            b.a = a;
                            b.link = mv->areas[a].ref;
                            b.tip = mv->areas[a].name;
                        }
                    }
                }
                return b;
            }
        }
        if (view == 0) {
            // the scrollbars (2 cells wide), where paint_jr draws them
            const int lx = list_x + list_w - 2 * cw, dx = det_x + det_w + cw / 2;
            if (list_content > list_h && px >= lx && px < lx + 2 * cw && py >= list_y && py < list_y + list_h) {
                JBox b;
                b.kind = JB_LBAR;
                return b;
            }
            if (det_content > det_h && px >= dx && px < dx + 2 * cw && py >= det_y && py < det_y + det_h) {
                JBox b;
                b.kind = JB_DBAR;
                return b;
            }
        }
        if (view == 0 && map_s > 0 && px >= det_x && px < det_x + map_s && py >= map_y && py < map_y + map_s &&
            in_det(px, py)) {
            JBox b;
            b.kind = JB_PAGEMAP;
            return b;
        }
        if (in_det(px, py)) {
            if (page_track.kind != JB_NONE && page_track.inside(px, py))
                return page_track;
            for (const JBox &b : links)
                if (b.inside(px, py))
                    return b;
        }
        if (px >= list_x && px < list_x + list_w && py >= list_y && py < list_y + list_h) {
            const int k = (py - list_y + list_scroll) / row_h;
            JBox b;
            b.kind = JB_ROW;
            b.a = k;
            b.x = list_x;
            b.y = list_y + k * row_h - list_scroll;
            b.w = list_w;
            b.h = row_h;
            // its track box, at the left of the first line (a little room around it)
            if (px < list_x + box + 8 && py >= b.y + 2 && py < b.y + box + 6) {
                b.kind = JB_TRACK;
                b.x = list_x + 4;
                b.y += 4;
                b.w = b.h = box;
            }
            return b;
        }
        JBox none;
        if (px >= 0 && py >= 0 && px < w && py < h)
            none.kind = JB_PANEL;
        return none;
    }
};

// the page's lines broken to the width, links boxed, the map's place
void wrap_page(const JModel &m, JLayout &L) {
    const int cw = L.cw, ch = L.ch, line_h = ch + 3;
    const int left = L.det_x, right = L.det_x + L.det_w, head_x = left + L.box + cw;
    int y = 0;
    std::vector<JBox> spans;  // one per link span
    const auto put = [&](int x, const std::string &t, uint32_t color, int link) {
        JPiece p;
        p.x = x;
        p.y = y;
        p.text = t;
        p.color = color;
        p.link = link;
        L.pieces.push_back(std::move(p));
    };
    if (m.detail.ref.ok()) {
        // the track box before the title
        L.page_track.kind = JB_TRACK;
        L.page_track.a = -1;
        L.page_track.link = m.detail.ref;
        L.page_track.x = left;
        L.page_track.y = 1;
        L.page_track.w = L.page_track.h = L.box;
        put(head_x, m.detail.english, UI_WHITE, -1);
        y += line_h;
    }
    if (!m.detail.native.empty()) {
        put(head_x, m.detail.native, UI_DIM, -1);
        y += line_h;
    }
    std::string sub = m.detail.kind;
    if (!m.detail.where.empty())
        sub += (sub.empty() ? "" : ", ") + m.detail.where;
    if (!sub.empty()) {
        put(head_x, sub, J_HEAD, -1);
        y += line_h;
    }
    if (m.detail.map.n > 0) {
        y += 4;
        L.map_s = std::min(L.det_w, 12 * ch);
        L.map_y = y;
        y += L.map_s + 6;
    }
    for (const JLine &line : m.detail.lines) {
        if (line.head)
            y += ch / 2 + 2;
        const int x0 = left + line.indent * 2 * cw;
        int x = x0;
        for (const JSpan &s : line.spans) {
            int link = -1;
            if (s.link.ok()) {
                JBox b;
                b.kind = JB_LINK;
                b.link = s.link;
                b.tip = s.tip;
                link = int(spans.size());
                spans.push_back(b);
            }
            // word by word, so a line breaks between words
            size_t i = 0;
            while (i < s.text.size()) {
                size_t j = s.text.find(' ', i);
                j = j == std::string::npos ? s.text.size() : j + 1;
                std::string word = s.text.substr(i, j - i);
                i = j;
                int wpx = int(word.size()) * cw;
                if (x + wpx > right && x > x0) {
                    y += line_h;
                    x = x0 + 2 * cw;
                    if (word == " ")
                        continue;
                }
                while (x + wpx > right && word.size() > 1) {
                    const size_t fit = std::max<size_t>(1, size_t((right - x) / cw));
                    put(x, word.substr(0, fit), s.color, link);
                    word.erase(0, fit);
                    y += line_h;
                    x = x0 + 2 * cw;
                    wpx = int(word.size()) * cw;
                }
                put(x, word, s.color, link);
                x += wpx;
            }
        }
        y += line_h;
    }
    L.det_content = y;
    L.links = std::move(spans);
}

bool layout_jr(const JModel &m, JLayout &L) {
    Cells c;
    if (!get_cells(c) || c.dimx < 60 || c.dimy < 24)
        return false;
    L.cw = c.cw;
    L.ch = c.ch;
    L.box = c.ch;
    // the inventory panel's room, so the panels read as one interface
    if (!panel_room(c, L.x, L.y, L.w, L.h))
        return false;
    const int cw = c.cw, ch = c.ch, pad = 3 * cw;
    L.pad = pad;
    L.title_y = 2 * ch + ch / 2 + 2;  // the first row inside the frame (no title: the tab above names the panel)
    const auto button = [&](int kind, int a, int x, int y, const std::string &label) {
        JBox b;
        b.kind = kind;
        b.a = a;
        b.x = x;
        b.y = y;
        b.w = (int(label.size()) + 4) * cw;
        b.h = 3 * ch;  // the game's button art is 3 rows
        b.tip = label;
        L.boxes.push_back(b);
        return b.w;
    };
    // Close in the top right corner, as the inventory has it
    {
        JBox b;
        b.kind = JB_CLOSE;
        b.w = 9 * cw;
        b.h = 3 * ch;
        b.x = L.w - 3 * cw - b.w;
        b.y = L.title_y - ch / 2;
        b.tip = "Close";
        L.boxes.push_back(b);
    }
    L.view = m.view;
    if (m.view == 1) {
        L.bar_y = L.title_y;
        L.hint_y = L.h - 4 * ch;
        map_area(L.w, L.h, cw, ch, L.mx, L.my, L.mw, L.mh);
        int bx = L.pad;
        bx += button(JB_CENTRE, -1, bx, L.bar_y, "Centre on you") + cw;
        {
            JBox b;
            b.kind = JB_ZOOM_OUT;
            b.x = bx;
            b.y = L.bar_y + ch / 2;
            b.w = 2 * cw;
            b.h = 2 * ch;
            L.boxes.push_back(b);
            b.kind = JB_ZOOM_IN;
            b.x = bx + b.w + cw / 2;
            L.boxes.push_back(b);
        }
        const JMapView &v = m.mapv;
        for (const JMapMark &k : v.marks) {
            JBox b;
            b.kind = JB_MARK;
            b.link = k.ref;
            b.tip = k.name;
            b.color = k.color;
            const int cx = L.mx + v.off_x + int((k.x - v.x0) * v.zoom), cy = L.my + v.off_y + int((k.y - v.y0) * v.zoom);
            b.w = std::max(cw + 6, v.zoom);
            b.h = std::max(ch + 6, v.zoom);
            b.x = cx - b.w / 2;
            b.y = cy - b.h;
            b.a = int(L.marks.size());
            if (b.x + b.w > L.mx && b.y + b.h > L.my && b.x < L.mx + L.mw && b.y < L.my + L.mh)
                L.marks.push_back(b);
        }
        L.mv = &m.mapv;
        for (const JMapSite &k : v.sites) {
            JBox b;
            b.kind = JB_MSITE;
            b.link = jr_ref(JK_SITE, k.id);
            b.tip = k.name;
            b.w = b.h = v.zoom;
            b.x = L.mx + v.off_x + (k.x - v.x0) * v.zoom;
            b.y = L.my + v.off_y + (k.y - v.y0) * v.zoom;
            b.a = int(L.sites.size());
            L.sites.push_back(b);
        }
        return true;
    }
    // the tabs (the game's short tabs, 2 rows), on as many rows as they need
    L.tabs_y = L.title_y;
    {
        int x = pad, y = L.tabs_y;
        const int limit = L.w - 3 * cw - 9 * cw - 2 * cw;  // left of Close
        for (int t = 0; t < JT_COUNT; ++t) {
            const size_t n = m.data ? m.data->rows[t].size() : 0;
            const std::string label = std::string(JT_NAMES[t]) + " " + std::to_string(n);
            const int w = game_tab_w(label, cw);
            if (x + w > limit && x > pad) {
                x = pad;
                y += 2 * ch + 2;
            }
            JBox b;
            b.kind = JB_TAB;
            b.a = t;
            b.x = x;
            b.y = y;
            b.w = w;
            b.h = 2 * ch;
            b.tip = label;
            L.boxes.push_back(b);
            x += w;
        }
        L.bar_y = y + 2 * ch + 4;
    }
    L.hint_y = L.h - 4 * ch;
    L.list_x = pad;
    L.list_w = std::max(36 * cw, (L.w - 2 * pad) * 2 / 5);
    // search and sort above the list; back and forward above the page
    {
        const std::string sort = std::string("Sort: ") + JS_NAMES[m.sort];
        JBox s;
        s.kind = JB_SEARCH;
        s.x = L.list_x;
        s.y = L.bar_y + ch / 2;
        s.w = L.list_w - (int(sort.size()) + 5) * cw;
        s.h = 2 * ch;
        L.boxes.push_back(s);
        button(JB_SORT, -1, L.list_x + L.list_w - (int(sort.size()) + 4) * cw, L.bar_y, sort);
    }
    L.det_x = L.list_x + L.list_w + 3 * cw;
    L.det_w = L.w - pad - L.det_x - 3 * cw;  // and the page's scrollbar
    {
        int bx = L.det_x;
        if (m.back)
            bx += button(JB_BACK, -1, bx, L.bar_y, "< Back") + cw;
        if (m.fwd)
            button(JB_FWD, -1, bx, L.bar_y, "Forward >");
    }
    L.list_y = L.bar_y + 3 * ch + 6;
    L.list_h = L.hint_y - ch / 2 - L.list_y;
    L.row_h = 2 * ch + 10;
    L.list_content = int(m.shown.size()) * L.row_h;
    L.list_scroll = std::clamp(jr_list_scroll.load(), 0, std::max(0, L.list_content - L.list_h));
    L.det_y = L.list_y;
    L.det_h = L.list_h;
    wrap_page(m, L);
    L.det_scroll = std::clamp(jr_det_scroll.load(), 0, std::max(0, L.det_content - L.det_h));
    const int dy = L.det_y - L.det_scroll;
    for (JPiece &p : L.pieces)
        p.y += dy;
    L.page_track.y += dy;
    if (L.map_y >= 0)
        L.map_y += dy;
    // each link's boxes: one per piece, all sharing the link's number
    std::vector<JBox> boxed;
    for (const JPiece &p : L.pieces) {
        if (p.link < 0)
            continue;
        JBox b = L.links[p.link];
        b.a = p.link;
        b.x = p.x;
        b.y = p.y - 1;
        b.w = int(p.text.size()) * cw;
        b.h = ch + 3;
        boxed.push_back(b);
    }
    L.links = std::move(boxed);
    return true;
}

// ---------------------------------------------------------------------------
// drawing (render thread)

UiTexture tex_jr, tex_jr_tip;
uint64_t jr_drawn = 0;
std::string jr_drawn_state, jr_tip_text;

// a track box: yellow when the place is known, grey when not; filled when tracked
void paint_track(fpv::Canvas &cv, int x, int y, int s, bool placed, uint32_t tracked, bool hot) {
    const uint32_t c = placed ? J_TRACK : J_UNKNOWN;
    cv.fill(x, y, s, s, fpv::rgb(8, 8, 12));
    cv.frame(x, y, s, s, hot ? UI_WHITE : c, 1);
    if (tracked)  // the marker's own colour, grey while its place is not known
        cv.fill(x + 3, y + 3, s - 6, s - 6, placed ? tracked : J_UNKNOWN);
}

// a rectangle on the page, cut to the page's room
void page_fill(fpv::Canvas &cv, const JLayout &L, int x, int y, int w, int h, uint32_t c) {
    const int y0 = std::max(y, L.det_y), y1 = std::min(y + h, L.det_y + L.det_h);
    if (y1 > y0)
        cv.fill(x, y0, w, y1 - y0, c);
}

void paint_map(fpv::Canvas &cv, const JLayout &L, const JMap &m) {
    if (L.map_y < -100000 || L.map_s <= 0 || m.n <= 0)
        return;
    const int S = L.map_s, x0 = L.det_x;
    const float f = float(S) / m.n;
    for (int py = 0; py < S; ++py) {
        const int y = L.map_y + py;
        if (y < L.det_y || y >= L.det_y + L.det_h || y < 0 || y >= cv.h)
            continue;
        const float fy = py / f;
        const int ty = std::min(m.n - 1, int(fy));
        const int iy = std::min(15, int((fy - ty) * 16));
        for (int px = 0; px < S && x0 + px < cv.w; ++px) {
            const float fx = px / f;
            const int tx = std::min(m.n - 1, int(fx));
            const fpv::Image *t = m.tiles[size_t(ty) * m.n + tx].get();
            uint32_t c = WM_UNKNOWN;
            if (t && t->ok())
                c = t->px[size_t(iy * t->h / 16) * t->w + std::min(15, int((fx - tx) * 16)) * t->w / 16];
            cv.px[size_t(y) * cv.w + x0 + px] = c | 0xFF000000u;
        }
    }
    const int cs = std::max(3, int(f));
    const auto mark = [&](int tx, int ty, int grow, uint32_t c) {
        const int cx = x0 + int((tx + 0.5f) * f), cy = L.map_y + int((ty + 0.5f) * f);
        page_fill(cv, L, cx - cs / 2 - grow, cy - cs / 2 - grow, cs + 2 * grow, cs + 2 * grow, c);
    };
    mark(m.tx, m.ty, 2, fpv::rgb(0, 0, 0));
    mark(m.tx, m.ty, 1, J_TRACK);
    mark(m.ax, m.ay, 1, fpv::rgb(0, 0, 0));
    mark(m.ax, m.ay, 0, UI_WHITE);
    page_fill(cv, L, x0, L.map_y, S, 1, INV_EDGE);
    page_fill(cv, L, x0, L.map_y + S - 1, S, 1, INV_EDGE);
    page_fill(cv, L, x0, L.map_y, 1, S, INV_EDGE);
    page_fill(cv, L, x0 + S - 1, L.map_y, 1, S, INV_EDGE);
}

// an exclamation mark standing on its square: dark outline, gold, white when hovered
void paint_bang(fpv::Canvas &cv, int cx, int bottom, int size, bool hot, int cw, int ch, uint32_t color = 0) {
    const int w = std::max(3, size / 4), stem = size * 2 / 3, gap = std::max(2, size / 8);
    const int x = cx - w / 2, top = bottom - size;
    const uint32_t c = hot ? UI_WHITE : color ? color : ART_GOLD;
    cv.fill(x - 1, top - 1, w + 2, stem + 2, fpv::rgb(0, 0, 0));
    cv.fill(x - 1, top + stem + gap - 1, w + 2, w + 2, fpv::rgb(0, 0, 0));
    cv.fill(x, top, w, stem, c);
    cv.fill(x, top + stem + gap, w, w, c);
    (void)cw;
    (void)ch;
}

void paint_map_view(fpv::Canvas &cv, const JLayout &L, const JModel &m, const JBox &hover) {
    const JMapView &v = m.mapv;
    const int cw = L.cw, ch = L.ch;
    cv.fill(L.mx, L.my, L.mw, L.mh, WM_UNKNOWN);
    const int z = std::max(1, v.zoom);
    for (int j = 0; j < v.rows; ++j)
        for (int i = 0; i < v.cols; ++i) {
            const fpv::Image *t = v.tiles[size_t(j) * v.cols + i].get();
            if (!t || !t->ok())
                continue;
            const int x0 = L.mx + v.off_x + i * z, y0 = L.my + v.off_y + j * z;
            for (int yy = 0; yy < z; ++yy) {
                const int y = y0 + yy;
                if (y < L.my || y >= L.my + L.mh || y >= cv.h)
                    continue;
                const uint32_t *src = &t->px[size_t(yy * t->h / z) * t->w];
                for (int xx = 0; xx < z; ++xx) {
                    const int x = x0 + xx;
                    if (x < L.mx || x >= L.mx + L.mw || x >= cv.w)
                        continue;
                    cv.px[size_t(y) * cv.w + x] = src[xx * t->w / z] | 0xFF000000u;
                }
            }
        }
    // tracked regions: their squares tinted in the marker's colour, stronger under the pointer
    for (int j = 0; j < v.rows && !v.areas.empty(); ++j)
        for (int i = 0; i < v.cols; ++i) {
            const int a = v.area_of[size_t(j) * v.cols + i];
            if (a < 0 || size_t(a) >= v.areas.size())
                continue;
            const uint32_t c = v.areas[a].color;
            const int alpha = hover.kind == JB_MAREA && hover.a == a ? 120 : 80;
            const int x0 = L.mx + v.off_x + i * z, y0 = L.my + v.off_y + j * z;
            for (int y = std::max(y0, L.my); y < std::min({y0 + z, L.my + L.mh, cv.h}); ++y)
                for (int x = std::max(x0, L.mx); x < std::min({x0 + z, L.mx + L.mw, cv.w}); ++x) {
                    uint32_t &d = cv.px[size_t(y) * cv.w + x];
                    const auto mix = [&](unsigned s, unsigned o) { return (s * alpha + o * (255 - alpha)) / 255; };
                    d = fpv::rgb(mix(fpv::px_r(c), fpv::px_r(d)), mix(fpv::px_g(c), fpv::px_g(d)), mix(fpv::px_b(c), fpv::px_b(d)));
                }
        }
    // the known site under the pointer: framed
    for (const JBox &b : L.sites)
        if (hover.kind == JB_MSITE && hover.a == b.a) {
            const int x0 = std::max(b.x, L.mx), y0 = std::max(b.y, L.my);
            const int x1 = std::min(b.x + b.w, L.mx + L.mw), y1 = std::min(b.y + b.h, L.my + L.mh);
            if (x1 > x0 && y1 > y0)
                cv.frame(x0, y0, x1 - x0, y1 - y0, UI_WHITE, 1);
        }
    // you: the game's travel marker, else a white square
    if (v.you_x >= 0) {
        const int cx = L.mx + v.off_x + int((v.you_x - v.x0) * z), cy = L.my + v.off_y + int((v.you_y - v.y0) * z);
        const int s = std::max(12, z);
        if (cx >= L.mx && cy >= L.my && cx < L.mx + L.mw && cy < L.my + L.mh) {
            if (v.you_icon && v.you_icon->ok())
                cv.blitFit(*v.you_icon, cx - s / 2, cy - s / 2, s, s);
            else
                cv.frame(cx - s / 2, cy - s / 2, s, s, UI_WHITE, 2);
        }
    }
    for (const JBox &b : L.marks)
        paint_bang(cv, b.x + b.w / 2, b.y + b.h, std::max(ch, std::min(b.h, 2 * ch)),
                   hover.kind == JB_MARK && hover.a == b.a, cw, ch, b.color);
    cv.frame(L.mx - 1, L.my - 1, L.mw + 2, L.mh + 2, ART_GOLD, 1);
}

void paint_jr(const JModel &m, const JLayout &L, const JBox &hover, fpv::Canvas &cv) {
    const int cw = L.cw, ch = L.ch;
    cv.resize(L.w, L.h);
    const std::shared_ptr<const GameArt> art_hold = get_game_art();
    const GameArt *art = art_hold.get();
    paint_game_frame(cv, art, L.w, L.h, cw, ch);

    for (const JBox &b : L.boxes) {
        const bool hot = hover.kind == b.kind && hover.a == b.a;
        if (b.kind == JB_SEARCH) {
            paint_game_filter(cv, art, b.x, b.y, b.w, b.h, cw, ch);
            const int room = b.w / cw - 6;
            std::string t = m.filter.empty() ? "Type to search both names" : m.filter + "_";
            if (int(t.size()) > room)
                t = t.substr(t.size() - std::max(0, room));
            panel_text(cv, b.x + cw + cw / 2, b.y + (b.h - ch) / 2, t, m.filter.empty() ? UI_DIM : UI_WHITE, cw, ch);
            continue;
        }
        if (b.kind == JB_TAB) {
            paint_game_tab(cv, art, b.x, b.y, b.tip, b.a == m.tab, hot, true, cw, ch);
            continue;
        }
        if (b.kind == JB_ZOOM_IN || b.kind == JB_ZOOM_OUT) {
            paint_game_zoom(cv, art, b.kind == JB_ZOOM_IN, hot, b.x, b.y, cw, ch);
            continue;
        }
        paint_game_button(cv, art, b.x, b.y, b.w, b.h, b.tip, hot, false, cw, ch);
    }
    if (m.view == 1) {
        paint_map_view(cv, L, m, hover);
        panel_text(cv, L.pad, L.hint_y, "Click a site or a marker to open it in the journal.", UI_DIM, cw, ch);
        return;
    }

    // the list
    const std::vector<JRow> *rows = m.data ? &m.data->rows[m.tab] : nullptr;
    if (rows && m.shown.empty())
        panel_text(cv, L.list_x + cw, L.list_y + 4, m.filter.empty() ? "Nothing known yet." : "Nothing matches.", UI_DIM, cw, ch);
    const int tx = L.list_x + 4 + L.box + cw;  // text starts after the track box
    const int right = L.list_x + L.list_w - 2 * cw - 2;  // the scrollbar's 2 cells at the right
    const int room = (right - cw - tx) / cw;
    for (size_t k = 0; rows && k < m.shown.size(); ++k) {
        const int y = L.list_y + int(k) * L.row_h - L.list_scroll;
        if (y < L.list_y || y + L.row_h > L.list_y + L.list_h)
            continue;  // only whole rows
        const JRow &r = (*rows)[m.shown[k]];
        const bool sel = int(k) == m.sel_row, hot = hover.kind == JB_ROW && hover.a == int(k);
        cv.fill(L.list_x, y, L.list_w - 2 * cw - 2, L.row_h - 2, sel ? fpv::rgb(96, 70, 26) : fpv::rgb(46, 42, 48));
        if (sel)
            cv.frame(L.list_x, y, L.list_w - 2 * cw - 2, L.row_h - 2, ART_GOLD, 1);
        if (hot)
            cv.frame(L.list_x, y, L.list_w - 2 * cw - 2, L.row_h - 2, UI_YELLOW, 1);
        paint_track(cv, L.list_x + 4, y + 4, L.box, r.placed, m.color_of(r.ref),
                    hover.kind == JB_TRACK && hover.a == int(k));
        const int wroom = std::max(0, room - int(r.where.size()) - 1);
        std::string a = r.english, b = r.native;
        if (int(a.size()) > wroom)
            a = a.substr(0, std::max(0, wroom - 1)) + "~";
        const int kroom = std::max(0, room - int(r.kind.size()) - 1);
        if (int(b.size()) > kroom)
            b = b.substr(0, std::max(0, kroom - 1)) + "~";
        panel_text(cv, tx, y + 4, a, sel ? UI_YELLOW : UI_WHITE, cw, ch);
        panel_text(cv, right - cw - int(r.where.size()) * cw, y + 4, r.where, J_HEAD, cw, ch);
        panel_text(cv, tx, y + 4 + ch + 2, b, UI_DIM, cw, ch);
        panel_text(cv, right - cw - int(r.kind.size()) * cw, y + 4 + ch + 2, r.kind, fpv::rgb(150, 150, 120), cw, ch);
    }
    // its scrollbar: a plain handle at the right edge
    if (L.list_content > L.list_h) {
        const int track = L.list_h - 2 * ch;
        const int len = std::max(2 * ch, track * L.list_h / L.list_content);
        const int at = (track - len) * L.list_scroll / std::max(1, L.list_content - L.list_h);
        paint_game_scrollbar(cv, art, L.list_x + L.list_w - 2 * cw, L.list_y, L.list_h, at, len, cw, ch);
    }

    // the page
    if (!m.detail.ref.ok())
        panel_text(cv, L.det_x, L.det_y + 4, "Pick an entry on the left.", UI_DIM, cw, ch);
    const JBox &pt = L.page_track;
    if (pt.kind == JB_TRACK && pt.y >= L.det_y && pt.y + pt.h <= L.det_y + L.det_h)
        paint_track(cv, pt.x, pt.y, pt.w, m.detail.placed, m.color_of(m.detail.ref), hover.kind == JB_TRACK && hover.a == -1);
    if (L.map_y >= -100000 && L.map_s > 0)
        paint_map(cv, L, m.detail.map);
    for (const JPiece &p : L.pieces) {
        if (p.y < L.det_y || p.y + ch > L.det_y + L.det_h)
            continue;
        uint32_t color = p.color;
        if (p.link >= 0 && !p.text.empty()) {
            // the hovered link, all its pieces
            if (hover.kind == JB_LINK && hover.a == p.link)
                color = UI_YELLOW;
            cv.fill(p.x, p.y + ch, int(p.text.size()) * cw - (p.text.back() == ' ' ? cw : 0), 1, color);
        }
        panel_text(cv, p.x, p.y, p.text, color, cw, ch);
    }
    if (L.det_content > L.det_h) {
        const int track = L.det_h - 2 * ch;
        const int len = std::max(2 * ch, track * L.det_h / L.det_content);
        const int at = (track - len) * L.det_scroll / std::max(1, L.det_content - L.det_h);
        paint_game_scrollbar(cv, art, L.det_x + L.det_w + cw / 2, L.det_y, L.det_h, at, len, cw, ch);
    }

    panel_text(cv, L.pad, L.hint_y,
               "Click a name to open it, a box to track it on the compass. Right click: back. Type to search, Esc clears.",
               UI_DIM, cw, ch);
    (void)art;
}

void destroy_jr_textures() {
    destroy_leaf_textures();
    tex_jr.destroy();
    tex_places_btn.destroy();
    places_btn_drawn = -1;
    tex_jr_tip.destroy();
    jr_drawn = 0;
    jr_drawn_state.clear();
    jr_tip_text.clear();
}

void draw_journal(SDL_Renderer *sdl) {
    const std::shared_ptr<const JModel> m = jr_up ? get_jr() : nullptr;
    if (!m || !ui_font_ready)
        return;
    JLayout L;
    if (!layout_jr(*m, L))
        return;
    int px = -1, py = -1;
    if (debug_px.load() >= 0) {
        px = debug_px.load();
        py = debug_py.load();
    } else {
        sdl_get_mouse_state(&px, &py);
    }
    const JBox hover = L.at(px, py);
    const std::string state = std::to_string(hover.kind) + "," + std::to_string(hover.a) + "," +
                              std::to_string(L.list_scroll) + "," + std::to_string(L.det_scroll) + "," +
                              std::to_string(L.w) + "," + std::to_string(L.h);
    if (m->version != jr_drawn || state != jr_drawn_state) {
        fpv::Canvas cv;
        paint_jr(*m, L, hover, cv);
        if (!tex_jr.upload(sdl, cv.px.data(), cv.w, cv.h))
            return;
        jr_drawn = m->version;
        jr_drawn_state = state;
    }
    tex_jr.draw(sdl, L.x, L.y);
    draw_leaf_tabs(sdl, L.x, L.y, m->view == 1 ? PT_MAP : PT_JOURNAL);

    // a link's other name and what it is, next to the pointer
    std::string tip =
        hover.kind == JB_LINK || hover.kind == JB_MARK || hover.kind == JB_MSITE || hover.kind == JB_MAREA ? hover.tip : "";
    if (hover.kind == JB_ROW && m->data && hover.a >= 0 && hover.a < int(m->shown.size())) {
        const JRow &r = m->data->rows[m->tab][m->shown[hover.a]];
        tip = r.english + (r.native.empty() ? "" : " / " + r.native);
    }
    if (hover.kind == JB_TRACK) {
        bool placed = m->detail.placed;
        if (hover.a >= 0 && m->data && hover.a < int(m->shown.size()))
            placed = m->data->rows[m->tab][m->shown[hover.a]].placed;
        tip = placed ? "Track on the compass" : "Where it is is not known: tracked, it shows once it is";
    }
    if (!tip.empty()) {
        if (tip != jr_tip_text) {
            fpv::Canvas cv;
            cv.resize(int(tip.size()) * L.cw + 12, L.ch + 8);
            cv.fill(0, 0, cv.w, cv.h, fpv::rgb(12, 12, 16));
            cv.frame(0, 0, cv.w, cv.h, UI_GOLD, 1);
            panel_text(cv, 6, 4, tip, UI_WHITE, L.cw, L.ch);
            if (!tex_jr_tip.upload(sdl, cv.px.data(), cv.w, cv.h))
                return;
            jr_tip_text = tip;
        }
        Cells c;
        get_cells(c);
        const int right = c.ox + c.dimx * c.cw, low = c.oy + c.dimy * c.ch;
        int tx = px + 18, ty = py + 20;
        if (tx + tex_jr_tip.w > right)
            tx = px - 8 - tex_jr_tip.w;
        if (ty + tex_jr_tip.h > low)
            ty = py - 8 - tex_jr_tip.h;
        tex_jr_tip.draw(sdl, tx, ty);
    }
}

// ---------------------------------------------------------------------------
// input and the frame (game thread)

// The game's own journal came up (its button): it is closed the way the game
// closes it (Esc fed to it), and once it is gone the panel opens in its place,
// on the same tab and at the site asked for. Called from the plugin's update,
// never from a draw.
//
// Not Screen::dismiss: that removed the journal without its own closing steps
// and the game crashed in its drawing a moment later (2026-11-15, a black
// screen, then a write through a null pointer on the main thread; the game's
// journal draws a world map of its own there).
struct JPending {
    bool on = false;
    int tab = -1;
    JRef at;
    int tries = 0;
} jr_pending;

void journal_takeover() {
    auto log = strict_virtual_cast<df::viewscreen_adventure_logst>(Gui::getCurViewscreen(true));
    if (!log) {
        jr.game_wanted = false;
        if (jr_pending.on) {
            jr_pending.on = false;
            jr_open(jr_pending.tab, jr_pending.at);
        }
        return;
    }
    if (jr.game_wanted || !settings.journal_panel || !ui_font_ready)
        return;
    if (!jr_pending.on) {
        jr_pending = JPending();
        jr_pending.on = true;
        using M = df::adventure_log_mode;
        switch (log->mode) {
        case M::EVENTS: jr_pending.tab = JT_RUMORS; break;
        case M::AGREEMENTS: jr_pending.tab = JT_QUESTS; break;
        case M::SITES: jr_pending.tab = JT_SITES; break;
        case M::PEOPLE: jr_pending.tab = JT_PEOPLE; break;
        case M::ENTITIES: jr_pending.tab = JT_CIVS; break;
        case M::SUBREGIONS: jr_pending.tab = JT_REGIONS; break;
        case M::BESTIARY: jr_pending.tab = JT_BEASTS; break;
        case M::ARTIFACTS: jr_pending.tab = JT_ARTIFACTS; break;
        default: break;
        }
        const int32_t site = journal_site.exchange(-1);
        if (site >= 0)
            jr_pending.at = jr_ref(JK_SITE, site);
    }
    // a key a frame; if it will not close, it is left to the player
    if (++jr_pending.tries > 10) {
        jr.game_wanted = true;
        jr_pending.on = false;
        return;
    }
    std::set<df::interface_key> keys{df::interface_key::LEAVESCREEN};
    log->feed(&keys);
}

// The game's journal key or a click on its Journal button, on the plain
// screen: the panel opens and the game's journal never does. Its coming up
// and going away a frame later (journal_takeover) switched the hidden map's
// zoom back and forth (restore_zoom, zoom_for_capture), and each switch makes
// the main thread reallocate every array of the map viewport; the game crashed
// twice that way (2026-11-15: a black screen, then memset into a null array of
// gps->main_viewport on the main thread), and once froze with a corrupted heap.
// On the travel map its own journal key and button (the book at the bottom left)
// do the same.
bool journal_key(std::set<df::interface_key> *input) {
    if (!settings.journal_panel || !ui_font_ready || !game)
        return false;
    const bool travel = travel_map_ours();
    if (!travel && !plain_adventure_focus())
        return false;
    using H = df::main_hover_instruction;
    const H hover = game->main_interface.current_hover;
    const bool key = input && (input->count(df::interface_key::A_LOG) ||
                               (travel && input->count(df::interface_key::A_TRAVEL_LOG)));
    const bool click = enabler->mouse_lbut && (hover == H::ADVENTURE_LOG || (travel && hover == H::ADVENTURE_TRAVEL_LOG));
    if (!key && !click)
        return false;
    if (click) {
        enabler->mouse_lbut = 0;
        enabler->mouse_lbut_down = 0;
    }
    jr_open(-1, JRef());
    return true;
}

// the game's journal is up only to be closed again: the view keeps its zoom meanwhile
bool journal_being_replaced() {
    return settings.journal_panel && !jr.game_wanted &&
           strict_virtual_cast<df::viewscreen_adventure_logst>(Gui::getCurViewscreen(true)) != nullptr;
}

// what the panel does with a key or click: 0 not up, 1 taken (the panel takes
// everything while it is up), 2 hand the game's journal key on
int journal_input(std::set<df::interface_key> *input) {
    if (!jr_up)
        return 0;
    using namespace df::enums::interface_key;
    const std::shared_ptr<const JModel> m = get_jr();
    JLayout L;
    const bool laid = m && layout_jr(*m, L);
    int px = -1, py = -1;
    pointer_pixels(px, py);
    if (enabler->mouse_rbut) {
        enabler->mouse_rbut = 0;
        enabler->mouse_rbut_down = 0;
        if (jr.view == 1) {
            jr.view = 0;
            jr_publish();
        } else if (jr.hpos > 0) {
            --jr.hpos;
            jr_go(jr.history[jr.hpos], false);
        } else {
            jr_close();
        }
        return 1;
    }
    if (enabler->mouse_lbut) {
        enabler->mouse_lbut = 0;
        enabler->mouse_lbut_down = 0;
        if (!laid)
            return 1;
        const JBox hit = L.at(px, py);
        switch (hit.kind) {
        case JB_LEAF:
            if (hit.a == PT_INVENTORY) {
                jr_close();
                panel_switch = PT_INVENTORY;
                panel_switch_tries = 0;
            } else if ((hit.a == PT_MAP) != (jr.view == 1)) {
                jr.view = hit.a == PT_MAP ? 1 : 0;
                jr_publish();
            }
            break;
        case JB_MAP:
        case JB_MARK:
        case JB_MSITE:
        case JB_MAREA:
            // a press: a drag moves the map, a click on a mark opens its page (journal_frame)
            jr.map_press = true;
            jr.map_moved = false;
            jr.press_x = px;
            jr.press_y = py;
            jr.press_cx = jr.map_cx;
            jr.press_cy = jr.map_cy;
            jr.press_mark = hit.kind == JB_MARK || hit.kind == JB_MSITE || hit.kind == JB_MAREA ? hit.link : JRef();
            break;
        case JB_LBAR:
        case JB_DBAR: {
            // held: the handle follows the pointer (journal_frame); taken where it was pressed
            const bool list = hit.kind == JB_LBAR;
            int at, len;
            jr_handle(list ? L.list_h : L.det_h, list ? L.list_content : L.det_content,
                      list ? L.list_scroll : L.det_scroll, L.ch, at, len);
            const int pos = py - L.y - (list ? L.list_y : L.det_y) - L.ch;
            jr.bar_press = list ? 1 : 2;
            jr.bar_grab = pos >= at && pos < at + len ? pos - at : len / 2;
            const int s = jr_scroll_for(list ? L.list_h : L.det_h, list ? L.list_content : L.det_content,
                                        pos - jr.bar_grab, L.ch);
            (list ? jr_list_scroll : jr_det_scroll) = s;
            break;
        }
        case JB_CENTRE:
            jr.map_cx = jr.map_cy = -1;
            jr_publish();
            break;
        case JB_ZOOM_IN:
        case JB_ZOOM_OUT: {
            const int n = int(sizeof(MAP_ZOOMS) / sizeof(MAP_ZOOMS[0]));
            int k = 0;
            while (k + 1 < n && MAP_ZOOMS[k] < jr.map_zoom)
                ++k;
            k = std::clamp(k + (hit.kind == JB_ZOOM_IN ? 1 : -1), 0, n - 1);
            jr.map_zoom = MAP_ZOOMS[k];
            jr_publish();
            break;
        }
        case JB_PAGEMAP: {
            double x, y, rad;
            if (entry_pos(jr.sel, x, y, rad)) {
                jr.map_cx = x / 768.0;
                jr.map_cy = y / 768.0;
            }
            jr.view = 1;
            jr_publish();
            break;
        }
        case JB_TAB:
            if (hit.a != jr.tab) {
                jr.tab = hit.a;
                jr.filter.clear();
                jr_list_scroll = 0;
                jr_publish();
                const std::shared_ptr<const JModel> n = get_jr();
                if (n && n->sel_row < 0 && !n->shown.empty())
                    jr_go(n->data->rows[n->tab][n->shown[0]].ref);
            }
            break;
        case JB_ROW:
            if (hit.a >= 0 && hit.a < int(m->shown.size()))
                jr_go(m->data->rows[m->tab][m->shown[hit.a]].ref);
            break;
        case JB_TRACK:
            if (hit.a == -1)
                toggle_tracked(m->detail.ref);
            else if (hit.a >= 0 && hit.a < int(m->shown.size()))
                toggle_tracked(m->data->rows[m->tab][m->shown[hit.a]].ref);
            jr_publish();
            break;
        case JB_LINK: jr_go(hit.link); break;
        case JB_SORT:
            jr.sort = (jr.sort + 1) % JS_COUNT;
            jr_publish();
            break;
        case JB_BACK:
            if (jr.hpos > 0) {
                --jr.hpos;
                jr_go(jr.history[jr.hpos], false);
            }
            break;
        case JB_FWD:
            if (jr.hpos + 1 < int(jr.history.size())) {
                ++jr.hpos;
                jr_go(jr.history[jr.hpos], false);
            }
            break;
        case JB_CLOSE: jr_close(); break;
        case JB_GAME:
            jr_close();
            jr.game_wanted = true;
            return 2;
        default: break;
        }
        return 1;
    }
    if (!input)
        return 1;
    if (jr.view == 1) {
        if (input->count(LEAVESCREEN) || input->count(A_LOG) || input->count(A_TRAVEL_LOG))
            jr_close();
        return 1;
    }
    // typing: printable keys search, Backspace takes one back
    bool typed = false;
    for (df::interface_key k : *input) {
        const int c = int(k) - int(STRING_A000);
        if (c >= 32 && c < 256 && int(k) <= int(STRING_A255)) {
            if (jr.filter.size() < 40)
                jr.filter += char(c);
            typed = true;
            break;
        }
    }
    if (!typed && (input->count(STRING_A000) || input->count(STRING_A008))) {
        if (!jr.filter.empty())
            jr.filter.pop_back();
        typed = true;
    }
    if (typed) {
        jr_list_scroll = 0;
        jr_publish();
        return 1;
    }
    if (input->count(LEAVESCREEN)) {
        if (!jr.filter.empty()) {
            jr.filter.clear();
            jr_publish();
        } else {
            jr_close();
        }
        return 1;
    }
    if (input->count(A_LOG) || input->count(A_TRAVEL_LOG)) {
        jr_close();
        return 1;
    }
    // up and down move along the list
    const bool up = input->count(STANDARDSCROLL_UP) > 0, down = input->count(STANDARDSCROLL_DOWN) > 0;
    if ((up || down) && m && !m->shown.empty()) {
        int k = m->sel_row < 0 ? 0 : m->sel_row + (down ? 1 : -1);
        k = std::clamp(k, 0, int(m->shown.size()) - 1);
        if (laid) {
            const int y = k * L.row_h;
            if (y < L.list_scroll)
                jr_list_scroll = y;
            else if (y + L.row_h > L.list_scroll + L.list_h)
                jr_list_scroll = y + L.row_h - L.list_h;
        }
        jr_go(m->data->rows[m->tab][m->shown[k]].ref);
    }
    return 1;
}

// once a frame, after the game has drawn: whether the panel is up, the wheel
void journal_frame(bool on_top) {
    df::unit *adv = World::getAdventurer();
    // on the travel map the view is off and the adventurer's unit gone; the panel works there too
    const bool travel = !adv && travel_map_ours();
    if ((adv || travel) && gps)
        load_game_art();
    // under the game's inventory (closing for a switch to here, or opened over the
    // journal) the panel waits and comes back
    if (jr.want && inv_up) {
        jr_up = false;
        cover_x1 = -1;
        return;
    }
    const bool here = (active && showing && adv) || travel_map_ours();
    const bool usable = jr.want && here && settings.journal_panel && game && gps && ui_font_ready && on_top &&
                        !game->main_interface.view_sheets.open;
    if (!usable) {
        if (jr_up)
            jr_close();
        jr.want = false;
        cover_x1 = -1;
        return;
    }
    jr_up = true;
    static int wheel_seen = 0;
    const int wheel_now = wheel_total.load();
    const int turned = wheel_now - wheel_seen;
    wheel_seen = wheel_now;
    const std::shared_ptr<const JModel> m = get_jr();
    JLayout L;
    if (!m || !layout_jr(*m, L))
        return;
    cover_x0 = L.x;
    cover_y0 = L.y - L.ch;
    cover_x1 = L.x + L.w;
    cover_y1 = L.y + L.h;
    int px = -1, py = -1;
    pointer_pixels(px, py);
    if (jr.view == 1) {
        if (jr.map_press) {
            const bool down = debug_px.load() >= 0 || !sdl_get_mouse_state
                                  ? enabler->mouse_lbut_down != 0
                                  : (sdl_get_mouse_state(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
            const int dx = px - jr.press_x, dy = py - jr.press_y;
            if (std::abs(dx) + std::abs(dy) > 4)
                jr.map_moved = true;
            if (down) {
                if (jr.map_moved) {
                    const double cx = jr.press_cx - double(dx) / jr.map_zoom, cy = jr.press_cy - double(dy) / jr.map_zoom;
                    if (std::abs(cx - jr.map_cx) * jr.map_zoom >= 1 || std::abs(cy - jr.map_cy) * jr.map_zoom >= 1) {
                        jr.map_cx = cx;
                        jr.map_cy = cy;
                        jr_publish();
                    }
                }
            } else {
                jr.map_press = false;
                if (!jr.map_moved && jr.press_mark.ok())
                    jr_go(jr.press_mark);
            }
        }
        if (turned) {
            const int n = int(sizeof(MAP_ZOOMS) / sizeof(MAP_ZOOMS[0]));
            int k = 0;
            while (k + 1 < n && MAP_ZOOMS[k] < jr.map_zoom)
                ++k;
            k = std::clamp(k + (turned > 0 ? 1 : -1), 0, n - 1);
            if (MAP_ZOOMS[k] != jr.map_zoom) {
                jr.map_zoom = MAP_ZOOMS[k];
                jr_publish();
            }
        }
    } else if (jr.bar_press) {
        const bool down = debug_px.load() >= 0 || !sdl_get_mouse_state
                              ? enabler->mouse_lbut_down != 0
                              : (sdl_get_mouse_state(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        if (!down) {
            jr.bar_press = 0;
        } else {
            const bool list = jr.bar_press == 1;
            const int pos = py - L.y - (list ? L.list_y : L.det_y) - L.ch;
            const int s = jr_scroll_for(list ? L.list_h : L.det_h, list ? L.list_content : L.det_content,
                                        pos - jr.bar_grab, L.ch);
            (list ? jr_list_scroll : jr_det_scroll) = s;
        }
    } else if (turned) {
        const int rx = px - L.x;
        if (rx < L.det_x - L.cw)
            jr_list_scroll = std::clamp(L.list_scroll - turned * L.row_h, 0, std::max(0, L.list_content - L.list_h));
        else
            jr_det_scroll = std::clamp(L.det_scroll - turned * 3 * (L.ch + 3), 0, std::max(0, L.det_content - L.det_h));
    }
    // the game's text under the panel is emptied, so none shows through
    Cells c;
    if (get_cells(c)) {
        const int x0 = std::max(0, (L.x - c.ox) / c.cw), x1 = std::min(c.dimx - 1, (L.x + L.w - 1 - c.ox) / c.cw);
        const int y0 = std::max(0, (L.y - c.oy) / c.ch), y1 = std::min(c.dimy - 1, (L.y + L.h - 1 - c.oy) / c.ch);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                gps->screen[(x * gps->dimy + y) * 8] = 0;
    }
}
