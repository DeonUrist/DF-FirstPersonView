// The renderer that draws through the graphics card.
//
// The world is turned into textured triangles on the processor (only the
// corners are worked out there) and handed to the game's own SDL renderer with
// SDL_RenderGeometry, which fills them in on the graphics card. That call works
// on every back end SDL has (Direct3D, OpenGL, ...), so nothing here depends on
// the system or the maker of the card.
//
// SDL draws flat triangles only: no depth buffer and no perspective inside a
// triangle. So tiles are drawn far to near (what is nearer covers what is
// behind it), and surfaces close to the eye are cut into small pieces, each of
// them nearly flat to the eye. Haze is a second, see-through triangle in the
// haze's colour over each one. All pictures live in one big texture.
#pragma once

#include <memory>
#include <string>

#include <SDL_render.h>

#include "render.h"
#include "render3d.h"

namespace fpv {

// the SDL functions it needs (the plugin looks them up in the game's SDL)
struct GpuApi {
    decltype(&SDL_CreateTexture) create_texture = nullptr;
    decltype(&SDL_DestroyTexture) destroy_texture = nullptr;
    decltype(&SDL_UpdateTexture) update_texture = nullptr;
    decltype(&SDL_SetTextureBlendMode) set_texture_blend_mode = nullptr;
    decltype(&SDL_SetTextureScaleMode) set_texture_scale_mode = nullptr;
    decltype(&SDL_RenderGeometry) render_geometry = nullptr;
    decltype(&SDL_GetRendererInfo) get_renderer_info = nullptr;   // optional
    bool ok() const {
        return create_texture && destroy_texture && update_texture && set_texture_blend_mode &&
               set_texture_scale_mode && render_geometry;
    }
};

class GpuRenderer {
public:
    explicit GpuRenderer(const GpuApi &api);
    ~GpuRenderer();

    // Works out the frame's triangles for a W x H window. The caller holds
    // scene.mutex. False if nothing can be drawn (no texture could be made).
    bool build(SDL_Renderer *sdl, const Scene &scene, const Camera &cam, const RenderOptions &opt, int W, int H);
    // hands the triangles of the last build() to SDL; false if SDL would not take them
    bool draw(SDL_Renderer *sdl);
    // forget the texture (before the plugin goes away); render thread only
    void dropTextures();

    // what the last built frame shows at a point of the window given as
    // fractions 0..1 of its width and height; the caller holds scene.mutex
    Pick pick(const Scene &scene, float fx, float fy) const;
    // a point (in the same fractions) that shows a creature; false if none is in view
    bool findCreature(const Scene &scene, float &fx, float &fy) const;

    int triangles() const;          // in the last frame
    int atlasPercent() const;       // how full the big texture is
    int atlasSize() const;
    std::string timings() const;    // where the last build's time went, for the status text

    struct Impl;

private:
    std::unique_ptr<Impl> d_;
};

} // namespace fpv
