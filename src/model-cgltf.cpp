#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

#include <cgltf.h>

#include "types.hpp"

namespace ung::model {
static u8 get_num_components(cgltf_type type)
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

static u32 get_location(const cgltf_attribute* attr)
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
        return UINT32_MAX;
    }
}

EXPORT ung_geometry_id ung_geometry_from_cgltf(const cgltf_primitive* prim)
{
    assert(prim);

    mugfx_geometry_create_params params {
        .draw_mode = map_draw_mode(prim->type),
    };

    params.vertex_count
        = prim->attributes[0].data ? (u32)prim->attributes[0].data->count : 0; // WHAT?

    // Maps vbuf index to buffer view
    std::array<const cgltf_buffer_view*, MUGFX_MAX_VERTEX_BUFFERS> vbuf_views = {};
    size_t vbuf_count = 0;

    // Vertex Buffers
    for (size_t ai = 0; ai < prim->attributes_count; ++ai) {
        const auto attr = &prim->attributes[ai];
        const auto acc = attr->data;
        assert(acc);

        const auto location = get_location(attr);
        if (location == UINT32_MAX) {
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
        params.index_count = (u32)acc->count;
    }

    return ung_geometry_create(params);
}

static const cgltf_accessor* find_attribute(
    const cgltf_primitive* prim, cgltf_attribute_type type, int index = 0)
{
    for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
        const auto& a = prim->attributes[i];
        if (a.type == type && a.index == index) {
            return a.data;
        }
    }
    return nullptr;
}

EXPORT ung_geometry_data ung_geometry_data_from_cgltf(const cgltf_primitive* prim)
{
    assert(prim);

    if (prim->type != cgltf_primitive_type_triangles) {
        ung_panicf("ung_geometry_data_from_cgltf: only triangle primitives are supported");
    }

    const auto pos_acc = find_attribute(prim, cgltf_attribute_type_position, 0);

    if (!pos_acc || pos_acc->type != cgltf_type_vec3) {
        ung_panicf("ung_geometry_data_from_cgltf: No vec3 position attribute");
    }
    if (pos_acc->count > UINT32_MAX) {
        ung_panicf("ung_geometry_data_from_cgltf: Too many vertices");
    }

    const auto norm_acc = find_attribute(prim, cgltf_attribute_type_normal, 0);
    if (norm_acc && norm_acc->type != cgltf_type_vec3) {
        ung_panicf("ung_geometry_data_from_cgltf: Normal attribute is not vec3");
    }

    const auto uv_acc = find_attribute(prim, cgltf_attribute_type_texcoord, 0);
    if (uv_acc && uv_acc->type != cgltf_type_vec2) {
        ung_panicf("ung_geometry_data_from_cgltf: Texture coordinate attribute is not vec2");
    }

    const auto col_acc = find_attribute(prim, cgltf_attribute_type_color, 0);
    if (col_acc && col_acc->type != cgltf_type_vec3 && col_acc->type != cgltf_type_vec4) {
        ung_panicf("ung_geometry_data_from_cgltf: Color attribute is not vec3 or vec4");
    }

    if ((norm_acc && norm_acc->count != pos_acc->count)
        || (uv_acc && uv_acc->count != pos_acc->count)
        || (col_acc && col_acc->count != pos_acc->count)) {
        ung_panicf("ung_geometry_data_from_cgltf: Mismatching number of attributes");
    }

    ung_geometry_data out = {
        .num_vertices = (u32)pos_acc->count,
    };

    const size_t floats_per_vertex = 3 + (norm_acc ? 3 : 0) + (uv_acc ? 2 : 0) + (col_acc ? 4 : 0);
    out.positions = allocate<float>(out.num_vertices * floats_per_vertex);

    size_t off = out.num_vertices * 3;
    if (norm_acc) {
        out.normals = out.positions + off;
        off += out.num_vertices * (norm_acc ? 3 : 0);
    }
    if (uv_acc) {
        out.texcoords = out.positions + off;
        off += out.num_vertices * (uv_acc ? 2 : 0);
    }
    if (col_acc) {
        out.colors = out.positions + off;
    }

    if (cgltf_accessor_unpack_floats(pos_acc, out.positions, out.num_vertices * 3)
        != out.num_vertices * 3) {
        ung_panicf("ung_geometry_data_from_cgltf: Error reading position data");
    }

    if (norm_acc) {
        if (cgltf_accessor_unpack_floats(norm_acc, out.normals, out.num_vertices * 3)
            != out.num_vertices * 3) {
            ung_panicf("ung_geometry_data_from_cgltf: Error reading normal data");
        }
    }

    if (uv_acc) {
        if (cgltf_accessor_unpack_floats(uv_acc, out.texcoords, out.num_vertices * 2)
            != out.num_vertices * 2) {
            ung_panicf("ung_geometry_data_from_cgltf: Error reading texture coordinate data");
        }
    }

    if (col_acc) {
        if (col_acc->type == cgltf_type_vec4) {
            if (cgltf_accessor_unpack_floats(col_acc, out.colors, out.num_vertices * 4)
                != out.num_vertices * 4) {
                ung_panicf("ung_geometry_data_from_cgltf: Error reading color data");
            }
        } else if (col_acc->type == cgltf_type_vec3) {
            auto tmp = allocate<float>(out.num_vertices * 3);
            const auto got = cgltf_accessor_unpack_floats(col_acc, tmp, out.num_vertices * 3);
            if (got != out.num_vertices * 3) {
                deallocate(tmp, out.num_vertices * 3);
                ung_panicf("ung_geometry_data_from_cgltf: Error reading color data");
            }
            for (u32 i = 0; i < out.num_vertices; ++i) {
                out.colors[i * 4 + 0] = tmp[i * 3 + 0];
                out.colors[i * 4 + 1] = tmp[i * 3 + 1];
                out.colors[i * 4 + 2] = tmp[i * 3 + 2];
                out.colors[i * 4 + 3] = 1.0f;
            }
            deallocate(tmp, out.num_vertices * 3);
        }
    }

    if (prim->indices) {
        const auto* idx_acc = prim->indices;
        if (idx_acc->count > UINT32_MAX) {
            ung_panicf("ung_geometry_data_from_cgltf: Too many indices");
        }

        out.num_indices = (u32)idx_acc->count;
        out.indices = allocate<u32>(out.num_indices);

        for (u32 i = 0; i < out.num_indices; ++i) {
            out.indices[i] = (u32)cgltf_accessor_read_index(idx_acc, i);
        }
    } else {
        if (out.num_vertices % 3 != 0) {
            ung_panicf("ung_geometry_data_from_cgltf: Vertices not divisible by 3");
        }
        out.num_indices = out.num_vertices;
        out.indices = allocate<u32>(out.num_indices);
        for (u32 i = 0; i < out.num_indices; ++i) {
            out.indices[i] = i;
        }
    }

    return out;
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

EXPORT ung_skeleton_id ung_skeleton_from_cgltf(const cgltf_skin* skin)
{
    assert(skin);

    auto joints = allocate<ung_skeleton_joint>(skin->joints_count);

    // Inverse bind matrices
    const cgltf_accessor* ibm_acc = skin->inverse_bind_matrices;
    assert(ibm_acc); // optional in glTF, TODO: determine from node transforms
    assert(skin->joints_count == ibm_acc->count);

    for (uint16_t i = 0; i < skin->joints_count; ++i) {
        const auto node = skin->joints[i];
        assert(node);
        cgltf_accessor_read_float(ibm_acc, i, joints[i].inverse_bind_matrix, 16);
        // If the parent is not in the skin, we just treat is as the root
        joints[i].parent_index = get_joint_index(skin, node->parent);
    }

    const auto skel
        = ung_skeleton_create({ .num_joints = (u16)skin->joints_count, .joints = joints });

    deallocate(joints, skin->joints_count);

    return skel;
}

static const uint8_t* get_data(const cgltf_accessor* acc)
{
    return cgltf_buffer_view_data(acc->buffer_view) + acc->offset;
}

static size_t read_vec3s(const cgltf_accessor* acc, float* dest)
{
    assert(acc->component_type == cgltf_component_type_r_32f && acc->type == cgltf_type_vec3);
    assert(acc->buffer_view->stride == 0 && !acc->is_sparse); // TODO: sparse/stride!=0 later
    std::memcpy(dest, get_data(acc), acc->count * 3 * sizeof(float));
    return acc->count * 3;
}

static size_t read_quats(const cgltf_accessor* acc, float* dest)
{
    assert(acc->component_type == cgltf_component_type_r_32f && acc->type == cgltf_type_vec4);
    assert(acc->buffer_view->stride == 0 && !acc->is_sparse); // TODO: sparse/stride!=0 later
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

EXPORT ung_animation_id ung_animation_from_cgltf(
    const cgltf_animation* anim, const cgltf_skin* skin)
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

    deallocate(values, total_values_floats);
    deallocate(channels, num_channels);

    return anim_id;
}

template <size_t N>
struct PathBuf {
    char data[N] = {};
    size_t len = 0;

    void clear()
    {
        data[0] = 0;
        len = 0;
    }

    void append(std::string_view seg)
    {
        assert(!seg.empty());
        if (len > 0 && data[len - 1] != '/' && seg[0] != '/') {
            assert(len + 1 < N);
            data[len++] = '/';
        }
        assert(len + seg.size() + 1 <= N);
        std::memcpy(data + len, seg.data(), seg.size());
        len += seg.size();
        data[len] = 0;
    }
};

const char* get_path(std::string_view gltf_path, std::string_view path)
{
    static PathBuf<512> buf;
    buf.clear();
    const auto last_slash = gltf_path.find_last_of("/");
    if (last_slash == std::string_view::npos) {
        buf.append("./");
    } else {
        buf.append(gltf_path.substr(0, last_slash));
    }
    buf.append(path);
    return buf.data;
}

EXPORT ung_texture_id ung_texture_from_cgltf(const char* gltf_path,
    const cgltf_texture_view* tex_view, bool flip_y, mugfx_texture_create_params params)
{
    const auto texture = tex_view->texture;
    if (!texture) {
        ung_panicf("No texture");
        return {};
    }
    const auto image = texture->image;
    if (!image) {
        ung_panicf("No image");
        return {};
    }
    const auto bv = image->buffer_view;
    if (bv) {
        return ung_texture_load_buffer(cgltf_buffer_view_data(bv), bv->size, flip_y, params);
    } else if (image->uri) {
        return ung_texture_load(get_path(gltf_path, image->uri), flip_y, params);
    } else {
        ung_panicf("No image buffer view");
    }
}

static ung_model_alpha_mode map_alpha_mode(cgltf_alpha_mode mode)
{
    switch (mode) {
    case cgltf_alpha_mode_opaque:
        return UNG_MODEL_ALPHA_OPAQUE;
    case cgltf_alpha_mode_mask:
        return UNG_MODEL_ALPHA_MASK;
    case cgltf_alpha_mode_blend:
        return UNG_MODEL_ALPHA_BLEND;
    default:
        return UNG_MODEL_ALPHA_INVALID;
    }
}

const char* get_texture_scope(uint32_t flag)
{
    switch (flag) {
    case UNG_MODEL_MATERIAL_TEXTURE_BASE_COLOR:
        return "base_color_texture";
    case UNG_MODEL_MATERIAL_TEXTURE_NORMAL:
        return "normal_texture";
    case UNG_MODEL_MATERIAL_TEXTURE_EMISSIVE:
        return "emissive_texture";
    case UNG_MODEL_MATERIAL_TEXTURE_METAL_ROUGH:
        return "metallic_roughness_texture";
    case UNG_MODEL_MATERIAL_TEXTURE_OCCLUSION:
        return "occlusion_texture";
    default:
        return "invalid";
    }
}

ung_model_load_result model_load_gltf(ung_model_load_params params)
{
    LoadProfScope lpscope(params.path);

    const cgltf_options options = {};
    cgltf_data* data = nullptr;
    // TODO: use ung_read_whole_file
    ung_load_profiler_push("cgltf_parse_file");
    const auto result = cgltf_parse_file(&options, params.path, &data);
    ung_load_profiler_pop("cgltf_parse_file");
    if (result != cgltf_result_success) {
        ung_panicf("Error loading glTF file '%s': %d\n", params.path, result);
    }

    ung_load_profiler_push("cgltf_load_buffers");
    const auto buf_result = cgltf_load_buffers(&options, data, params.path);
    ung_load_profiler_pop("cgltf_load_buffers");
    if (buf_result != cgltf_result_success) {
        ung_panicf("Error loading buffers for glTF file '%s': %d\n", params.path, buf_result);
    }

    ung_model_load_result res = {};

    if (params.flags & (UNG_MODEL_LOAD_GEOMETRIES | UNG_MODEL_LOAD_GEOMETRY_DATA)) {
        for (const auto& g_node : std::span<cgltf_node>(data->nodes, data->nodes_count)) {
            if (g_node.mesh) {
                res.num_primitives += (u32)g_node.mesh->primitives_count;
            }
        }

        if (params.flags & UNG_MODEL_LOAD_GEOMETRIES) {
            res.geometries = allocate<ung_geometry_id>(res.num_primitives);
            res.material_indices = allocate<uint32_t>(res.num_primitives);
        }
        if (params.flags & UNG_MODEL_LOAD_GEOMETRY_DATA) {
            res.geometry_data = allocate<ung_geometry_data>(res.num_primitives);
        }
        size_t geom_idx = 0;

        for (const auto& node : std::span<cgltf_node>(data->nodes, data->nodes_count)) {
            if (node.mesh) {
                for (const auto& prim : std::span<cgltf_primitive>(
                         node.mesh->primitives, node.mesh->primitives_count)) {
                    if (params.flags & UNG_MODEL_LOAD_GEOMETRIES) {
                        LoadProfScope s("geometry");
                        res.geometries[geom_idx] = ung_geometry_from_cgltf(&prim);
                        res.material_indices[geom_idx] = prim.material
                            ? (u32)(prim.material - data->materials)
                            : (u32)UINT32_MAX;
                    }
                    if (params.flags & UNG_MODEL_LOAD_GEOMETRY_DATA) {
                        LoadProfScope s("geometry data");
                        res.geometry_data[geom_idx] = ung_geometry_data_from_cgltf(&prim);
                    }
                    geom_idx++;
                }
            }
        }
    }

    auto load_texture = [&](const char* gltf_path, const cgltf_texture_view& tex,
                            uint32_t flag) -> ung_texture_id {
        if (params.material_flags & flag) {
            LoadProfScope lpscope(get_texture_scope(flag));
            return ung_texture_from_cgltf(
                gltf_path, &tex, params.texture_flip_y, params.texture_params);
        } else {
            return { 0 };
        }
    };

    if (params.flags & UNG_MODEL_LOAD_MATERIALS) {
        LoadProfScope s("materials");
        res.num_materials = (u32)data->materials_count;
        res.materials = allocate<ung_model_material>(data->materials_count);
        res.gltf_materials = allocate<ung_gltf_material>(data->materials_count);

        for (size_t i = 0; i < data->materials_count; ++i) {
            const auto& m = data->materials[i];
            const auto& pbr = m.pbr_metallic_roughness;
            res.materials[i] = {
                .base_color_texture = load_texture(
                    params.path, pbr.base_color_texture, UNG_MODEL_MATERIAL_TEXTURE_BASE_COLOR),
                .normal_texture
                = load_texture(params.path, m.normal_texture, UNG_MODEL_MATERIAL_TEXTURE_NORMAL),
                .emissive_texture = load_texture(
                    params.path, m.emissive_texture, UNG_MODEL_MATERIAL_TEXTURE_EMISSIVE),
                .normal_scale = 1.0f,
                .alpha_cutoff = m.alpha_cutoff,
                .alpha_mode = map_alpha_mode(m.alpha_mode),
                .double_sided = m.double_sided != 0,
            };
            std::memcpy(
                res.materials[i].base_color_factor, pbr.base_color_factor, sizeof(float) * 4);
            std::memcpy(res.materials[i].emissive_factor, m.emissive_factor, sizeof(float) * 3);
            res.gltf_materials[i] = {
                .metal_rough_texture = load_texture(params.path, pbr.metallic_roughness_texture,
                    UNG_MODEL_MATERIAL_TEXTURE_METAL_ROUGH),
                .occlusion_texture = load_texture(
                    params.path, m.occlusion_texture, UNG_MODEL_MATERIAL_TEXTURE_OCCLUSION),
                .metallic_factor = pbr.metallic_factor,
                .roughness_factor = pbr.roughness_factor,
                .occlusion_strength = 1.0f,
            };
        }
    }

    cgltf_skin* skin = nullptr;
    if (params.flags & (UNG_MODEL_LOAD_SKELETON | UNG_MODEL_LOAD_ANIMATIONS)) {
        // We actually go through the nodes here, because we allow only one skeleton and we
        // at least want to restrict it to one *used* skeleton.
        for (const auto& g_node : std::span<cgltf_node>(data->nodes, data->nodes_count)) {
            if (g_node.skin) {
                if (skin && skin != g_node.skin) {
                    ung_panicf("More than one skeleton in glTF file '%s'", params.path);
                }
                skin = g_node.skin;

                if (params.flags & UNG_MODEL_LOAD_SKELETON) {
                    res.skeleton = ung_skeleton_from_cgltf(skin);
                }
            }
        }
    }

    if (skin && (params.flags & UNG_MODEL_LOAD_ANIMATIONS)) {
        LoadProfScope s("animations");
        if (params.animation_names) {
            res.num_animations = params.num_animation_names;
            res.animations = allocate<ung_animation_id>(res.num_animations);

            size_t anim_idx = 0;
            for (u32 n = 0; n < params.num_animation_names; ++n) {
                const std::string_view name_to_find = params.animation_names[n];

                res.animations[anim_idx] = {};
                for (size_t i = 0; i < data->animations_count; ++i) {
                    if (data->animations[i].name && name_to_find == data->animations[i].name) {
                        res.animations[anim_idx]
                            = ung_animation_from_cgltf(&data->animations[i], skin);
                        break;
                    }
                }
                anim_idx++;
            }
        } else {
            res.num_animations = (u32)data->animations_count;
            res.animations = allocate<ung_animation_id>(res.num_animations);
            for (size_t i = 0; i < data->animations_count; ++i) {
                res.animations[i] = ung_animation_from_cgltf(&data->animations[i], skin);
            }
        }
    }

    cgltf_free(data);

    return res;
}
}