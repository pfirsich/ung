#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "state.hpp"
#include "types.hpp"

namespace ung::sprite_renderer {
struct Vertex {
    float x, y;
    uint16_t u, v;
    uint8_t r, g, b, a;
};

struct State {

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
    ung_transform_id identity_trafo;
};

State* state = nullptr;

void init(ung_init_params params)
{
    assert(!state);
    state = allocate<State>();
    std::memset(state, 0, sizeof(State));

    state->num_vertices
        = params.max_num_sprite_vertices ? params.max_num_sprite_vertices : 16 * 1024;
    state->vertices = allocate<Vertex>(state->num_vertices);
    state->vertex_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_ARRAY,
        .usage = MUGFX_BUFFER_USAGE_HINT_STREAM,
        .data = { .data = nullptr, .length = sizeof(Vertex) * state->num_vertices },
    }),

    state->num_indices = params.max_num_sprite_indices ? params.max_num_sprite_indices : 16 * 1024;
    state->indices = allocate<u16>(state->num_indices);
    state->index_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_INDEX,
        .usage = MUGFX_BUFFER_USAGE_HINT_STREAM,
        .data = { .data = nullptr, .length = sizeof(u16) * state->num_indices },
    });

    state->geometry = ung_geometry_create({
        .vertex_buffers = {
            {
                .buffer = state->vertex_buffer,
                .attributes = {
                    {.location = 0, .components = 2, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_F32}, // xy
                    {.location = 1, .components = 2, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U16_NORM}, // uv
                    {.location = 2, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U8_NORM}, // rgba
                },
            }
        },
        .index_buffer = state->index_buffer,
        .index_type = MUGFX_INDEX_TYPE_U16,
    });

    state->identity_trafo = ung_transform_create();
}

void shutdown()
{
    if (!state) {
        return;
    }

    ung_transform_destroy(state->identity_trafo);
    // ung_geometry_destroy(state->geometry);
    mugfx_buffer_destroy(state->index_buffer);
    deallocate(state->indices, state->num_indices);
    mugfx_buffer_destroy(state->vertex_buffer);
    deallocate(state->vertices, state->num_vertices);

    deallocate(state, 1);
    state = nullptr;
}

static mugfx_texture_id get_texture(ung_material_id material, uint32_t binding)
{
    auto mat = get(ung::state->materials, material.id);
    for (const auto& b : mat->bindings) {
        if (b.type == MUGFX_BINDING_TYPE_TEXTURE && b.texture.binding == binding) {
            return b.texture.id;
        }
    }
    return { 0 };
}

EXPORT void ung_sprite_set_material(ung_material_id mat)
{
    if (mat.id != state->current_material.id) {
        ung_sprite_flush();
        state->current_material = mat;
        const auto tex = get_texture(mat, 0);
        mugfx_texture_get_size(tex, &state->current_tex_width, &state->current_tex_height);
    }
}

EXPORT uint16_t ung_sprite_add_vertex(float x, float y, float u, float v, ung_color color)
{
    assert(state->vertex_offset < state->num_vertices);
    state->vertices[state->vertex_offset] = Vertex {
        x,
        y,
        f2u16norm(u),
        f2u16norm(v),
        f2u8norm(color.r),
        f2u8norm(color.g),
        f2u8norm(color.b),
        f2u8norm(color.a),
    };
    return (u16)state->vertex_offset++;
}

EXPORT void ung_sprite_add_index(uint16_t idx)
{
    assert(state->index_offset < state->num_indices);
    state->indices[state->index_offset++] = idx;
}

EXPORT void ung_sprite_add_quad(
    float x, float y, float w, float h, ung_texture_region texture, ung_color color)
{
    const auto tl = ung_sprite_add_vertex(x, y, texture.x, texture.y, color);
    const auto bl = ung_sprite_add_vertex(x, y + h, texture.x, texture.y + texture.h, color);
    const auto tr = ung_sprite_add_vertex(x + w, y, texture.x + texture.w, texture.y, color);
    const auto br
        = ung_sprite_add_vertex(x + w, y + h, texture.x + texture.w, texture.y + texture.h, color);

    ung_sprite_add_index(tl);
    ung_sprite_add_index(bl);
    ung_sprite_add_index(tr);

    ung_sprite_add_index(bl);
    ung_sprite_add_index(br);
    ung_sprite_add_index(tr);
}

struct vec2 {
    float x, y;
};

static vec2 transform_vec2(ung_transform_2d t, float x, float y)
{
    // offset
    x += t.offset_x;
    y += t.offset_y;

    // scale
    x *= t.scale_x;
    y *= t.scale_y;

    // rotate
    const auto s = sinf(t.rotation);
    const auto c = cosf(t.rotation);
    const auto rx = x * c - y * s;
    const auto ry = x * s + y * c;

    // translate
    x = rx + t.x;
    y = ry + t.y;

    return vec2 { x, y };
}

static u16 add_vertex(
    const vec2& p, ung_transform_2d transform, ung_texture_region region, ung_color color)
{
    const auto pos = transform_vec2(transform, p.x * (float)state->current_tex_width * region.w,
        p.y * (float)state->current_tex_height * region.h);
    const auto u = region.x + p.x * region.w;
    const auto v = region.y + p.y * region.h;
    return ung_sprite_add_vertex(pos.x, pos.y, u, v, color);
}

EXPORT void ung_sprite_add(
    ung_material_id mat, ung_transform_2d transform, ung_texture_region texture, ung_color color)
{
    transform.scale_x = transform.scale_x != 0.0f ? transform.scale_x : 1.0f;
    transform.scale_y = transform.scale_y != 0.0f ? transform.scale_y : 1.0f;

    ung_sprite_set_material(mat);

    const auto tl = add_vertex({ 0.0f, 0.0f }, transform, texture, color);
    const auto bl = add_vertex({ 0.0f, 1.0f }, transform, texture, color);
    const auto tr = add_vertex({ 1.0f, 0.0f }, transform, texture, color);
    const auto br = add_vertex({ 1.0f, 1.0f }, transform, texture, color);

    ung_sprite_add_index(tl);
    ung_sprite_add_index(bl);
    ung_sprite_add_index(tr);

    ung_sprite_add_index(bl);
    ung_sprite_add_index(br);
    ung_sprite_add_index(tr);
}

EXPORT void ung_sprite_flush()
{
    if (state->index_offset > 0) {
        const auto geom = get(ung::state->geometries, state->geometry.id);
        mugfx_buffer_update(state->vertex_buffer, 0,
            { .data = state->vertices, .length = sizeof(Vertex) * state->vertex_offset });
        mugfx_buffer_update(state->index_buffer, 0,
            { .data = state->indices, .length = sizeof(u16) * state->index_offset });
        mugfx_geometry_set_index_range(geom->geometry, 0, state->index_offset);
        ung_draw(state->current_material, state->geometry, state->identity_trafo);
        state->vertex_offset = 0;
        state->index_offset = 0;
    }
}

}