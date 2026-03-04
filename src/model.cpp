#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "types.hpp"

namespace ung::model {
ung_model_load_result model_load_gltf(ung_model_load_params params);

EXPORT ung_model_load_result ung_model_load(ung_model_load_params params)
{
    if (!params.flags) {
        params.flags = UNG_MODEL_LOAD_ALL;
    }

    const std::string_view path = params.path;
#ifdef UNG_CGLTF
    if (path.ends_with(".gltf") || path.ends_with(".glb")) {
        return model_load_gltf(params);
    }
#endif
    ung_panicf("Unsupported model file format");
}

EXPORT void ung_model_load_result_free(const ung_model_load_result* result)
{
    if (result->geometries) {
        deallocate(result->geometries, result->num_primitives);
    }
    if (result->geometry_data) {
        deallocate(result->geometry_data, result->num_primitives);
    }
    if (result->material_indices) {
        deallocate(result->material_indices, result->num_primitives);
    }
    if (result->materials) {
        deallocate(result->materials, result->num_materials);
    }
    if (result->gltf_materials) {
        deallocate(result->gltf_materials, result->num_materials);
    }
    if (result->animations) {
        deallocate(result->animations, result->num_animations);
    }
}
}