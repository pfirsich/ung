#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mugfx.h>
#include <utxt.h>

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
} ung_gamepad_id;

typedef struct {
    uint64_t id;
} ung_sound_source_id;

typedef struct {
    uint64_t id;
} ung_sound_id;

typedef struct {
    uint64_t id;
} ung_skeleton_id;

typedef struct {
    uint64_t id;
} ung_animation_id;

typedef struct {
    uint64_t id;
} ung_texture_id;

typedef struct {
    uint64_t id;
} ung_shader_id;

typedef struct {
    uint64_t id;
} ung_geometry_id;

typedef struct {
    const char* data;
    size_t length;
} ung_string;

#define UNG_LITERAL(s) ((ung_string) { s, sizeof(s) - 1 })

ung_string ung_zstr(const char* str); // calls strlen for length

typedef struct {
    // these are all texture coordinates
    float x, y; // top-left
    float w, h; // bottom-right
} ung_texture_region;

#define UNG_REGION_FULL ((ung_texture_region) { 0.0f, 0.0f, 1.0f, 1.0f })

typedef struct {
    float r, g, b, a;
} ung_color;

#define UNG_COLOR_WHITE ((ung_color) { 1.0f, 1.0f, 1.0f, 1.0f })

typedef void* (*ung_allocator_allocate)(size_t size, void* ctx);
typedef void* (*ung_allocator_reallocate)(void* ptr, size_t old_size, size_t new_size, void* ctx);
typedef void (*ung_allocator_deallocate)(void* ptr, size_t size, void* ctx);

typedef struct {
    ung_allocator_allocate allocate;
    ung_allocator_reallocate reallocate;
    ung_allocator_deallocate deallocate;
    void* ctx;
} ung_allocator;

typedef enum {
    UNG_FULLSCREEN_MODE_DEFAULT = 0,
    UNG_FULLSCREEN_MODE_WINDOWED,
    UNG_FULLSCREEN_MODE_DESKTOP_FULLSCREEN,
    UNG_FULLSCREEN_MODE_FULLSCREEN,
} ung_fullscreen_mode;

typedef struct {
    uint32_t width; // if 0, will default to monitor resolution
    uint32_t height;
    ung_fullscreen_mode fullscreen_mode;
    uint8_t msaa_samples;
    bool vsync;
    bool srgb;
} ung_window_mode;

/*
 * Initialization
 */
typedef struct {
    const char* title;
    ung_window_mode window_mode;
    ung_allocator* allocator;
    uint32_t max_num_textures; // default: 128
    uint32_t max_num_shaders; // default: 64
    uint32_t max_num_geometries; // default: 1024
    uint32_t max_num_transforms; // default: 1024
    uint32_t max_num_materials; // default: 1024
    uint32_t max_num_cameras; // default: 8
    uint32_t max_num_sprite_vertices; // default: 1024*16
    uint32_t max_num_sprite_indices; // default: 1024*16
    uint32_t max_num_gamepads; // default: 8
    uint32_t max_num_sound_sources; // default: 64
    uint32_t max_num_sounds; // default: 64
    uint32_t num_sound_groups; // default: 4
    uint32_t max_num_skeletons; // default: 64
    uint32_t max_num_animations; // default: 256
    uint32_t max_num_file_watches; // default: 128
    mugfx_init_params mugfx;
    bool debug; // do error checking and panic if something is wrong
} ung_init_params;

void ung_init(ung_init_params params);
void ung_shutdown();

ung_allocator* ung_get_allocator();
void* ung_malloc(size_t size);
void* ung_realloc(void* ptr, size_t new_size);
void ung_free(void* ptr);
void* ung_utxt_realloc(void* ptr, size_t old_size, size_t new_size, void*);
utxt_alloc ung_get_utxt_alloc();

/*
 * Window
 */
struct SDL_Window;
SDL_Window* ung_get_window();
void* ung_get_gl_context();
void ung_get_window_size(uint32_t* width, uint32_t* height);

/*
 * Platform
 */
float ung_get_time();

/*
 * Events
 */
union SDL_Event;
typedef void (*ung_event_callback)(void*, SDL_Event*);
void ung_set_event_callback(void* ctx, ung_event_callback func);
bool ung_poll_events(); // MUST be called every frame, returns false if window is closed

/*
 * SlotMap
 */
typedef struct {
    uint64_t* keys;
    uint32_t capacity;
    uint32_t free_list_head;
} ung_slotmap;

// set keys and capacity before init
void ung_slotmap_init(ung_slotmap* s);
uint64_t ung_slotmap_insert(ung_slotmap* s, uint32_t* idx);
uint32_t ung_slotmap_get_index(uint64_t key);
uint32_t ung_slotmap_get_generation(uint64_t key);
// You can use this function to check if a slot is alive (it return 0 if not).
uint64_t ung_slotmap_get_key(const ung_slotmap* s, uint32_t idx);
uint32_t ung_slotmap_next_alive(const ung_slotmap* s, uint32_t min_index);
bool ung_slotmap_contains(const ung_slotmap* s, uint64_t key);
bool ung_slotmap_remove(ung_slotmap* s, uint64_t key);

/*
 * Input
 */
// The input state will be updated by ung_poll_events!
#define UNG_MOUSE_BUTTON_LEFT 1
#define UNG_MOUSE_BUTTON_MIDDLE 2
#define UNG_MOUSE_BUTTON_RIGHT 3
#define UNG_MOUSE_BUTTON_SIDE_1 4
#define UNG_MOUSE_BUTTON_SIDE_2 5

bool ung_key_down(const char* key);
uint8_t ung_key_pressed(const char* key);
uint8_t ung_key_released(const char* key);
bool ung_mouse_down(int button);
uint8_t ung_mouse_pressed(int button);
uint8_t ung_mouse_released(int button);
void ung_mouse_set_relative(bool relative);
void ung_mouse_get(int* x, int* y, int* dx, int* dy);
void ung_mouse_get_scroll_x(int* left, int* right);
void ung_mouse_get_scroll_y(int* pos, int* neg); // pos = away from the user

// Gamepads
typedef enum {
    UNG_GAMEPAD_ACTION_CONFIRM = 0x40, // PS: X, Nintendo: A, XBox: A
    UNG_GAMEPAD_ACTION_CANCEL, // PS: O, Nintendo: B, XBox: B

    UNG_GAMEPAD_ACTION_PRIMARY, // PS: X, Nintendo: A, XBox: A
    UNG_GAMEPAD_ACTION_SECONDARY, // PS: O, Nintendo: X, XBox: X
    UNG_GAMEPAD_ACTION_TERTIARY, // PS: Square, Nintendo: Y, XBox: Y
    UNG_GAMEPAD_ACTION_QUATERNARY, // PS: Triangle, Nintendo: B, XBox: B
} ung_gamepad_action;

typedef struct {
    char name[128]; // display name for menus/prompts
    uint16_t vendor_id; // USB VID for model/quirk detection
    uint16_t product_id; // USB PID for model/quirk detection
    uint8_t guid[16]; // SDL mapping lookup key
    char serial[64]; // unique per physical pad
} ung_gamepad_info;

// The returned gamepads are reused and matched by serial. I.e. it tries to represent
// an individual physical gamepad.
size_t ung_get_gamepads(ung_gamepad_id* gamepads, size_t max_num_gamepads);
// This will return the most recently active, connected gamepad or 0 if no gamepad is connected
// (anymore). Use this for single-player games and fall back to keyboard if it returns 0.
ung_gamepad_id ung_gamepad_get_any();

struct _SDL_GameController;
typedef struct _SDL_GameController SDL_GameController;

SDL_GameController* ung_gamepad_get_sdl(ung_gamepad_id gamepad);
ung_gamepad_id ung_get_gamepad_from_event(uint32_t type, int32_t which);

// instance id is unique for every gamepad connection, i.e. it changes whenever the
// gamepad reconnect. -1 if not connected.
int32_t ung_gamepad_instance_id(ung_gamepad_id gamepad);
bool ung_gamepad_is_connected(ung_gamepad_id gamepad);
const ung_gamepad_info* ung_gamepad_get_info(ung_gamepad_id gamepad);

float ung_gamepad_axis(ung_gamepad_id gamepad, uint8_t axis); // SDL_GameControllerAxis
// button may either be SDL_GameControllerButton or one of ung_gamepad_action.
bool ung_gamepad_button_down(ung_gamepad_id gamepad, uint8_t button); // SDL_GameControllerButton
uint32_t ung_gamepad_button_pressed(ung_gamepad_id gamepad, uint8_t button);
uint32_t ung_gamepad_button_released(ung_gamepad_id gamepad, uint8_t button);

int ung_gamepad_get_player_index(ung_gamepad_id gamepad); // negative = unset
void ung_gamepad_set_player_index(ung_gamepad_id gamepad, int player_index);

void ung_gamepad_rumble(
    ung_gamepad_id gamepad, uint16_t low_freq, uint16_t high_freq, uint32_t duration_ms);
void ung_gamepad_rumble_triggers(
    ung_gamepad_id gamepad, uint16_t left, uint16_t right, uint32_t duration_ms);
void ung_gamepad_set_led(ung_gamepad_id gamepad, uint8_t red, uint8_t green, uint8_t blue);

// default: 0.1f, 0.9f
void ung_gamepad_axis_deadzone(ung_gamepad_id gamepad, uint8_t axis, float inner, float outer);

/*
 * Transforms
 */
ung_transform_id ung_transform_create();
// This will reparent all children to the parent of the deleted transform
void ung_transform_destroy(ung_transform_id transform);

void ung_transform_set_position(ung_transform_id transform, float x, float y, float z);
void ung_transform_get_position(ung_transform_id transform, float position[3]);
void ung_transform_set_orientation(
    ung_transform_id transform, float w, float x, float y, float z); // quaternion
void ung_transform_get_orientation(ung_transform_id transform, float quat[4]);
void ung_transform_set_scale(ung_transform_id transform, float x, float y, float z);
void ung_transform_get_scale(ung_transform_id transform, float scale[3]);

void ung_transform_look_at(ung_transform_id transform, float x, float y, float z);
void ung_transform_look_at_up(
    ung_transform_id transform, float x, float y, float z, float up_x, float up_y, float up_z);

// Returns matrix in parent coordinate system
void ung_transform_get_local_matrix(ung_transform_id transform, float matrix[16]);
// Takes into account parent transform, if parent is set
void ung_transform_get_world_matrix(ung_transform_id transform, float matrix[16]);

void ung_transform_local_to_world(ung_transform_id transform, float dir[3]);

// Object hierarchy
void ung_transform_set_parent(ung_transform_id transform, ung_transform_id parent);
ung_transform_id ung_transform_get_parent(ung_transform_id transform);
ung_transform_id ung_transform_get_first_child(ung_transform_id transform);
ung_transform_id ung_transform_get_next_sibling(ung_transform_id transform);

/*
 * Graphics
 */
ung_shader_id ung_shader_create(mugfx_shader_create_params params);
void ung_shader_recreate(ung_shader_id shader, mugfx_shader_create_params params);

// This will load the source from the given path and try to determine the bindings from either
// a .meta file (same as path + ".meta") (TODO) or from parsing the GLSL source. If the bindings are
// already in params, no attempt to determine the bindings in another way is made.
ung_shader_id ung_shader_load(mugfx_shader_stage stage, const char* path);
bool ung_shader_reload(ung_shader_id shader, const char* path);

ung_texture_id ung_texture_create(mugfx_texture_create_params params);
void ung_texture_recreate(ung_texture_id texture, mugfx_texture_create_params params);

ung_texture_id ung_texture_load(const char* path, bool flip_y, mugfx_texture_create_params params);
bool ung_texture_reload(
    ung_texture_id texture, const char* path, bool flip_y, mugfx_texture_create_params params);

/*
 * Materials
 * These wrap a mugfx material (i.e. pipeline), all uniform buffers and bindings.
 * You should create a separate material for each different set of material parameters you want to
 * use at the same time. Using the material for multiple objects and changing parameters between
 * draws will not behave as you might expect as the actual draw call might happen another time. The
 * uniform buffer with the constant material data will be at binding 8, the dynamic data at 9.
 */
typedef struct {
    mugfx_material_create_params mugfx;
    ung_shader_id vert;
    ung_shader_id frag;
    const void* constant_data;
    size_t constant_data_size;
    size_t dynamic_data_size;
} ung_material_create_params;

ung_material_id ung_material_create(ung_material_create_params params);
// This will use ung_shader_load for both shaders (see notes below)
ung_material_id ung_material_load(
    const char* vert_path, const char* frag_path, ung_material_create_params params);
void ung_material_destroy(ung_material_id material);
void ung_material_set_binding(ung_material_id material, mugfx_draw_binding binding);
void ung_material_set_uniform_data(
    ung_material_id material, uint32_t binding, mugfx_uniform_data_id uniform_data);
void ung_material_set_texture(ung_material_id material, uint32_t binding, ung_texture_id texture);
// Getting this data pointer marks the associated uniform buffer dirty, because it is assumed
// you modified it
void* ung_material_get_dynamic_data(ung_material_id material);
// You can mark the associated uniform buffers dirty with this function
void ung_material_update(ung_material_id material);

/*
 * Files
 */
char* ung_read_whole_file(const char* path, size_t* size);
void ung_free_file_data(char* data, size_t size);

// Returns platform file modification time, units are platform-dependent (both rate and epoch).
// You should only compare values with values returned from the same function earlier (i.e. check if
// a file is newer/same/older, nothing else).
// Returns 0 on WASM (noop).
// In the future this function might insert the path into a file watcher on first use, but for now
// it will just stat(), which is probably good enough.
uint64_t ung_file_get_mtime(const char* path);

typedef void (*ung_file_watch_cb)(void* ctx, const char* changed_path);

typedef struct {
    uint64_t id;
} ung_file_watch_id;

// The callback will be called from ung_begin_frame.
// Inside this function you can queue _reloads, which will be applied transactionally when this
// function terminates. If you need to update a watch, just recreate it.
ung_file_watch_id ung_file_watch_create(
    const char* const* paths, size_t num_paths, ung_file_watch_cb cb, void* ctx);

void ung_file_watch_destroy(ung_file_watch_id watch);

/*
 * Geometry
 */
typedef struct {
    size_t num_vertices;
    float* positions; // non-null, 3 values per vertex (xyz)
    float* normals; // null or 3 values per vertex (xyz)
    float* texcoords; // null or 2 values per vertex (uv)
    float* colors; // null or 4 values per vertex (rgba)
    uint16_t* joints; // null or 4 values per vertex
    float* weights; // null or 4 values per vertex

    size_t num_indices; // always 3 vertices per face
    uint32_t* indices;
} ung_geometry_data;

ung_geometry_data ung_geometry_data_load(const char* path); // Wavefront OBJ
ung_geometry_data ung_geometry_data_box(float w, float h, float d);
ung_geometry_data ung_geometry_data_sphere(float radius);
void ung_geometry_data_destroy(ung_geometry_data gdata);

ung_geometry_id ung_geometry_create(mugfx_geometry_create_params params);
void ung_geometry_recreate(ung_geometry_id geom, mugfx_geometry_create_params params);
ung_geometry_id ung_geometry_create_from_data(ung_geometry_data gdata);
// creates geometry data, creates draw geometry, destroys geometry data
ung_geometry_id ung_geometry_load(const char* path);
void ung_geometry_reload(ung_geometry_id geom, const char* path);
ung_geometry_id ung_geometry_box(float w, float h, float d);
ung_geometry_id ung_geometry_sphere(float radius);

/*
 * Sprites
 */
// I don't think multiple sprite renderers are very useful, so there is a singleton

void ung_sprite_set_material(ung_material_id mat);
uint16_t ung_sprite_add_vertex(float x, float y, float u, float v, ung_color color);
void ung_sprite_add_index(uint16_t idx);

void ung_sprite_add_quad(
    float x, float y, float w, float h, ung_texture_region texture, ung_color color);

typedef struct {
    float x, y;
    float rotation;
    float scale_x, scale_y; // default: 1, 1
    float offset_x, offset_y;
} ung_transform_2d;

void ung_sprite_add(
    ung_material_id mat, ung_transform_2d transform, ung_texture_region texture, ung_color color);

void ung_sprite_flush();

/*
 * Text
 */
typedef struct {
    utxt_font* font;
    ung_texture_id texture;
    ung_material_id material;
} ung_font;

typedef struct {
    const char* ttf_path;
    utxt_load_ttf_params load_params;
    const char* vert_path;
    const char* frag_path;
    ung_material_create_params material_params;
} ung_font_load_ttf_param;

void ung_font_load_ttf(ung_font* font, ung_font_load_ttf_param params);

void ung_font_draw_quad(const utxt_quad* quad, ung_color color);
void ung_font_draw_quads(
    const ung_font* font, const utxt_quad* quads, size_t num_quads, ung_color color);

/*
 * Camera Management
 */
ung_camera_id ung_camera_create();
void ung_camera_destroy(ung_camera_id camera);
void ung_camera_set_projection(ung_camera_id camera, const float matrix[16]);
void ung_camera_set_perspective(
    ung_camera_id camera, float fov, float aspect, float near, float far);
void ung_camera_set_orthographic_fullscreen(ung_camera_id camera);
void ung_camera_set_orthographic(
    ung_camera_id camera, float left, float right, float bottom, float top);
void ung_camera_set_orthographic_z(
    ung_camera_id camera, float left, float right, float bottom, float top, float near, float far);
ung_transform_id ung_camera_get_transform(ung_camera_id camera);
void ung_camera_get_view_matrix(ung_camera_id camera, float matrix[16]);
void ung_camera_get_projection_matrix(ung_camera_id camera, float matrix[16]);

/*
 * Animation
 */
typedef struct {
    float inverse_bind_matrix[16];
    int16_t parent_index; // negative is root
} ung_skeleton_joint;

typedef struct {
    float translation[3];
    float rotation[4]; // quat: wxyz
    float scale[3];
} ung_joint_transform;

typedef struct {
    uint16_t num_joints;
    const ung_skeleton_joint* joints; // these will be copied
    // optional local bind pose transforms, one per joint, will be copied.
    // if not provided, it will be determined from the inverse bind matrices
    const ung_joint_transform* local_bind;
} ung_skeleton_create_params;

// The joints must be topologically ordered (i.e. parent before child!)
ung_skeleton_id ung_skeleton_create(ung_skeleton_create_params params);
void ung_skeleton_destroy(ung_skeleton_id skel);

// This is already done on creation.
void ung_skeleton_reset_to_bind_pose(ung_skeleton_id skel);

// The returned pointer is valid for the lifetime of the skeleton.
// The returned joint transforms are local to the parent and should be written
// before obtaining joint matrices using the function below.
ung_joint_transform* ung_skeleton_get_joint_transforms(ung_skeleton_id skel, uint16_t* num_joints);

// Returns a pointer to num_joints joint matrices (mat4) to pass to a shader.
// The returned pointer is valid for the lifetime of the skeleton.
// The joint matrices are updated when this function is called (so it is not cheap).
const float* ung_skeleton_update_joint_matrices(ung_skeleton_id skel, uint16_t* num_joints);

// The weights are not normalized!
// for each joint i: `out[i] = a[i] * a_weight + b[i] * b_weight * joint_mask[i]`
// you can use this to mask single joints from b (as a float for flexibility).
// joint_mask may be NULL.
// translation and scale are interpolated linearly, rotation is NLERPed.
// results are undefined for: a_weight + b_weight * joint_mask[i] == 0
void ung_blend_poses(const ung_joint_transform* a, float a_weight, const ung_joint_transform* b,
    float b_weight, const float* joint_mask, ung_joint_transform* out, uint16_t num_joints);

typedef enum {
    UNG_JOINT_DOF_INVALID = 0,
    UNG_JOINT_DOF_TRANSLATION,
    UNG_JOINT_DOF_ROTATION,
    UNG_JOINT_DOF_SCALE,
} ung_joint_dof;

typedef struct {
    uint16_t joint_index;
    ung_joint_dof dof;
} ung_animation_key;

typedef enum {
    // add scalar, vec2, vec4 later, when needed
    UNG_ANIM_SAMPLER_TYPE_INVALID = 0,
    UNG_ANIM_SAMPLER_TYPE_VEC3,
    UNG_ANIM_SAMPLER_TYPE_QUAT,
} ung_animation_sampler_type;

typedef enum {
    UNG_ANIM_INTERP_INVALID = 0,
    UNG_ANIM_INTERP_STEP,
    UNG_ANIM_INTERP_LINEAR,
    // UNG_ANIM_INTERP_CUBIC,
} ung_animation_interp;

typedef struct {
    ung_animation_key key;
    ung_animation_sampler_type sampler_type;
    ung_animation_interp interp_type;
    size_t num_samples;
    // times and values are copied as well, times must be sorted (ascending)!
    const float* times;
    const float* values; // vec3 or quat per sample
} ung_animation_channel;

typedef struct {
    // channels will be copied in ung_animation_create
    const ung_animation_channel* channels;
    size_t num_channels;
    float duration_s;
} ung_animation_create_params;

ung_animation_id ung_animation_create(ung_animation_create_params params);
void ung_animation_destroy(ung_animation_id anim);

float ung_animation_get_duration(ung_animation_id anim);

// t will be clamped to [0, duration_s]
void ung_animation_sample(
    ung_animation_id anim, float t, ung_joint_transform* joints, uint16_t num_joints);

/*
 * Render Context
 */
// use mugfx_clear, mugfx_set_viewport, mugfx_set_scissor
void ung_begin_frame();
void ung_begin_pass(mugfx_render_target_id target, ung_camera_id camera);
void ung_draw(ung_material_id material, ung_geometry_id geometry, ung_transform_id transform);
void ung_end_pass();
void ung_end_frame();

/*
 * Sound
 */
typedef struct {
    uint8_t group;
    size_t num_prewarm_sounds;
    bool stream;
} ung_sound_source_load_params;

ung_sound_source_id ung_sound_source_load(const char* path, ung_sound_source_load_params params);
void ung_sound_source_destroy(ung_sound_source_id src);

typedef struct {
    float volume;
    float pitch;
    float position[3];
    bool spatial;
    bool loop;
    bool fail_if_no_idle;
} ung_sound_play_params;

ung_sound_id ung_sound_play(ung_sound_source_id src, ung_sound_play_params params);
void ung_sound_update(ung_sound_id snd, float position[3], float velocity[3]);
bool ung_sound_is_playing(ung_sound_id snd);
void ung_sound_set_paused(ung_sound_id snd, bool paused);
void ung_sound_stop(ung_sound_id snd); // this is the same as delete

void ung_update_listener(float position[3], float orientation_quat[4], float velocity[3]);

float ung_sound_get_volume();
void ung_sound_set_volume(float vol);

float ung_sound_group_get_volume(uint8_t group);
void ung_sound_group_set_volume(uint8_t group, float vol);
void ung_sound_group_set_paused(uint8_t group, bool paused);

/*
 * Random Number Generator
 */
uint64_t ung_random_get_state();
void ung_random_set_state(uint64_t state);

// This returns the "raw" random value from the RNG without any processing.
// Use this to build your own random primitives on top.
uint64_t ung_random_u64();
uint64_t ung_random_u64_s(uint64_t* state);

// min and max are always inclusive!

uint64_t ung_random_uint(uint64_t min, uint64_t max);
uint64_t ung_random_uint_s(uint64_t min, uint64_t max, uint64_t* state);

int64_t ung_random_int(int64_t min, int64_t max);
int64_t ung_random_int_s(int64_t min, int64_t max, uint64_t* state);

float ung_random_float(float min, float max);
float ung_random_float_s(float min, float max, uint64_t* state);

// You don't have to use ung_run, but it will handle the mainloop in emscripten for you.
// Return true if you want the program to continue and false if you want to terminate.
typedef bool (*ung_mainloop_func)(void* ctx, float dt);
void ung_run_mainloop(void* ctx, ung_mainloop_func mainloop);

#ifdef __cplusplus
}
#endif
