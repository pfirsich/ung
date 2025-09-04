#include "um.h"

namespace um {

using deg = ::um_deg;
using rad = ::um_rad;
using vec3 = ::um_vec3;
using vec4 = ::um_vec4;
using quat = ::um_quat;
using mat = ::um_mat;

[[nodiscard]] inline float clamp(float v, float lo, float hi) noexcept
{
    return um_clamp(v, lo, hi);
}

[[nodiscard]] inline rad to_rad(deg deg) noexcept
{
    return um_to_rad(deg);
}

[[nodiscard]] inline deg to_deg(rad rad) noexcept
{
    return um_to_deg(rad);
}

// vec3

[[nodiscard]] inline vec3 make_vec3(const float xyz[3]) noexcept
{
    return { xyz[0], xyz[1], xyz[2] };
}

[[nodiscard]] inline vec3 make_vec3(float v) noexcept
{
    return { v, v, v };
}

inline void to_ptr(vec3 v, float xyz[3]) noexcept
{
    um_vec3_to_ptr(v, xyz);
}

[[nodiscard]] inline float len(vec3 v) noexcept
{
    return um_vec3_len(v);
}

[[nodiscard]] inline float len_sq(vec3 v) noexcept
{
    return um_vec3_len_sq(v);
}

[[nodiscard]] inline vec3 normalized(vec3 v) noexcept
{
    return um_vec3_normalized(v);
}

[[nodiscard]] inline vec3 mul(vec3 v, float s) noexcept
{
    return um_vec3_mul(v, s);
}

[[nodiscard]] inline float dot(vec3 a, vec3 b) noexcept
{
    return um_vec3_dot(a, b);
}

[[nodiscard]] inline float dist(vec3 a, vec3 b) noexcept
{
    return um_vec3_dist(a, b);
}

[[nodiscard]] inline vec3 add(vec3 a, vec3 b) noexcept
{
    return um_vec3_add(a, b);
}

[[nodiscard]] inline vec3 sub(vec3 a, vec3 b) noexcept
{
    return um_vec3_sub(a, b);
}

[[nodiscard]] inline vec3 cross(vec3 a, vec3 b) noexcept
{
    return um_vec3_cross(a, b);
}

[[nodiscard]] inline vec3 madd(vec3 a, vec3 b, float s) noexcept
{
    return um_vec3_madd(a, b, s);
}

[[nodiscard]] inline vec3 lerp(vec3 a, vec3 b, float t) noexcept
{
    return um_vec3_lerp(a, b, t);
}

[[nodiscard]] inline bool eq(vec3 a, vec3 b) noexcept
{
    return um_vec3_eq(a, b);
}

// quat

[[nodiscard]] inline quat make_quat() noexcept
{
    return um_quat_identity();
}

[[nodiscard]] inline quat make_quat(const float xyzw[4]) noexcept
{
    return { xyzw[0], xyzw[1], xyzw[2], xyzw[3] };
}

[[nodiscard]] inline quat make_quat(mat m) noexcept
{
    return um_quat_from_matrix(m);
}

[[nodiscard]] inline quat make_quat(vec3 axis, rad angle) noexcept
{
    return um_quat_from_axis_angle(axis, angle);
}

inline void to_ptr(quat v, float xyzw[3]) noexcept
{
    um_quat_to_ptr(v, xyzw);
}

[[nodiscard]] inline float len(um_quat q) noexcept
{
    return um_quat_len(q);
}

[[nodiscard]] inline quat normalized(quat v) noexcept
{
    return um_quat_normalized(v);
}

[[nodiscard]] inline quat conjugate(quat v) noexcept
{
    return um_quat_conjugate(v);
}

[[nodiscard]] inline quat mul(quat a, quat b) noexcept
{
    return um_quat_mul(a, b);
}

[[nodiscard]] inline vec3 mul(quat q, vec3 v) noexcept
{
    return um_quat_mul_vec3(q, v);
}

[[nodiscard]] inline quat slerp(quat a, quat b, float t) noexcept
{
    return um_quat_slerp(a, b, t);
}

// mat

[[nodiscard]] inline mat make_mat() noexcept
{
    return um_mat_identity();
}

[[nodiscard]] inline mat make_mat(const float p[16]) noexcept
{
    return um_mat_from_ptr(p);
}

[[nodiscard]] inline mat make_mat(quat q) noexcept
{
    return um_mat_from_quat(q);
}

inline void to_ptr(um_mat m, float p[16])
{
    um_mat_to_ptr(m, p);
}

[[nodiscard]] inline mat scale(vec3 s) noexcept
{
    return um_mat_scale(s);
}

[[nodiscard]] inline mat rotate(vec3 axis, rad angle) noexcept
{
    return um_mat_rotate(axis, angle);
}

[[nodiscard]] inline mat translate(vec3 t) noexcept
{
    return um_mat_translate(t);
}

[[nodiscard]] inline mat ortho(float left, float right, float bottom, float top, float znear, float zfar) noexcept
{
    return um_mat_ortho(left, right, bottom, top, znear, zfar);
}

[[nodiscard]] inline mat perspective(deg fovy, float aspect, float znear, float zfar) noexcept
{
    return um_mat_perspective(fovy, aspect, znear, zfar);
}

[[nodiscard]] inline mat look_at(vec3 eye, vec3 target, vec3 up) noexcept
{
    return um_mat_look_at(eye, target, up);
}

[[nodiscard]] inline mat transpose(mat m) noexcept
{
    return um_mat_transpose(m);
}

[[nodiscard]] inline mat invert(mat m) noexcept
{
    return um_mat_invert(m);
}

[[nodiscard]] inline mat mul(mat a, mat b) noexcept
{
    return um_mat_mul(a, b);
}

[[nodiscard]] inline vec3 mul(mat m, vec3 v, float w) noexcept
{
    return um_mat_mul_vec3(m, v, w);
}

[[nodiscard]] inline vec4 mul(mat m, vec4 v) noexcept
{
    return um_mat_mul_vec4(m, v);
}

inline void decompose(mat m, vec3& t, quat& r, vec3& s) noexcept
{
    um_mat_decompose_trs(m, &t, &r, &s);
}

}

// Operators (global namespace)

[[nodiscard]] inline constexpr um_vec3 operator+(const um_vec3& a, const um_vec3& b) noexcept
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

[[nodiscard]] inline constexpr um_vec3 operator-(const um_vec3& a, const um_vec3& b) noexcept
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

[[nodiscard]] inline constexpr um_vec3 operator-(const um_vec3& v) noexcept
{
    return { -v.x, -v.y, -v.z };
}

[[nodiscard]] inline constexpr um_vec3 operator*(const um_vec3& v, float s) noexcept
{
    return { v.x * s, v.y * s, v.z * s };
}

[[nodiscard]] inline constexpr um_vec3 operator*(float s, const um_vec3& v) noexcept
{
    return v * s;
}

[[nodiscard]] inline constexpr um_vec3 operator/(um_vec3 v, float s) noexcept
{
    return { v.x / s, v.y / s, v.z / s };
}

inline um_vec3& operator+=(um_vec3& a, const um_vec3& b) noexcept
{
    a = a + b;
    return a;
}

inline um_vec3& operator-=(um_vec3& a, const um_vec3& b) noexcept
{
    a = a - b;
    return a;
}

inline um_vec3& operator*=(um_vec3& a, float s) noexcept
{
    a = a * s;
    return a;
}

inline um_vec3& operator/=(um_vec3& a, float s) noexcept
{
    a = a / s;
    return a;
}

[[nodiscard]] inline um_quat operator*(const um_quat& a, const um_quat& b)
{
    return um::mul(a, b);
}

[[nodiscard]] inline um_vec3 operator*(const um_quat& q, const um_vec3& v)
{
    return um::mul(q, v);
}

[[nodiscard]] inline um_mat operator*(const um_mat& a, const um_mat& b) noexcept
{
    return um::mul(a, b);
}

[[nodiscard]] inline um_vec4 operator*(const um_mat& m, const um_vec4& v) noexcept
{
    return um::mul(m, v);
}