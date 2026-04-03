#include "state.hpp"

#include <span>

namespace ung::text {

static const char* default_text_vert = R"(
layout (binding = 2, std140) uniform UngCamera {
    mat4 view;
    mat4 view_inv;
    mat4 projection;
    mat4 projection_inv;
    mat4 view_projection;
    mat4 view_projection_inv;
};

layout (location = 0) in vec2 a_position;
layout (location = 1) in vec2 a_texcoord;
layout (location = 2) in vec4 a_color;

out vec2 vs_out_texcoord;
out vec4 vs_out_color;

void main() {
    vs_out_texcoord = a_texcoord;
    vs_out_color = a_color;
    gl_Position = view_projection * vec4(a_position, 0.0, 1.0);
}
)";

static const char* default_text_frag = R"(
layout(binding = 0) uniform sampler2D u_base;

in vec2 vs_out_texcoord;
in vec4 vs_out_color;

out vec4 frag_color;

void main() {
    frag_color = vs_out_color * texture(u_base, vs_out_texcoord).r;
}
)";

void init(ung_init_params params)
{
    state->fonts.init(params.max_num_fonts ? params.max_num_fonts : 16);
    state->text_layouts.init(params.max_num_text_layouts ? params.max_num_text_layouts : 16);

    state->text_vert_shader = ung_shader_create({
        .stage = MUGFX_SHADER_STAGE_VERTEX,
        .source = default_text_vert,
        .bindings = {
            { .type = MUGFX_SHADER_BINDING_TYPE_UNIFORM, .binding = 2 },
        },
        .debug_label = "ung:default_text.vert",
    });

    state->text_frag_shader = ung_shader_create({
        .stage = MUGFX_SHADER_STAGE_FRAGMENT,
        .source = default_text_frag,
        .bindings = {
            { .type = MUGFX_SHADER_BINDING_TYPE_SAMPLER, .binding = 0 },
        },
        .debug_label = "ung:default_text.frag",
    });
}

void shutdown()
{
    for (u32 i = 0; i < state->text_layouts.capacity(); ++i) {
        const auto key = state->text_layouts.get_key(i);
        if (key) {
            ung_text_layout_destroy({ key });
        }
    }

    for (u32 i = 0; i < state->fonts.capacity(); ++i) {
        const auto key = state->fonts.get_key(i);
        if (key) {
            ung_font_destroy({ key });
        }
    }

    state->text_layouts.free();
    state->fonts.free();
}

static Font* get_font(u64 key)
{
    return get(state->fonts, key);
}

static ung_material_id create_font_material(ung_texture_id texture)
{
    const auto material = ung_material_create({
        .mugfx = {
            .depth_func = MUGFX_DEPTH_FUNC_ALWAYS,
            .write_mask = MUGFX_WRITE_MASK_RGBA,
            .cull_face = MUGFX_CULL_FACE_MODE_NONE,
            .src_blend = MUGFX_BLEND_FUNC_SRC_ALPHA,
            .dst_blend = MUGFX_BLEND_FUNC_ONE_MINUS_SRC_ALPHA,
        },
        .vert = state->text_vert_shader,
        .frag = state->text_frag_shader,
    });
    ung_material_set_texture(material, 0, texture);
    return material;
}

static ung_font_id create_font(utxt_font* utxt_font, const char* debug_label)
{
    if (!utxt_font) {
        const auto err = utxt_get_last_error();
        ung_panicf("Error loading font: %.*s", (int)err.len, err.data);
    }

    uint32_t atlas_width, atlas_height, atlas_channels;
    const auto atlas_data = utxt_get_atlas(utxt_font, &atlas_width, &atlas_height, &atlas_channels);
    if (atlas_channels != 1) {
        utxt_font_free(utxt_font);
        ung_panicf("Font atlas has %u channels (expected 1)", atlas_channels);
    }

    const auto [id, font] = state->fonts.insert();
    if (id == 0) {
        utxt_font_free(utxt_font);
        ung_panicf("Font pool exhausted");
    }

    font->font = utxt_font;
    font->texture = ung_texture_create({
        .width = atlas_width,
        .height = atlas_height,
        .format = MUGFX_PIXEL_FORMAT_R8,
        .data = { atlas_data, atlas_width * atlas_height },
        .data_format = MUGFX_PIXEL_FORMAT_R8,
        .debug_label = debug_label,
    });
    font->material = create_font_material(font->texture);

    return { id };
}

static void* utxt_realloc(void* ptr, size_t old_size, size_t new_size, void*)
{
    if (!ptr) {
        return allocator.allocate(new_size, allocator.ctx);
    } else if (new_size) {
        return allocator.reallocate(ptr, old_size, new_size, allocator.ctx);
    } else {
        allocator.deallocate(ptr, old_size, allocator.ctx);
        return nullptr;
    }
}

static utxt_alloc get_utxt_alloc()
{
    return { utxt_realloc, nullptr };
}

EXPORT ung_font_id ung_font_load_ttf(const char* ttf_path, utxt_load_ttf_params params)
{
    LoadProfScope s(ttf_path);
    const auto font = utxt_font_load_ttf(get_utxt_alloc(), ttf_path, params);
    return create_font(font, ttf_path);
}

EXPORT ung_font_id ung_font_load_ttf_buffer(
    const void* data, size_t size, utxt_load_ttf_params params)
{
    LoadProfScope s("ttf buffer");
    const auto font
        = utxt_font_load_ttf_buffer(get_utxt_alloc(), (const uint8_t*)data, size, params);
    return create_font(font, "font_atlas_ttf_buffer");
}

EXPORT void ung_font_destroy(ung_font_id font_id)
{
    auto font = get_font(font_id.id);

    if (font->material.id) {
        ung_material_destroy(font->material);
    }
    if (font->texture.id) {
        ung_texture_destroy(font->texture);
    }
    if (font->font) {
        utxt_font_free(font->font);
    }

    state->fonts.remove(font_id.id);
}

EXPORT const utxt_font* ung_font_get_utxt(ung_font_id font)
{
    return get_font(font.id)->font;
}

EXPORT ung_texture_id ung_font_get_texture(ung_font_id font)
{
    return get_font(font.id)->texture;
}

EXPORT const utxt_font_metrics* ung_font_get_metrics(ung_font_id font)
{
    return utxt_get_font_metrics(get_font(font.id)->font);
}

EXPORT float ung_font_get_text_width(ung_font_id font, ung_string string)
{
    return utxt_get_text_width(get_font(font.id)->font, { string.data, string.length });
}

static void draw_text_quads(
    ung_material_id material, std::span<const utxt_quad> quads, ung_color color)
{
    ung_sprite_set_material(material);
    for (const auto& q : quads) {
        ung_sprite_add_quad(q.x, q.y, q.w, q.h, { q.u0, q.v0, q.u1 - q.u0, q.v1 - q.v0 }, color);
    }
}

static void draw_text(
    utxt_font* font, ung_material_id material, ung_string text, float x, float y, ung_color color)
{
    static std::array<utxt_quad, 512> quads;

    utxt_draw_text_state state = { { text.data, text.length }, x, 0 };
    while (state.text.len) {
        const auto n = utxt_draw_text_batch(quads.data(), quads.size(), font, &state, y);
        if (n > 0) {
            draw_text_quads(material, std::span { quads }.first(n), color);
        }
    }
}

EXPORT void ung_font_draw(ung_font_id font_id, ung_string text, float x, float y, ung_color color)
{
    const auto font = get_font(font_id.id);
    draw_text(font->font, font->material, text, x, y, color);
}

EXPORT void ung_font_draw_mat(ung_font_id font_id, ung_material_id material, ung_string text,
    float x, float y, ung_color color)
{
    const auto font = get_font(font_id.id);
    ung_sprite_flush(); // we change texture, so we need to batch break
    ung_material_set_texture(material, 0, font->texture);
    draw_text(font->font, material, text, x, y, color);
}

static TextLayout* get_text_layout(u64 key)
{
    return get(state->text_layouts, key);
}

EXPORT ung_text_layout_id ung_text_layout_create(uint32_t num_glyphs, uint32_t num_runs)
{
    UNG_OR_PANIC(num_glyphs > 0, "Text layout must have at least one glyph");
    UNG_OR_PANIC(num_runs > 0, "Text layout must have at least one run");

    const auto [id, layout] = state->text_layouts.insert();
    UNG_OR_PANIC(id, "Text layout pool exhausted");

    layout->layout = utxt_layout_create(get_utxt_alloc(), num_glyphs);
    if (!layout->layout) {
        state->text_layouts.remove(id);
        const auto err = utxt_get_last_error();
        ung_panicf("Error creating text layout: %.*s", (int)err.len, err.data);
    }

    layout->runs.init(num_runs);
    layout->num_glyphs = 0;
    layout->num_runs = 0;
    layout->dirty = false;

    return { id };
}

EXPORT void ung_text_layout_destroy(ung_text_layout_id layout_id)
{
    auto layout = get_text_layout(layout_id.id);
    if (layout->runs.data) {
        layout->runs.free();
    }
    utxt_layout_free(layout->layout);
    state->text_layouts.remove(layout_id.id);
}

EXPORT void ung_text_layout_reset(
    ung_text_layout_id layout_id, float wrap_width, utxt_text_align align)
{
    auto layout = get_text_layout(layout_id.id);
    utxt_layout_reset(layout->layout, wrap_width, align);
    layout->num_glyphs = 0;
    layout->num_runs = 0;
    layout->dirty = true;
}

EXPORT uint32_t ung_text_layout_add_text(ung_text_layout_id layout_id, ung_font_id font_id,
    uintptr_t user_data, ung_string text, ung_color color)
{
    auto layout = get_text_layout(layout_id.id);
    const auto font = get_font(font_id.id)->font;
    const auto first_glyph = (u32)layout->num_glyphs;
    const auto added_glyphs
        = utxt_layout_add_text(layout->layout, font, { text.data, text.length });

    assert(layout->num_runs < layout->runs.size);
    layout->runs[layout->num_runs++] = {
        .first_glyph = first_glyph,
        .glyph_count = (u32)added_glyphs,
        .font = font_id,
        .color = color,
        .user_data = user_data,
    };

    layout->num_glyphs += added_glyphs;
    layout->dirty = true;
    return (uint32_t)added_glyphs;
}

static void ensure_computed(TextLayout* layout)
{
    if (!layout->dirty) {
        return;
    }
    utxt_layout_compute(layout->layout);
    layout->dirty = false;
}

EXPORT uint32_t ung_text_layout_get_num_glyphs(ung_text_layout_id layout_id)
{
    auto layout = get_text_layout(layout_id.id);
    return (uint32_t)layout->num_glyphs;
}

EXPORT uint32_t ung_text_layout_get_num_lines(ung_text_layout_id layout_id)
{
    auto layout = get_text_layout(layout_id.id);
    return (uint32_t)utxt_layout_get_num_lines(layout->layout);
}

EXPORT uint32_t ung_text_layout_compute(ung_text_layout_id layout_id)
{
    auto layout = get_text_layout(layout_id.id);
    ensure_computed(layout);
    return (uint32_t)layout->num_glyphs;
}

static size_t build_layout_draw_items(TextLayout* layout, uint32_t glyph_offset,
    uint32_t num_glyphs, ung_text_draw_item* items, size_t max_items)
{
    const auto total_glyphs = (uint32_t)layout->num_glyphs;
    if (glyph_offset >= total_glyphs) {
        return 0;
    }

    if (num_glyphs == 0 || glyph_offset + num_glyphs > total_glyphs) {
        num_glyphs = total_glyphs - glyph_offset;
    }

    const auto num_items = (size_t)num_glyphs > max_items ? max_items : (size_t)num_glyphs;
    if (num_items == 0) {
        return 0;
    }

    size_t num_glyphs_layout = 0;
    const auto glyphs = utxt_layout_get_glyphs(layout->layout, &num_glyphs_layout);
    assert(num_glyphs);
    assert(layout->num_glyphs == num_glyphs_layout);

    u32 run_idx = 0;
    while (run_idx < layout->num_runs) {
        const auto& run = layout->runs[run_idx];
        if (glyph_offset < run.first_glyph + run.glyph_count) {
            break;
        }
        ++run_idx;
    }

    for (size_t i = 0; i < num_items; ++i) {
        const auto glyph_idx = glyph_offset + (uint32_t)i;

        while (run_idx < layout->num_runs) {
            const auto& run = layout->runs[run_idx];
            if (run.first_glyph + run.glyph_count > glyph_idx) {
                break;
            }
            ++run_idx;
        }
        assert(run_idx < layout->num_runs);
        const auto& run = layout->runs[run_idx];
        assert(glyph_idx >= run.first_glyph);

        utxt_quad quad;
        // TODO: use batchers here (I guess by run?)
        utxt_layout_glyph_get_quads(&glyphs[glyph_idx], 1, &quad, 0.0f, 0.0f);
        items[i] = {
            .x = quad.x,
            .y = quad.y,
            .w = quad.w,
            .h = quad.h,
            .u0 = quad.u0,
            .v0 = quad.v0,
            .u1 = quad.u1,
            .v1 = quad.v1,
            .font = run.font,
            .color = run.color,
            .user_data = run.user_data,
        };
    }

    return num_items;
}

EXPORT size_t ung_text_layout_build_draw_items(ung_text_layout_id layout_id, uint32_t glyph_offset,
    uint32_t num_glyphs, ung_text_draw_item* items, size_t max_items)
{
    auto layout = get_text_layout(layout_id.id);
    ensure_computed(layout);
    return build_layout_draw_items(layout, glyph_offset, num_glyphs, items, max_items);
}

EXPORT void ung_text_draw_items(const ung_text_draw_item* items, size_t num_items,
    ung_material_id mat_override, float x, float y)
{
    assert(items);

    ung_material_id current_material = { 0 };
    u64 mat_override_last_font = 0;

    for (size_t i = 0; i < num_items; ++i) {
        const auto& item = items[i];
        if (!item.font.id) { // super secret way to skip glyphs
            continue;
        }

        const auto font = get_font(item.font.id);
        auto mat = font->material;

        if (mat_override.id) {
            mat = mat_override;
            if (mat_override_last_font != item.font.id) {
                // Changing material texture modifies the material, but the sprite renderer
                // will not flush by itself (it doesn't know that the material changed).
                // We need to flush ourselves.
                ung_sprite_flush();
                ung_material_set_texture(mat, 0, font->texture);
                mat_override_last_font = item.font.id;
            }
        }

        if (current_material.id != mat.id) {
            ung_sprite_set_material(mat);
            current_material = mat;
        }

        ung_sprite_add_quad(item.x + x, item.y + y, item.w, item.h,
            { item.u0, item.v0, item.u1 - item.u0, item.v1 - item.v0 }, item.color);
    }
}

EXPORT void ung_text_layout_draw(
    ung_text_layout_id layout_id, ung_material_id mat, float x, float y)
{
    static std::array<ung_text_draw_item, 512> draw_items;

    auto layout = get_text_layout(layout_id.id);
    ensure_computed(layout);

    const auto total_glyphs = (u32)layout->num_glyphs;
    u32 glyph_offset = 0;
    while (glyph_offset < total_glyphs) {
        const auto n = build_layout_draw_items(
            layout, glyph_offset, 0, draw_items.data(), draw_items.size());
        if (n == 0) { // should not happen, but we don't want to loop infinitely
            break;
        }
        ung_text_draw_items(draw_items.data(), n, mat, x, y);
        glyph_offset += (u32)n;
    }
}

}