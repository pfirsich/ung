#include "um.h"

#include <cstring>
#include <math.h>

#define PIf 3.14159265358979323846f

float um_clamp(float v, float lo, float hi)
{
    return fminf(fmaxf(v, lo), hi);
}

um_vec3 um_vec3_from_ptr(const float v[3])
{
    return { v[0], v[1], v[2] };
}

void um_vec3_to_ptr(um_vec3 v, float p[3])
{
    std::memcpy(p, &v.x, sizeof(float) * 3);
}

float um_vec3_len(um_vec3 v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

float um_vec3_len_sq(um_vec3 v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

um_vec3 um_vec3_normalized(um_vec3 v)
{
    float len = um_vec3_len(v);
    if (len < 0.0001f) {
        return { 0.0f, 0.0f, 0.0f };
    }
    float inv_len = 1.0f / len;
    return { v.x * inv_len, v.y * inv_len, v.z * inv_len };
}

um_vec3 um_vec3_mul(um_vec3 v, float s)
{
    return { v.x * s, v.y * s, v.z * s };
}

float um_vec3_dot(um_vec3 a, um_vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float um_vec3_dist(um_vec3 a, um_vec3 b)
{
    um_vec3 d = um_vec3_sub(a, b);
    return um_vec3_len(d);
}

um_vec3 um_vec3_add(um_vec3 a, um_vec3 b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

um_vec3 um_vec3_sub(um_vec3 a, um_vec3 b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

um_vec3 um_vec3_cross(um_vec3 a, um_vec3 b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

um_vec3 um_vec3_lerp(um_vec3 a, um_vec3 b, float t)
{
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

bool um_vec3_eq(um_vec3 a, um_vec3 b)
{
    // Use small epsilon for floating point comparison
    const float EPSILON = 0.000001f;
    return fabsf(a.x - b.x) < EPSILON && fabsf(a.y - b.y) < EPSILON && fabsf(a.z - b.z) < EPSILON;
}

// Vec4 implementations
um_vec4 um_vec4_from_ptr(const float v[4])
{
    return { v[0], v[1], v[2], v[3] };
}

void um_vec4_to_ptr(um_vec4 v, float p[4])
{
    std::memcpy(p, &v.x, sizeof(float) * 4);
}

float um_vec4_len(um_vec4 v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
}

float um_vec4_len_sq(um_vec4 v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w;
}

um_vec4 um_vec4_normalized(um_vec4 v)
{
    float len = um_vec4_len(v);
    if (len < 0.0001f) {
        return { 0.0f, 0.0f, 0.0f, 0.0f };
    }
    float inv_len = 1.0f / len;
    return { v.x * inv_len, v.y * inv_len, v.z * inv_len, v.w * inv_len };
}

um_vec4 um_vec4_mul(um_vec4 v, float s)
{
    return { v.x * s, v.y * s, v.z * s, v.w * s };
}

float um_vec4_dot(um_vec4 a, um_vec4 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

float um_vec4_dist(um_vec4 a, um_vec4 b)
{
    um_vec4 d = um_vec4_sub(a, b);
    return um_vec4_len(d);
}

um_vec4 um_vec4_add(um_vec4 a, um_vec4 b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
}

um_vec4 um_vec4_sub(um_vec4 a, um_vec4 b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w };
}

um_vec4 um_vec4_lerp(um_vec4 a, um_vec4 b, float t)
{
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t };
}

bool um_vec4_eq(um_vec4 a, um_vec4 b)
{
    const float EPSILON = 0.000001f;
    return fabsf(a.x - b.x) < EPSILON && fabsf(a.y - b.y) < EPSILON && fabsf(a.z - b.z) < EPSILON
        && fabsf(a.w - b.w) < EPSILON;
}

// Quaternion implementations
um_quat um_quat_from_ptr(const float xyzw[4])
{
    return { xyzw[0], xyzw[1], xyzw[2], xyzw[3] };
}

void um_quat_to_ptr(um_quat q, float xyzw[4])
{
    std::memcpy(xyzw, &q.x, sizeof(float) * 4);
}

um_quat um_quat_identity()
{
    return { 0.0f, 0.0f, 0.0f, 1.0f };
}

float um_quat_len(um_quat q)
{
    return sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

um_quat um_quat_normalized(um_quat q)
{
    float len = um_quat_len(q);
    if (len < 0.0001f) {
        return um_quat_identity();
    }
    float inv_len = 1.0f / len;
    return { q.x * inv_len, q.y * inv_len, q.z * inv_len, q.w * inv_len };
}

um_quat um_quat_conjugate(um_quat q)
{
    // The conjugate of a quaternion is (-x, -y, -z, w)
    return { -q.x, -q.y, -q.z, q.w };
}

um_quat um_quat_mul(um_quat a, um_quat b)
{
    um_quat result;
    result.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    result.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    result.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    result.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return result;
}

um_quat um_quat_from_matrix(um_mat m)
{
    // Extract the rotation matrix (upper-left 3x3)
    float m00 = m.cols[0].x;
    float m01 = m.cols[1].x;
    float m02 = m.cols[2].x;
    float m10 = m.cols[0].y;
    float m11 = m.cols[1].y;
    float m12 = m.cols[2].y;
    float m20 = m.cols[0].z;
    float m21 = m.cols[1].z;
    float m22 = m.cols[2].z;

    um_quat q;

    // Algorithm from:
    // http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
    float trace = m00 + m11 + m22;

    if (trace > 0) {
        float s = 0.5f / sqrtf(trace + 1.0f);
        q.x = (m21 - m12) * s;
        q.y = (m02 - m20) * s;
        q.z = (m10 - m01) * s;
        q.w = 0.25f / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = 2.0f * sqrtf(1.0f + m00 - m11 - m22);
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
        q.w = (m21 - m12) / s;
    } else if (m11 > m22) {
        float s = 2.0f * sqrtf(1.0f + m11 - m00 - m22);
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
        q.w = (m02 - m20) / s;
    } else {
        float s = 2.0f * sqrtf(1.0f + m22 - m00 - m11);
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
        q.w = (m10 - m01) / s;
    }

    return um_quat_normalized(q);
}

um_quat um_quat_from_axis_angle(um_vec3 axis, float angle)
{
    const auto half_angle = angle * 0.5f;
    um_vec3 normalized_axis = um_vec3_normalized(axis);
    const auto s = sinf(half_angle);

    return {
        normalized_axis.x * s,
        normalized_axis.y * s,
        normalized_axis.z * s,
        cosf(half_angle),
    };
}

um_quat um_quat_slerp(um_quat a, um_quat b, float t)
{
    // Calculate angle between quaternions
    float cos_half_theta = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;

    // If a and b are very close, linearly interpolate to avoid divide by zero
    if (fabsf(cos_half_theta) >= 0.999f) {
        return um_quat_normalized({
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t,
        });
    }

    // Ensure we take the shortest path
    if (cos_half_theta < 0.0f) {
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        b.w = -b.w;
        cos_half_theta = -cos_half_theta;
    }

    float half_theta = acosf(cos_half_theta);
    float sin_half_theta = sqrtf(1.0f - cos_half_theta * cos_half_theta);

    // If theta = 180 degrees, rotation not well-defined
    // We could rotate around any axis perpendicular to a or b
    if (fabsf(sin_half_theta) < 0.001f) {
        return um_quat_normalized({
            a.x * 0.5f + b.x * 0.5f,
            a.y * 0.5f + b.y * 0.5f,
            a.z * 0.5f + b.z * 0.5f,
            a.w * 0.5f + b.w * 0.5f,
        });
    }

    float ratio_a = sinf((1.0f - t) * half_theta) / sin_half_theta;
    float ratio_b = sinf(t * half_theta) / sin_half_theta;

    return um_quat_normalized({
        a.x * ratio_a + b.x * ratio_b,
        a.y * ratio_a + b.y * ratio_b,
        a.z * ratio_a + b.z * ratio_b,
        a.w * ratio_a + b.w * ratio_b,
    });
}

um_vec3 um_quat_mul_vec3(um_quat q, um_vec3 v)
{
    // Extract vector part of quaternion
    um_vec3 qv = { q.x, q.y, q.z };

    // Compute vector-vector cross product part
    um_vec3 uv = um_vec3_cross(qv, v);
    // Second cross
    um_vec3 uuv = um_vec3_cross(qv, uv);

    // Apply rotation: v' = v + 2q_w * (q_v × v) + 2(q_v × (q_v × v))
    uv = um_vec3_mul(uv, 2.0f * q.w);
    uuv = um_vec3_mul(uuv, 2.0f);

    return um_vec3_add(um_vec3_add(v, uv), uuv);
}

// Matrix implementations
um_mat um_mat_from_ptr(const float m[16])
{
    um_mat mat;
    std::memcpy(&mat.cols[0].x, m, sizeof(float) * 16);
    return mat;
}

void um_mat_to_ptr(um_mat m, float p[16])
{
    std::memcpy(p, &m.cols[0].x, sizeof(float) * 16);
}

um_mat um_mat_identity()
{
    um_mat m = {};

    m.cols[0] = { 1.0f, 0.0f, 0.0f, 0.0f };
    m.cols[1] = { 0.0f, 1.0f, 0.0f, 0.0f };
    m.cols[2] = { 0.0f, 0.0f, 1.0f, 0.0f };
    m.cols[3] = { 0.0f, 0.0f, 0.0f, 1.0f };

    return m;
}

um_mat um_mat_scale(um_vec3 v)
{
    um_mat m = um_mat_identity();
    m.cols[0] = { v.x, 0.0f, 0.0f, 0.0f };
    m.cols[1] = { 0.0f, v.y, 0.0f, 0.0f };
    m.cols[2] = { 0.0f, 0.0f, v.z, 0.0f };
    return m;
}

um_mat um_mat_rotate(um_vec3 axis, float angle)
{
    // Create rotation matrix from axis and angle
    um_quat q = um_quat_from_axis_angle(axis, angle);

    float xx = q.x * q.x;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float xw = q.x * q.w;

    float yy = q.y * q.y;
    float yz = q.y * q.z;
    float yw = q.y * q.w;

    float zz = q.z * q.z;
    float zw = q.z * q.w;

    um_mat m;

    m.cols[0] = { 1.0f - 2.0f * (yy + zz), 2.0f * (xy + zw), 2.0f * (xz - yw), 0.0f };
    m.cols[1] = { 2.0f * (xy - zw), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + xw), 0.0f };
    m.cols[2] = { 2.0f * (xz + yw), 2.0f * (yz - xw), 1.0f - 2.0f * (xx + yy), 0.0f };
    m.cols[3] = { 0.0f, 0.0f, 0.0f, 1.0f };

    return m;
}

um_mat um_mat_from_quat(um_quat q)
{
    // Convert quaternion directly to a rotation matrix
    float xx = q.x * q.x;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float xw = q.x * q.w;

    float yy = q.y * q.y;
    float yz = q.y * q.z;
    float yw = q.y * q.w;

    float zz = q.z * q.z;
    float zw = q.z * q.w;

    um_mat m;

    m.cols[0] = { 1.0f - 2.0f * (yy + zz), 2.0f * (xy + zw), 2.0f * (xz - yw), 0.0f };
    m.cols[1] = { 2.0f * (xy - zw), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + xw), 0.0f };
    m.cols[2] = { 2.0f * (xz + yw), 2.0f * (yz - xw), 1.0f - 2.0f * (xx + yy), 0.0f };
    m.cols[3] = { 0.0f, 0.0f, 0.0f, 1.0f };

    return m;
}

void um_mat_decompose_trs(um_mat m, um_vec3* translation, um_quat* rotation, um_vec3* scale)
{
    // translation
    *translation = { m.cols[3].x, m.cols[3].y, m.cols[3].z };

    // vec3 columns
    const auto c0 = um_vec3_from_ptr(&m.cols[0].x);
    const auto c1 = um_vec3_from_ptr(&m.cols[1].x);
    const auto c2 = um_vec3_from_ptr(&m.cols[2].x);

    // scale
    *scale = { um_vec3_len(c0), um_vec3_len(c1), um_vec3_len(c2) };

    // remove scale to get rotation basis
    um_vec3 n0 = um_vec3_mul(c0, 1.0f / scale->x);
    um_vec3 n1 = um_vec3_mul(c1, 1.0f / scale->y);
    um_vec3 n2 = um_vec3_mul(c2, 1.0f / scale->z);

    // Build a pure rotation matrix from n0|n1|n2
    auto r = um_mat_identity();
    r.cols[0].x = n0.x;
    r.cols[0].y = n0.y;
    r.cols[0].z = n0.z;
    r.cols[1].x = n1.x;
    r.cols[1].y = n1.y;
    r.cols[1].z = n1.z;
    r.cols[2].x = n2.x;
    r.cols[2].y = n2.y;
    r.cols[2].z = n2.z;
    *rotation = um_quat_from_matrix(r);
}

um_mat um_mat_translate(um_vec3 vec)
{
    um_mat m = um_mat_identity();
    m.cols[3] = { vec.x, vec.y, vec.z, 1.0f };
    return m;
}

um_mat um_mat_ortho(float left, float right, float bottom, float top, float znear, float zfar)
{
    um_mat m = um_mat_identity();

    float rl = 1.0f / (right - left);
    float tb = 1.0f / (top - bottom);
    float fn = 1.0f / (zfar - znear);

    m.cols[0] = { 2.0f * rl, 0.0f, 0.0f, 0.0f };
    m.cols[1] = { 0.0f, 2.0f * tb, 0.0f, 0.0f };
    m.cols[2] = { 0.0f, 0.0f, -2.0f * fn, 0.0f };
    m.cols[3] = { -(right + left) * rl, -(top + bottom) * tb, -(zfar + znear) * fn, 1.0f };

    return m;
}

um_mat um_mat_perspective(float fovy, float aspect, float znear, float zfar)
{
    um_mat m = {};

    float f = 1.0f / tanf(fovy * 0.5f);
    float fn = 1.0f / (znear - zfar);

    m.cols[0] = { f / aspect, 0.0f, 0.0f, 0.0f };
    m.cols[1] = { 0.0f, f, 0.0f, 0.0f };
    m.cols[2] = { 0.0f, 0.0f, (znear + zfar) * fn, -1.0f };
    m.cols[3] = { 0.0f, 0.0f, 2.0f * znear * zfar * fn, 0.0f };

    return m;
}

um_mat um_mat_look_at(um_vec3 eye, um_vec3 target, um_vec3 up)
{
    um_mat m;

    // Calculate forward direction (z axis)
    um_vec3 f = um_vec3_normalized(um_vec3_sub(target, eye));

    // Calculate right direction (x axis)
    um_vec3 r = um_vec3_normalized(um_vec3_cross(f, up));

    // Calculate adjusted up vector (y axis)
    um_vec3 u = um_vec3_cross(r, f);

    // Set view matrix columns
    m.cols[0] = { r.x, u.x, -f.x, 0.0f };
    m.cols[1] = { r.y, u.y, -f.y, 0.0f };
    m.cols[2] = { r.z, u.z, -f.z, 0.0f };
    m.cols[3] = { -um_vec3_dot(r, eye), -um_vec3_dot(u, eye), um_vec3_dot(f, eye), 1.0f };

    return m;
}

um_mat um_mat_transpose(um_mat m)
{
    um_mat result;

    result.cols[0] = { m.cols[0].x, m.cols[1].x, m.cols[2].x, m.cols[3].x };
    result.cols[1] = { m.cols[0].y, m.cols[1].y, m.cols[2].y, m.cols[3].y };
    result.cols[2] = { m.cols[0].z, m.cols[1].z, m.cols[2].z, m.cols[3].z };
    result.cols[3] = { m.cols[0].w, m.cols[1].w, m.cols[2].w, m.cols[3].w };

    return result;
}

um_mat um_mat_invert(um_mat m)
{
    um_mat result;

    // Extract the 3x3 rotation & scale part
    float a00 = m.cols[0].x, a01 = m.cols[1].x, a02 = m.cols[2].x, a03 = m.cols[3].x;
    float a10 = m.cols[0].y, a11 = m.cols[1].y, a12 = m.cols[2].y, a13 = m.cols[3].y;
    float a20 = m.cols[0].z, a21 = m.cols[1].z, a22 = m.cols[2].z, a23 = m.cols[3].z;
    float a30 = m.cols[0].w, a31 = m.cols[1].w, a32 = m.cols[2].w, a33 = m.cols[3].w;

    float b00 = a00 * a11 - a01 * a10;
    float b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10;
    float b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11;
    float b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30;
    float b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30;
    float b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31;
    float b11 = a22 * a33 - a23 * a32;

    // Calculate determinant
    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;

    if (fabsf(det) < 0.000001f) {
        // Matrix is not invertible, return identity
        return um_mat_identity();
    }

    det = 1.0f / det;

    result.cols[0].x = (a11 * b11 - a12 * b10 + a13 * b09) * det;
    result.cols[0].y = (a12 * b08 - a10 * b11 - a13 * b07) * det;
    result.cols[0].z = (a10 * b10 - a11 * b08 + a13 * b06) * det;
    result.cols[0].w = (a11 * b07 - a10 * b09 - a12 * b06) * det;

    result.cols[1].x = (a02 * b10 - a01 * b11 - a03 * b09) * det;
    result.cols[1].y = (a00 * b11 - a02 * b08 + a03 * b07) * det;
    result.cols[1].z = (a01 * b08 - a00 * b10 - a03 * b06) * det;
    result.cols[1].w = (a00 * b09 - a01 * b07 + a02 * b06) * det;

    result.cols[2].x = (a31 * b05 - a32 * b04 + a33 * b03) * det;
    result.cols[2].y = (a32 * b02 - a30 * b05 - a33 * b01) * det;
    result.cols[2].z = (a30 * b04 - a31 * b02 + a33 * b00) * det;
    result.cols[2].w = (a31 * b01 - a30 * b03 - a32 * b00) * det;

    result.cols[3].x = (a22 * b04 - a21 * b05 - a23 * b03) * det;
    result.cols[3].y = (a20 * b05 - a22 * b02 + a23 * b01) * det;
    result.cols[3].z = (a21 * b02 - a20 * b04 - a23 * b00) * det;
    result.cols[3].w = (a20 * b03 - a21 * b01 + a22 * b00) * det;

    return result;
}

um_vec3 um_mat_mul_vec3(um_mat m, um_vec3 v, float w)
{
    um_vec4 v4 = { v.x, v.y, v.z, w };
    um_vec4 result = um_mat_mul_vec4(m, v4);

    // Convert back to 3D coordinates (perspective divide)
    if (fabsf(result.w) > 0.000001f) {
        float w_inv = 1.0f / result.w;
        return { result.x * w_inv, result.y * w_inv, result.z * w_inv };
    }

    return { result.x, result.y, result.z };
}

um_vec4 um_mat_mul_vec4(um_mat m, um_vec4 v)
{
    um_vec4 result;

    result.x = m.cols[0].x * v.x + m.cols[1].x * v.y + m.cols[2].x * v.z + m.cols[3].x * v.w;
    result.y = m.cols[0].y * v.x + m.cols[1].y * v.y + m.cols[2].y * v.z + m.cols[3].y * v.w;
    result.z = m.cols[0].z * v.x + m.cols[1].z * v.y + m.cols[2].z * v.z + m.cols[3].z * v.w;
    result.w = m.cols[0].w * v.x + m.cols[1].w * v.y + m.cols[2].w * v.z + m.cols[3].w * v.w;

    return result;
}

um_mat um_mat_mul(um_mat a, um_mat b)
{
    um_mat result;

    for (int i = 0; i < 4; i++) {
        um_vec4 col = { b.cols[i].x, b.cols[i].y, b.cols[i].z, b.cols[i].w };

        result.cols[i] = um_mat_mul_vec4(a, col);
    }

    return result;
}
