#pragma once

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

struct Texture {
    mugfx_texture_id texture;
};

struct Shader {
    mugfx_shader_id shader;
};

struct Geomery {
    mugfx_geometry_id geometry;
};

struct Transform {
    um_mat local_matrix;
    um_quat orientation;
    um_vec3 position;
    um_vec3 scale;
    u64 parent;
    u64 first_child;
    u64 prev_sibling;
    u64 next_sibling;
    mugfx_uniform_data_id uniform_data;
    bool local_matrix_dirty;
};

struct Material {
    mugfx_material_id material;
    mugfx_uniform_data_id constant_data;
    mugfx_uniform_data_id dynamic_data;
    StaticVector<mugfx_draw_binding, 16> bindings;
};

struct Camera {
    um_mat projection;
    um_mat projection_inv;
    ung_transform_id transform;
};

struct SpriteRenderer {
    struct Vertex {
        float x, y;
        uint16_t u, v;
        uint8_t r, g, b, a;
    };

    Vertex* vertices;
    size_t num_vertices;
    uint16_t* indices;
    size_t num_indices;
    mugfx_buffer_id vertex_buffer;
    mugfx_buffer_id index_buffer;
    ung_geometry_id geometry;
    size_t vertex_offset;
    size_t index_offset;
    ung_material_id current_material;
    u32 current_tex_width;
    u32 current_tex_height;
};

struct State {
    // Pools
    Pool<Texture> textures;
    Pool<Shader> shaders;
    Pool<Geomery> geometries;
    Pool<Transform> transforms;
    Pool<Material> materials;
    Pool<Camera> cameras;

    // Uniform Buffers
    UConstant u_constant;
    UFrame u_frame;
    UCamera u_camera;
    mugfx_uniform_data_id constant_data;
    mugfx_uniform_data_id frame_data;
    mugfx_uniform_data_id camera_data;

    // SDL
    SDL_Window* window;
    void* context; // SDL_GLContext
    ung_event_callback event_callback;
    void* event_callback_ctx;
    u32 win_width;
    u32 win_height;

    // Renderer
    ung_transform_id identity_trafo;
    SpriteRenderer sprite_renderer;
};

extern State* state;

}