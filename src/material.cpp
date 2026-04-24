#include "state.hpp"

#include <cstdio>

namespace ung {

Material* get_material(u64 key)
{
    return get(state->materials, key);
}

static bool recreate_material(Material* mat, MaterialReloadCtx* ctx, Shader* vert, Shader* frag)
{
    auto params = ctx->params;
    params.vert_shader = vert->shader;
    params.frag_shader = frag->shader;

    const auto new_mat = mugfx_material_create(params);
    if (!new_mat.id) {
        return false;
    }

    mugfx_material_destroy(mat->material);
    mat->material = new_mat;

    return true;
}

static void update_texture_bindings(Material* mat, MaterialReloadCtx* ctx)
{
    for (size_t i = 0; i < mat->bindings.size(); ++i) {
        if (mat->bindings[i].type == MUGFX_BINDING_TYPE_TEXTURE) {
            ung_material_set_texture(
                ctx->material, mat->bindings[i].texture.binding, ctx->textures[i]);
        }
    }
}

static bool reload_material(Material* mat, MaterialReloadCtx* ctx)
{
    const auto vert = get(state->shaders, ctx->vert.id);
    const auto frag = get(state->shaders, ctx->frag.id);
    const auto recreate = ung_resource_get_version(vert->resource) != ctx->vert_version
        || ung_resource_get_version(frag->resource) != ctx->frag_version;

    bool res = true;
    if (recreate) {
        res = recreate_material(mat, ctx, vert, frag);
    }

    update_texture_bindings(mat, ctx);
    return res;
}

static bool material_reload_cb(void* userdata)
{
    auto ctx = (MaterialReloadCtx*)userdata;
    std::fprintf(stderr, "Reloading material\n");
    auto mat = get(state->materials, ctx->material.id);
    return reload_material(mat, ctx);
}

static void update_deps(Material* mat)
{
    StaticVector<ung_resource_id, 32> deps = {};
    // shaders from ung_shader_create don't have resources
    const auto vert_res = ung_shader_get_resource(mat->reload_ctx->vert);
    if (vert_res.id) {
        deps.append() = vert_res;
    }
    const auto frag_res = ung_shader_get_resource(mat->reload_ctx->frag);
    if (frag_res.id) {
        deps.append() = frag_res;
    }
    for (const auto tex : mat->reload_ctx->textures) {
        if (tex.id) {
            const auto res = ung_texture_get_resource(tex);
            if (res.id) {
                deps.append() = res;
            }
        }
    }
    ung_resource_set_deps(mat->resource, nullptr, 0, deps.data(), deps.size());
}

EXPORT ung_material_id ung_material_create(ung_material_create_params params)
{
    assert(params.mugfx.vert_shader.id == 0);
    assert(params.mugfx.frag_shader.id == 0);
    const auto vert = get(state->shaders, params.vert.id);
    const auto frag = get(state->shaders, params.frag.id);
    params.mugfx.vert_shader = vert->shader;
    params.mugfx.frag_shader = frag->shader;

    const auto [id, material] = state->materials.insert();
    material->material = mugfx_material_create(params.mugfx);

    // UngFrame
    material->bindings.append() = {
        .type = MUGFX_BINDING_TYPE_BUFFER,
        .buffer = { .binding = 0 }, // rest set in draw
    };
    // UngPass
    material->bindings.append() = {
        .type = MUGFX_BINDING_TYPE_BUFFER,
        .buffer = { .binding = 1 }, // rest set in draw
    };
    // UngTransform
    material->bindings.append() = {
        .type = MUGFX_BINDING_TYPE_BUFFER,
        .buffer = { .binding = 2 }, // rest set in draw
    };

    if (params.constant_data) {
        material->constant_buf = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_UNIFORM,
            .usage = MUGFX_BUFFER_USAGE_HINT_STATIC,
            .data = { params.constant_data, params.constant_data_size },
            .debug_label = "mat.constant",
        });
        material->bindings.append() = {
            .type = MUGFX_BINDING_TYPE_BUFFER,
            .buffer = { .binding = 8, .id = material->constant_buf },
        };
    }

    if (params.dynamic_data_size) {
        material->dynamic_data = allocate<uint8_t>(params.dynamic_data_size);
        material->dynamic_data_size = params.dynamic_data_size;

        material->dynamic_buf = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_UNIFORM,
            .usage = MUGFX_BUFFER_USAGE_HINT_DYNAMIC,
            .data = { nullptr, params.dynamic_data_size },
            .debug_label = "mat.dynamic",
        });
        material->bindings.append() = {
            .type = MUGFX_BINDING_TYPE_BUFFER,
            .buffer = { .binding = 9, .id = material->dynamic_buf },
        };
    }

    if (state->auto_reload) {
        material->reload_ctx = allocate<MaterialReloadCtx>();
        material->reload_ctx->material = { id };
        material->reload_ctx->params = params.mugfx;
        material->reload_ctx->constant_data_size = params.constant_data_size;
        material->reload_ctx->dynamic_data_size = params.dynamic_data_size;

        material->reload_ctx->vert = params.vert;
        material->reload_ctx->vert_version
            = ung_resource_get_version(ung_shader_get_resource(params.vert));
        material->reload_ctx->frag = params.frag;
        material->reload_ctx->frag_version
            = ung_resource_get_version(ung_shader_get_resource(params.frag));
        material->resource = ung_resource_create(material_reload_cb, material->reload_ctx);

        update_deps(material);
    }

    return { id };
}

EXPORT ung_material_id ung_material_load(
    const char* vert_path, const char* frag_path, ung_material_create_params params)
{
    LoadProfScope s("material");
    assert(params.vert.id == 0);
    assert(params.frag.id == 0);
    params.vert = ung_shader_load(MUGFX_SHADER_STAGE_VERTEX, vert_path);
    params.frag = ung_shader_load(MUGFX_SHADER_STAGE_FRAGMENT, frag_path);

    const auto mat_id = ung_material_create(params);
    auto mat = get_material(mat_id.id);
    mat->vert = params.vert;
    mat->frag = params.frag;

    if (state->auto_reload) {
        assign(mat->reload_ctx->vert_path, vert_path);
        assign(mat->reload_ctx->frag_path, frag_path);
    }

    return mat_id;
}

EXPORT bool ung_material_recreate(ung_material_id material_id, ung_material_create_params params)
{
    auto mat = get_material(material_id.id);

    assert(params.mugfx.vert_shader.id == 0);
    assert(params.mugfx.frag_shader.id == 0);
    auto vert = get(state->shaders, params.vert.id);
    auto frag = get(state->shaders, params.frag.id);
    params.mugfx.vert_shader = vert->shader;
    params.mugfx.frag_shader = frag->shader;

    const auto new_mat = mugfx_material_create(params.mugfx);
    if (!new_mat.id) {
        return false;
    }

    mugfx_material_destroy(mat->material);
    mat->material = new_mat;

    if (params.constant_data) {
        if (mat->constant_buf.id) {
            mugfx_buffer_destroy(mat->constant_buf);
        }
        mat->constant_buf = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_UNIFORM,
            .usage = MUGFX_BUFFER_USAGE_HINT_STATIC,
            .data = { params.constant_data, params.constant_data_size },
            .debug_label = "mat.constant",
        });
        for (auto& b : mat->bindings) {
            if (b.type == MUGFX_BINDING_TYPE_BUFFER && b.buffer.binding == 8) {
                b.buffer.id = mat->constant_buf;
                break;
            }
        }
    }

    if (params.dynamic_data_size) {
        if (mat->dynamic_buf.id) {
            mugfx_buffer_destroy(mat->dynamic_buf);
        }
        if (mat->dynamic_data) {
            deallocate(mat->dynamic_data, mat->dynamic_data_size);
        }

        mat->dynamic_data = allocate<uint8_t>(params.dynamic_data_size);
        mat->dynamic_data_size = params.dynamic_data_size;

        mat->dynamic_buf = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_UNIFORM,
            .usage = MUGFX_BUFFER_USAGE_HINT_DYNAMIC,
            .data = { nullptr, params.dynamic_data_size },
            .debug_label = "mat.dynamic",
        });
        for (auto& b : mat->bindings) {
            if (b.type == MUGFX_BINDING_TYPE_BUFFER && b.buffer.binding == 9) {
                b.buffer.id = mat->dynamic_buf;
                break;
            }
        }
    }

    if (mat->reload_ctx) {
        mat->reload_ctx->params = params.mugfx;
        mat->reload_ctx->constant_data_size = params.constant_data_size;
        mat->reload_ctx->dynamic_data_size = params.dynamic_data_size;
        mat->reload_ctx->vert = params.vert;
        mat->reload_ctx->vert_version = ung_resource_get_version(vert->resource);
        mat->reload_ctx->frag = params.frag;
        mat->reload_ctx->frag_version = ung_resource_get_version(frag->resource);

        update_deps(mat);
    }

    return true;
}

EXPORT bool ung_material_reload(ung_material_id material_id, const char* vert_path,
    const char* frag_path, ung_material_create_params params)
{
    assert(params.vert.id == 0);
    assert(params.frag.id == 0);

    auto mat = get_material(material_id.id);

    if (mat->vert.id) {
        ung_shader_reload(mat->vert, vert_path);
    } else {
        mat->vert = ung_shader_load(MUGFX_SHADER_STAGE_VERTEX, vert_path);
    }

    if (mat->frag.id) {
        ung_shader_reload(mat->frag, frag_path);
    } else {
        mat->frag = ung_shader_load(MUGFX_SHADER_STAGE_FRAGMENT, frag_path);
    }

    params.vert = mat->vert;
    params.frag = mat->frag;

    const auto res = ung_material_recreate(material_id, params);
    if (!res) {
        return false;
    }

    if (mat->reload_ctx) {
        assign(mat->reload_ctx->vert_path, vert_path);
        assign(mat->reload_ctx->frag_path, frag_path);
    }

    return true;
}

EXPORT void ung_material_destroy(ung_material_id material)
{
    auto mat = get_material(material.id);
    if (mat->constant_buf.id) {
        mugfx_buffer_destroy(mat->constant_buf);
    }
    if (mat->dynamic_data) {
        deallocate(mat->dynamic_data, mat->dynamic_data_size);
    }
    if (mat->dynamic_buf.id) {
        mugfx_buffer_destroy(mat->dynamic_buf);
    }
    mugfx_material_destroy(mat->material);

    if (mat->resource.id) {
        ung_resource_destroy(mat->resource);
    }

    if (mat->reload_ctx) {
        mat->reload_ctx->vert_path.free();
        mat->reload_ctx->frag_path.free();
        deallocate(mat->reload_ctx);
    }

    /* ung_shader_destroy is not implemented yet
    if (mat->vert.id) {
        ung_shader_destroy(mat->vert);
    }
    if (mat->frag.id) {
        ung_shader_destroy(mat->frag);
    }
    */

    state->materials.remove(material.id);
}

EXPORT void ung_material_set_binding(ung_material_id material, mugfx_draw_binding binding)
{
    auto mat = get_material(material.id);
    for (auto& b : mat->bindings) {
        if (is_same_binding(b, binding)) {
            b = binding;
            return;
        }
    }
    mat->bindings.append() = binding;
}

EXPORT void ung_material_set_buffer(ung_material_id material, u32 binding, mugfx_buffer_id buffer)
{
    ung_material_set_binding(material,
        {
            .type = MUGFX_BINDING_TYPE_BUFFER,
            .buffer = { .binding = binding, .id = buffer },
        });
}

static void store_texture(
    Material* mat, MaterialReloadCtx* ctx, u32 binding, ung_texture_id texture)
{
    for (size_t i = 0; i < mat->bindings.size(); ++i) {
        if (mat->bindings[i].type == MUGFX_BINDING_TYPE_TEXTURE
            && mat->bindings[i].texture.binding == binding) {
            ctx->textures[i] = texture;
        }
    }
}

EXPORT void ung_material_set_texture(ung_material_id material, u32 binding, ung_texture_id texture)
{
    const auto tex = get(state->textures, texture.id);

    ung_material_set_binding(material,
        {
            .type = MUGFX_BINDING_TYPE_TEXTURE,
            .texture = { .binding = binding, .id = tex->texture },
        });

    if (state->auto_reload) {
        auto mat = get_material(material.id);
        if (mat->reload_ctx) {
            store_texture(mat, mat->reload_ctx, binding, texture);
            update_deps(mat);
        }
    }
}

EXPORT void* ung_material_get_dynamic_data(ung_material_id material)
{
    auto mat = get_material(material.id);
    mat->dynamic_data_dirty = true;
    return mat->dynamic_data;
}

}