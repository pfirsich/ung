#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mugfx.h>

#ifdef __cplusplus
extern "C" {
#endif

// id == 0 always represents an invalid object, the id uses 48 bits and therefore fits into a double
// precision floating point number exactly.

typedef struct {
    uint64_t id;
} ung_controller_id;

typedef struct {
    uint64_t id;
} ung_transform_id;

typedef struct {
    uint64_t id;
} ung_material_id;

typedef struct {
    uint64_t id;
} ung_camera_id;

typedef struct {
    uint64_t id;
} ung_geometry_data_id;

typedef mugfx_texture_id ung_texture_id;
typedef mugfx_shader_id ung_shader_id;
typedef mugfx_geometry_id ung_draw_geometry_id;

typedef struct {
    const char* data;
    size_t length;
} ung_string;

#define UNG_LITERAL(s) ((ung_string) { s, sizeof(s) - 1 })

ung_string ung_zstr(const char* str); // calls strlen for length

#ifdef __cplusplus
}
#endif