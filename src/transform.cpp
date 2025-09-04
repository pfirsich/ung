#include "state.hpp"
#include "types.hpp"
#include "um.h"

namespace ung::transform {
struct Transform {
    um_mat local_matrix;
    um_quat orientation;
    um_vec3 position;
    um_vec3 scale;
    u64 parent;
    u64 first_child;
    u64 prev_sibling;
    u64 next_sibling;
    mugfx_uniform_data_id uniform_data;
    bool local_matrix_dirty;
};

struct State {
    Pool<Transform> transforms;
};

State* state = nullptr;

void init(ung_init_params params)
{
    assert(!state);
    state = allocate<State>();
    std::memset(state, 0, sizeof(State));

    state->transforms.init(params.max_num_transforms ? params.max_num_transforms : 1024);
}

void shutdown()
{
    if (!state) {
        return;
    }

    state->transforms.free();

    deallocate(state, 1);
    state = nullptr;
}

static Transform* get_transform(u64 key)
{
    return get(state->transforms, key);
}

mugfx_uniform_data_id get_uniform_data(ung_transform_id transform_id)
{
    auto trafo = get_transform(transform_id.id);
    return trafo->uniform_data;
}

EXPORT ung_transform_id ung_transform_create()
{
    const auto [id, trafo] = state->transforms.insert();
    trafo->orientation = um_quat_identity();
    trafo->scale = um_vec3 { 1.0f, 1.0f, 1.0f };
    trafo->uniform_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
        .size = sizeof(UTransform),
    });
    trafo->local_matrix_dirty = true;
    return { id };
}

static void link(u64 prev_key, u64 next_key)
{
    if (!prev_key && !next_key) {
        return; // nothing to do
    } else if (!prev_key) {
        get_transform(next_key)->prev_sibling = 0;
    } else if (!next_key) {
        get_transform(prev_key)->next_sibling = 0;
    } else {
        get_transform(prev_key)->next_sibling = next_key;
        get_transform(next_key)->prev_sibling = prev_key;
    }
}

EXPORT void ung_transform_destroy(ung_transform_id transform)
{
    auto trafo = get_transform(transform.id);
    if (trafo->first_child) {
        if (trafo->parent) {
            // Reparent all children to the parent
            auto child_key = trafo->first_child;
            while (child_key) {
                auto child = get_transform(child_key);
                child->parent = trafo->parent;
                if (!child->next_sibling) {
                    break;
                }
                child_key = child->next_sibling;
            }
            const auto last_sibling = child_key;

            // Insert children between surrounding siblings
            auto parent = get_transform(trafo->parent);
            if (parent->first_child == transform.id) {
                assert(!get_transform(trafo->first_child)->prev_sibling);
                parent->first_child = trafo->first_child;
            } else {
                assert(trafo->prev_sibling);
                link(trafo->prev_sibling, trafo->first_child);
            }
            link(last_sibling, trafo->next_sibling);
        } else {
            // If the node has children, but no parent, unparent all children
            auto child_key = trafo->first_child;
            while (child_key) {
                auto child = get_transform(child_key);
                child_key = child->next_sibling;

                child->parent = 0;
                child->prev_sibling = 0;
                child->next_sibling = 0;
            }
        }
    } else if (trafo->parent) {
        // If the node has no children, but a parent, fix up siblings
        auto parent = get_transform(trafo->parent);
        link(trafo->prev_sibling, trafo->next_sibling);
        if (parent->first_child == transform.id) {
            parent->first_child = trafo->next_sibling;
        }
    }
    state->transforms.remove(transform.id);
}

EXPORT void ung_transform_set_position(ung_transform_id transform, const float xyz[3])
{
    ung_transform_set_position_v(transform, xyz[0], xyz[1], xyz[2]);
}

EXPORT void ung_transform_set_position_v(ung_transform_id transform, float x, float y, float z)
{
    auto trafo = get_transform(transform.id);
    trafo->position = { x, y, z };
    trafo->local_matrix_dirty = true;
}

EXPORT void ung_transform_get_position(ung_transform_id transform, float position[3])
{
    const auto trafo = get_transform(transform.id);
    position[0] = trafo->position.x;
    position[1] = trafo->position.y;
    position[2] = trafo->position.z;
}

EXPORT void ung_transform_set_orientation(ung_transform_id transform, const float xyzw[4])
{
    ung_transform_set_orientation_v(transform, xyzw[0], xyzw[1], xyzw[2], xyzw[3]);
}

EXPORT void ung_transform_set_orientation_v(
    ung_transform_id transform, float x, float y, float z, float w)
{

    auto trafo = get_transform(transform.id);
    trafo->orientation = { x, y, z, w };
    trafo->local_matrix_dirty = true;
}

EXPORT void ung_transform_get_orientation(ung_transform_id transform, float quat_xyzw[4])
{
    const auto trafo = get_transform(transform.id);
    um_quat_to_ptr(trafo->orientation, quat_xyzw);
}

EXPORT void ung_transform_set_scale(ung_transform_id transform, const float xyz[3])
{
    ung_transform_set_scale_v(transform, xyz[0], xyz[1], xyz[2]);
}

EXPORT void ung_transform_set_scale_u(ung_transform_id transform, float s)
{
    ung_transform_set_scale_v(transform, s, s, s);
}

EXPORT void ung_transform_set_scale_v(ung_transform_id transform, float x, float y, float z)
{
    auto trafo = get_transform(transform.id);
    trafo->scale = { x, y, z };
    trafo->local_matrix_dirty = true;
}

EXPORT void ung_transform_get_scale(ung_transform_id transform, float scale[3])
{
    const auto trafo = get_transform(transform.id);
    scale[0] = trafo->scale.x;
    scale[1] = trafo->scale.y;
    scale[2] = trafo->scale.z;
}

static void look_at(Transform* trafo, um_vec3 at, um_vec3 up)
{
    const auto look_at = um_mat_look_at(trafo->position, at, up);
    trafo->orientation = um_quat_normalized(um_quat_conjugate(um_quat_from_matrix(look_at)));
    trafo->local_matrix_dirty = true;
}

EXPORT void ung_transform_look_at(ung_transform_id transform, const float xyz[3])
{
    ung_transform_look_at_v(transform, xyz[0], xyz[1], xyz[2]);
}

EXPORT void ung_transform_look_at_v(ung_transform_id transform, float x, float y, float z)
{
    auto trafo = get_transform(transform.id);
    const um_vec3 at = { x, y, z };
    // We will guess an up vector by first calculating a right vector and from that an up vector
    const auto look = um_vec3_sub(at, trafo->position);
    const auto right = um_vec3_cross(look, { 0.0f, 1.0f, 0.0f });
    const auto up = um_vec3_normalized(um_vec3_cross(look, right));
    look_at(trafo, at, up);
}

EXPORT void ung_transform_look_at_up(
    ung_transform_id transform, const float xyz[3], const float up_xyz[3])
{
    ung_transform_look_at_up_v(transform, xyz[0], xyz[1], xyz[2], up_xyz[0], up_xyz[1], up_xyz[2]);
}

EXPORT void ung_transform_look_at_up_v(
    ung_transform_id transform, float x, float y, float z, float up_x, float up_y, float up_z)
{
    auto trafo = get_transform(transform.id);
    look_at(trafo, { x, y, z }, { up_x, up_y, up_z });
}

static const um_mat& get_local_matrix(Transform* trafo)
{
    if (trafo->local_matrix_dirty) {
        const auto t = um_mat_translate(trafo->position);
        const auto r = um_mat_from_quat(trafo->orientation);
        const auto s = um_mat_scale(trafo->scale);
        trafo->local_matrix = um_mat_mul(um_mat_mul(t, r), s);
        trafo->local_matrix_dirty = false;
    }
    return trafo->local_matrix;
}

static um_mat get_world_matrix(Transform* trafo)
{
    const auto& local_matrix = get_local_matrix(trafo);
    if (trafo->parent) {
        const auto parent = state->transforms.find(trafo->parent);
        return um_mat_mul(get_world_matrix(parent), local_matrix);
    }
    return local_matrix;
}

um_mat get_world_matrix(ung_transform_id transform)
{
    return get_world_matrix(get_transform(transform.id));
}

EXPORT void ung_transform_get_local_matrix(ung_transform_id transform, float matrix[16])
{
    const auto trafo = get_transform(transform.id);
    const auto& m = get_local_matrix(trafo);
    std::memcpy(matrix, &m, sizeof(float) * 16);
}

EXPORT void ung_transform_get_world_matrix(ung_transform_id transform, float matrix[16])
{
    // TODO: Consider going over all transforms in ung_begin_frame, calculating all local matrices
    // and then going over all of them again to calculate world transforms. This keeps a tight loop,
    // though it might calculate more than is necessary. And requires that transforms are constant
    // between ung_begin_frame and ung_end_frame.
    const auto trafo = get_transform(transform.id);
    const auto m = get_world_matrix(trafo);
    std::memcpy(matrix, &m, sizeof(float) * 16);
}

EXPORT void ung_transform_local_to_world(ung_transform_id transform, float dir[3])
{
    const auto trafo = get_transform(transform.id);
    const auto world = um_quat_mul_vec3(trafo->orientation, { dir[0], dir[1], dir[2] });
    std::memcpy(dir, &world, sizeof(float) * 3);
}

static void unparent(u64 child_key, Transform* child)
{
    auto parent = get_transform(child->parent);
    link(child->prev_sibling, child->next_sibling);
    if (parent->first_child == child_key) {
        assert(!child->prev_sibling);
        parent->first_child = child->next_sibling;
    }
    child->parent = 0;
    child->prev_sibling = 0;
    child->next_sibling = 0;
}

EXPORT void ung_transform_set_parent(ung_transform_id transform, ung_transform_id parent)
{
    // NOTE: I explicitly do not check for loops, because it's too expensive to check every time and
    // you'll notice quick (infinite loops).
    // NOTE: I also shortcut a redundant set_parent with the current parent of transform,
    // because I consider this a mistake, which you should pay for.
    assert(transform.id != parent.id);
    auto trafo = get_transform(transform.id);

    if (trafo->parent) {
        unparent(transform.id, trafo);
    }

    if (parent.id) {
        auto parent_trafo = get_transform(parent.id);
        trafo->parent = parent.id;
        link(transform.id, parent_trafo->first_child);
        parent_trafo->first_child = transform.id;
    }
}

EXPORT ung_transform_id ung_transform_get_parent(ung_transform_id transform)
{
    const auto trafo = state->transforms.find(transform.id);
    assert(trafo);
    return { trafo->parent };
}

EXPORT ung_transform_id ung_transform_get_first_child(ung_transform_id transform)
{
    const auto trafo = state->transforms.find(transform.id);
    assert(trafo);
    return { trafo->first_child };
}

EXPORT ung_transform_id ung_transform_get_next_sibling(ung_transform_id transform)
{
    const auto trafo = state->transforms.find(transform.id);
    assert(trafo);
    return { trafo->next_sibling };
}

}