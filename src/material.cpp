#include "mugfx.h"
#include "state.hpp"
#include "types.hpp"
#include "ung.h"

#include <cstdio>

namespace ung {

struct MaterialPending {
    const char* error;
};

struct MaterialResource {
    ung_material_id material;
    char* vert_path;
    char* frag_path;
    ung_material_create_params params;
    MaterialPending* pending;
};

Material* get_material(u64 key)
{
    return get(state->materials, key);
}

static size_t get_binding(Material& mat, mugfx_draw_binding binding)
{
    for (size_t i = 0; i < mat.bindings.size(); ++i) {
        if (is_same_binding(mat.bindings[i], binding)) {
            return i;
        }
    }
    mat.bindings.append() = binding;
    return mat.bindings.size() - 1;
}

static void set_binding(Material& mat, mugfx_draw_binding binding)
{
    const auto idx = get_binding(mat, binding);
    mat.bindings[idx] = binding;
}

static bool res_material_upload(ung_resource_id res_id, void* instance)
{
    auto res = (MaterialResource*)instance;
    auto mat = get_material(res->material.id);
    auto& params = res->params;

    if (res->vert_path) {
        assert(!params.vert.id);
        params.vert = ung_shader_load(MUGFX_SHADER_STAGE_VERTEX, res->vert_path);
    }
    if (res->frag_path) {
        assert(!params.frag.id);
        params.frag = ung_shader_load(MUGFX_SHADER_STAGE_FRAGMENT, res->frag_path);
    }

    const auto vert = get(state->shaders, params.vert.id);
    const auto frag = get(state->shaders, params.frag.id);

    if (vert->resource.id) {
        ung_resource_depend_res(vert->resource);
        ung_resource_wait_ready(vert->resource);
    }
    if (frag->resource.id) {
        ung_resource_depend_res(frag->resource);
        ung_resource_wait_ready(frag->resource);
    }

    params.mugfx.vert_shader = vert->shader;
    params.mugfx.frag_shader = frag->shader;

    const auto mugfx_mat = mugfx_material_create(params.mugfx);
    if (!mugfx_mat.id) {
        res->pending = allocate<MaterialPending>();
        res->pending->error = "Could not create material";
        return false;
    }

    if (mat->material.id) {
        mugfx_material_destroy(mat->material);
    }
    mat->material = mugfx_mat;

    return true;
}

static const char* res_material_get_error(void* instance)
{
    auto res = (MaterialResource*)instance;
    return res->pending ? res->pending->error : nullptr;
}

static void res_material_cleanup_load(ung_resource_id self, void* instance)
{
    auto res = (MaterialResource*)instance;
    if (res->pending) {
        deallocate(res->pending);
        res->pending = nullptr;
    }
}

static void res_material_destroy(ung_resource_id self, void* instance)
{
    auto res = (MaterialResource*)instance;
    const auto mat_id = res->material.id;
    auto mat = get(state->materials, mat_id);

    deallocate(res);

    if (mat->constant_buf.id) {
        mugfx_buffer_destroy(mat->constant_buf);
    }
    if (mat->dynamic_data) {
        deallocate(mat->dynamic_data, mat->dynamic_data_size);
    }
    if (mat->dynamic_buf.id) {
        mugfx_buffer_destroy(mat->dynamic_buf);
    }
    if (mat->material.id) {
        mugfx_material_destroy(mat->material);
    }
    state->materials.remove(mat_id);
}

static ung_resource_type_id material_resource()
{
    static ung_resource_type_id res_type = {};
    if (!res_type.id) {
        res_type = ung_resource_type_register({
            .type_name = "material",
            .upload = res_material_upload,
            .get_error = res_material_get_error,
            .cleanup_load = res_material_cleanup_load,
            .destroy = res_material_destroy,
        });
    }
    return res_type;
}

static ung_material_id load_material(MaterialResource* mat_res)
{
    const auto [id, mat] = state->materials.insert();
    mat_res->material = { id };
    // We cannot use any dedup on materials, because we often create the same material multiple
    // times, because we intend to use them with different textures.
    const auto [res, created] = ung_resource_load(material_resource(), nullptr, mat_res);

    if (!created) {
        state->materials.remove(id);
        deallocate(mat_res);
        return ((MaterialResource*)ung_resource_instance(res))->material;
    }

    // Prepare bindings so they can be set already (with ung_material_set_*)
    // The actual buffers will be set in ung_draw.
    // UngFrame
    mat->bindings.append() = { .type = MUGFX_BINDING_TYPE_BUFFER, .buffer = { .binding = 0 } };
    // UngPass
    mat->bindings.append() = { .type = MUGFX_BINDING_TYPE_BUFFER, .buffer = { .binding = 1 } };
    // UngTransform
    mat->bindings.append() = { .type = MUGFX_BINDING_TYPE_BUFFER, .buffer = { .binding = 2 } };

    // We create the buffers here, because we only want it to happen once and we don't want to copy
    // constant data (to pending).
    if (mat_res->params.constant_data) {
        mat->constant_buf = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_UNIFORM,
            .usage = MUGFX_BUFFER_USAGE_HINT_STATIC,
            .data = { mat_res->params.constant_data, mat_res->params.constant_data_size },
            .debug_label = "mat.constant",
        });
        mat->bindings.append() = {
            .type = MUGFX_BINDING_TYPE_BUFFER,
            .buffer = { .binding = 8, .id = mat->constant_buf },
        };
    }

    if (mat_res->params.dynamic_data_size) {
        mat->dynamic_data = allocate<uint8_t>(mat_res->params.dynamic_data_size);
        mat->dynamic_data_size = mat_res->params.dynamic_data_size;

        mat->dynamic_buf = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_UNIFORM,
            .usage = MUGFX_BUFFER_USAGE_HINT_DYNAMIC,
            .data = { nullptr, mat_res->params.dynamic_data_size },
            .debug_label = "mat.dynamic",
        });

        mat->bindings.append() = {
            .type = MUGFX_BINDING_TYPE_BUFFER,
            .buffer = { .binding = 9, .id = mat->dynamic_buf },
        };
    }

    mat->resource = res;

    return { id };
}

EXPORT ung_material_id ung_material_create(ung_material_create_params params)
{
    auto mat_res = allocate<MaterialResource>();
    mat_res->params = params;
    return load_material(mat_res);
}

EXPORT ung_material_id ung_material_load(
    const char* vert_path, const char* frag_path, ung_material_create_params params)
{
    assert(params.vert.id == 0);
    assert(params.frag.id == 0);

    auto mat_res = allocate<MaterialResource>();
    mat_res->params = params;
    mat_res->vert_path = allocate_string(vert_path);
    mat_res->frag_path = allocate_string(frag_path);
    return load_material(mat_res);
}

EXPORT void ung_material_destroy(ung_material_id material)
{
    auto mat = get_material(material.id);
    ung_resource_destroy(mat->resource);
}

EXPORT void ung_material_set_binding(ung_material_id material, mugfx_draw_binding binding)
{
    set_binding(*get_material(material.id), binding);
}

EXPORT void ung_material_set_buffer(ung_material_id material, u32 binding, mugfx_buffer_id buffer)
{
    ung_material_set_binding(material,
        {
            .type = MUGFX_BINDING_TYPE_BUFFER,
            .buffer = { .binding = binding, .id = buffer },
        });
}

EXPORT void ung_material_set_texture(ung_material_id material, u32 binding, ung_texture_id texture)
{
    assert(texture.id);

    auto mat = get_material(material.id);
    const auto idx = get_binding(*mat,
        {
            .type = MUGFX_BINDING_TYPE_TEXTURE,
            .texture = { .binding = binding },
        });
    mat->textures[idx] = texture;
}

EXPORT void* ung_material_get_dynamic_data(ung_material_id material)
{
    auto mat = get_material(material.id);
    mat->dynamic_data_dirty = true;
    return mat->dynamic_data;
}

EXPORT ung_resource_id ung_material_resource(ung_material_id id)
{
    return get(state->materials, id.id)->resource;
}

}