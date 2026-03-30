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

#define UNG_LITERAL(s) { (s), sizeof((s)) - 1 }

ung_string ung_zstr(const char* str); // calls strlen for length

typedef struct {
    // these are all texture coordinates
    float x, y; // top-left
    float w, h; // bottom-right
} ung_texture_region;

#define UNG_REGION_FULL { 0.0f, 0.0f, 1.0f, 1.0f }

typedef struct {
    float r, g, b, a;
} ung_color;

#define UNG_COLOR_WHITE { 1.0f, 1.0f, 1.0f, 1.0f }

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
    // width, height are in logical units (points)
    // if 0, will default to display resolution
    uint32_t width;
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
    uint32_t max_num_sprite_indices; // default: max_num_sprite_vertices / 4
    uint32_t max_num_gamepads; // default: 8
    uint32_t max_num_sound_sources; // default: 64
    uint32_t max_num_sounds; // default: 64
    uint32_t num_sound_groups; // default: 4
    uint32_t max_num_skeletons; // default: 64
    uint32_t max_num_animations; // default: 256
    uint32_t max_num_file_watches; // default: 128
    uint32_t max_num_fonts; // default: 16
    uint32_t max_num_text_layouts; // default: 16
    // default for max_num_resources: 0 if auto_reload is false, sum of textures, shaders,
    // geometries, materials otherwise.
    uint32_t max_num_resources;
    mugfx_init_params mugfx;
    bool debug; // do error checking and panic if something is wrong
    bool auto_reload;
    bool load_cache;
} ung_init_params;

void ung_init(ung_init_params params);
void ung_shutdown();

#if defined(__GNUC__) || defined(__clang__)
#define UNG_PRINTF(fmt_idx, first_arg_idx) __attribute__((format(printf, fmt_idx, first_arg_idx)))
#define UNG_NORETURN_PRE
#define UNG_NORETURN_POST __attribute__((noreturn))
#else
#define UNG_PRINTF(fmt_idx, first_arg_idx)
#define UNG_NORETURN_PRE __declspec(noreturn)
#define UNG_NORETURN_POST
#endif

// This will log the message to the console and break into the debugger in debug builds.
// In non-debug builds this will show a message box.
UNG_NORETURN_PRE void ung_panic(const char* message) UNG_NORETURN_POST;
UNG_NORETURN_PRE void ung_panicf(const char* fmt, ...) UNG_PRINTF(1, 2) UNG_NORETURN_POST;

ung_allocator* ung_get_allocator();
void* ung_malloc(size_t size);
void* ung_realloc(void* ptr, size_t new_size);
void ung_free(void* ptr);

/*
 * Window
 */
struct SDL_Window;
SDL_Window* ung_get_window();
void* ung_get_gl_context();

// window size is in logical units (points)
// use this for ui/layouting, mouse coordinates
void ung_get_window_size(uint32_t* width, uint32_t* height);
// with scaling/highdpi framebuffer size might be larger than window size
// use this for scissor, creating render buffers, projection matrices, etc.
// framebuffer size is in pixels
void ung_get_framebuffer_size(uint32_t* width, uint32_t* height);

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
// returns 0 on exhaustion
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
typedef uint16_t ung_key; // SDL_Scancode
typedef uint8_t ung_mouse_button;
typedef uint8_t ung_gamepad_axis;
typedef uint8_t ung_gamepad_button;

// see SDL2 docs (SDL_GetScancodeFromName)
ung_key ung_key_from_name(const char* key);
// left, middle, right, side1, side2
ung_mouse_button ung_mouse_button_from_name(const char* button);
// leftx, lefty, rightx, righty, lefttrigger, righttrigger
ung_gamepad_axis ung_gamepad_axis_from_name(const char* axis);
// confirm, cancel, primary, secondary, tertiary, quaternary
// a, b, x, y, back, guide, start, leftstick, rightstick, leftshoulder, rightshoulder, dpadup,
// dpaddown, dpadleft, dpadright
ung_gamepad_button ung_gamepad_button_from_name(const char* button);

bool ung_key_down(ung_key key);
uint8_t ung_key_pressed(ung_key key);
uint8_t ung_key_released(ung_key key);
bool ung_key_down_s(const char* key);
uint8_t ung_key_pressed_s(const char* key);
uint8_t ung_key_released_s(const char* key);

bool ung_mouse_down(ung_mouse_button button);
uint8_t ung_mouse_pressed(ung_mouse_button button);
uint8_t ung_mouse_released(ung_mouse_button button);
bool ung_mouse_down_s(const char* button);
uint8_t ung_mouse_pressed_s(const char* button);
uint8_t ung_mouse_released_s(const char* button);
void ung_mouse_set_relative(bool relative);
// x, y, dx, dy are in logical screen coordinates (points)
void ung_mouse_get(int32_t* x, int32_t* y, int32_t* dx, int32_t* dy);
void ung_mouse_get_scroll_x(int32_t* left, int32_t* right);
void ung_mouse_get_scroll_y(int32_t* pos, int32_t* neg); // pos = away from the user

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
uint32_t ung_get_gamepads(ung_gamepad_id* gamepads, uint32_t max_num_gamepads);
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

float ung_gamepad_axis_get(ung_gamepad_id gamepad, ung_gamepad_axis axis);
float ung_gamepad_axis_get_s(ung_gamepad_id gamepad, const char* axis);
// button may either be SDL_GameControllerButton or one of ung_gamepad_action.
bool ung_gamepad_button_down(ung_gamepad_id gamepad, ung_gamepad_button button);
uint32_t ung_gamepad_button_pressed(ung_gamepad_id gamepad, ung_gamepad_button button);
uint32_t ung_gamepad_button_released(ung_gamepad_id gamepad, ung_gamepad_button button);
bool ung_gamepad_button_down_s(ung_gamepad_id gamepad, const char* button);
uint32_t ung_gamepad_button_pressed_s(ung_gamepad_id gamepad, const char* button);
uint32_t ung_gamepad_button_released_s(ung_gamepad_id gamepad, const char* button);

int32_t ung_gamepad_get_player_index(ung_gamepad_id gamepad); // negative = unset
void ung_gamepad_set_player_index(ung_gamepad_id gamepad, int32_t player_index);

void ung_gamepad_rumble(
    ung_gamepad_id gamepad, uint16_t low_freq, uint16_t high_freq, uint32_t duration_ms);
void ung_gamepad_rumble_triggers(
    ung_gamepad_id gamepad, uint16_t left, uint16_t right, uint32_t duration_ms);
void ung_gamepad_set_led(ung_gamepad_id gamepad, uint8_t red, uint8_t green, uint8_t blue);

// default: 0.1f, 0.9f
void ung_gamepad_axis_deadzone(
    ung_gamepad_id gamepad, ung_gamepad_axis axis, float inner, float outer);

/*
 * Transforms
 */
ung_transform_id ung_transform_create();
// This will reparent all children to the parent of the deleted transform
void ung_transform_destroy(ung_transform_id transform);

void ung_transform_set_position(ung_transform_id transform, const float xyz[3]);
void ung_transform_set_position_v(ung_transform_id transform, float x, float y, float z);
void ung_transform_get_position(ung_transform_id transform, float position[3]);
void ung_transform_set_orientation(ung_transform_id transform, const float xyzw[4]);
void ung_transform_set_orientation_v(
    ung_transform_id transform, float x, float y, float z, float w);
void ung_transform_get_orientation(ung_transform_id transform, float quat_xyzw[4]);
void ung_transform_set_scale(ung_transform_id transform, const float xyz[3]);
void ung_transform_set_scale_u(ung_transform_id transform, float scale); // uniform
void ung_transform_set_scale_v(ung_transform_id transform, float x, float y, float z);
void ung_transform_get_scale(ung_transform_id transform, float scale[3]);

void ung_transform_look_at(ung_transform_id transform, const float xyz[3]);
void ung_transform_look_at_v(ung_transform_id transform, float x, float y, float z);
void ung_transform_look_at_up(
    ung_transform_id transform, const float xyz[3], const float up_xyz[3]);
void ung_transform_look_at_up_v(
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

typedef struct {
    uint32_t width, height;
} ung_dimensions;

ung_texture_id ung_texture_create(mugfx_texture_create_params params);
void ung_texture_recreate(ung_texture_id texture, mugfx_texture_create_params params);
void ung_texture_destroy(ung_texture_id texture);
ung_dimensions ung_texture_get_size(ung_texture_id texture);

ung_texture_id ung_texture_load(const char* path, bool flip_y, mugfx_texture_create_params params);
ung_texture_id ung_texture_load_buffer(
    const void* buffer, size_t size, bool flip_y, mugfx_texture_create_params params);
bool ung_texture_reload(
    ung_texture_id texture, const char* path, bool flip_y, mugfx_texture_create_params params);

/*
 * Materials
 * These wrap a mugfx material (i.e. pipeline), all uniform buffers and bindings.
 * You should create a separate material for each different set of material parameters you want to
 * use at the same time. Using the material for multiple objects and changing parameters between
 * draws will not behave as you might expect as the actual draw call might happen another time. The
 * uniform buffer with the constant material data will be at binding 8, the dynamic data at 9.
 *
 * The following bindings are provided by ung by default:

layout (binding = 0, std140) uniform UngConstant {
    vec4 screen_dimensions; // xy: size, zw: reciprocal size
};

layout (binding = 1, std140) uniform UngFrame {
    vec4 time; // x: seconds since game started, y: frame counter
};

layout (binding = 2, std140) uniform UngCamera {
    mat4 view;
    mat4 view_inv;
    mat4 projection;
    mat4 projection_inv;
    mat4 view_projection;
    mat4 view_projection_inv;
};

layout (binding = 3, std140) uniform UngTransform {
    mat4 model;
    mat4 model_inv;
    mat4 model_view;
    mat4 model_view_projection;
};

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
bool ung_material_recreate(ung_material_id material, ung_material_create_params params);
// This will use ung_shader_load for both shaders (see notes below)
ung_material_id ung_material_load(
    const char* vert_path, const char* frag_path, ung_material_create_params params);
bool ung_material_reload(ung_material_id material, const char* vert_path, const char* frag_path,
    ung_material_create_params params);
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
char* ung_read_whole_file(const char* path, size_t* size, bool panic_on_error);
void ung_free_file_data(char* data, size_t size);

typedef struct {
    ung_string section;
    ung_string key;
    ung_string value;
} ung_kv_pair;

uint32_t ung_parse_kv_file(const char* data, size_t size, ung_kv_pair* pairs, size_t max_num_pairs);

bool ung_parse_float(ung_string str, float* ptr, size_t num);
bool ung_parse_int(ung_string str, int64_t* ptr, size_t num);

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

// Resources
// This is just so you can add auto-reloading for custom resource files (e.g. glTF).
// If auto_reload is false, all these will no-op as much as possible.

typedef struct {
    uint64_t id;
} ung_resource_id;

// If false is returned, the reload is considered failed and dependents are not reloaded.
typedef bool (*ung_resource_reload_cb)(void* ctx);

// The reload callback will be called only after all dependencies were reloaded.
ung_resource_id ung_resource_create(ung_resource_reload_cb cb, void* ctx);

// This function may be called inside the reload callback (e.g. if dependencies change).
void ung_resource_set_deps(ung_resource_id resource, const char* const* files_deps,
    uint32_t num_files_deps, const ung_resource_id* res_deps, size_t num_res_deps);

// Whenever the resource is reloaded, this version incremented.
uint32_t ung_resource_get_version(ung_resource_id resource);

void ung_resource_destroy(ung_resource_id resource);

ung_resource_id ung_shader_get_resource(ung_shader_id shader);
ung_resource_id ung_material_get_resource(ung_material_id material);
ung_resource_id ung_texture_get_resource(ung_texture_id texture);
ung_resource_id ung_geometry_get_resource(ung_geometry_id geometry);
ung_resource_id ung_sound_get_resource(ung_sound_id sound);

void ung_load_profiler_push(const char* name);
void ung_load_profiler_pop(const char* name); // name optional, asserts equality
void ung_load_profiler_dump();

/*
 * Geometry
 */
typedef struct {
    uint32_t num_vertices;
    float* positions; // non-null, 3 values per vertex (xyz)
    float* normals; // null or 3 values per vertex (xyz)
    float* texcoords; // null or 2 values per vertex (uv)
    float* colors; // null or 4 values per vertex (rgba)
    uint16_t* joints; // null or 4 values per vertex
    float* weights; // null or 4 values per vertex

    uint32_t num_indices; // always 3 vertices per face
    uint32_t* indices;
} ung_geometry_data;

// Wavefront OBJ (requires UNG_FAST_OBJ).
ung_geometry_data ung_geometry_data_load(const char* path);
ung_geometry_data ung_geometry_data_box(float w, float h, float d);
ung_geometry_data ung_geometry_data_sphere(float radius);
void ung_geometry_data_destroy(ung_geometry_data gdata);

ung_geometry_id ung_geometry_create(mugfx_geometry_create_params params);
void ung_geometry_recreate(ung_geometry_id geom, mugfx_geometry_create_params params);
void ung_geometry_set_vertex_range(ung_geometry_id geom, uint32_t offset, uint32_t count);
void ung_geometry_set_index_range(ung_geometry_id geom, uint32_t offset, uint32_t count);
ung_geometry_id ung_geometry_create_from_data(ung_geometry_data gdata);
// creates geometry data, creates draw geometry, destroys geometry data
ung_geometry_id ung_geometry_load(const char* path);
bool ung_geometry_reload(ung_geometry_id geom, const char* path);
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
    uint64_t id;
} ung_font_id;

ung_font_id ung_font_load_ttf(const char* ttf_path, utxt_load_ttf_params params);
ung_font_id ung_font_load_ttf_buffer(const void* data, size_t size, utxt_load_ttf_params params);
void ung_font_destroy(ung_font_id font);

const utxt_font* ung_font_get_utxt(ung_font_id font);
ung_texture_id ung_font_get_texture(ung_font_id font);
const utxt_font_metrics* ung_font_get_metrics(ung_font_id font);

float ung_font_get_text_width(ung_font_id font, ung_string string);

// uses built-in text rendering material
void ung_font_draw(ung_font_id font, ung_string text, float x, float y, ung_color color);
// a text rendering material has:
// - a UngCamera uniform block at binding 2,
// - three vertes attributes (vec2 pos, vec2 texcoord, vec4 color) (location 0, 1, 2 respectively)
// - a font atlas texture at uniform binding = 0
// This will set the texture at binding=0 to the font atlas texture!
void ung_font_draw_mat(
    ung_font_id font, ung_material_id mat, ung_string text, float x, float y, ung_color color);

typedef struct {
    float x, y, w, h;
    float u0, v0, u1, v1;
    ung_font_id font;
    ung_color color;
    uintptr_t user_data;
} ung_text_draw_item;

// If mat is {0}, the default material is used
void ung_text_draw_items(
    const ung_text_draw_item* items, size_t num_items, ung_material_id mat, float x, float y);

typedef struct {
    uint64_t id;
} ung_text_layout_id;

ung_text_layout_id ung_text_layout_create(uint32_t num_glyphs, uint32_t num_runs);
void ung_text_layout_destroy(ung_text_layout_id layout);
void ung_text_layout_reset(ung_text_layout_id layout, float wrap_width, utxt_text_align align);
// Returns number of glyphs added.
uint32_t ung_text_layout_add_text(ung_text_layout_id layout, ung_font_id font, uintptr_t user_data,
    ung_string text, ung_color color);
// Different from utxt, compute will be called automatically when you draw the layout and it has
// been marked dirty (by resetting or adding).
uint32_t ung_text_layout_get_num_glyphs(ung_text_layout_id layout);
uint32_t ung_text_layout_get_num_lines(ung_text_layout_id layout);
// Returns number total of glyphs
uint32_t ung_text_layout_compute(ung_text_layout_id layout);

// glyph_count=0 => all glyphs
// returns number of draw items, one per glyph
size_t ung_text_layout_build_draw_items(ung_text_layout_id layout, uint32_t glyph_offset,
    uint32_t num_glyphs, ung_text_draw_item* items, size_t max_items);
// If mat is {0}, the default material is used
void ung_text_layout_draw(ung_text_layout_id layout, ung_material_id mat, float x, float y);

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
    float rotation[4]; // quat: xyzw
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

// This is a general purpose blend function, you most likely want to wrap this with something more
// high level first.
// `poses` should be an array of poses, with num_joints transforms each.
// `weights` should be an array of per-joint weights, with num_joints weights each.
// It does: for each joint i: `out[i][k] = sum_i poses[i][k] * weights[i][k]`
// At the end of your blend(s), you must normalize the rotation yourself!
// results are undefined for: sum_i weights[i][k] == 0.
// You may not pass an input pose as the output pose.
void ung_blend_poses(const ung_joint_transform** poses, const float** weights, size_t num_poses,
    ung_joint_transform* pose_out, uint16_t num_joints);

// You can composite layers with this (with an opacity).
// per_joint_t may be null, the resulting weight per joint j is per_joint_t[j] * t.
// pose_a or pose_b may be pose_out.
// The resulting rotation is not normalized.
// Or do order-independent mixing when you normalize your t with a running sum of the weights,
// i.e. lerp(accum, a, ta/ta), then lerp(accum, b, tb/(ta+tb)), ...
// This works because (induction):
// lerp(p_n, p_m, t_m/(t_m+sum_n)) = p_n + (p_m - p_n) * t_m/(t_m+sum_n)
// = p_n*(t_m+sum_n)/(t_m+sum_n) - p_n*t_m/(t_m+sum_n) + p_m*t_m/(t_m+sum_n)
// = p_n*sum_n/(t_m+sum_n) + p_m*t_m/(t_m+sum_n)
// which is exactly the weighted blend
// (the old normalization is multiplied away and the new one is divided in)
void ung_lerp_poses(const ung_joint_transform* pose_a, const ung_joint_transform* pose_b, float t,
    const float* per_joint_t, ung_joint_transform* pose_out, uint16_t num_joints);

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
 * Model Loading
 */
typedef enum {
    // Default is not ALL, so additional stuff that gets added in the future does not
    // get loaded without a change in flags.
    // This would silently add more resources, which you would have to free.
    // For the same reason I do not provide an ALL flag.
    UNG_MODEL_LOAD_DEFAULT = 0, // UNG_MODEL_LOAD_GEOMETRIES
    UNG_MODEL_LOAD_GEOMETRIES = 1u << 0,
    UNG_MODEL_LOAD_GEOMETRY_DATA = 1u << 1,
    UNG_MODEL_LOAD_MATERIALS = 1u << 2, // fills materials + material_indices
    UNG_MODEL_LOAD_SKELETON = 1u << 3, // first skin only
    UNG_MODEL_LOAD_ANIMATIONS = 1u << 4,
} ung_model_load_flags;

typedef enum {
    UNG_MODEL_MATERIAL_TEXTURE_NONE = 0,
    UNG_MODEL_MATERIAL_TEXTURE_BASE_COLOR = 1u << 0,
    UNG_MODEL_MATERIAL_TEXTURE_NORMAL = 1u << 1,
    UNG_MODEL_MATERIAL_TEXTURE_EMISSIVE = 1u << 2,
    UNG_MODEL_MATERIAL_TEXTURE_METAL_ROUGH = 1u << 3,
    UNG_MODEL_MATERIAL_TEXTURE_OCCLUSION = 1u << 4,
} ung_model_load_material_flags;

typedef enum {
    UNG_MODEL_ALPHA_INVALID = 0,
    UNG_MODEL_ALPHA_OPAQUE,
    UNG_MODEL_ALPHA_MASK, // discard fragment below alpha_cutoff
    UNG_MODEL_ALPHA_BLEND, // some sort of blending
} ung_model_alpha_mode;

// This represents some sort of subset across common file formats.
typedef struct {
    // 0 = missing / not loaded
    ung_texture_id base_color_texture;
    ung_texture_id normal_texture;
    ung_texture_id emissive_texture;

    float base_color_factor[4]; // default: (1.0, 1.0, 1.0, 1.0)
    float normal_scale; // default: 1.0
    float emissive_factor[3]; // default: 1.0

    float alpha_cutoff; // default: 0.5 for MASK, ignored otherwise
    ung_model_alpha_mode alpha_mode;
    bool double_sided;
} ung_model_material;

typedef struct {
    ung_texture_id metal_rough_texture; // glTF packed metallic-roughness texture
    ung_texture_id occlusion_texture;

    float metallic_factor; // default 1.0
    float roughness_factor; // default 1.0
    float occlusion_strength; // default 1.0
} ung_gltf_material;

typedef struct {
    const char* path;
    uint32_t flags; // ung_model_load_flags

    // applied if UNG_MODEL_LOAD_MATERIALS is given
    mugfx_texture_create_params texture_params;
    uint32_t material_flags; // ung_model_load_material_flags
    bool texture_flip_y;

    // If names are provided, result.animations has exactly this many entries in this order.
    // Missing names will have animation id {0}.
    const char* const* animation_names;
    uint32_t num_animation_names;
} ung_model_load_params;

typedef struct {
    ung_geometry_id* geometries; // non-null if UNG_MODEL_LOAD_GEOMETRIES was given
    uint32_t* material_indices; // parallel to geometries, UINT32_MAX means no material
    ung_geometry_data* geometry_data; // non-null if UNG_MODEL_LOAD_GEOMETRY_DATA was given
    uint32_t num_primitives;

    // only non-null if UNG_MODEL_LOAD_MATERIALS was given
    ung_model_material* materials;
    uint32_t num_materials;

    // only non-null if file is glTF and if UNG_MODEL_LOAD_MATERIALS was given
    ung_gltf_material* gltf_materials;

    // {0} if UNG_MODEL_LOAD_SKELETON was not given
    ung_skeleton_id skeleton;

    // If names were provided num_animations == num_animation_names.
    // If no filter was provided, animations are in file order, names may be filled.
    ung_animation_id* animations;
    uint32_t num_animations;
} ung_model_load_result;

// Loads model files into ung resources + temporary import arrays.
ung_model_load_result ung_model_load(ung_model_load_params params);

// Frees only memory owned by ung_model_load_result (arrays + copied names).
// DOES NOT destroy any ung handles referenced inside (geometries/textures/skeleton/animations).
// This does also not free ung_geometry_data!
// Safe to call on zero-initialized result.
void ung_model_load_result_free(const ung_model_load_result* result);

#ifdef UNG_CGLTF
typedef struct cgltf_primitive cgltf_primitive;
ung_geometry_id ung_geometry_from_cgltf(const cgltf_primitive* prim);
ung_geometry_data ung_geometry_data_from_cgltf(const cgltf_primitive* prim);

typedef struct cgltf_skin cgltf_skin;
ung_skeleton_id ung_skeleton_from_cgltf(const cgltf_skin* skin);

// Skin is used to map animation target nodes to joint indices. Animation channels that point at
// nodes outside the skin are simply ignored.
typedef struct cgltf_animation cgltf_animation;
ung_animation_id ung_animation_from_cgltf(const cgltf_animation* anim, const cgltf_skin* skin);

typedef struct cgltf_texture_view cgltf_texture_view;
ung_texture_id ung_texture_from_cgltf(const char* gltf_path, const cgltf_texture_view* tex_view,
    bool flip_y, mugfx_texture_create_params params);
#endif

/*
 * Rendering
 */
typedef struct {
    const mugfx_draw_binding* binding_overrides;
    size_t num_binding_overrides;
    uint32_t instance_count; // non-instanced
} ung_draw_ex_params;

// use mugfx_clear, mugfx_set_viewport, mugfx_set_scissor
void ung_begin_frame();
void ung_begin_pass(mugfx_render_target_id target, ung_camera_id camera);
// Transform may be 0 to use the identity transform
void ung_draw(ung_material_id material, ung_geometry_id geometry, ung_transform_id transform);
void ung_draw_ex(ung_material_id material, ung_geometry_id geometry, ung_transform_id transform,
    ung_draw_ex_params params);
// Kept for backwards compatibility, just calls ung_draw_ex
void ung_draw_instanced(ung_material_id material, ung_geometry_id geometry,
    ung_transform_id transform, uint32_t instance_count);
void ung_end_pass();
void ung_end_frame();

/*
 * Sound
 */
typedef enum {
    UNG_SOUND_ATTENUATION_NONE = 0,
    UNG_SOUND_ATTENUATION_LINEAR,
    UNG_SOUND_ATTENUATION_INVERSE,
    UNG_SOUND_ATTENUATION_EXPONENTIAL,
} ung_sound_attenuation_model;

// The defaults mentioned here are the builtin defaults, not per-field defaults.
typedef struct {
    ung_sound_attenuation_model attenuation_model; // default: INVERSE
    float min_distance; // maximum volume below this distance, default: 1
    float max_distance; // minimum volume over this distance, default: FLT_MAX
    float rolloff; // model dependent, strength of volume attenuation, default: 1
    float directional_attenuation_factor; // 0 = disable directional attenuation, default 1
    float doppler_factor; // 0 = disable doppler effect, default: 1
} ung_sound_spatial_params;

ung_sound_spatial_params ung_sound_get_default_spatial_params(void);
void ung_sound_set_default_spatial_params(ung_sound_spatial_params params);

typedef struct {
    uint8_t group;
    uint32_t num_prewarm_sounds;
    bool stream;
    const ung_sound_spatial_params* spatial_params; // optional
} ung_sound_source_load_params;

ung_sound_source_id ung_sound_source_load(const char* path, ung_sound_source_load_params params);
void ung_sound_source_destroy(ung_sound_source_id src);

typedef struct {
    float volume;
    float pitch;
    float position[3];
    bool spatial; // has to be set for spatialization, even if spatial_params is set on the source
    bool loop;
    bool fail_if_no_idle;
} ung_sound_play_params;

ung_sound_id ung_sound_play(ung_sound_source_id src, ung_sound_play_params params);
void ung_sound_update(ung_sound_id snd, const float position[3], const float velocity[3]);
bool ung_sound_is_playing(ung_sound_id snd);
void ung_sound_set_paused(ung_sound_id snd, bool paused);
void ung_sound_stop(ung_sound_id snd); // this is the same as delete

void ung_update_listener(
    const float position[3], const float orientation_quat[4], const float velocity[3]);

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

/*
 * Misc
 */
// FNV-1a is a non-cryptographic hash, that is very fast and can be used for
// hash tables or checksums. It's trivial to create collisions deliberately,
// but its diffusion is pretty good. So if forging data to make hashes collide
// with negatives effects is an option, don't use it.
uint64_t ung_fnv1a(const void* data, size_t size);

/*
 * Mainloop
 */

// You don't have to use ung_run, but it will handle the mainloop in emscripten for you.
// Return true if you want the program to continue and false if you want to terminate.
typedef bool (*ung_mainloop_func)(void* ctx, float dt);
void ung_run_mainloop(void* ctx, ung_mainloop_func mainloop);

#if __GNUC__
// https://nullprogram.com/blog/2022/06/26/
#define UNG_TRAP __builtin_trap();
#elif _MSC_VER
#define UNG_TRAP __debugbreak();
#else
#define UNG_TRAP *(volatile int*)0 = 0
#endif

#ifdef __cplusplus
}
#endif
