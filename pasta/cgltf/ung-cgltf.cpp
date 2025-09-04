#include "ung-cgltf.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <cgltf.h>

#define EXPORT extern "C"

template <typename T>
static T* allocate(size_t n = 1)
{
    auto alloc = ung_get_allocator();
    auto p = (T*)alloc->allocate(sizeof(T) * n, alloc->ctx);
    for (size_t i = 0; i < n; ++i) {
        new (p + i) T {};
    }
    return p;
}

template <typename T>
static void deallocate(T* ptr, size_t n = 1)
{
    auto alloc = ung_get_allocator();
    if (!ptr) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        (ptr + i)->~T();
    }
    alloc->deallocate(ptr, sizeof(T) * n, alloc->ctx);
}

static size_t get_num_components(cgltf_type type)
{
    switch (type) {
    case cgltf_type_scalar:
        return 1;
    case cgltf_type_vec2:
        return 2;
    case cgltf_type_vec3:
        return 3;
    case cgltf_type_vec4:
        return 4;
    default:
        return 0;
    }
}

static size_t get_default_stride(const cgltf_accessor* acc)
{
    const auto components = get_num_components(acc->type);
    switch (acc->component_type) {
    case cgltf_component_type_r_8:
    case cgltf_component_type_r_8u:
        return components;
    case cgltf_component_type_r_16:
    case cgltf_component_type_r_16u:
        return components * 2;
    case cgltf_component_type_r_32u:
    case cgltf_component_type_r_32f:
        return components * 4;
    default:
        return 0;
    }
}

static mugfx_vertex_attribute_type map_attrib_type(const cgltf_accessor* acc)
{
    const bool norm = acc->normalized;
    switch (acc->component_type) {
    case cgltf_component_type_r_32f:
        return MUGFX_VERTEX_ATTRIBUTE_TYPE_F32;
    case cgltf_component_type_r_16:
        return norm ? MUGFX_VERTEX_ATTRIBUTE_TYPE_I16_NORM : MUGFX_VERTEX_ATTRIBUTE_TYPE_I16;
    case cgltf_component_type_r_16u:
        return norm ? MUGFX_VERTEX_ATTRIBUTE_TYPE_U16_NORM : MUGFX_VERTEX_ATTRIBUTE_TYPE_U16;
    case cgltf_component_type_r_8:
        return norm ? MUGFX_VERTEX_ATTRIBUTE_TYPE_I8_NORM : MUGFX_VERTEX_ATTRIBUTE_TYPE_I8;
    case cgltf_component_type_r_8u:
        return norm ? MUGFX_VERTEX_ATTRIBUTE_TYPE_U8_NORM : MUGFX_VERTEX_ATTRIBUTE_TYPE_U8;
    default:
        return MUGFX_VERTEX_ATTRIBUTE_TYPE_DEFAULT;
    }
}

static mugfx_index_type map_index_type(cgltf_component_type type)
{
    switch (type) {
    case cgltf_component_type_r_8u:
        return MUGFX_INDEX_TYPE_U8;
    case cgltf_component_type_r_16u:
        return MUGFX_INDEX_TYPE_U16;
    case cgltf_component_type_r_32u:
        return MUGFX_INDEX_TYPE_U32;
    default:
        return MUGFX_INDEX_TYPE_DEFAULT;
    }
}

static mugfx_draw_mode map_draw_mode(cgltf_primitive_type type)
{
    switch (type) {
    case cgltf_primitive_type_triangles:
        return MUGFX_DRAW_MODE_TRIANGLES;
    case cgltf_primitive_type_triangle_strip:
        return MUGFX_DRAW_MODE_TRIANGLE_STRIP;
    case cgltf_primitive_type_lines:
        return MUGFX_DRAW_MODE_LINES;
    case cgltf_primitive_type_line_strip:
        return MUGFX_DRAW_MODE_LINE_STRIP;
    default:
        return MUGFX_DRAW_MODE_DEFAULT;
    }
}

static size_t get_location(const cgltf_attribute* attr)
{
    // TWEAK, possibly use attr->index
    switch (attr->type) {
    case cgltf_attribute_type_position:
        return 0;
    case cgltf_attribute_type_normal:
        return 2;
    case cgltf_attribute_type_texcoord:
        return 1;
    case cgltf_attribute_type_color:
        return 3;
    case cgltf_attribute_type_tangent:
        return 6;
    case cgltf_attribute_type_joints:
        return 4;
    case cgltf_attribute_type_weights:
        return 5;
    default:
        return SIZE_MAX;
    }
}

EXPORT ung_geometry_id get_geom_from_cgltf(const cgltf_primitive* prim)
{
    assert(prim);

    mugfx_geometry_create_params params {
        .draw_mode = map_draw_mode(prim->type),
    };

    params.vertex_count = params.vertex_count
        = prim->attributes[0].data ? prim->attributes[0].data->count : 0; // WHAT?

    // Maps vbuf index to buffer view
    std::array<const cgltf_buffer_view*, MUGFX_MAX_VERTEX_BUFFERS> vbuf_views = {};
    size_t vbuf_count = 0;

    // Vertex Buffers
    for (size_t ai = 0; ai < prim->attributes_count; ++ai) {
        const auto attr = &prim->attributes[ai];
        const auto acc = attr->data;
        assert(acc);

        const auto location = get_location(attr);
        if (location == SIZE_MAX) {
            continue; // skip unmapped
        }

        const cgltf_buffer_view* view = acc->buffer_view;
        assert(view);
        const cgltf_buffer* buffer = view->buffer;
        assert(buffer && buffer->data);

        // Create or reuse one vertex buffer per buffer view
        // TODO: Pack everything into a single vertex buffer

        // Find existing vertex buffer
        mugfx_vertex_buffer* vbuf = nullptr;
        for (size_t v = 0; v < vbuf_count; ++v) {
            if (vbuf_views[v] == view) {
                vbuf = &params.vertex_buffers[v];
                break;
            }
        }

        // No vertex buffer found
        if (vbuf == nullptr) {
            if (vbuf_count >= MUGFX_MAX_VERTEX_BUFFERS) {
                std::fprintf(stderr, "Too many buffer views in cgltf primitive");
                return { 0 };
            }
            vbuf_views[vbuf_count] = view;
            vbuf = &params.vertex_buffers[vbuf_count++];

            vbuf->buffer = mugfx_buffer_create({
                .target = MUGFX_BUFFER_TARGET_ARRAY,
                .usage = MUGFX_BUFFER_USAGE_HINT_STATIC,
                .data = {
                    .data = (uint8_t*)buffer->data + view->offset,
                    .length = view->size,
                },
            });

            // We upload only the view's bytes, so offsets in accessors are relative to 0.
            vbuf->buffer_offset = 0;
            vbuf->stride = view->stride ? view->stride : get_default_stride(acc);
        }

        for (size_t j = 0; j < MUGFX_MAX_VERTEX_ATTRIBUTES; ++j) {
            if (vbuf->attributes[j].components == 0) {
                vbuf->attributes[j] = {
                    .location = location,
                    .components = get_num_components(acc->type),
                    .type = map_attrib_type(acc),
                    .offset = acc->offset, // relative to buffer view and therefore vbuf
                };
                break;
            }
        }
    }

    // Index Buffer (optional)
    if (prim->indices) {
        const auto acc = prim->indices;
        const auto view = acc->buffer_view;
        const auto buffer = acc->buffer_view->buffer;
        assert(buffer && buffer->data);

        params.index_buffer = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_INDEX,
            .usage = MUGFX_BUFFER_USAGE_HINT_STATIC,
            .data = {
                .data = (uint8_t*)buffer->data + view->offset,
                .length = view->size,
            },
        });

        params.index_type = map_index_type(acc->component_type);
        params.index_buffer_offset = (size_t)acc->offset;
        params.index_count = acc->count;
    }

    return ung_geometry_create(params);
}

static int16_t get_joint_index(const cgltf_skin* skin, const cgltf_node* node)
{
    // node may be null
    for (size_t i = 0; i < skin->joints_count; ++i) {
        if (skin->joints[i] == node) {
            return (int16_t)i;
        }
    }
    return -1;
}

EXPORT uint16_t get_joints_from_cgltf(
    const cgltf_skin* skin, ung_skeleton_joint* joints, uint16_t max_num_joints)
{
    assert(skin);

    const auto num_joints = (uint16_t)skin->joints_count;
    if (num_joints > max_num_joints) {
        return num_joints;
    }

    // Inverse bind matrices
    const cgltf_accessor* ibm_acc = skin->inverse_bind_matrices;
    if (!ibm_acc) { // optional in glTF
        // TODO: Determine from node transforms
        std::fprintf(stderr, "No inverse bind matrices\n");
        return 0;
    }
    assert(num_joints == ibm_acc->count);

    for (uint16_t i = 0; i < num_joints; ++i) {
        const auto node = skin->joints[i];
        assert(node);
        cgltf_accessor_read_float(ibm_acc, i, joints[i].inverse_bind_matrix, 16);
        // If the parent is not in the skin, we just treat is as the root
        joints[i].parent_index = get_joint_index(skin, node->parent);
    }

    return num_joints;
}

static const uint8_t* get_data(const cgltf_accessor* acc)
{
    return cgltf_buffer_view_data(acc->buffer_view) + acc->offset;
}

static size_t read_vec3s(const cgltf_accessor* acc, float* dest)
{
    assert(acc->component_type == cgltf_component_type_r_32f && acc->type == cgltf_type_vec3);
    assert(acc->buffer_view->stride == 0 && !acc->is_sparse);
    std::memcpy(dest, get_data(acc), acc->count * 3 * sizeof(float));
    return acc->count * 3;
}

static size_t read_quats(const cgltf_accessor* acc, float* dest)
{
    assert(acc->component_type == cgltf_component_type_r_32f && acc->type == cgltf_type_vec4);
    assert(acc->buffer_view->stride == 0 && !acc->is_sparse);
    std::memcpy(dest, get_data(acc), acc->count * 4 * sizeof(float));
    return acc->count * 4;
}

static ung_animation_interp map_interp(cgltf_interpolation_type t)
{
    switch (t) {
    case cgltf_interpolation_type_step:
        return UNG_ANIM_INTERP_STEP;
    case cgltf_interpolation_type_linear:
        return UNG_ANIM_INTERP_LINEAR;
    default:
        return UNG_ANIM_INTERP_INVALID;
    }
}

EXPORT ung_animation_id get_anim_from_cgltf(const cgltf_animation* anim, const cgltf_skin* skin)
{
    assert(anim && skin);

    // First pass to figure out how much memory we need to allocate and exit out early if necessary
    size_t num_channels = 0;
    size_t total_values_floats = 0;

    for (cgltf_size i = 0; i < anim->channels_count; ++i) {
        const auto src_channel = &anim->channels[i];
        const auto sampler = src_channel->sampler;
        assert(sampler && sampler->input && sampler->output);
        assert(sampler->input->count == sampler->output->count);

        const auto interp = map_interp(sampler->interpolation);
        if (interp == UNG_ANIM_INTERP_INVALID) {
            std::fprintf(stderr, "Unsupported interpolation type: %d\n", sampler->interpolation);
            return { 0 };
        }

        const auto joint = get_joint_index(skin, src_channel->target_node);
        if (joint < 0) {
            continue; // target not part of this skin
        }

        const auto num_samples = sampler->input->count;
        if (num_samples == 0) {
            continue;
        }

        switch (src_channel->target_path) {
        case cgltf_animation_path_type_translation:
            total_values_floats += sampler->output->count * 3;
            num_channels++;
            break;
        case cgltf_animation_path_type_rotation:
            total_values_floats += sampler->output->count * 4;
            num_channels++;
            break;
        case cgltf_animation_path_type_scale:
            total_values_floats += sampler->output->count * 3;
            num_channels++;
            break;
        default:
            continue; // ignore weights/morph targets
        }
    }

    auto channels = allocate<ung_animation_channel>(num_channels);
    size_t channel_idx = 0;
    auto values = allocate<float>(total_values_floats);
    float* values_ptr = values;
    float duration = 0.0f;

    for (cgltf_size i = 0; i < anim->channels_count; ++i) {
        const auto src_channel = &anim->channels[i];
        const auto sampler = src_channel->sampler;

        const auto joint = get_joint_index(skin, src_channel->target_node);
        if (joint < 0) {
            continue; // target not part of this skin
        }

        const auto num_samples = sampler->input->count;
        if (num_samples == 0) {
            continue;
        }

        // times
        assert(sampler->input->component_type == cgltf_component_type_r_32f);
        assert(sampler->input->type == cgltf_type_scalar);
        const auto times = (const float*)get_data(sampler->input);

        float last_time = 0.0f;
        // avoid UB by copying to float
        std::memcpy(&last_time, times + num_samples - 1, sizeof(float));

        if (last_time > duration) {
            duration = last_time;
        }

        // values
        ung_animation_channel channel = {
            .key = { (uint16_t)joint, UNG_JOINT_DOF_INVALID },
            .interp_type = map_interp(sampler->interpolation),
            .num_samples = num_samples,
            .times = times,
            .values = values_ptr,
        };

        switch (src_channel->target_path) {
        case cgltf_animation_path_type_translation:
            channel.key.dof = UNG_JOINT_DOF_TRANSLATION;
            channel.sampler_type = UNG_ANIM_SAMPLER_TYPE_VEC3;
            values_ptr += read_vec3s(sampler->output, values_ptr);
            break;
        case cgltf_animation_path_type_rotation:
            channel.key.dof = UNG_JOINT_DOF_ROTATION;
            channel.sampler_type = UNG_ANIM_SAMPLER_TYPE_QUAT;
            values_ptr += read_quats(sampler->output, values_ptr);
            break;
        case cgltf_animation_path_type_scale:
            channel.key.dof = UNG_JOINT_DOF_SCALE;
            channel.sampler_type = UNG_ANIM_SAMPLER_TYPE_VEC3;
            values_ptr += read_vec3s(sampler->output, values_ptr);
            break;
        default:
            continue;
        }

        channels[channel_idx++] = channel;
    }
    assert(values_ptr == values + total_values_floats);
    assert(channel_idx == num_channels);

    const auto anim_id = ung_animation_create({
        .channels = channels,
        .num_channels = num_channels,
        .duration_s = duration,
    });

    return anim_id;
}