#pragma once

#include <ung.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cgltf_primitive cgltf_primitive;
typedef struct cgltf_skin cgltf_skin;
typedef struct cgltf_animation cgltf_animation;

ung_geometry_id get_geom_from_cgltf(const cgltf_primitive* prim);

uint16_t get_joints_from_cgltf(
    const cgltf_skin* skin, ung_skeleton_joint* joints, uint16_t max_num_joints);

// Skin is used to map animation target nodes to joint indices. Animation channels that point at
// nodes outside the skin are simply ignored.
ung_animation_id get_anim_from_cgltf(const cgltf_animation* anim, const cgltf_skin* skin);

#ifdef __cplusplus
}
#endif
