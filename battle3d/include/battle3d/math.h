#ifndef BATTLE3D_MATH_H
#define BATTLE3D_MATH_H

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    float x, y, z, w;
} Vec4;

typedef struct {
    float m[16];
} Mat4;

static inline Vec3 vec3(float x, float y, float z)
{
    Vec3 v = {x, y, z};
    return v;
}

static inline Vec3 vec3_add(Vec3 a, Vec3 b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 vec3_sub(Vec3 a, Vec3 b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 vec3_scale(Vec3 a, float s) { return vec3(a.x * s, a.y * s, a.z * s); }
static inline float vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float vec3_len(Vec3 a) { return sqrtf(vec3_dot(a, a)); }

static inline Vec3 vec3_norm(Vec3 a)
{
    float l = vec3_len(a);
    return l > 1e-8f ? vec3_scale(a, 1.0f / l) : vec3(0, 1, 0);
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b)
{
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static inline Vec4 quat_ident(void)
{
    Vec4 q = {0, 0, 0, 1};
    return q;
}

static inline Vec4 quat_norm(Vec4 q)
{
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-8f)
        return quat_ident();
    q.x /= l;
    q.y /= l;
    q.z /= l;
    q.w /= l;
    return q;
}

static inline Vec4 quat_slerp(Vec4 a, Vec4 b, float t)
{
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        b.w = -b.w;
        dot = -dot;
    }
    if (dot > 0.9995f) {
        Vec4 r = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z), a.w + t * (b.w - a.w)};
        return quat_norm(r);
    }
    float theta = acosf(dot);
    float s = sinf(theta);
    float w1 = sinf((1.0f - t) * theta) / s;
    float w2 = sinf(t * theta) / s;
    Vec4 r = {a.x * w1 + b.x * w2, a.y * w1 + b.y * w2, a.z * w1 + b.z * w2, a.w * w1 + b.w * w2};
    return r;
}

static inline Mat4 mat4_ident(void)
{
    Mat4 m;
    memset(m.m, 0, sizeof(m.m));
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

static inline Mat4 mat4_mul(Mat4 a, Mat4 b)
{
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            r.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] + a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] + a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return r;
}

static inline Vec3 mat4_mul_point(Mat4 m, Vec3 p)
{
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    if (fabsf(w) > 1e-8f) {
        x /= w;
        y /= w;
        z /= w;
    }
    return vec3(x, y, z);
}

static inline Vec3 mat4_mul_dir(Mat4 m, Vec3 p)
{
    return vec3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z, m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z,
                m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z);
}

static inline Mat4 mat4_translate(Vec3 t)
{
    Mat4 m = mat4_ident();
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

static inline Mat4 mat4_scale(Vec3 s)
{
    Mat4 m = mat4_ident();
    m.m[0] = s.x;
    m.m[5] = s.y;
    m.m[10] = s.z;
    return m;
}

static inline Mat4 mat4_from_quat(Vec4 q)
{
    q = quat_norm(q);
    float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 m = mat4_ident();
    m.m[0] = 1.0f - 2.0f * (y * y + z * z);
    m.m[1] = 2.0f * (x * y + z * w);
    m.m[2] = 2.0f * (x * z - y * w);
    m.m[4] = 2.0f * (x * y - z * w);
    m.m[5] = 1.0f - 2.0f * (x * x + z * z);
    m.m[6] = 2.0f * (y * z + x * w);
    m.m[8] = 2.0f * (x * z + y * w);
    m.m[9] = 2.0f * (y * z - x * w);
    m.m[10] = 1.0f - 2.0f * (x * x + y * y);
    return m;
}

static inline Mat4 mat4_trs(Vec3 t, Vec4 r, Vec3 s)
{
    return mat4_mul(mat4_translate(t), mat4_mul(mat4_from_quat(r), mat4_scale(s)));
}

static inline Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up)
{
    Vec3 f = vec3_norm(vec3_sub(center, eye));
    Vec3 s = vec3_norm(vec3_cross(f, up));
    Vec3 u = vec3_cross(s, f);
    Mat4 m = mat4_ident();
    m.m[0] = s.x;
    m.m[4] = s.y;
    m.m[8] = s.z;
    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;
    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;
    m.m[12] = -vec3_dot(s, eye);
    m.m[13] = -vec3_dot(u, eye);
    m.m[14] = vec3_dot(f, eye);
    return m;
}

static inline Mat4 mat4_perspective(float fovy_deg, float aspect, float zn, float zf)
{
    float f = 1.0f / tanf(fovy_deg * 0.5f * (float)M_PI / 180.0f);
    Mat4 m;
    memset(m.m, 0, sizeof(m.m));
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = (zf + zn) / (zn - zf);
    m.m[11] = -1.0f;
    m.m[14] = (2.0f * zf * zn) / (zn - zf);
    return m;
}

static inline Mat4 mat4_ortho(float l, float r, float b, float t, float n, float f)
{
    Mat4 m = mat4_ident();
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = -2.0f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    return m;
}

static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

static inline Vec3 vec3_lerp(Vec3 a, Vec3 b, float t)
{
    return vec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t));
}

#endif
