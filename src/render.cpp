#include "state.hpp"

namespace ung::transform {
um_mat get_world_matrix(ung_transform_id trafo);
}

namespace ung::render {

void init(ung_init_params params)
{
    state->cameras.init(params.max_num_cameras ? params.max_num_cameras : 8);

    state->u_constant_buf = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_UNIFORM,
        .usage = MUGFX_BUFFER_USAGE_HINT_STATIC,
        .data = { nullptr, sizeof(UConstant) },
        .debug_label = "UngConstant",
    });
    mugfx_buffer_update(state->u_constant_buf, 0, { &state->u_constant_data, sizeof(UConstant) });

    state->u_frame_buf = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_UNIFORM,
        .usage = MUGFX_BUFFER_USAGE_HINT_DYNAMIC,
        .data = { nullptr, sizeof(UFrame) },
        .debug_label = "UngFrame",
    });

    state->u_camera_buf = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_UNIFORM,
        .usage = MUGFX_BUFFER_USAGE_HINT_DYNAMIC,
        .data = { nullptr, sizeof(UCamera) },
        .debug_label = "UngCamera",
    });

    state->u_transform_buf = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_UNIFORM,
        .usage = MUGFX_BUFFER_USAGE_HINT_STREAM,
        .data = { nullptr, sizeof(UTransform) },
        .debug_label = "UngTransform",
    });
}

void shutdown()
{
    mugfx_buffer_destroy(state->u_transform_buf);
    mugfx_buffer_destroy(state->u_camera_buf);
    mugfx_buffer_destroy(state->u_frame_buf);
    mugfx_buffer_destroy(state->u_constant_buf);

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

    UFrame frame_data = {
        .time = {
            .x = ung_get_time(),
            .y = (float)state->frame_counter,
        },
    };

    mugfx_buffer_update(state->u_frame_buf, 0, {}); // orphan
    mugfx_buffer_update(state->u_frame_buf, 0, { &frame_data, sizeof(UFrame) });
}

EXPORT void ung_begin_pass(mugfx_render_target_id target, ung_camera_id camera)
{
    mugfx_begin_pass(target);

    auto cam = get_camera(camera.id);
    state->pass_camera.projection = cam->projection;
    state->pass_camera.projection_inv = cam->projection_inv;
    state->pass_camera.view_inv = transform::get_world_matrix(cam->transform);
    state->pass_camera.view = um_mat_invert(state->pass_camera.view_inv);
    state->pass_camera.view_projection
        = um_mat_mul(state->pass_camera.projection, state->pass_camera.view);
    state->pass_camera.view_projection_inv = um_mat_invert(state->pass_camera.view_projection);

    mugfx_buffer_update(state->u_camera_buf, 0, {}); // orphan
    mugfx_buffer_update(state->u_camera_buf, 0, { &state->pass_camera, sizeof(UCamera) });

    if (!target.id) {
        mugfx_set_viewport(0, 0, state->fb_width, state->fb_height);
    }
}

static void upload_transform(ung_transform_id transform)
{
    UTransform trafo_data;
    if (transform.id) {
        trafo_data.model = transform::get_world_matrix(transform);
    } else {
        trafo_data.model = um_mat_identity();
    }
    trafo_data.model_inv = um_mat_invert(trafo_data.model);
    trafo_data.model_view = um_mat_mul(state->pass_camera.view, trafo_data.model);
    trafo_data.model_view_projection
        = um_mat_mul(state->pass_camera.projection, trafo_data.model_view);

    mugfx_buffer_update(state->u_transform_buf, 0, { &trafo_data, sizeof(UTransform) });
}

EXPORT void ung_draw(ung_material_id material, ung_geometry_id geometry, ung_transform_id transform)
{
    ung_draw_ex(material, geometry, transform, {});
}

static mugfx_draw_binding buffer_binding(uint32_t binding, mugfx_buffer_id buffer)
{
    return { .type = MUGFX_BINDING_TYPE_BUFFER, .buffer = { binding, buffer } };
}

EXPORT void ung_draw_ex(ung_material_id material, ung_geometry_id geometry,
    ung_transform_id transform, ung_draw_ex_params params)
{
    auto mat = get(state->materials, material.id);
    auto geom = get(state->geometries, geometry.id);

    if (mat->dynamic_data_dirty && mat->dynamic_data) {
        if (mat->last_update_frame == state->frame_counter) {
            mugfx_buffer_update(mat->dynamic_buf, 0, {}); // orphan
        }
        mugfx_buffer_update(mat->dynamic_buf, 0, { mat->dynamic_data, mat->dynamic_data_size });
        mat->dynamic_data_dirty = false;
        mat->last_update_frame = state->frame_counter;
    }

    StaticVector<mugfx_draw_binding, MUGFX_MAX_SHADER_BINDINGS> draw_bindings {};
    std::memcpy(draw_bindings.data(), mat->bindings.data(),
        sizeof(mugfx_draw_binding) * mat->bindings.size());
    draw_bindings.size_ = mat->bindings.size();

    // TODO: maybe avoid upload if transform is overriden
    draw_bindings[0] = buffer_binding(0, state->u_constant_buf);
    draw_bindings[1] = buffer_binding(1, state->u_frame_buf);
    draw_bindings[2] = buffer_binding(2, state->u_camera_buf);
    draw_bindings[3] = buffer_binding(3, state->u_transform_buf);
    upload_transform(transform);

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
