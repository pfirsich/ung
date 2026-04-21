#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include <SDL.h>

#include "state.hpp"

namespace ung {

namespace sound {
    void init(ung_init_params params);
    void begin_frame();
    void shutdown();
}

namespace random {
    void init();
}

namespace animation {
    void init(ung_init_params params);
    void shutdown();
}

namespace files {
    void init(ung_init_params params);
    void begin_frame();
    void shutdown();
}

namespace input {
    void init(ung_init_params params);
    void reset();
    void process_event(SDL_Event* event);
    void shutdown();
}

namespace transform {
    void init(ung_init_params params);
    void shutdown();
}

namespace sprite_renderer {
    void init(ung_init_params params);
    void shutdown();
}

namespace render {
    void init(ung_init_params params);
    void begin_frame();
    void end_frame();
    void shutdown();
}

namespace text {
    void init(ung_init_params params);
    void shutdown();
}

static const char* default_sprite_vert = R"(
layout (binding = 2, std140) uniform UngCamera {
    mat4 view;
    mat4 view_inv;
    mat4 projection;
    mat4 projection_inv;
    mat4 view_projection;
    mat4 view_projection_inv;
};

layout (location = 0) in vec2 a_position;
layout (location = 1) in vec2 a_texcoord;
layout (location = 2) in vec4 a_color;

out vec2 vs_out_texcoord;
out vec4 vs_out_color;

void main() {
    vs_out_texcoord = a_texcoord;
    vs_out_color = a_color;
    gl_Position = view_projection * vec4(a_position, 0.0, 1.0);
}
)";

State* state = nullptr;

static void update_window_metrics(bool update_constant_uniform_data)
{
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(state->window, &w, &h);
    state->win_width = (u32)w;
    state->win_height = (u32)h;

    SDL_GL_GetDrawableSize(state->window, &w, &h);
    state->fb_width = (u32)w;
    state->fb_height = (u32)h;

    state->u_constant.screen_dimensions = um_vec4 {
        (float)state->win_width,
        (float)state->win_height,
        state->win_width ? 1.0f / (float)state->win_width : 0.0f,
        state->win_height ? 1.0f / (float)state->win_height : 0.0f,
    };

    if (update_constant_uniform_data && state->constant_data.id) {
        mugfx_uniform_data_update(state->constant_data);
    }
}

EXPORT ung_string ung_zstr(const char* str)
{
    return { str, std::strlen(str) };
}

EXPORT void ung_init(ung_init_params params)
{
    assert(!state);
    if (params.allocator) {
        allocator = *params.allocator;
    }
    state = allocate<State>();
    std::memset(state, 0, sizeof(State));

    if (params.window_mode.fullscreen_mode == UNG_FULLSCREEN_MODE_DEFAULT) {
        params.window_mode.fullscreen_mode = UNG_FULLSCREEN_MODE_WINDOWED;
    }

#ifdef __linux__
    // TODO: Revisit when "headless" support is added
    bool has_video_driver = false;
    for (int i = 0; i < SDL_GetNumVideoDrivers(); ++i) {
        const std::string_view driver = SDL_GetVideoDriver(i);
        if (driver == "x11" || driver == "wayland") {
            has_video_driver = true;
            break;
        }
    }

    if (!has_video_driver) {
        ung_panicf(
            "No video driver found. Please install the appropriate dependencies and recompile.");
    }
#endif

#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif

    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) < 0) {
        ung_panicf("Could not initialize SDL2: %s", SDL_GetError());
    }

#ifdef MUGFX_WEBGL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE,
        params.window_mode.backbuffer_color_space == MUGFX_COLOR_SPACE_SRGB ? 1 : 0);
    if (params.window_mode.msaa_samples) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, params.window_mode.msaa_samples);
    }

#if !defined(NDEBUG) && !defined(MUGFX_WEBGL)
    int contextFlags = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &contextFlags);
    contextFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, contextFlags);
#endif

    // Window
    u32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (params.window_mode.fullscreen_mode == UNG_FULLSCREEN_MODE_DESKTOP_FULLSCREEN
        || params.window_mode.fullscreen_mode == UNG_FULLSCREEN_MODE_FULLSCREEN) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (params.window_mode.fullscreen_mode == UNG_FULLSCREEN_MODE_DESKTOP_FULLSCREEN) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    state->window = SDL_CreateWindow(params.title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        (int)params.window_mode.width, (int)params.window_mode.height, flags);
    if (!state->window) {
        ung_panicf("Error creating window: %s", SDL_GetError());
    }

    // Context
    state->context = SDL_GL_CreateContext(state->window);
    if (!state->context) {
        ung_panicf("Error creating GL context: %s", SDL_GetError());
    }

    printf("SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver());

    SDL_GL_SetSwapInterval(params.window_mode.vsync);

    update_window_metrics(false);

    // mugfx
    if (!params.mugfx.allocator) {
        params.mugfx.allocator = &mugfx_alloc;
    }

    params.mugfx.backbuffer_color_space = params.window_mode.backbuffer_color_space;

    params.max_num_textures = params.max_num_textures ? params.max_num_textures : 128;
    params.mugfx.max_num_textures
        = params.mugfx.max_num_textures ? params.mugfx.max_num_textures : params.max_num_textures;

    params.max_num_shaders = params.max_num_shaders ? params.max_num_shaders : 64;
    params.mugfx.max_num_shaders
        = params.mugfx.max_num_shaders ? params.mugfx.max_num_shaders : params.max_num_shaders;

    params.max_num_geometries = params.max_num_geometries ? params.max_num_geometries : 1024;
    params.mugfx.max_num_geometries = params.mugfx.max_num_geometries
        ? params.mugfx.max_num_geometries
        : params.max_num_geometries;

    mugfx_init(params.mugfx);

    // Objects
    state->textures.init(params.max_num_textures);
    state->shaders.init(params.max_num_shaders);
    state->geometries.init(params.max_num_geometries);
    state->materials.init(params.max_num_materials ? params.max_num_materials : 1024);

    state->auto_reload = params.auto_reload;
    state->load_cache = params.load_cache;

    state->default_sprite_vert = ung_shader_create({
        .stage = MUGFX_SHADER_STAGE_VERTEX,
        .source = default_sprite_vert,
        .bindings = {
            { .type = MUGFX_SHADER_BINDING_TYPE_UNIFORM, .binding = 2 },
        },
        .debug_label = "ung:default_sprite.vert",
    });

    files::init(params);
    transform::init(params);
    render::init(params);
    text::init(params);
    input::init(params);
    sound::init(params);
    random::init();
    animation::init(params);
    sprite_renderer::init(params);

    state->prof_zones.init(512);
    state->prof_stack.init(8);
    state->prof_strpool.init(1024);
}

EXPORT void ung_shutdown()
{
    if (!state) {
        return;
    }

    sprite_renderer::shutdown();
    animation::shutdown();
    sound::shutdown();
    input::shutdown();
    text::shutdown();
    render::shutdown();
    transform::shutdown();
    files::shutdown();

    state->materials.free();
    state->cameras.free();

    mugfx_shutdown();

    if (state->context) {
        SDL_GL_DeleteContext(state->context);
    }
    if (state->window) {
        SDL_DestroyWindow(state->window);
    }

    deallocate(state);

    state = nullptr;
}

EXPORT void ung_panic(const char* message)
{
    std::fprintf(stderr, "%s\n", message);
#ifndef NDEBUG
    UNG_TRAP
#endif
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR, "ung panic", message, state ? state->window : nullptr);
    std::exit(1);
}

EXPORT void ung_panicf(const char* fmt, ...)
{
    char buf[512];

    va_list ap;
    va_start(ap, fmt);
    const auto n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0) {
        ung_panic("ung_panicf: format error");
    }

    // Ensure null-terminator in case of truncation
    buf[sizeof(buf) - 1] = '\0';

    ung_panic(buf);
}

EXPORT SDL_Window* ung_get_window()
{
    return state->window;
}

EXPORT void* ung_get_gl_context()
{
    return state->context;
}

EXPORT ung_allocator* ung_get_allocator()
{
    return &allocator;
}

struct alignas(std::max_align_t) Malloced {
    size_t size;
};
static_assert(alignof(Malloced) == alignof(std::max_align_t));
static_assert(sizeof(Malloced) % alignof(std::max_align_t) == 0);

EXPORT void* ung_malloc(size_t size)
{
    auto malloced = (Malloced*)allocator.allocate(sizeof(Malloced) + size, allocator.ctx);
    if (!malloced) {
        return nullptr;
    }
    malloced->size = sizeof(Malloced) + size;
    return (uint8_t*)malloced + sizeof(Malloced);
}

EXPORT void* ung_realloc(void* ptr, size_t new_size)
{
    if (!ptr) {
        return ung_malloc(new_size);
    }
    if (!new_size) {
        ung_free(ptr);
        return nullptr;
    }
    auto malloced = (Malloced*)((uint8_t*)ptr - sizeof(Malloced));
    malloced = (Malloced*)allocator.reallocate(
        malloced, malloced->size, sizeof(Malloced) + new_size, allocator.ctx);
    if (!malloced) {
        return nullptr;
    }
    malloced->size = sizeof(Malloced) + new_size;
    return (uint8_t*)malloced + sizeof(Malloced);
}

EXPORT void ung_free(void* ptr)
{
    if (!ptr) {
        return;
    }
    auto malloced = (Malloced*)((uint8_t*)ptr - sizeof(Malloced));
    allocator.deallocate(malloced, malloced->size, allocator.ctx);
}

EXPORT void ung_get_window_size(u32* width, u32* height)
{
    *width = state->win_width;
    *height = state->win_height;
}

EXPORT void ung_get_framebuffer_size(u32* width, u32* height)
{
    *width = state->fb_width;
    *height = state->fb_height;
}

EXPORT float ung_get_time()
{
    static const auto start = SDL_GetPerformanceCounter();
    const auto now = SDL_GetPerformanceCounter();
    return (float)(now - start) / (float)SDL_GetPerformanceFrequency();
}

EXPORT void ung_set_event_callback(void* ctx, ung_event_callback func)
{
    state->event_callback = func;
    state->event_callback_ctx = ctx;
}

EXPORT bool ung_poll_events()
{
    input::reset();
    SDL_Event event;
    const auto window_id = SDL_GetWindowID(state->window);
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_WINDOWEVENT && event.window.windowID == window_id) {
            switch (event.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_DISPLAY_CHANGED:
                update_window_metrics(true);
                break;
            default:
                break;
            }
        }

        input::process_event(&event);
        if (state->event_callback) {
            state->event_callback(state->event_callback_ctx, &event);
        }
        if (event.type == SDL_QUIT) {
            return false;
        }
    }
    return true;
}

EXPORT void ung_begin_frame()
{
    state->frame_counter++;
    files::begin_frame();
    render::begin_frame();
    sound::begin_frame();
}

EXPORT void ung_end_frame()
{
    render::end_frame();
    SDL_GL_SwapWindow(state->window);
}

EXPORT uint64_t ung_fnv1a(const void* data, size_t size)
{
    static constexpr u64 fnv1a_offset = 0xcbf29ce484222325;
    static constexpr u64 fnv1a_prime = 0x100000001b3;
    u64 hash = fnv1a_offset;
    auto bytes = (const u8*)data;
    for (size_t i = 0; i < size; ++i) {
        hash = hash ^ bytes[i];
        hash = hash * fnv1a_prime;
    }
    return hash;
}

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

struct EmscriptenMainloopCtx {
    void* ctx;
    ung_mainloop_func mainloop;
};

static void mainloop_wrap(void* arg)
{
    static auto time = ung_get_time();
    const auto now = ung_get_time();
    const auto dt = now - time;
    time = now;

    auto ectx = static_cast<EmscriptenMainloopCtx*>(arg);
    if (!ectx->mainloop(ectx->ctx, dt)) {
        emscripten_cancel_main_loop();
    }
}

EXPORT void ung_run_mainloop(void* ctx, ung_mainloop_func mainloop)
{
    EmscriptenMainloopCtx ectx { ctx, mainloop };
    emscripten_set_main_loop_arg(mainloop_wrap, &ectx, 0, true);
}

#else

EXPORT void ung_run_mainloop(void* ctx, ung_mainloop_func mainloop)
{
    auto time = ung_get_time();
    while (ung_poll_events()) {
        const auto now = ung_get_time();
        const auto dt = now - time;
        time = now;

        if (!mainloop(ctx, dt)) {
            break;
        }
    }
}

#endif
}
