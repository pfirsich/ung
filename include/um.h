#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define UM_PI 3.141592653589793f
#define UM_2PI 6.283185307179586f

float um_clamp(float v, float lo, float hi);

typedef struct {
    float v;
} um_rad;

typedef struct {
    float v;
} um_deg;

um_rad um_to_rad(um_deg deg);
um_deg um_to_deg(um_rad rad);

typedef struct {
    float x, y, z;
} um_vec3;

// Construction
um_vec3 um_vec3_from_ptr(const float v[3]);
void um_vec3_to_ptr(um_vec3 v, float p[3]);

// Unary
float um_vec3_len(um_vec3 v);
float um_vec3_len_sq(um_vec3 v);
um_vec3 um_vec3_normalized(um_vec3 v);

// Binary
um_vec3 um_vec3_mul(um_vec3 v, float s);
float um_vec3_dot(um_vec3 a, um_vec3 b);
float um_vec3_dist(um_vec3 a, um_vec3 b);
um_vec3 um_vec3_add(um_vec3 a, um_vec3 b);
um_vec3 um_vec3_sub(um_vec3 a, um_vec3 b);
um_vec3 um_vec3_cross(um_vec3 a, um_vec3 b);
bool um_vec3_eq(um_vec3 a, um_vec3 b);

// Other
um_vec3 um_vec3_madd(um_vec3 a, um_vec3 b, float s); // a + b * s
um_vec3 um_vec3_lerp(um_vec3 a, um_vec3 b, float t);

typedef struct {
    float x, y, z, w;
} um_vec4;

um_vec4 um_vec4_from_ptr(const float v[4]);
void um_vec4_to_ptr(um_vec4 v, float p[4]);
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
    float x, y, z, w;
} um_quat;

typedef struct {
    um_vec4 cols[4];
} um_mat;

// Quaternion

// Construction
um_quat um_quat_from_ptr(const float xyzw[4]);
void um_quat_to_ptr(um_quat q, float xyzw[4]);
um_quat um_quat_identity();
um_quat um_quat_from_matrix(um_mat m);
um_quat um_quat_from_axis_angle(um_vec3 axis, um_rad angle);

// Unary
float um_quat_len(um_quat q);
um_quat um_quat_normalized(um_quat q);
um_quat um_quat_conjugate(um_quat q);

// Binary
um_quat um_quat_mul(um_quat a, um_quat b);
um_vec3 um_quat_mul_vec3(um_quat q, um_vec3 v);

um_quat um_quat_slerp(um_quat a, um_quat b, float t);

// Matrix

// Construction
um_mat um_mat_from_ptr(const float m[16]);
void um_mat_to_ptr(um_mat m, float p[16]);
um_mat um_mat_identity();
um_mat um_mat_scale(um_vec3 v);
um_mat um_mat_rotate(um_vec3 axis, um_rad angle);
um_mat um_mat_translate(um_vec3 v);
um_mat um_mat_ortho(float left, float right, float bottom, float top, float znear, float zfar);
um_mat um_mat_perspective(um_deg fovy, float aspect, float znear, float zfar);
um_mat um_mat_look_at(um_vec3 eye, um_vec3 target, um_vec3 up);
um_mat um_mat_from_quat(um_quat q);

// Unary
um_mat um_mat_transpose(um_mat m);
um_mat um_mat_invert(um_mat m);

// Binary
um_mat um_mat_mul(um_mat a, um_mat b);
um_vec3 um_mat_mul_vec3(um_mat m, um_vec3 v, float w);
um_vec4 um_mat_mul_vec4(um_mat m, um_vec4 v);

// Other
void um_mat_decompose_trs(um_mat m, um_vec3* translation, um_quat* rotation, um_vec3* scale);

#ifdef __cplusplus
}
#endif
