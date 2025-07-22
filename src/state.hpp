#pragma once

#include "types.hpp"
#include "um.h"
#include "ung/core.h"

#include <SDL_scancode.h>

struct SDL_Window;

#define EXPORT extern "C"

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

struct Transform {
    um_mat local_matrix;
    um_quat orientation;
    um_vec3 position;
    um_vec3 scale;
    SlotMap::Key parent;
    SlotMap::Key first_child;
    SlotMap::Key prev_sibling;
    SlotMap::Key next_sibling;
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

struct GeometryData {
    void* mesh; // fastObjMesh
    usize num_vertices;
    usize num_indices;
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
    ung_draw_geometry_id geometry;
    size_t vertex_offset;
    size_t index_offset;
    ung_material_id current_material;
    u32 current_tex_width;
    u32 current_tex_height;
};

constexpr usize MaxMouseButtons = 16;

struct InputState {
    std::array<bool, SDL_NUM_SCANCODES> key_down;
    std::array<u8, SDL_NUM_SCANCODES> key_pressed;
    std::array<u8, SDL_NUM_SCANCODES> key_released;
    int mouse_x, mouse_y, mouse_dx, mouse_dy;
    int mouse_scroll_left, mouse_scroll_right, mouse_scroll_y_pos, mouse_scroll_y_neg;
    std::array<bool, MaxMouseButtons> mouse_button_down;
    std::array<u8, MaxMouseButtons> mouse_button_pressed;
    std::array<u8, MaxMouseButtons> mouse_button_released;
};

struct State {
    Pool<Transform> transforms;
    Pool<Material> materials;
    Pool<Camera> cameras;
    Pool<GeometryData> geometry_data;
    UConstant u_constant;
    UFrame u_frame;
    UCamera u_camera;
    mugfx_uniform_data_id constant_data;
    mugfx_uniform_data_id frame_data;
    mugfx_uniform_data_id camera_data;
    SDL_Window* window;
    void* context; // SDL_GLContext
    ung_event_callback event_callback = nullptr;
    void* event_callback_ctx = nullptr;
    u32 win_width;
    u32 win_height;
    InputState input;
    ung_transform_id identity_trafo;
    SpriteRenderer sprite_renderer;
};

extern State* state;

template <typename T>
auto get(Pool<T>& pool, SlotMap::Key key)
{
    auto obj = pool.find(key);
    assert(obj);
    return obj;
}

}