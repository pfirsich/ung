#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mugfx.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Basic Types
 */
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
    uint32_t max_num_transforms; // default: 1024
    uint32_t max_num_materials; // default: 1024
    uint32_t max_num_cameras; // default: 8
    uint32_t max_num_geometry_data; // default: 256
    mugfx_init_params mugfx_params;
    bool debug; // do error checking and panic if something is wrong
} ung_init_params;

void ung_init(ung_init_params params);
void ung_shutdown();

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
 * Materials
 * These wrap a mugfx material (i.e. pipeline), all uniform buffers and bindings.
 * You should create a separate material for each different set of material parameters you want to
 * use at the same time. Using the material for multiple objects and changing parameters between
 * draws will not behave as you might expect as the actual draw call might happen another time. The
 * uniform buffer with the constant material data will be at binding 8, the dynamic data at 9.
 */
typedef struct {
    mugfx_material_create_params mugfx_params;
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
 * Resources
 */
ung_texture_id ung_texture_load(const char* path, bool flip_y,
    mugfx_texture_create_params params); // many params fields are ignored

// This will load the source from the given path and try to determine the bindings from either
// a .meta file (same as path + ".meta") (TODO) or from parsing the GLSL source. If the bindings are
// already in params, no attempt to determine the bindings in another way is made.
ung_shader_id ung_shader_load(
    mugfx_shader_stage stage, const char* path, mugfx_shader_create_params params);

char* ung_read_whole_file(const char* path, size_t* size);
void ung_free_file_data(char* data, size_t size);

/*
 * Geometry
 */
ung_geometry_data_id ung_geometry_data_load(const char* path); // Wavefront OBJ
void ung_geometry_data_destroy(ung_geometry_data_id gdata);

ung_draw_geometry_id ung_draw_geometry_from_data(ung_geometry_data_id gdata);
// loads data, creates draw geometry, frees data
ung_draw_geometry_id ung_draw_geometry_load(const char* path);
ung_draw_geometry_id ung_draw_geometry_box(float w, float h, float d);
ung_draw_geometry_id ung_draw_geometry_sphere(float radius);

/*
 * Camera Management
 */
ung_camera_id ung_camera_create();
void ung_camera_destroy(ung_camera_id camera);
void ung_camera_set_projection(ung_camera_id camera, const float matrix[16]);
void ung_camera_set_perspective(
    ung_camera_id camera, float fov, float aspect, float near, float far);
void ung_camera_set_orthographic(
    ung_camera_id camera, float left, float right, float bottom, float top, float near, float far);
ung_transform_id ung_camera_get_transform(ung_camera_id camera);
void ung_camera_get_view_matrix(ung_camera_id camera, float matrix[16]);
void ung_camera_get_projection_matrix(ung_camera_id camera, float matrix[16]);

/*
 * Render Context
 */
// use mugfx_clear, mugfx_set_viewport, mugfx_set_scissor
void ung_begin_frame();
void ung_begin_pass(mugfx_render_target_id target, ung_camera_id camera);
void ung_draw(ung_material_id material, ung_draw_geometry_id geometry, ung_transform_id transform);
void ung_end_pass();
void ung_end_frame();

// You don't have to use ung_run, but it will handle the mainloop in emscripten for you.
// Return true if you want the program to continue and false if you want to terminate.
typedef bool (*ung_mainloop_func)(void* ctx, float dt);
void ung_run_mainloop(void* ctx, ung_mainloop_func mainloop);

#ifdef __cplusplus
}
#endif
