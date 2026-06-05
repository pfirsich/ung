#pragma once

#include <string_view>

#include "types.hpp"
#include "um.h"
#include "ung.h"

struct SDL_Window;

namespace ung {
struct UFrame {
    um_vec4 time; // x: seconds since game started, y: frame counter
};

struct UPass {
    um_mat view;
    um_mat view_inv;
    um_mat projection;
    um_mat projection_inv;
    um_mat view_projection;
    um_mat view_projection_inv;
    um_vec4 view_dimensions; // xy: size, zw: reciprocal size
};

struct UTransform {
    um_mat model;
    um_mat model_view;
    um_mat model_view_projection;
    um_mat normal_matrix;
};

struct Texture {
    mugfx_texture_id texture;
    ung_resource_id resource;
};

struct Shader {
    mugfx_shader_stage stage;
    mugfx_shader_id shader;
    ung_resource_id resource;
};

struct InstanceBuffer {
    size_t stride;
    uint32_t max_num_instances;
    uint32_t num_instances;
    mugfx_buffer_id buffer;
    mugfx_vertex_attribute attributes[MUGFX_MAX_VERTEX_ATTRIBUTES];
};

struct Geometry {
    mugfx_geometry_id geometry;
    mugfx_geometry_create_params mugfx_params;
    ung_instance_buffer_id instance_buffer;
    ung_resource_id resource;
};

struct Material {
    mugfx_material_id material;
    ung_resource_id resource;
    mugfx_buffer_id constant_buf;
    mugfx_buffer_id dynamic_buf;
    uint8_t* dynamic_data;
    size_t dynamic_data_size;
    bool dynamic_data_dirty;
    u64 last_update_frame;
    StaticVector<mugfx_draw_binding, 16> bindings;
    std::array<ung_texture_id, 16> textures;
};

struct Camera {
    um_mat projection;
    um_mat projection_inv;
    um_mat view;
    um_mat view_inv;
};

struct Font {
    utxt_font* font;
    ung_texture_id texture;
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
    Pool<InstanceBuffer> instance_buffers;

    // Uniform Buffers
    mugfx_buffer_id u_frame_buf;
    mugfx_buffer_id u_pass_buf;
    mugfx_buffer_id u_transform_buf;

    // Rendering
    UPass pass_data;

    // SDL
    SDL_Window* window;
    void* context; // SDL_GLContext
    ung_event_callback event_callback;
    void* event_callback_ctx;
    u32 win_width;
    u32 win_height;
    u32 fb_width;
    u32 fb_height;

    // Text
    ung_shader_id default_text_frag;
    ung_material_id default_text_mat;

    // Other
    bool auto_reload;
    bool async_decode;
    bool load_cache;
    u64 frame_counter;
    ung_shader_id default_sprite_vert;

    // Load Profiling
    Vector<LoadProfilerZone> prof_zones;
    StrPool prof_strpool;
    Vector<u32> prof_stack; // holds index to current parent
};

extern State* state;

}
