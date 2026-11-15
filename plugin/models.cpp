#include "models.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "media.h"
#include "scene.h"

namespace fpv {

const char *const MODEL_FILES[SH_COUNT] = {
    "table", "chair", "bed", "chest", "cabinet", "coffin", "statue", "armor_stand", "weapon_rack", "slab",
    "hatch", "well", "pedestal", "pillar", "fortification", "bookcase", nullptr, nullptr,
    "sign", "altar", "stairs", "stairs_bottom",
};

namespace {

namespace fs = std::filesystem;

// the built-in models: the .obj and .mtl files of media/models as they were
// when the plugin was built (build.ps1 writes this from them)
struct BuiltIn {
    const char *name, *text;
};
#include "models_builtin.inc"

const char *const PATTERN_NAMES[] = {"blocks", "smooth", "rough", "soil",  "planks", "bark",  "leaves",
                                     "grass",  "ice",    "grit",  "concrete", "logs", "slabs"};
static_assert(sizeof(PATTERN_NAMES) / sizeof(PATTERN_NAMES[0]) == size_t(Pattern::Count),
              "a name for every pattern");

std::atomic<uint32_t> image_serial{0};

std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}

std::string trim(const std::string &s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool read_file(const fs::path &path, std::string &out) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return false;
    std::ifstream f(path, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

const char *built_in(const std::string &name) {
    for (const BuiltIn &b : BUILT_IN_MODELS)
        if (lower(name) == b.name)
            return b.text;
    return nullptr;
}

// Blender writes colours as they are stored (linear light); pictures and the
// rest of the plugin use screen colours
int to_screen(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    const float s = linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1 / 2.4f) - 0.055f;
    return int(std::lround(s * 255));
}

// one model file being read: from the player's folder, or built in
struct Reader {
    Models &out;
    std::string dir;          // the models folder; empty for the built-in ones
    std::string file;         // for messages
    std::map<std::string, ModelMat> mats;
    std::map<std::string, int> images;   // picture path -> index in out.images

    void problem(const std::string &what) { out.problems.push_back(file + ": " + what); }

    int image(const std::string &mtl_dir, std::string path) {
        path = trim(path);
        if (path.empty() || dir.empty())
            return -1;
        // Blender may write an absolute path, one relative to the .mtl, or just the name
        std::error_code ec;
        fs::path found;
        for (const fs::path &p : {fs::path(path), fs::path(mtl_dir) / path, fs::path(dir) / fs::path(path).filename()})
            if (fs::is_regular_file(p, ec)) {
                found = p;
                break;
            }
        if (found.empty()) {
            problem("picture not found: " + path);
            return -1;
        }
        const std::string key = found.lexically_normal().string();
        const auto it = images.find(key);
        if (it != images.end())
            return it->second;
        Image img;
        if (!load_png(key, img)) {
            problem("cannot read the picture " + path + " (PNG only)");
            return images[key] = -1;
        }
        // shrunk to 32 x 32: every pixel the mean of the area it stands for
        ModelImage mi;
        mi.serial = ++image_serial;
        for (int y = 0; y < TEX; ++y)
            for (int x = 0; x < TEX; ++x) {
                const int x0 = x * img.w / TEX, x1 = std::max(x0 + 1, (x + 1) * img.w / TEX);
                const int y0 = y * img.h / TEX, y1 = std::max(y0 + 1, (y + 1) * img.h / TEX);
                uint64_t r = 0, g = 0, b = 0, n = 0;
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx) {
                        const uint32_t c = img.px[size_t(yy) * img.w + xx];
                        r += px_r(c);
                        g += px_g(c);
                        b += px_b(c);
                        ++n;
                    }
                mi.px[size_t(y) * TEX + x] = rgb(int(r / n), int(g / n), int(b / n));
            }
        out.images.push_back(mi);
        return images[key] = int(out.images.size() - 1);
    }

    // a material library: its own file in the folder, else the built-in one of that name
    void mtllib(const std::string &name) {
        std::string text, mtl_dir;
        if (!dir.empty() && read_file(fs::path(dir) / name, text)) {
            mtl_dir = dir;
        } else if (const char *b = built_in(name)) {
            text = b;
        } else {
            problem("material file not found: " + name);
            return;
        }
        std::istringstream in(text);
        std::string line, current;
        std::map<std::string, std::string> maps;
        std::map<std::string, uint32_t> kd;
        while (std::getline(in, line)) {
            line = trim(line);
            std::istringstream w(line);
            std::string key;
            w >> key;
            if (key == "newmtl") {
                std::getline(w, current);
                current = trim(current);
                kd[current] = 0;
            } else if (key == "Kd" && !current.empty()) {
                float r = 1, g = 1, b = 1;
                w >> r >> g >> b;
                kd[current] = rgb(to_screen(r), to_screen(g), to_screen(b));
            } else if (key == "map_Kd" && !current.empty()) {
                std::string rest;
                std::getline(w, rest);
                maps[current] = rest;
            }
        }
        for (const auto &[name, color] : kd) {
            ModelMat m;
            const std::string l = lower(name);
            const auto map = maps.find(name);
            const int img = map == maps.end() ? -1 : image(mtl_dir, map->second);
            if (l.rfind("picture", 0) == 0) {
                m.kind = ModelMat::Picture;
            } else if (l.rfind("own", 0) == 0) {
                m.kind = img >= 0 ? ModelMat::Image : ModelMat::Own;
                m.own_tint = true;
            } else if (img >= 0) {
                m.kind = ModelMat::Image;
            } else {
                m.kind = ModelMat::Paint;
                for (int i = 0; i < int(Pattern::Count); ++i)
                    if (l.rfind(PATTERN_NAMES[i], 0) == 0)
                        m.pattern = Pattern(i);
            }
            m.image = img;
            m.color = color;
            mats[name] = m;
        }
    }

    ModelMat mat(const std::string &name) {
        const auto it = mats.find(name);
        if (it != mats.end())
            return it->second;
        ModelMat m;
        const std::string l = lower(name);
        m.kind = l.rfind("picture", 0) == 0 ? ModelMat::Picture : ModelMat::Own;
        if (!name.empty() && l.rfind("own", 0) != 0 && m.kind != ModelMat::Picture)
            problem("material " + name + " is not in the .mtl file; the thing's own material is used");
        return m;
    }

    struct Face {
        std::vector<int> v;
        std::string mat;
    };
    struct Object {
        std::string name;
        std::vector<Face> faces;
    };

    Model read(const std::string &text) {
        Model model;
        std::vector<std::array<float, 3>> verts;   // u, v, z
        std::vector<Object> objects;
        const bool has_o = text.find("\no ") != std::string::npos || text.rfind("o ", 0) == 0;
        std::istringstream in(text);
        std::string line, usemtl;
        while (std::getline(in, line)) {
            line = trim(line);
            std::istringstream w(line);
            std::string key;
            w >> key;
            if (key == "v") {
                float x = 0, y = 0, z = 0;
                w >> x >> y >> z;
                // OBJ as Blender writes it (Y up, -Z forward): Blender's X is east,
                // its Y north, its Z up; the tile is -0.5..0.5 around the origin
                verts.push_back({x + 0.5f, 0.5f + z, y});
            } else if (key == "o" || (key == "g" && !has_o)) {
                std::string name;
                std::getline(w, name);
                objects.push_back({trim(name), {}});
            } else if (key == "usemtl") {
                std::getline(w, usemtl);
                usemtl = trim(usemtl);
            } else if (key == "mtllib") {
                std::string name;
                std::getline(w, name);
                mtllib(trim(name));
            } else if (key == "f") {
                if (objects.empty())
                    objects.push_back({"", {}});
                Face f;
                f.mat = usemtl;
                for (std::string s; w >> s;) {
                    int i = std::atoi(s.c_str());
                    i = i < 0 ? int(verts.size()) + i : i - 1;
                    if (i >= 0 && i < int(verts.size()))
                        f.v.push_back(i);
                }
                if (f.v.size() >= 3)
                    objects.back().faces.push_back(f);
            }
        }
        for (const Object &o : objects)
            object(o, verts, model);
        return model;
    }

    void object(const Object &o, const std::vector<std::array<float, 3>> &verts, Model &model) {
        if (o.faces.empty())
            return;
        // what the name says: the words in [square brackets]
        ModelPart p;
        bool surface = false, stand = false;
        for (size_t at = o.name.find('['); at != std::string::npos; at = o.name.find('[', at + 1)) {
            const size_t end = o.name.find(']', at);
            std::istringstream words(o.name.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1));
            for (std::string word; words >> word;) {
                const std::string l = lower(word);
                if (l == "hang") p.hang = true;
                else if (l == "fit") p.fit = true;
                else if (l == "surface") surface = true;
                else if (l == "stand") stand = true;
                else if (l == "when=shut") p.when = WHEN_SHUT;
                else if (l == "when=open") p.when = WHEN_OPEN;
                else if (l == "when=wall") p.when = WHEN_WALL;
                else if (l == "when=free") p.when = WHEN_FREE;
                else problem("object " + o.name + ": unknown word " + word);
            }
        }
        // its extent, and its outline seen from above
        p.u0 = p.v0 = p.z0 = 1e9f;
        p.u1 = p.v1 = p.z1 = -1e9f;
        std::vector<std::pair<float, float>> pts;
        for (const Face &f : o.faces)
            for (const int i : f.v) {
                const auto &q = verts[i];
                p.u0 = std::min(p.u0, q[0]);
                p.u1 = std::max(p.u1, q[0]);
                p.v0 = std::min(p.v0, q[1]);
                p.v1 = std::max(p.v1, q[1]);
                p.z0 = std::min(p.z0, q[2]);
                p.z1 = std::max(p.z1, q[2]);
                pts.push_back({q[0], q[1]});
            }
        if (surface || stand) {
            Model::Value val;
            val.when = p.when;
            if (surface)
                val.v[0] = p.z1;
            else
                val.v[0] = 0.5f * (p.u0 + p.u1), val.v[1] = 0.5f * (p.v0 + p.v1), val.v[2] = p.z0;
            (surface ? model.surface : model.stand).push_back(val);
            return;
        }
        if (p.z1 - p.z0 < 1e-4f || p.u1 - p.u0 < 1e-4f || p.v1 - p.v0 < 1e-4f) {
            problem("object " + o.name + " is flat; a part needs some thickness every way");
            return;
        }

        // the faces by the way they look: up, down, or to a side; the picture on
        // the sides it covers, the material with the most area for the rest
        std::map<std::string, float> top_area, under_area, side_area;
        for (const Face &f : o.faces) {
            float nu = 0, nv = 0, nz = 0;   // Newell's normal (u east, v south, z up)
            for (size_t k = 0; k < f.v.size(); ++k) {
                const auto &a = verts[f.v[k]], &b = verts[f.v[(k + 1) % f.v.size()]];
                nu += (a[1] - b[1]) * (a[2] + b[2]);
                nv += (a[2] - b[2]) * (a[0] + b[0]);
                nz += (a[0] - b[0]) * (a[1] + b[1]);
            }
            // (u, v, z) is a left-handed frame (v runs south): turn it outward
            nu = -nu;
            nv = -nv;
            nz = -nz;
            const float area = std::sqrt(nu * nu + nv * nv + nz * nz);
            if (area < 1e-9f)
                continue;
            const bool pic = mat(f.mat).kind == ModelMat::Picture;
            if (nz > 0.7f * area) {
                top_area[f.mat] += area;
            } else if (nz < -0.7f * area) {
                under_area[f.mat] += area;
            } else if (pic) {
                p.face_sides |= std::abs(nu) >= std::abs(nv) ? (nu > 0 ? FACE_E : FACE_W) : (nv > 0 ? FACE_S : FACE_N);
            } else {
                side_area[f.mat] += area;
            }
        }
        const auto most = [&](const std::map<std::string, float> &areas, ModelMat &out) {
            float best = 0;
            for (const auto &[name, area] : areas)
                if (area > best) {
                    best = area;
                    out = mat(name);
                }
        };
        most(side_area, p.side);
        most(top_area, p.top);
        most(under_area, p.under);
        if (p.side.kind == ModelMat::None)
            p.side.kind = ModelMat::Own;

        // a box if its outline is its rectangle, else a prism on the outline
        std::sort(pts.begin(), pts.end());
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
        const auto cross = [](std::pair<float, float> o_, std::pair<float, float> a, std::pair<float, float> b) {
            return (a.first - o_.first) * (b.second - o_.second) - (a.second - o_.second) * (b.first - o_.first);
        };
        std::vector<std::pair<float, float>> hull;
        for (int pass = 0; pass < 2; ++pass) {
            const size_t start = hull.size();
            for (size_t k = 0; k < pts.size(); ++k) {
                const auto &q = pass == 0 ? pts[k] : pts[pts.size() - 1 - k];
                while (hull.size() >= start + 2 && cross(hull[hull.size() - 2], hull.back(), q) <= 1e-7f)
                    hull.pop_back();
                hull.push_back(q);
            }
            hull.pop_back();
        }
        const auto same = [](float a, float b) { return std::abs(a - b) < 2e-3f; };
        bool rect = hull.size() == 4;
        for (const auto &q : hull)
            rect = rect && (same(q.first, p.u0) || same(q.first, p.u1)) && (same(q.second, p.v0) || same(q.second, p.v1));
        if (rect || hull.size() < 3) {
            model.parts.push_back(p);
            return;
        }
        // too many corners: leave out those that cost the least area until it fits
        while (hull.size() > size_t(MODEL_CORNERS)) {
            size_t drop = 0;
            float least = 1e9f;
            for (size_t k = 0; k < hull.size(); ++k) {
                const float a = std::abs(cross(hull[(k + hull.size() - 1) % hull.size()], hull[k], hull[(k + 1) % hull.size()]));
                if (a < least) {
                    least = a;
                    drop = k;
                }
            }
            hull.erase(hull.begin() + drop);
        }
        p.corners = int(hull.size());
        for (int k = 0; k < p.corners; ++k) {
            p.cu[k] = hull[k].first;
            p.cv[k] = hull[k].second;
        }
        model.parts.push_back(p);
    }
};

} // namespace

std::shared_ptr<const Models> load_models(const std::string &dir) {
    auto out = std::make_shared<Models>();
    out->dir = dir;
    const std::string models_dir = dir.empty() ? "" : (fs::path(dir) / "models").string();
    for (int sh = 0; sh < SH_COUNT; ++sh) {
        if (!MODEL_FILES[sh])
            continue;
        const std::string file = std::string(MODEL_FILES[sh]) + ".obj";
        std::string text;
        if (!models_dir.empty() && read_file(fs::path(models_dir) / file, text)) {
            Reader r{*out, models_dir, "models/" + file};
            Model m = r.read(text);
            if (!m.parts.empty()) {
                out->shape[sh] = std::move(m);
                ++out->files;
                continue;
            }
            r.problem("no part could be read; the built-in model is used");
        }
        if (const char *b = built_in(file)) {
            Models scratch;   // the built-in ones are known to read; their messages are not wanted
            Reader r{scratch, "", file};
            out->shape[sh] = r.read(b);
        }
    }
    // a model file nothing uses (a misspelt name, say) is worth a word
    std::error_code ec;
    if (!models_dir.empty())
        for (fs::directory_iterator it(models_dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (lower(it->path().extension().string()) != ".obj")
                continue;
            const std::string stem = lower(it->path().stem().string());
            bool known = false;
            for (int sh = 0; sh < SH_COUNT; ++sh)
                known = known || (MODEL_FILES[sh] && stem == MODEL_FILES[sh]);
            if (!known)
                out->problems.push_back("models/" + it->path().filename().string() +
                                        ": no thing uses this name (the names are in models/README.txt)");
        }
    return out;
}

uint64_t models_stamp(const std::string &dir) {
    if (dir.empty())
        return 0;
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    std::error_code ec;
    for (fs::recursive_directory_iterator it(fs::path(dir) / "models", ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2))
            continue;
        for (const char c : it->path().string())
            mix(uint8_t(c));
        mix(uint64_t(it->last_write_time(e2).time_since_epoch().count()));
        mix(uint64_t(it->file_size(e2)));
    }
    return h;
}

} // namespace fpv
