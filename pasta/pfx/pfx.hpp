#pragma once

#include <array>
#include <cmath>
#include <span>

#include <um.h>
#include <ung.h>

/* The general idea is:
 * - Keep a ring buffer of particles,
 * - update them with a couple different behaviors,
 * - build a small instance buffer of particles,
 * - use that for instanced drawing.
 *
 * You may extend this to use behaviors that sample from user-defined curves or
 * extend instance data to use mesh particles.
 * You may add velocity to the instance data to stretch the particles (e.g. for sparks).
 * Or add a texture index to flip through particle animations.
 */

namespace pfx {

struct Particle {
    um_vec3 pos;
    um_vec3 vel;
    float rot; // about view axis
    float rvel;
    float r, g, b, a;
    float size;
    float age;
    float lifetime;
    uint32_t flags;

    bool alive() const { return age < lifetime; }
    float progress() const { return age / lifetime; }
};

struct Buffer {
    std::span<Particle> particles;
    size_t next_idx = 0;

    Particle& next();
};

// Spawning

struct SpawnParams {
    um_vec3 pos_min = { 0.f, 0.f, 0.f };
    um_vec3 pos_max = { 0.f, 0.f, 0.f }; // box
    float vel_spread_deg = 180.0f; // cone half angle
    float rot_min = 0.f;
    float rot_max = 2.f * M_PIf;
    float rvel_min = 0.f;
    float rvel_max = 0.f;
    float speed_min = 0.f;
    float speed_max = 0.f;
    float lifetime_min = 1.f;
    float lifetime_max = 1.f;
    float scale_min = 1.f;
    float scale_max = 1.f;
    um_vec4 color_base = { 1.f, 1.f, 1.f, 1.f };
    um_vec4 color_var = { 0.f, 0.f, 0.f, 0.f };

    void spawn(Particle& p, const um_vec3& pos, const um_vec3& dir = { 0.f, 1.f, 0.f }) const;
    Particle& spawn(Buffer& buf, const um_vec3& pos, const um_vec3& dir = { 0.f, 1.f, 0.f }) const;
    void spawn(Buffer& buf, size_t n, um_vec3 pos, um_vec3 dir = { 0.f, 1.f, 0.f }) const;
};

struct Behavior {
    using UpdateFunc = void(const void* params, std::span<Particle> particles, float dt);
    const void* params;
    UpdateFunc* func;
};

bool add_behavior(std::span<Behavior> behaviors, const void* params, Behavior::UpdateFunc* func);

void update(std::span<const Behavior> behaviors, std::span<Particle> particles, float dt);

struct Gravity {
    float y = -9.81f;
};

void gravity_behavior(const void* params, std::span<Particle> particles, float dt);

struct Drag {
    float k = 1.5f; // per-second damping
};

void drag_behavior(const void* params, std::span<Particle> particles, float dt);

struct Fade {
    float in = 0.05f, out = 0.3f; // fractions of lifetime
};

void fade_behavior(const void* params, std::span<Particle> particles, float dt);

// Rendering

struct GpuParticleInstance {
    float pos[3];
    float size;
    float rot;
    uint8_t r, g, b, a;
};

enum class Sort {
    None = 0,
    BackToFront,
    FrontToBack,
};

void sort_particles(std::span<GpuParticleInstance> gpu_particles, ung_camera_id cam, Sort sort);

ung_material_id create_particle_material(
    const char* vert_path, const char* frag_path, ung_texture_id texture);
mugfx_buffer_id create_instance_buffer(size_t max_instances);
ung_geometry_id create_particle_geometry(mugfx_buffer_id instance_buffer);

struct DrawData {
    ung_material_id material;
    mugfx_buffer_id instance_buffer;
    ung_geometry_id geometry;
    size_t max_num_particles;
    Sort sort = Sort::None;

    void init(size_t max_num_particles, const char* vert_path, const char* frag_path,
        ung_texture_id texture, Sort sort_ = Sort::None)
    {
        material = create_particle_material(vert_path, frag_path, texture);
        instance_buffer = create_instance_buffer(max_num_particles);
        geometry = create_particle_geometry(instance_buffer);
        sort = sort_;
    }
};

size_t pack_gpu_particle_instances(
    std::span<const Particle> particles, std::span<GpuParticleInstance> gpu_particles);

size_t update_instance_buffer(mugfx_buffer_id instance_buffer, std::span<const Particle> particles,
    std::span<GpuParticleInstance> gpu_particles, ung_camera_id cam = { 0 },
    Sort sort = Sort::None);

struct Renderer {
    // This is really just a container for gpu_particles, because it can easily be shared
    // across different effects.
    GpuParticleInstance* gpu_particles;
    size_t num_gpu_particles;

    // TODO: Add batching so draw will call multiple times if max_num_particles too small
    void init(size_t max_num_particles);
    void free();

    void draw(std::span<Particle> particles, DrawData& draw, ung_camera_id cam = { 0 });
};

// High-Level

void parse_kv(SpawnParams& spawn, const ung_kv_pair& kv);
void parse_kv(Gravity& gravity, const ung_kv_pair& kv);
void parse_kv(Drag& drag, const ung_kv_pair& kv);
void parse_kv(Fade& fade, const ung_kv_pair& kv);

struct Effect {
    const char* path = nullptr;
    ung_resource_id res = { 0 };

    Buffer buffer;
    DrawData draw_data;
    SpawnParams spawn_params = {};
    std::array<Behavior, 32> behaviors = {};
    Gravity* gravity = nullptr;
    Drag* drag = nullptr;
    Fade* fade = nullptr;

    void load(const char* path);

    Particle& spawn(um_vec3 pos, um_vec3 dir = {});
    void spawn(size_t n, um_vec3 pos, um_vec3 dir = {});
    void update(float dt);
    void draw(Renderer& renderer, ung_camera_id cam = { 0 });
};

}