#pragma once

#include <string_view>

#include "types.hpp"
#include "um.h"
#include "ung.h"

struct SDL_Window;

namespace ung {
struct UConstant {
    um_vec4 screen_dimensions; // xy: size, zw: reciprocal size
};

struct UFrame {
    um_vec4 time; // x: seconds since game started, y: frame counter
};

struct UCamera {
    um_mat view;
    um_mat view_inv;
    um_mat projection;
    um_mat projection_inv;
    um_mat view_projection;
    um_mat view_projection_inv;
};

struct UTransform {
    um_mat model;
    um_mat model_inv;
    um_mat model_view;
    um_mat model_view_projection;
};

struct TextureReloadCtx {
    ung_texture_id texture;
    Array<char> path;
    ung_texture_type type;
    ung_texture_load_params params;
};

struct Texture {
    mugfx_texture_id texture;
    ung_resource_id resource;
    TextureReloadCtx* reload_ctx;
};

struct ShaderReloadCtx {
    ung_shader_id shader;
    Array<char> path;
};

struct Shader {
    mugfx_shader_stage stage;
    mugfx_shader_id shader;
    ung_resource_id resource;
    ShaderReloadCtx* reload_ctx;
};

struct GeometryReloadCtx {
    ung_geometry_id geometry;
    Array<char> path;
};

struct Geometry {
    mugfx_geometry_id geometry;
    ung_resource_id resource;
    GeometryReloadCtx* reload_ctx;
};

struct MaterialReloadCtx {
    ung_material_id material;
    Array<char> vert_path;
    Array<char> frag_path;
    mugfx_material_create_params params;
    size_t constant_data_size;
    size_t dynamic_data_size;

    // We have to remember vert and frag, even if the shaders are not owned, so they are duplicated
    // here. These are always set.
    ung_shader_id vert;
    uint32_t vert_version;
    ung_shader_id frag;
    uint32_t frag_version;
    // Auxiliary data to Material::bindings
    std::array<ung_texture_id, 16> textures;
};

struct Material {
    mugfx_material_id material;
    // ung_material_load creates shaders so the material may own shaders. These are set iff material
    // owns them.
    ung_shader_id vert;
    ung_shader_id frag;
    mugfx_uniform_data_id constant_data;
    mugfx_uniform_data_id dynamic_data;
    StaticVector<mugfx_draw_binding, 16> bindings;
    ung_resource_id resource;
    MaterialReloadCtx* reload_ctx;
};

struct Camera {
    um_mat projection;
    um_mat projection_inv;
    ung_transform_id transform;
};

struct Font {
    utxt_font* font;
    ung_texture_id texture;
    ung_material_id material;
};

struct TextLayoutRun {
    u32 first_glyph;
    u32 glyph_count;
    ung_font_id font;
    ung_color color;
    uintptr_t user_data;
};

struct TextLayout {
    utxt_layout* layout;
    Array<TextLayoutRun> runs;
    size_t num_glyphs;
    u32 num_runs;
    bool dirty;
};

struct LoadProfilerZone {
    std::string_view name;
    u32 parent_idx;
    float start_time;
    float duration;
    float child_duration;
};

struct State {
    // Pools
    Pool<Texture> textures;
    Pool<Shader> shaders;
    Pool<Geometry> geometries;
    Pool<Material> materials;
    Pool<Camera> cameras;
    Pool<Font> fonts;
    Pool<TextLayout> text_layouts;

    // Uniform Buffers
    UConstant u_constant;
    UFrame u_frame;
    UCamera u_camera;
    mugfx_uniform_data_id constant_data;
    mugfx_uniform_data_id frame_data;
    mugfx_uniform_data_id camera_data;
    ung_transform_id identity_trafo;
    ung_shader_id text_vert_shader;
    ung_shader_id text_frag_shader;

    // SDL
    SDL_Window* window;
    void* context; // SDL_GLContext
    ung_event_callback event_callback;
    void* event_callback_ctx;
    u32 win_width;
    u32 win_height;
    u32 fb_width;
    u32 fb_height;

    bool auto_reload;
    bool load_cache;
    u64 frame_counter;

    // Load Profiling
    Vector<LoadProfilerZone> prof_zones;
    StrPool prof_strpool;
    Vector<u32> prof_stack; // holds index to current parent
};

extern State* state;

}
