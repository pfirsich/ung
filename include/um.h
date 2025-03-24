#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z;
} um_vec3;

float um_vec3_len(um_vec3 v);
float um_vec3_len_sq(um_vec3 v);
um_vec3 um_vec3_normalized(um_vec3 v);

um_vec3 um_vec3_mul(um_vec3 v, float s);

float um_vec3_dot(um_vec3 a, um_vec3 b);
float um_vec3_dist(um_vec3 a, um_vec3 b);

um_vec3 um_vec3_add(um_vec3 a, um_vec3 b);
um_vec3 um_vec3_sub(um_vec3 a, um_vec3 b);
um_vec3 um_vec3_cross(um_vec3 a, um_vec3 b);

um_vec3 um_vec3_lerp(um_vec3 a, um_vec3 b, float t);

bool um_vec3_eq(um_vec3 a, um_vec3 b);

typedef struct {
    float x, y, z, w;
} um_vec4;

float um_vec4_len(um_vec4 v);
float um_vec4_len_sq(um_vec4 v);
um_vec4 um_vec4_normalized(um_vec4 v);

um_vec4 um_vec4_mul(um_vec4 v, float s);

float um_vec4_dot(um_vec4 a, um_vec4 b);
float um_vec4_dist(um_vec4 a, um_vec4 b);

um_vec4 um_vec4_add(um_vec4 a, um_vec4 b);
um_vec4 um_vec4_sub(um_vec4 a, um_vec4 b);

um_vec4 um_vec4_lerp(um_vec4 a, um_vec4 b, float t);

bool um_vec4_eq(um_vec4 a, um_vec4 b);

typedef struct {
    float w, x, y, z;
} um_quat;

typedef struct {
    um_vec4 cols[4];
} um_mat;

// Quaternion

um_quat um_quat_identity();
float um_quat_len(um_quat q);
um_quat um_quat_normalized(um_quat q);
um_quat um_quat_conjugate(um_quat q);
um_quat um_quat_mul(um_quat a, um_quat b);

um_quat um_quat_from_matrix(um_mat m);
um_quat um_quat_from_axis_angle(um_vec3 axis, float angle);
um_quat um_quat_slerp(um_quat a, um_quat b, float t);
um_vec3 um_quat_mul_vec3(um_quat q, um_vec3 v);

// Matrix

um_mat um_mat_identity();
um_mat um_mat_scale(um_vec3 v);
um_mat um_mat_rotate(um_vec3 axis, float angle);
um_mat um_mat_translate(um_vec3 v);
um_mat um_mat_ortho(float left, float right, float bottom, float top, float near, float far);
um_mat um_mat_perspective(float fovy, float aspect, float near, float far);
um_mat um_mat_look_at(um_vec3 eye, um_vec3 target, um_vec3 up);
um_mat um_mat_from_quat(um_quat q);

um_mat um_mat_transpose(um_mat m);
um_mat um_mat_invert(um_mat m);

um_vec3 um_mat_mul_vec3(um_mat m, um_vec3 v);
um_vec4 um_mat_mul_vec4(um_mat m, um_vec4 v);

um_mat um_mat_mul(um_mat a, um_mat b);

#ifdef __cplusplus
}
#endif
