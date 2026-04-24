#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "state.hpp"
#include "types.hpp"

namespace ung::sprite_renderer {
static const char* default_sprite_frag = R"(
layout(binding = 0) uniform sampler2D u_base;

in vec2 vs_out_texcoord;
in vec4 vs_out_color;

out vec4 frag_color;

void main() {
    frag_color = vs_out_color * texture(u_base, vs_out_texcoord);
}
)";

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
    u32 vertex_offset;
    u32 index_offset;
    ung_material_id current_material;
    ung_material_id next_material;
    ung_texture_id current_texture;
    ung_texture_id next_texture;
    u32 current_tex_width;
    u32 current_tex_height;
    ung_shader_id default_frag;
    ung_material_id default_material;
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

    state->num_indices
        = params.max_num_sprite_indices ? params.max_num_sprite_indices : state->num_vertices / 4;
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

    state->default_frag = ung_shader_create({
        .stage = MUGFX_SHADER_STAGE_FRAGMENT,
        .source = default_sprite_frag,
        .bindings = {
            { .type = MUGFX_SHADER_BINDING_TYPE_SAMPLER, .binding = 0 },
        },
        .debug_label = "ung:default_sprite.frag",
    });

    state->default_material = ung_material_create({
        .mugfx = {
            .depth_func = MUGFX_DEPTH_FUNC_ALWAYS,
            .write_mask = MUGFX_WRITE_MASK_RGBA,
            .cull_face = MUGFX_CULL_FACE_MODE_NONE,
            .src_blend = MUGFX_BLEND_FUNC_SRC_ALPHA,
            .dst_blend = MUGFX_BLEND_FUNC_ONE_MINUS_SRC_ALPHA,
        },
        .vert = ung::state->default_sprite_vert,
        .frag = state->default_frag,
    });

    state->current_material = state->default_material;
    state->next_material = state->default_material;
}

void shutdown()
{
    if (!state) {
        return;
    }

    // ung_geometry_destroy(state->geometry);
    mugfx_buffer_destroy(state->index_buffer);
    deallocate(state->indices, state->num_indices);
    mugfx_buffer_destroy(state->vertex_buffer);
    deallocate(state->vertices, state->num_vertices);

    deallocate(state, 1);
    state = nullptr;
}

static void apply_next()
{
    const auto mat_changed = state->next_material.id != state->current_material.id;
    const auto tex_changed = state->next_texture.id != state->current_texture.id;
    if (mat_changed || tex_changed) {
        ung_sprite_flush();
    }
    if (tex_changed) {
        const auto [w, h] = ung_texture_get_size(state->next_texture);
        state->current_tex_width = w;
        state->current_tex_height = h;
    }
    state->current_material = state->next_material;
    state->current_texture = state->next_texture;
}

EXPORT void ung_sprite_set_material(ung_material_id mat)
{
    if (!mat.id) {
        mat = state->default_material;
    }
    state->next_material = mat;
}

EXPORT ung_material_id ung_sprite_get_material()
{
    return state->next_material;
}

EXPORT void ung_sprite_set_texture(ung_texture_id tex)
{
    state->next_texture = tex;
}

EXPORT ung_texture_id ung_sprite_get_texture()
{
    return state->next_texture;
}

EXPORT uint16_t ung_sprite_add_vertex(float x, float y, float u, float v, ung_color color)
{
    apply_next();
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
    ung_texture_id tex, ung_transform_2d transform, ung_texture_region texture, ung_color color)
{
    transform.scale_x = transform.scale_x != 0.0f ? transform.scale_x : 1.0f;
    transform.scale_y = transform.scale_y != 0.0f ? transform.scale_y : 1.0f;

    ung_sprite_set_texture(tex);
    apply_next(); // make sure current texture size is correct

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
    static mugfx_draw_binding binding = {
        .type = MUGFX_BINDING_TYPE_TEXTURE,
        .texture = { .binding = 0 },
    };
    if (state->index_offset > 0) {
        if (state->current_texture.id) {
            const auto tex = ung::state->textures.find(state->current_texture.id);
            assert(tex);
            binding.texture.id = tex->texture;

            const auto geom = get(ung::state->geometries, state->geometry.id);
            mugfx_buffer_update(state->vertex_buffer, 0,
                { .data = state->vertices, .length = sizeof(Vertex) * state->vertex_offset });
            mugfx_buffer_update(state->index_buffer, 0,
                { .data = state->indices, .length = sizeof(u16) * state->index_offset });
            mugfx_geometry_set_index_range(geom->geometry, 0, state->index_offset);
            ung_draw(state->current_material, state->geometry, nullptr,
                { .binding_overrides = &binding, .num_binding_overrides = 1 });
        }
        state->vertex_offset = 0;
        state->index_offset = 0;
    }
}
}
