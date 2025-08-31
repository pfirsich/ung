#include "types.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <span>

#include "um.h"

namespace ung::animation {
struct Joint {
    um_mat inverse_bind_matrix;
    int16_t parent_index;
};

struct Skeleton {
    u16 num_joints;
    // all four arrays below have num_joints elements
    Joint* joints; // immutable
    ung_joint_transform* joint_transforms; // mutable pose (local space)
    ung_joint_transform* local_bind; // local bind pose transforms
    // cached globals & skin matrices:
    um_mat* global_transforms;
    um_mat* joint_matrices; // skin matrix (global_trafo * inverse_bind)
};

struct AnimationChannel {
    ung_animation_key key;
    ung_animation_sampler_type sampler_type;
    ung_animation_interp interp_type;
    size_t num_samples;
    float* times; // size = num_samples
    float* values; // vec3: 3*num_samples, quat(wxyz): 4*num_samples
};

struct Animation {
    float duration_s;
    Array<AnimationChannel> channels;
};

struct State {
    Pool<Skeleton> skeletons;
    Pool<Animation> animations;
};

State* state;

void init(ung_init_params params)
{
    assert(!state);
    state = allocate<State>();
    std::memset(state, 0, sizeof(State));

    state->skeletons.init(params.max_num_skeletons ? params.max_num_skeletons : 64);
    state->animations.init(params.max_num_animations ? params.max_num_animations : 256);
}

void shutdown()
{
    if (!state) {
        return;
    }

    // free remaining skeleton buffers to avoid leaks
    for (u32 i = 0; i < state->skeletons.capacity(); ++i) {
        const auto key = state->skeletons.get_key(i);
        if (key == 0) {
            continue;
        }
        ung_skeleton_destroy({ key });
    }

    for (u32 i = 0; i < state->animations.capacity(); ++i) {
        const auto key = state->animations.get_key(i);
        if (key == 0) {
            continue;
        }
        ung_animation_destroy({ key });
    }

    state->animations.free();
    state->skeletons.free();

    deallocate(state, 1);
    state = nullptr;
}

EXPORT ung_skeleton_id ung_skeleton_create(ung_skeleton_create_params params)
{
    assert(state);
    assert(params.joints);
    assert(params.num_joints > 0);

    auto [id, obj] = state->skeletons.insert();
    if (id == 0) {
        return { 0 };
    }

    Skeleton& s = *obj;
    s.num_joints = params.num_joints;

    s.joints = allocate<Joint>(s.num_joints);
    s.joint_transforms = allocate<ung_joint_transform>(s.num_joints);
    s.local_bind = allocate<ung_joint_transform>(s.num_joints);
    s.global_transforms = allocate<um_mat>(s.num_joints);
    s.joint_matrices = allocate<um_mat>(s.num_joints);

    // local rest pose is all identities
    for (u16 i = 0; i < s.num_joints; ++i) {
        const auto parent_idx = params.joints[i].parent_index;
        assert(parent_idx < i); // ensure topological ordering

        s.joints[i] = {
            .inverse_bind_matrix = um_mat_from_ptr(params.joints[i].inverse_bind_matrix),
            .parent_index = parent_idx,
        };

        if (params.local_bind) {
            s.local_bind[i] = params.local_bind[i];
        } else {
            // Derive local bind from inverse bind matrix
            const auto bind_global = um_mat_invert(s.joints[i].inverse_bind_matrix);

            um_mat local_m;
            if (parent_idx >= 0) {
                const auto parent_global = um_mat_invert(s.joints[parent_idx].inverse_bind_matrix);
                const auto parent_inv = um_mat_invert(parent_global);
                local_m = um_mat_mul(parent_inv, bind_global);
            } else {
                local_m = bind_global;
            }

            um_vec3 tr, sc;
            um_quat ro;
            um_mat_decompose_trs(local_m, &tr, &ro, &sc);
            um_vec3_to_ptr(tr, s.local_bind[i].translation);
            um_quat_to_ptr(ro, s.local_bind[i].rotation);
            um_vec3_to_ptr(sc, s.local_bind[i].scale);
        }

        s.joint_transforms[i] = s.local_bind[i];
        s.global_transforms[i] = um_mat_identity();
        s.joint_matrices[i] = um_mat_identity();
    }

    return { id };
}

EXPORT void ung_skeleton_destroy(ung_skeleton_id skel)
{
    assert(state);
    auto s = get(state->skeletons, skel.id);
    deallocate(s->joints, s->num_joints);
    deallocate(s->joint_transforms, s->num_joints);
    deallocate(s->global_transforms, s->num_joints);
    deallocate(s->joint_matrices, s->num_joints);
    state->skeletons.remove(skel.id);
}

EXPORT void ung_skeleton_reset_to_bind_pose(ung_skeleton_id skel)
{
    assert(state);
    auto s = get(state->skeletons, skel.id);
    std::memcpy(s->joint_transforms, s->local_bind, s->num_joints * sizeof(ung_joint_transform));
}

EXPORT ung_joint_transform* ung_skeleton_get_joint_transforms(
    ung_skeleton_id skel, uint16_t* num_joints)
{
    assert(state);
    auto s = get(state->skeletons, skel.id);
    if (num_joints) {
        *num_joints = s->num_joints;
    }
    return s->joint_transforms;
}

static um_mat get_matrix(const ung_joint_transform& trafo)
{
    const auto t
        = um_mat_translate({ trafo.translation[0], trafo.translation[1], trafo.translation[2] });
    const auto r = um_mat_from_quat(
        { trafo.rotation[0], trafo.rotation[1], trafo.rotation[2], trafo.rotation[3] });
    const auto s = um_mat_scale({ trafo.scale[0], trafo.scale[1], trafo.scale[2] });
    return um_mat_mul(um_mat_mul(t, r), s);
}

EXPORT const float* ung_skeleton_update_joint_matrices(ung_skeleton_id skel, uint16_t* num_joints)
{
    assert(state);
    auto s = get(state->skeletons, skel.id);
    if (num_joints) {
        *num_joints = s->num_joints;
    }

    // This works only because the joints are topologically ordered (asserted in create)!
    for (u16 i = 0; i < s->num_joints; ++i) {
        const auto local_trafo = get_matrix(s->joint_transforms[i]);

        const auto parent = s->joints[i].parent_index;
        if (parent >= 0) {
            const auto& parent_trafo = s->global_transforms[(u16)parent];
            s->global_transforms[i] = um_mat_mul(parent_trafo, local_trafo);
        } else {
            // root
            s->global_transforms[i] = local_trafo;
        }

        s->joint_matrices[i]
            = um_mat_mul(s->global_transforms[i], s->joints[i].inverse_bind_matrix);
    }

    return &s->joint_matrices[0].cols[0].x;
}

static float qdot(const um_quat& a, const um_quat& b)
{
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

static um_quat qmix(const um_quat& a, float wa, const um_quat& b, float wb)
{
    return {
        a.w * wa + b.w * wb,
        a.x * wa + b.x * wb,
        a.y * wa + b.y * wb,
        a.z * wa + b.z * wb,
    };
}

EXPORT void ung_blend_poses(const ung_joint_transform* a, float a_weight,
    const ung_joint_transform* b, float b_weight, const float* joint_mask, ung_joint_transform* out,
    uint16_t num_joints)
{
    for (u16 i = 0; i < num_joints; ++i) {
        const float mask = joint_mask ? joint_mask[i] : 1.0f;
        const float wa = a_weight;
        const float wb = b_weight * mask;

        for (size_t k = 0; k < 3; ++k) {
            out[i].translation[k] = a[i].translation[k] * wa + b[i].translation[k] * wb;
            out[i].scale[k] = a[i].scale[k] * wa + b[i].scale[k] * wb;
        }

        const auto qa = um_quat_from_ptr(a[i].rotation);
        const auto qb = um_quat_from_ptr(b[i].rotation);

        // q and -q represent the same rotation, so if I try to blend two quaternions
        // which are close together, but differ in sign, I get really small (in terms of length)
        // values and after normalization, nothing sensible is left.
        // You might think this is rare, but it happens blending almost any two animations! To avoid
        // this, I check if the quats point away from each other and if so, I flip one of them. I
        // use `>=` so it doesn't flip the sign for the first animation being added (one quat is
        // unit quat).
        const float sgn = (qdot(qa, qb) >= 0.0f) ? 1.0f : -1.0f;
        // TODO: Should I check for wa+wb == 0? (normalize will blow up)
        const auto qsum = um_quat_normalized(qmix(qa, wa, qb, wb * sgn));
        um_quat_to_ptr(qsum, out[i].rotation);
    }
}

EXPORT ung_animation_id ung_animation_create(ung_animation_create_params params)
{
    assert(state);
    auto [id, obj] = state->animations.insert();
    if (id == 0) {
        return { 0 };
    }

    auto& anim = *obj;
    anim.duration_s = params.duration_s;
    anim.channels.init((u32)params.num_channels);

    for (u32 i = 0; i < anim.channels.size; ++i) {
        const ung_animation_channel& src = params.channels[i];
        auto& dst = anim.channels[i];
        dst = {
            .key = src.key,
            .sampler_type = src.sampler_type,
            .interp_type = src.interp_type,
            .num_samples = src.num_samples,
        };

        dst.times = allocate<float>(dst.num_samples);
        std::memcpy(dst.times, src.times, sizeof(float) * dst.num_samples);
        for (size_t s = 0; s < dst.num_samples - 1; ++s) {
            assert(dst.times[s] < dst.times[s + 1]);
        }

        const size_t stride = dst.sampler_type == UNG_ANIM_SAMPLER_TYPE_VEC3 ? 3 : 4;
        dst.values = allocate<float>(dst.num_samples * stride);

        if (dst.sampler_type == UNG_ANIM_SAMPLER_TYPE_VEC3) {
            std::memcpy(dst.values, src.values, sizeof(float) * dst.num_samples * stride);
        } else if (dst.sampler_type == UNG_ANIM_SAMPLER_TYPE_QUAT) {
            for (size_t s = 0; s < dst.num_samples; ++s) {
                const auto q = um_quat_from_ptr(src.values + s * 4);
                um_quat_to_ptr(um_quat_normalized(q), dst.values + s * 4);
            }
        }
    }

    return { id };
}

EXPORT void ung_animation_destroy(ung_animation_id anim_id)
{
    assert(state);
    auto anim = get(state->animations, anim_id.id);

    for (u32 i = 0; i < anim->channels.size; ++i) {
        auto& ch = anim->channels[i];
        deallocate(ch.times, (u32)ch.num_samples);
        const size_t stride = (ch.sampler_type == UNG_ANIM_SAMPLER_TYPE_VEC3) ? 3 : 4;
        deallocate(ch.values, (u32)(ch.num_samples * stride));
    }
    anim->channels.free();
    state->animations.remove(anim_id.id);
}

EXPORT float ung_animation_get_duration(ung_animation_id anim_id)
{
    assert(state);
    return get(state->animations, anim_id.id)->duration_s;
}

// returns i in [0, n-2] so that t is in [times[i], times[i+1]) (n = times.size())
// or n-2 if t >= times[n-1]
static size_t find_interval(std::span<float> times, float t)
{
    assert(times.size() >= 2);

    size_t low = 0;
    size_t high = times.size(); // exclusive

    if (t <= times[0]) {
        return 0;
    }
    if (t >= times[high - 1]) {
        return high - 2;
    }

    while (high > low + 1) {
        const auto mid = (low + high) / 2;
        if (t >= times[mid]) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return low;
}

static float clamp(float v, float lo, float hi)
{
    assert(lo <= hi);
    return fminf(fmaxf(v, lo), hi);
}

static float unlerp(float t, float t0, float t1)
{
    assert(t0 < t1);
    return (clamp(t, t0, t1) - t0) / (t1 - t0);
}

static um_vec3 interp(
    ung_animation_interp interp, float t0, const um_vec3& v0, float t1, const um_vec3& v1, float t)
{
    if (interp == UNG_ANIM_INTERP_STEP || t0 == t1) {
        return (t < t1) ? v0 : v1; // this check is only for case where t is greater than times[n-1]
    } else if (interp == UNG_ANIM_INTERP_LINEAR) {
        return um_vec3_lerp(v0, v1, unlerp(t, t0, t1));
    } else {
        std::abort();
    }
}

static um_quat interp(
    ung_animation_interp interp, float t0, const um_quat& q0, float t1, const um_quat& q1, float t)
{
    if (interp == UNG_ANIM_INTERP_STEP || t0 == t1) {
        return (t < t1) ? q0 : q1;
    } else if (interp == UNG_ANIM_INTERP_LINEAR) {
        return um_quat_normalized(um_quat_slerp(q0, q1, unlerp(t, t0, t1)));
    } else {
        std::abort();
    }
}

EXPORT void ung_animation_sample(
    ung_animation_id anim_id, float t, ung_joint_transform* joints, uint16_t num_joints)
{
    assert(state);
    auto anim = get(state->animations, anim_id.id);

    t = clamp(t, 0.0f, anim->duration_s);

    for (u32 c = 0; c < anim->channels.size; ++c) {
        const auto& ch = anim->channels[c];

        if (ch.key.joint_index >= num_joints) {
            // just ignore out of range
            continue;
        }
        auto& joint = joints[ch.key.joint_index];

        if (ch.num_samples == 0) {
            continue;
        }

        // Single sample
        if (ch.num_samples == 1) {
            if (ch.sampler_type == UNG_ANIM_SAMPLER_TYPE_VEC3) {
                const auto v = um_vec3_from_ptr(ch.values);
                if (ch.key.dof == UNG_JOINT_DOF_TRANSLATION) {
                    um_vec3_to_ptr(v, joint.translation);
                } else if (ch.key.dof == UNG_JOINT_DOF_SCALE) {
                    um_vec3_to_ptr(v, joint.scale);
                }
            } else if (ch.sampler_type == UNG_ANIM_SAMPLER_TYPE_QUAT) {
                const auto q = um_quat_from_ptr(ch.values);
                if (ch.key.dof == UNG_JOINT_DOF_ROTATION) {
                    um_quat_to_ptr(q, joint.rotation);
                }
            }
            continue;
        }

        // More than one sample
        const auto i0 = find_interval({ ch.times, ch.num_samples }, t);
        const auto i1 = i0 + 1;

        if (ch.sampler_type == UNG_ANIM_SAMPLER_TYPE_VEC3) {
            const auto v0 = um_vec3_from_ptr(ch.values + i0 * 3);
            const auto v1 = um_vec3_from_ptr(ch.values + i1 * 3);
            const auto v = interp(ch.interp_type, ch.times[i0], v0, ch.times[i1], v1, t);

            if (ch.key.dof == UNG_JOINT_DOF_TRANSLATION) {
                um_vec3_to_ptr(v, joint.translation);
            } else if (ch.key.dof == UNG_JOINT_DOF_SCALE) {
                um_vec3_to_ptr(v, joint.scale);
            }
        } else if (ch.sampler_type == UNG_ANIM_SAMPLER_TYPE_QUAT) {
            const auto q0 = um_quat_from_ptr(ch.values + i0 * 4);
            const auto q1 = um_quat_from_ptr(ch.values + i1 * 4);
            const auto q = interp(ch.interp_type, ch.times[i0], q0, ch.times[i1], q1, t);

            if (ch.key.dof == UNG_JOINT_DOF_ROTATION) {
                um_quat_to_ptr(q, joint.rotation);
            }
        }
    }
}
}