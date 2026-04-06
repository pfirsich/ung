#include "state.hpp"

namespace ung::transform {
um_mat get_world_matrix(ung_transform_id trafo);
mugfx_uniform_data_id get_uniform_data(ung_transform_id trafo);
}

namespace ung::render {

void init(ung_init_params params)
{
    state->cameras.init(params.max_num_cameras ? params.max_num_cameras : 8);

    state->constant_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_CONSTANT,
        .size = sizeof(UConstant),
        .cpu_buffer = &state->u_constant,
        .debug_label = "UngConstant",
    });

    state->frame_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
        .size = sizeof(UFrame),
        .cpu_buffer = &state->u_frame,
        .debug_label = "UngFrame",
    });

    state->camera_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
        .size = sizeof(UCamera),
        .cpu_buffer = &state->u_camera,
        .debug_label = "UngCamera",
    });

    state->identity_trafo = ung_transform_create();
}

void shutdown()
{
    ung_transform_destroy(state->identity_trafo);

    mugfx_uniform_data_destroy(state->camera_data);
    mugfx_uniform_data_destroy(state->frame_data);
    mugfx_uniform_data_destroy(state->constant_data);

    for (u32 i = 0; i < state->cameras.capacity(); ++i) {
        const auto key = state->cameras.get_key(i);
        if (key) {
            ung_camera_destroy({ key });
        }
    }
    state->cameras.free();
}

Camera* get_camera(u64 key)
{
    return get(state->cameras, key);
}

EXPORT ung_camera_id ung_camera_create()
{
    const auto [id, camera] = state->cameras.insert();
    camera->transform = ung_transform_create();
    return { id };
}

EXPORT void ung_camera_destroy(ung_camera_id camera)
{
    auto cam = get_camera(camera.id);
    ung_transform_destroy(cam->transform);
    state->cameras.remove(camera.id);
}

static void set_projection(Camera* camera, const um_mat& proj)
{
    camera->projection = proj;
    camera->projection_inv = um_mat_invert(proj);
}

EXPORT void ung_camera_set_projection(ung_camera_id camera, const float matrix[16])
{
    um_mat proj;
    std::memcpy(&proj, matrix, sizeof(float) * 16);
    set_projection(get_camera(camera.id), proj);
}

EXPORT void ung_camera_set_perspective(
    ung_camera_id camera, float fovy_deg, float aspect, float near, float far)
{
    const auto proj = um_mat_perspective(um_deg { fovy_deg }, aspect, near, far);
    set_projection(get_camera(camera.id), proj);
}

EXPORT void ung_camera_set_orthographic_fullscreen(ung_camera_id camera)
{
    const auto proj
        = um_mat_ortho(0.0f, (float)state->win_width, (float)state->win_height, 0.0f, -1.0f, 1.0f);
    set_projection(get_camera(camera.id), proj);
}

EXPORT void ung_camera_set_orthographic(
    ung_camera_id camera, float left, float right, float bottom, float top)
{
    const auto proj = um_mat_ortho(left, right, bottom, top, -1.0f, 1.0f);
    set_projection(get_camera(camera.id), proj);
}

EXPORT void ung_camera_set_orthographic_z(
    ung_camera_id camera, float left, float right, float bottom, float top, float near, float far)
{
    const auto proj = um_mat_ortho(left, right, bottom, top, near, far);
    set_projection(get_camera(camera.id), proj);
}

EXPORT ung_transform_id ung_camera_get_transform(ung_camera_id camera)
{
    return get_camera(camera.id)->transform;
}

EXPORT void ung_camera_get_view_matrix(ung_camera_id camera, float matrix[16])
{
    auto cam = get_camera(camera.id);
    const auto view = um_mat_invert(transform::get_world_matrix(cam->transform));
    std::memcpy(matrix, &view, sizeof(float) * 16);
}

EXPORT void ung_camera_get_projection_matrix(ung_camera_id camera, float matrix[16])
{
    std::memcpy(matrix, &get_camera(camera.id)->projection, sizeof(float) * 16);
}

void begin_frame()
{
    mugfx_begin_frame();
    state->u_frame.time.x = ung_get_time();
    state->u_frame.time.y = (float)state->frame_counter;
    mugfx_uniform_data_update(state->frame_data);
}

EXPORT void ung_begin_pass(mugfx_render_target_id target, ung_camera_id camera)
{
    mugfx_begin_pass(target);

    auto cam = get_camera(camera.id);
    state->u_camera.projection = cam->projection;
    state->u_camera.projection_inv = cam->projection_inv;
    state->u_camera.view_inv = transform::get_world_matrix(cam->transform);
    state->u_camera.view = um_mat_invert(state->u_camera.view_inv);
    state->u_camera.view_projection = um_mat_mul(state->u_camera.projection, state->u_camera.view);
    state->u_camera.view_projection_inv = um_mat_invert(state->u_camera.view_projection);
    mugfx_uniform_data_update(state->camera_data);

    if (!target.id) {
        mugfx_set_viewport(0, 0, state->fb_width, state->fb_height);
    }
}

static mugfx_uniform_data_id update_uniform_data(ung_transform_id transform)
{
    if (transform.id == 0) {
        transform = state->identity_trafo;
    }
    UTransform u_trafo;
    u_trafo.model = transform::get_world_matrix(transform);
    u_trafo.model_inv = um_mat_invert(u_trafo.model);
    u_trafo.model_view = um_mat_mul(state->u_camera.view, u_trafo.model);
    u_trafo.model_view_projection = um_mat_mul(state->u_camera.projection, u_trafo.model_view);
    const auto uniform_data = transform::get_uniform_data(transform);
    std::memcpy(mugfx_uniform_data_get_ptr(uniform_data), &u_trafo, sizeof(UTransform));
    mugfx_uniform_data_update(uniform_data);
    return uniform_data;
}

EXPORT void ung_draw(ung_material_id material, ung_geometry_id geometry, ung_transform_id transform)
{
    ung_draw_ex(material, geometry, transform, {});
}

EXPORT void ung_draw_ex(ung_material_id material, ung_geometry_id geometry,
    ung_transform_id transform, ung_draw_ex_params params)
{
    auto mat = get(state->materials, material.id);
    auto geom = get(state->geometries, geometry.id);

    StaticVector<mugfx_draw_binding, MUGFX_MAX_SHADER_BINDINGS> draw_bindings {};
    std::memcpy(draw_bindings.data(), mat->bindings.data(),
        sizeof(mugfx_draw_binding) * mat->bindings.size());
    draw_bindings.size_ = mat->bindings.size();
    assert(draw_bindings[3].uniform_data.binding == 3);
    // TODO: maybe avoid upload if transform is overriden
    draw_bindings[3].uniform_data.id = update_uniform_data(transform);

    if (params.num_binding_overrides) {
        assert(params.binding_overrides);
        for (size_t i = 0; i < params.num_binding_overrides; ++i) {
            const auto override = params.binding_overrides[i];
            bool found = false;
            for (size_t j = 0; j < draw_bindings.size(); ++j) {
                if (is_same_binding(draw_bindings[j], override)) {
                    draw_bindings[j] = override;
                    found = true;
                    break;
                }
            }
            if (!found) {
                UNG_OR_PANIC(draw_bindings.size() < MUGFX_MAX_SHADER_BINDINGS,
                    "Too many draw bindings after overrides (%zu > %u)", draw_bindings.size() + 1,
                    MUGFX_MAX_SHADER_BINDINGS);
                draw_bindings.append() = override;
            }
        }
    }

    mugfx_draw_instanced(mat->material, geom->geometry, draw_bindings.data(), draw_bindings.size(),
        params.instance_count);
}

EXPORT void ung_draw_instanced(ung_material_id material, ung_geometry_id geometry,
    ung_transform_id transform, uint32_t instance_count)
{
    ung_draw_ex(material, geometry, transform, { .instance_count = instance_count });
}

EXPORT void ung_end_pass()
{
    mugfx_end_pass();
}

void end_frame()
{
    mugfx_end_frame();
}

}