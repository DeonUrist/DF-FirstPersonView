#include "media.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <SDL_pixels.h>
#include <SDL_surface.h>

#include "modules/DFSDL.h"

namespace fpv {

namespace {

std::string trim(const std::string &s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}

bool load_png(const std::string &path, Image &out) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;
    SDL_Surface *raw = DFHack::DFSDL::DFIMG_Load(path.c_str());
    if (!raw)
        return false;
    SDL_PixelFormat *fmt = DFHack::DFSDL::DFSDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);
    SDL_Surface *surf = fmt ? DFHack::DFSDL::DFSDL_ConvertSurface(raw, fmt, 0) : nullptr;
    DFHack::DFSDL::DFSDL_FreeSurface(raw);
    if (!surf)
        return false;
    const bool good = surf->pixels && surf->w > 0 && surf->h > 0 && surf->w <= 2048 && surf->h <= 2048;
    if (good) {
        out.w = surf->w;
        out.h = surf->h;
        out.px.resize(size_t(out.w) * out.h);
        for (int y = 0; y < out.h; ++y)
            std::memcpy(&out.px[size_t(y) * out.w], static_cast<const uint8_t *>(surf->pixels) + y * surf->pitch,
                        size_t(out.w) * 4);
    }
    DFHack::DFSDL::DFSDL_FreeSurface(surf);
    return good;
}

void load_slots(const std::string &path, Doll &doll) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        const size_t eq = line.rfind('=');
        if (eq == std::string::npos)
            continue;
        DollSlot slot;
        if (std::sscanf(line.c_str() + eq + 1, " %d , %d", &slot.x, &slot.y) != 2)
            continue;
        std::string names = line.substr(0, eq);
        size_t at = 0;
        while (at <= names.size()) {
            size_t bar = names.find('|', at);
            if (bar == std::string::npos)
                bar = names.size();
            const std::string name = lower(trim(names.substr(at, bar - at)));
            if (!name.empty())
                slot.names.push_back(name);
            at = bar + 1;
        }
        if (!slot.names.empty())
            doll.slots.push_back(slot);
    }
}

} // namespace

std::shared_ptr<const Media> load_media(const std::string &dir) {
    auto m = std::make_shared<Media>();
    m->dir = dir;
    const auto one = [&](const char *name, Image &img) {
        if (load_png(dir + "/" + name, img))
            ++m->files;
    };
    one("cursor.png", m->cursor);
    one("crosshair.png", m->crosshair);
    one("aim_strike.png", m->aim[AIM_STRIKE]);
    one("aim_shoot.png", m->aim[AIM_SHOOT]);
    one("aim_throw.png", m->aim[AIM_THROW]);
    one("aim_wrestle.png", m->aim[AIM_WRESTLE]);
    one("checkbox_on.png", m->checkbox_on);
    one("checkbox_off.png", m->checkbox_off);
    one("compass.png", m->compass);
    one("compass_frame.png", m->compass_frame);
    one("wound_bruise.png", m->wound_bruise);
    one("wound_cut.png", m->wound_cut);
    one("msg_combat.png", m->msg_icon[0]);
    one("msg_talk.png", m->msg_icon[1]);
    one("msg_other.png", m->msg_icon[2]);
    one("msg_hide.png", m->msg_icon[3]);
    one("msg_show.png", m->msg_icon[4]);

    std::error_code ec;
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(dir + "/dolls", ec)) {
        if (entry.path().extension() == ".png")
            names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    for (const std::string &name : names) {
        Doll doll;
        doll.name = name;
        if (!load_png(dir + "/dolls/" + name + ".png", doll.img))
            continue;
        load_slots(dir + "/dolls/" + name + ".txt", doll);
        if (doll.slots.empty())
            continue;
        ++m->files;
        m->dolls.push_back(std::move(doll));
    }
    return m;
}

const Doll *Media::bestDoll(const std::vector<std::string> &parts, std::vector<int> &slot_of) const {
    const Doll *best = nullptr;
    int best_score = 0;
    std::vector<int> fit;
    for (const Doll &doll : dolls) {
        fit.assign(parts.size(), -1);
        int matched = 0;
        for (size_t s = 0; s < doll.slots.size(); ++s) {
            bool found = false;
            for (const std::string &name : doll.slots[s].names) {
                for (size_t p = 0; p < parts.size() && !found; ++p)
                    if (fit[p] < 0 && lower(parts[p]) == name) {
                        fit[p] = int(s);
                        found = true;
                    }
                if (found)
                    break;
            }
            if (found)
                ++matched;
        }
        // every place that is filled counts, every empty one counts against
        const int score = 3 * matched - 2 * (int(doll.slots.size()) - matched);
        if (matched > 0 && score > best_score) {
            best_score = score;
            best = &doll;
            slot_of = fit;
        }
    }
    if (!best)
        slot_of.assign(parts.size(), -1);
    return best;
}

// ---------------------------------------------------------------------------

void Canvas::resize(int w_, int h_) {
    w = std::max(0, w_);
    h = std::max(0, h_);
    px.assign(size_t(w) * h, 0);
}

void Canvas::fill(int x, int y, int w_, int h_, uint32_t color) {
    const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(w, x + w_), y1 = std::min(h, y + h_);
    for (int yy = y0; yy < y1; ++yy)
        std::fill(px.begin() + size_t(yy) * w + x0, px.begin() + size_t(yy) * w + std::max(x0, x1), color);
}

void Canvas::frame(int x, int y, int w_, int h_, uint32_t color, int thick) {
    fill(x, y, w_, thick, color);
    fill(x, y + h_ - thick, w_, thick, color);
    fill(x, y, thick, h_, color);
    fill(x + w_ - thick, y, thick, h_, color);
}

void Canvas::blitFit(const Image &img, int x, int y, int w_, int h_) {
    if (!img.ok() || w_ < 1 || h_ < 1)
        return;
    for (int yy = 0; yy < h_; ++yy) {
        const int dy = y + yy;
        if (dy < 0 || dy >= h)
            continue;
        const uint32_t *src = &img.px[size_t(yy * img.h / h_) * img.w];
        for (int xx = 0; xx < w_; ++xx) {
            const int dx = x + xx;
            if (dx < 0 || dx >= w)
                continue;
            const uint32_t c = src[xx * img.w / w_];
            if (px_a(c) >= 128)
                px[size_t(dy) * w + dx] = c | 0xFF000000u;
        }
    }
}

void Canvas::blit(const Image &img, int x, int y, int scale) {
    blitFit(img, x, y, img.w * scale, img.h * scale);
}

void Canvas::text(const Font &font, int x, int y, const std::string &s, uint32_t color, int cw, int ch) {
    if (cw < 1 || ch < 1)
        return;
    for (size_t i = 0; i < s.size(); ++i) {
        const uint8_t *rows = font.rows[uint8_t(s[i])];
        const int ox = x + int(i) * cw;
        if (ox >= w)
            break;
        for (int yy = 0; yy < ch; ++yy) {
            const int dy = y + yy;
            if (dy < 0 || dy >= h)
                continue;
            const uint8_t bits = rows[yy * 12 / ch];
            if (!bits)
                continue;
            for (int xx = 0; xx < cw; ++xx) {
                const int dx = ox + xx;
                if (dx >= 0 && dx < w && (bits >> (xx * 8 / cw)) & 1)
                    px[size_t(dy) * w + dx] = color;
            }
        }
    }
}

} // namespace fpv
