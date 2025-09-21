#include "pfx.hpp"

#include <algorithm>
#include <assert.h>
#include <cstdio>
#include <math.h>
#include <string_view>

namespace pfx {

static float clamp(float v, float lo, float hi)
{
    return fminf(fmaxf(v, lo), hi);
}

template <typename T>
static T* allocate(size_t n = 1)
{
    auto alloc = ung_get_allocator();
    auto p = (T*)alloc->allocate(sizeof(T) * n, alloc->ctx);
    for (size_t i = 0; i < n; ++i) {
        new (p + i) T {};
    }
    return p;
}

template <typename T>
static void deallocate(T* ptr, size_t n = 1)
{
    auto alloc = ung_get_allocator();
    if (!ptr) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        (ptr + i)->~T();
    }
    alloc->deallocate(ptr, sizeof(T) * n, alloc->ctx);
}

Particle& Buffer::next()
{
    auto& p = particles[next_idx];
    next_idx = (next_idx + 1) % particles.size();
    p.age = 0.0f;
    return p;
}

bool add_behavior(std::span<Behavior> behaviors, const void* params, Behavior::UpdateFunc* func)
{
    for (auto& b : behaviors) {
        if (!b.func) {
            b.params = params;
            b.func = func;
            return true;
        }
    }
    return false;
}

void update(std::span<const Behavior> behaviors, std::span<Particle> particles, float dt)
{
    for (auto& p : particles) {
        if (!p.alive()) {
            continue;
        }
        p.age = fminf(p.lifetime, p.age + dt);
    }

    for (const auto& b : behaviors) {
        if (!b.func) {
            continue;
        }
        b.func(b.params, particles, dt);
    }

    for (auto& p : particles) {
        p.pos = um_vec3_add(p.pos, um_vec3_mul(p.vel, dt));
        p.rot += p.rvel * dt;
    }
}

void gravity_behavior(const void* params, std::span<Particle> particles, float dt)
{
    const auto grav = (const Gravity*)(params);
    for (auto& p : particles) {
        if (!p.alive()) {
            continue;
        }
        p.vel.y += grav->y * dt;
    }
}

void drag_behavior(const void* params, std::span<Particle> particles, float dt)
{
    const auto drag = (const Drag*)(params);
    const float f = 1.0f / (1.0f + drag->k * dt);
    for (auto& p : particles) {
        if (!p.alive()) {
            continue;
        }
        p.vel = um_vec3_mul(p.vel, f);
    }
}

void fade_behavior(const void* params, std::span<Particle> particles, float /*dt*/)
{
    const auto fade = (const Fade*)(params);
    for (auto& p : particles) {
        if (!p.alive()) {
            continue;
        }
        const auto t = p.progress();
        if (t < fade->in) {
            p.a = clamp(t / fade->in, 0.f, 1.f);
        } else if (t > 1.0f - fade->out) {
            p.a = clamp((1.0f - t) / fade->out, 0.f, 1.f);
        } else {
            p.a = 1.0f;
        }
    }
}

static um_vec3 random_dir()
{
    while (true) {
        const um_vec3 v = {
            ung_random_float(-1.0f, 1.0f),
            ung_random_float(-1.0f, 1.0f),
            ung_random_float(-1.0f, 1.0f),
        };
        if (um_vec3_len_sq(v) < 1.0f) {
            return v;
        }
    }
}

static um_vec3 random_dir(const um_vec3& base_dir, float cone_angle_deg)
{
    if (cone_angle_deg >= 180.f) {
        return random_dir();
    } else {
        const auto cangle = cosf(cone_angle_deg / 180.f * M_PIf);
        while (true) {
            const auto dir = random_dir();
            if (um_vec3_dot(dir, base_dir) >= cangle) {
                return dir;
            }
        }
    }
}

static float random_vary(float x, float var)
{
    return x + ung_random_float(-var, var);
}

void SpawnParams::spawn(Particle& p, const um_vec3& pos, const um_vec3& dir) const
{
    const um_vec3 off = {
        ung_random_float(pos_min.x, pos_max.x),
        ung_random_float(pos_min.y, pos_max.y),
        ung_random_float(pos_min.z, pos_max.z),
    };
    p.pos = um_vec3_add(pos, off);

    p.rot = ung_random_float(rot_min, rot_max);
    p.rvel = ung_random_float(rvel_min, rvel_max);

    const float speed = ung_random_float(speed_min, speed_max);
    p.vel = um_vec3_mul(random_dir(dir, vel_spread_deg), speed);

    p.lifetime = ung_random_float(lifetime_min, lifetime_max);
    p.size = ung_random_float(scale_min, scale_max);

    p.r = random_vary(color_base.x, color_var.x);
    p.g = random_vary(color_base.y, color_var.y);
    p.b = random_vary(color_base.z, color_var.z);
    p.a = random_vary(color_base.w, color_var.w);

    p.age = 0.f;
    p.flags = 1u; // PF_ALIVE if you add enum
}

Particle& SpawnParams::spawn(Buffer& buf, const um_vec3& pos, const um_vec3& dir) const
{
    auto& p = buf.next();
    spawn(p, pos, dir);
    return p;
}

void SpawnParams::spawn(Buffer& buf, size_t n, um_vec3 pos, um_vec3 dir) const
{
    for (size_t i = 0; i < n; ++i) {
        spawn(buf, pos, dir);
    }
}

size_t pack_gpu_particle_instances(
    std::span<const Particle> particles, std::span<GpuParticleInstance> gpu_particles)
{
    size_t i = 0;
    for (const auto& p : particles) {
        if (!p.alive()) {
            continue;
        }
        assert(i < gpu_particles.size());

        auto& inst = gpu_particles[i++];
        inst = {
            { p.pos.x, p.pos.y, p.pos.z },
            p.size,
            p.rot,
            (uint8_t)clamp(p.r * 255.f, 0.f, 255.f),
            (uint8_t)clamp(p.g * 255.f, 0.f, 255.f),
            (uint8_t)clamp(p.b * 255.f, 0.f, 255.f),
            (uint8_t)clamp(p.a * 255.f, 0.f, 255.f),
        };
    }
    return i;
}

void sort_particles(std::span<GpuParticleInstance> gpu_particles, ung_camera_id cam, Sort sort)
{
    if (sort == Sort::None) {
        return;
    }

    // Camera pose
    const auto trafo = ung_camera_get_transform(cam);
    um_vec3 cam_pos;
    ung_transform_get_position(trafo, &cam_pos.x);

    um_quat q;
    ung_transform_get_orientation(trafo, &q.x);
    auto fwd = um_vec3_normalized(um_quat_mul_vec3(q, { 0.0f, 0.0f, -1.0f }));

    if (sort == Sort::FrontToBack) {
        fwd = um_vec3_mul(fwd, -1.f);
    }

    std::stable_sort(gpu_particles.begin(), gpu_particles.end(),
        [&](const GpuParticleInstance& a, const GpuParticleInstance& b) {
            const auto dot_a = (a.pos[0] - cam_pos.x) * fwd.x + (a.pos[1] - cam_pos.y) * fwd.y
                + (a.pos[2] - cam_pos.z) * fwd.z;
            const auto dot_b = (b.pos[0] - cam_pos.x) * fwd.x + (b.pos[1] - cam_pos.y) * fwd.y
                + (b.pos[2] - cam_pos.z) * fwd.z;
            return dot_a > dot_b;
        });
}

size_t update_instance_buffer(mugfx_buffer_id instance_buffer, std::span<const Particle> particles,
    std::span<GpuParticleInstance> gpu_particles, ung_camera_id cam, Sort sort)
{
    const auto count = pack_gpu_particle_instances(particles, gpu_particles);

    if (count == 0) {
        return 0;
    }

    sort_particles(gpu_particles.first(count), cam, sort);

    // orphan, then upload instance data
    mugfx_buffer_update(instance_buffer, 0, { nullptr, 0 });
    mugfx_buffer_update(
        instance_buffer, 0, { gpu_particles.data(), count * sizeof(GpuParticleInstance) });

    return count;
}

mugfx_buffer_id create_instance_buffer(size_t max_instances)
{
    mugfx_buffer_create_params b {};
    b.target = MUGFX_BUFFER_TARGET_ARRAY;
    b.usage = MUGFX_BUFFER_USAGE_HINT_DYNAMIC;
    b.data = mugfx_slice { .data = nullptr, .length = max_instances * sizeof(GpuParticleInstance) };
    return mugfx_buffer_create(b);
}

ung_geometry_id create_particle_geometry(mugfx_buffer_id instance_buffer)
{
    // [-0.5..0.5] quad in local space; expanded in VS using camera right/up
    struct BillboardVert {
        float pos[2];
        float uv[2];
    };

    static const BillboardVert verts[4] = {
        { { -0.5f, -0.5f }, { 0.0f, 0.0f } },
        { { 0.5f, -0.5f }, { 1.0f, 0.0f } },
        { { 0.5f, 0.5f }, { 1.0f, 1.0f } },
        { { -0.5f, 0.5f }, { 0.0f, 1.0f } },
    };
    static const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };

    mugfx_buffer_id vbuf = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_ARRAY,
        .usage = MUGFX_BUFFER_USAGE_HINT_STATIC,
        .data = mugfx_slice { verts, sizeof(verts) },
    });

    mugfx_buffer_id ibuf = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_INDEX,
        .usage = MUGFX_BUFFER_USAGE_HINT_STATIC,
        .data = mugfx_slice { indices, sizeof(indices) },
    });

    const static auto tF32 = MUGFX_VERTEX_ATTRIBUTE_TYPE_F32;
    const static auto tU8norm = MUGFX_VERTEX_ATTRIBUTE_TYPE_U8_NORM;
    const static auto inst = MUGFX_VERTEX_ATTRIBUTE_RATE_INSTANCE;
    return ung_geometry_create({
        .draw_mode = MUGFX_DRAW_MODE_TRIANGLES,
        .vertex_buffers = {
            // VERTEX
            {
                .buffer = vbuf,
                .stride = sizeof(BillboardVert),
                .attributes = {
                    { .location = 0, .components = 2, .type = tF32 }, // pos
                    { .location = 1, .components = 2, .type = tF32 }, // uv
                }
            },
            // INSTANCE
            {
                .buffer = instance_buffer,
                .stride = sizeof(GpuParticleInstance),
                .attributes = {
                    { .location = 2, .components = 3, .type = tF32, .rate = inst }, // pos
                    { .location = 3, .components = 1, .type = tF32, .rate = inst }, // size
                    { .location = 4, .components = 1, .type = tF32, .rate = inst }, // rot
                    { .location = 5, .components = 4, .type = tU8norm, .rate = inst }, // rgba
                }
            },
        },
        .index_buffer = ibuf,
        .index_type = MUGFX_INDEX_TYPE_U16,
        .vertex_count = 4,
        .index_count = 6,
    });
}

ung_material_id create_particle_material(
    const char* vert_path, const char* frag_path, ung_texture_id texture)
{
    const auto mat = ung_material_load(vert_path, frag_path, {
        .mugfx = {
            .depth_func = MUGFX_DEPTH_FUNC_LEQUAL,
            .write_mask = MUGFX_WRITE_MASK_RGBA, // no depth writes
            .cull_face = MUGFX_CULL_FACE_MODE_NONE, // double sided
            .src_blend = MUGFX_BLEND_FUNC_SRC_ALPHA,
            .dst_blend = MUGFX_BLEND_FUNC_ONE_MINUS_SRC_ALPHA,
        },
    });
    ung_material_set_texture(mat, 0, texture);
    return mat;
}

void Renderer::init(size_t max_num_particles)
{
    gpu_particles = allocate<GpuParticleInstance>(max_num_particles);
    num_gpu_particles = max_num_particles;
}

void Renderer::free()
{
    deallocate(gpu_particles, num_gpu_particles);
    gpu_particles = nullptr;
}

void Renderer::draw(std::span<Particle> particles, DrawData& draw_data, ung_camera_id cam)
{
    const auto count = update_instance_buffer(draw_data.instance_buffer, particles,
        { gpu_particles, num_gpu_particles }, cam, draw_data.sort);
    if (count == 0) {
        return;
    }
    ung_draw_instanced(draw_data.material, draw_data.geometry, { 0 }, count);
}

static std::string_view sv(const ung_string& s)
{
    return std::string_view(s.data, s.length);
}

static void check_parse(const char* field, const ung_string& value, float* dst, size_t num)
{
    const auto res = ung_parse_float(value, dst, num);
    if (!res) {
        ung_panicf("Could not parse field '%s' (%lu values)", field, num);
    }
}

void parse_kv(SpawnParams& spawn, const ung_kv_pair& kv)
{
    auto key = sv(kv.key);
    if (key == "pos.min") {
        check_parse("pos.min", kv.value, &spawn.pos_min.x, 3);
    } else if (key == "pos.max") {
        check_parse("pos.max", kv.value, &spawn.pos_max.x, 3);
    } else if (key == "vel_spread_deg") {
        check_parse("vel_spread_deg", kv.value, &spawn.vel_spread_deg, 1);
    } else if (key == "rot") {
        check_parse("rot", kv.value, &spawn.rot_min, 2);
    } else if (key == "rvel") {
        check_parse("rvel", kv.value, &spawn.rvel_min, 2);
    } else if (key == "speed") {
        check_parse("speed", kv.value, &spawn.speed_min, 2);
    } else if (key == "lifetime") {
        check_parse("lifetime", kv.value, &spawn.lifetime_min, 2);
    } else if (key == "scale") {
        check_parse("scale", kv.value, &spawn.scale_min, 2);
    } else if (key == "color_base") {
        check_parse("color_base", kv.value, &spawn.color_base.x, 4);
    } else if (key == "color_var") {
        check_parse("color_var", kv.value, &spawn.color_var.x, 4);
    } else {
        ung_panicf("Invalid field '%.*s' for particle spawn params", (int)key.size(), key.data());
    }
}

void parse_kv(Gravity& gravity, const ung_kv_pair& kv)
{
    auto key = sv(kv.key);
    if (key == "y") {
        check_parse("y", kv.value, &gravity.y, 1);
    } else {
        ung_panicf("Invalid field '%.*s' for gravity behavior", (int)key.size(), key.data());
    }
}

void parse_kv(Drag& drag, const ung_kv_pair& kv)
{
    auto key = sv(kv.key);
    if (key == "k") {
        check_parse("k", kv.value, &drag.k, 1);
    } else {
        ung_panicf("Invalid field '%.*s' for drag behavior", (int)key.size(), key.data());
    }
}

void parse_kv(Fade& fade, const ung_kv_pair& kv)
{
    auto key = sv(kv.key);
    if (key == "in") {
        check_parse("in", kv.value, &fade.in, 1);
    } else if (key == "out") {
        check_parse("out", kv.value, &fade.out, 1);
    } else {
        ung_panicf("Invalid field '%.*s' for drag behavior", (int)key.size(), key.data());
    }
}

static bool reload_cb(void* ctx)
{
    auto effect = (Effect*)ctx;
    std::printf("Reloading particle effect: %s\n", effect->path);
    effect->load(effect->path);
    return true;
}

void Effect::load(const char* path_)
{
    // Reset
    if (gravity) {
        deallocate(gravity);
        gravity = nullptr;
    }
    if (drag) {
        deallocate(drag);
        drag = nullptr;
    }
    if (fade) {
        deallocate(fade);
        fade = nullptr;
    }
    spawn_params = {};
    behaviors = {};

    // On first load
    if (res.id == 0) {
        res = ung_resource_create(reload_cb, this);
    }

    if (!path || std::string_view(path) != path_) {
        path = path_;
        ung_resource_set_deps(res, &path, 1, nullptr, 0);
    }

    // Load file
    size_t size = 0;
    auto data = ung_read_whole_file(path, &size, true);

    std::array<ung_kv_pair, 32> kvs;
    const auto num_kvs = ung_parse_kv_file(data, size, kvs.data(), kvs.size());
    for (size_t i = 0; i < num_kvs; ++i) {
        const auto& kv = kvs[i];
        auto section = sv(kv.section);
        if (section == "spawn") {
            parse_kv(spawn_params, kv);
        } else if (section == "gravity") {
            if (!gravity) {
                gravity = allocate<pfx::Gravity>();
                add_behavior(behaviors, gravity, gravity_behavior);
            }
            parse_kv(*gravity, kv);
        } else if (section == "drag") {
            if (!drag) {
                drag = allocate<pfx::Drag>();
                add_behavior(behaviors, drag, drag_behavior);
            }
            parse_kv(*drag, kv);
        } else if (section == "fade") {
            if (!fade) {
                fade = allocate<pfx::Fade>();
                add_behavior(behaviors, fade, fade_behavior);
            }
            parse_kv(*fade, kv);
        }
    }

    ung_free_file_data(data, size);
}

Particle& Effect::spawn(um_vec3 pos, um_vec3 dir)
{
    return spawn_params.spawn(buffer, pos, dir);
}

void Effect::spawn(size_t n, um_vec3 pos, um_vec3 dir)
{
    spawn_params.spawn(buffer, n, pos, dir);
}

void Effect::update(float dt)
{
    pfx::update(behaviors, buffer.particles, dt);
}

void Effect::draw(Renderer& renderer, ung_camera_id cam)
{
    renderer.draw(buffer.particles, draw_data, cam);
}
}