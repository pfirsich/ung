#include <array>
#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <SDL.h>
#include <fast_obj.h>
#include <stb_image.h>
#include <tuple>

#include "ung.h"

#include "state.hpp"
#include "types.hpp"
#include "um.h"

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

State* state = nullptr;

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

    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) < 0) {
        std::fprintf(stderr, "Could not initialize SDL2\n");
        std::exit(1);
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

    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, params.window_mode.srgb ? 1 : 0);
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
    u32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (params.window_mode.fullscreen_mode == UNG_FULLSCREEN_MODE_DESKTOP_FULLSCREEN
        || params.window_mode.fullscreen_mode == UNG_FULLSCREEN_MODE_FULLSCREEN) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (params.window_mode.fullscreen_mode == UNG_FULLSCREEN_MODE_DESKTOP_FULLSCREEN) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    state->win_width = params.window_mode.width;
    state->win_height = params.window_mode.height;
    state->window = SDL_CreateWindow(params.title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        (int)params.window_mode.width, (int)params.window_mode.height, flags);
    if (!state->window) {
        std::fprintf(stderr, "Could not create window\n");
        std::exit(1);
    }

    // Context
    state->context = SDL_GL_CreateContext(state->window);
    if (!state->context) {
        std::fprintf(stderr, "Could not create context\n");
        std::exit(1);
    }

    // mugfx
    if (!params.mugfx.allocator) {
        params.mugfx.allocator = &mugfx_alloc;
    }

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
    state->transforms.init(params.max_num_transforms ? params.max_num_transforms : 1024);
    state->materials.init(params.max_num_materials ? params.max_num_materials : 1024);
    state->cameras.init(params.max_num_cameras ? params.max_num_cameras : 8);

    state->identity_trafo = ung_transform_create();

    state->sprite_renderer = {
        .vertices = nullptr,
        .num_vertices = params.max_num_sprite_vertices ? params.max_num_sprite_vertices : 16 * 1024,
        .indices = nullptr,
        .num_indices = params.max_num_sprite_indices ? params.max_num_sprite_indices : 16 * 1024,
        .vertex_buffer = { 0 },
        .index_buffer = { 0 },
        .geometry = { 0 },
        .vertex_offset = 0,
        .index_offset = 0,
        .current_material = { 0 },
        .current_tex_width = 0,
        .current_tex_height = 0,
    };

    state->sprite_renderer.vertices
        = allocate<SpriteRenderer::Vertex>(state->sprite_renderer.num_vertices);
    state->sprite_renderer.vertex_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_ARRAY,
        .usage = MUGFX_BUFFER_USAGE_HINT_STREAM,
        .data = { .data = nullptr,
            .length = sizeof(SpriteRenderer::Vertex) * state->sprite_renderer.num_vertices },
    }),

    state->sprite_renderer.indices = allocate<u16>(state->sprite_renderer.num_indices);
    state->sprite_renderer.index_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_INDEX,
        .usage = MUGFX_BUFFER_USAGE_HINT_STREAM,
        .data = { .data = nullptr, .length = sizeof(u16) * state->sprite_renderer.num_indices },
    });

    state->sprite_renderer.geometry = ung_geometry_create({
        .vertex_buffers = {
            {
                .buffer = state->sprite_renderer.vertex_buffer,
                .attributes = {
                    {.location = 0, .components = 2, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_F32}, // xy
                    {.location = 1, .components = 2, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U16_NORM}, // uv
                    {.location = 2, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U8_NORM}, // rgba
                },
            }
        },
        .index_buffer = state->sprite_renderer.index_buffer,
        .index_type = MUGFX_INDEX_TYPE_U16,
    });

    state->input.gamepads.init(params.max_num_gamepads ? params.max_num_gamepads : 8);

    state->u_constant.screen_dimensions = um_vec4 {
        static_cast<float>(params.window_mode.width),
        static_cast<float>(params.window_mode.height),
        1.0f / static_cast<float>(params.window_mode.width),
        1.0f / static_cast<float>(params.window_mode.height),
    };

    state->constant_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_CONSTANT,
        .size = sizeof(UConstant),
        .cpu_buffer = &state->u_constant,
    });

    state->frame_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
        .size = sizeof(UFrame),
        .cpu_buffer = &state->u_frame,
    });

    state->camera_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
        .size = sizeof(UCamera),
        .cpu_buffer = &state->u_camera,
    });

    sound::init(params);

    random::init();

    animation::init(params);

    files::init(params);
}

EXPORT void ung_shutdown()
{
    if (!state) {
        return;
    }

    files::shutdown();

    animation::shutdown();

    sound::shutdown();

    mugfx_shutdown();

    if (state->context) {
        SDL_GL_DeleteContext(state->context);
    }
    if (state->window) {
        SDL_DestroyWindow(state->window);
    }

    state->transforms.free();
    state->materials.free();
    state->cameras.free();

    deallocate(state);
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

EXPORT void* ung_utxt_realloc(void* ptr, size_t old_size, size_t new_size, void*)
{
    if (!ptr) {
        return allocator.allocate(new_size, allocator.ctx);
    } else if (new_size) {
        return allocator.reallocate(ptr, old_size, new_size, allocator.ctx);
    } else {
        allocator.deallocate(ptr, old_size, allocator.ctx);
        return nullptr;
    }
}

EXPORT utxt_alloc ung_get_utxt_alloc()
{
    return { ung_utxt_realloc, nullptr };
}

EXPORT void ung_get_window_size(u32* width, u32* height)
{
    *width = state->win_width;
    *height = state->win_height;
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

static void reset_input_state(InputState* input)
{
    std::memset(input->key_pressed.data(), 0, sizeof(input->key_pressed));
    std::memset(input->key_released.data(), 0, sizeof(input->key_released));
    input->mouse_dx = 0;
    input->mouse_dy = 0;
    input->mouse_scroll_left = 0;
    input->mouse_scroll_right = 0;
    input->mouse_scroll_y_pos = 0;
    input->mouse_scroll_y_neg = 0;
    std::memset(input->mouse_button_pressed.data(), 0, sizeof(input->mouse_button_pressed));
    std::memset(input->mouse_button_released.data(), 0, sizeof(input->mouse_button_released));
    for (uint32_t i = 0; i < input->gamepads.sm.capacity; ++i) {
        input->gamepads.data[i].button_pressed.fill(0);
        input->gamepads.data[i].button_released.fill(0);
    }
}

Gamepad* get_gamepad(u64 key)
{
    return get(state->input.gamepads, key);
}

static void get_gamepad_info(
    ung_gamepad_info* info, int device_index, SDL_GameController* controller)
{
    std::memset(info, 0, sizeof(ung_gamepad_info));

    const auto name = SDL_JoystickNameForIndex(device_index);
    const auto name_len = name ? std::strlen(name) : 0;
    assert(name_len < sizeof(info->name));
    std::memcpy(info->name, name, name_len);

    info->vendor_id = SDL_GameControllerGetVendor(controller);
    info->product_id = SDL_GameControllerGetProduct(controller);

    const auto guid = SDL_JoystickGetDeviceGUID(device_index);
    std::memcpy(info->guid, guid.data, sizeof(info->guid));

    const auto serial = SDL_GameControllerGetSerial(controller);
    const auto serial_len = serial ? std::strlen(serial) : 0;
    assert(serial_len < sizeof(info->serial));
    std::memcpy(info->serial, serial, serial_len);
}

static void process_input_event(InputState* input, SDL_Event* event)
{
    switch (event->type) {
    // Keyboard
    case SDL_KEYDOWN:
        input->key_down[event->key.keysym.scancode] = true;
        input->key_pressed[event->key.keysym.scancode]++;
        break;
    case SDL_KEYUP:
        input->key_down[event->key.keysym.scancode] = false;
        input->key_released[event->key.keysym.scancode]++;
        break;
    // Mouse
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button < MaxMouseButtons) {
            input->mouse_button_down[event->button.button] = true;
            input->mouse_button_pressed[event->button.button]++;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button < MaxMouseButtons) {
            input->mouse_button_down[event->button.button] = false;
            input->mouse_button_released[event->button.button]++;
        }
        break;
    case SDL_MOUSEMOTION:
        input->mouse_x = event->motion.x;
        input->mouse_y = event->motion.y;
        input->mouse_dx = event->motion.xrel;
        input->mouse_dy = event->motion.yrel;
        break;
    case SDL_MOUSEWHEEL:
        if (event->wheel.x < 0) {
            input->mouse_scroll_left += event->wheel.x;
        } else if (event->wheel.x > 0) {
            input->mouse_scroll_right += event->wheel.x;
        }
        if (event->wheel.y < 0) {
            input->mouse_scroll_y_neg += event->wheel.y;
        } else if (event->wheel.y > 0) {
            input->mouse_scroll_y_pos += event->wheel.y;
        }
        break;
    // Gamepads
    case SDL_CONTROLLERDEVICEADDED: {
        const auto device_index = event->cdevice.which;
        if (!SDL_IsGameController(device_index)) {
            break;
        }

        auto controller = SDL_GameControllerOpen(device_index);
        assert(controller); // TODO: handle NULL return (SDL_GetError)

        ung_gamepad_info info;
        get_gamepad_info(&info, device_index, controller);

        u64 id = 0;
        Gamepad* gamepad = nullptr;
        auto& gps = state->input.gamepads;
        for (u32 i = 0; i < gps.capacity(); ++i) {
            const auto key = gps.get_key(i);
            if (!key) {
                continue;
            }
            if (std::memcmp(&info, &gps.data[i].info, sizeof(ung_gamepad_info)) == 0) {
                id = gps.keys[i];
                gamepad = &gps.data[i];
            }
        }

        if (gamepad) {
            SDL_GameControllerClose(gamepad->controller);
        } else {
            std::tie(id, gamepad) = input->gamepads.insert();
            gamepad->deadzone_inner = 0.1f;
            gamepad->deadzone_outer = 0.9f;
            std::memcpy(&gamepad->info, &info, sizeof(ung_gamepad_info));
        }

        gamepad->controller = controller;
        gamepad->device_index = device_index;
        gamepad->instance_id = SDL_JoystickGetDeviceInstanceID(device_index);
        gamepad->connected = true;
        gamepad->type = SDL_GameControllerGetType(gamepad->controller);

        gamepad->last_active = ung_get_time();
        if (!input->last_active_gamepad) {
            input->last_active_gamepad = id;
        }
        break;
    }
    case SDL_CONTROLLERDEVICEREMOVED: {
        const auto key
            = ung_get_gamepad_from_event(SDL_CONTROLLERDEVICEREMOVED, event->cdevice.which);
        assert(key.id);
        auto gamepad = get_gamepad(key.id);

        gamepad->connected = false;

        if (input->last_active_gamepad == key.id) {
            input->last_active_gamepad = 0;
            float max_last_active = 0.0f;
            auto& gps = state->input.gamepads;
            for (u32 i = 0; i < gps.capacity(); ++i) {
                const auto key = gps.get_key(i);
                if (!key) {
                    continue;
                }
                if (gps.data[i].last_active > max_last_active) {
                    input->last_active_gamepad = gps.keys[i];
                    max_last_active = gps.data[i].last_active;
                }
            }
        }
        break;
    }
    case SDL_CONTROLLERBUTTONDOWN: {
        const auto key
            = ung_get_gamepad_from_event(SDL_CONTROLLERDEVICEREMOVED, event->cdevice.which);
        assert(key.id);
        auto gamepad = get_gamepad(key.id);

        gamepad->button_pressed[event->cbutton.button]++;

        input->last_active_gamepad = key.id;
        gamepad->last_active = ung_get_time();
        break;
    }
    case SDL_CONTROLLERBUTTONUP: {
        const auto key
            = ung_get_gamepad_from_event(SDL_CONTROLLERDEVICEREMOVED, event->cdevice.which);
        assert(key.id);
        auto gamepad = get_gamepad(key.id);

        gamepad->button_released[event->cbutton.button]++;

        break;
    }
    }
}

EXPORT bool ung_poll_events()
{
    reset_input_state(&state->input);
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        process_input_event(&state->input, &event);
        if (state->event_callback) {
            state->event_callback(state->event_callback_ctx, &event);
        }
        if (event.type == SDL_QUIT) {
            return false;
        }
    }
    return true;
}

static constexpr u32 MaxIdx = 0xFF'FFFF;
static constexpr u64 FreeMask = 0xFF00'0000'0000'0000;

// Keys array is used for free list too. If an element is free the index has the upper 8 bits
// set. The rest of the index contains the index to the next free element.
// Key 0 is always invalid

static u64 make_key(u32 idx, u32 gen)
{
    return gen << 24 | idx;
}

EXPORT uint32_t ung_slotmap_get_index(uint64_t key)
{
    return (u32)(key & MaxIdx);
}

EXPORT uint32_t ung_slotmap_get_generation(uint64_t key)
{
    return (u32)((key & 0xFFFF'FF00'0000) >> 24);
}

EXPORT void ung_slotmap_init(ung_slotmap* s)
{
    assert(s->keys);
    assert(s->capacity < 0xFF'FFFF);
    for (u32 i = 0; i < s->capacity; ++i) {
        // We invalidate on removal and we want to start with generation 1, so we init with 1
        s->keys[i] = FreeMask | make_key(i + 1, 1);
    }
    s->free_list_head = 0;
}

EXPORT uint64_t ung_slotmap_insert(ung_slotmap* s, uint32_t* oidx)
{
    assert(oidx);
    const auto idx = s->free_list_head;
    assert(idx < s->capacity);
    if (idx >= s->capacity) {
        return 0;
    }
    assert(s->keys[idx] & FreeMask);
    s->free_list_head = ung_slotmap_get_index(s->keys[idx]);
    const auto gen = ung_slotmap_get_generation(s->keys[idx]);
    s->keys[idx] = make_key(idx, gen);
    *oidx = idx;
    return s->keys[idx];
}

EXPORT uint64_t ung_slotmap_get_key(const ung_slotmap* s, uint32_t idx)
{
    // We just have to check the index here because for valid keys the index will be
    // equal to it's index in the array (that is clear).
    // But for free keys the index will always point to another index, lest the free
    // list would contain a cycle.
    return idx < s->capacity && ung_slotmap_get_index(s->keys[idx]) == idx ? s->keys[idx] : 0;
}

EXPORT uint32_t ung_slotmap_next_alive(const ung_slotmap* s, uint32_t min_index)
{
    for (uint32_t i = min_index; i < s->capacity; ++i) {
        if (ung_slotmap_get_key(s, i)) {
            return i;
        }
    }
    return s->capacity; // past the end
}

EXPORT bool ung_slotmap_contains(const ung_slotmap* s, uint64_t key)
{
    const auto idx = ung_slotmap_get_index(key);
    return (key & FreeMask) == 0 && idx < s->capacity && s->keys[idx] == key;
}

EXPORT bool ung_slotmap_remove(ung_slotmap* s, uint64_t key)
{
    assert(ung_slotmap_contains(s, key));
    if (!ung_slotmap_contains(s, key)) {
        return false;
    }
    const auto idx = ung_slotmap_get_index(key);
    const auto gen = ung_slotmap_get_generation(key);
    assert(s->free_list_head < s->capacity);
    s->keys[idx] = FreeMask | make_key(s->free_list_head, gen + 1);
    s->free_list_head = idx;
    return true;
}

EXPORT bool ung_key_down(const char* key)
{
    const auto scancode = SDL_GetScancodeFromName(key);
    return state->input.key_down.at(scancode);
}

EXPORT u8 ung_key_pressed(const char* key)
{
    const auto scancode = SDL_GetScancodeFromName(key);
    return state->input.key_pressed.at(scancode);
}

EXPORT u8 ung_key_released(const char* key)
{
    const auto scancode = SDL_GetScancodeFromName(key);
    return state->input.key_released.at(scancode);
}

EXPORT void ung_mouse_set_relative(bool relative)
{
    SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE);
}

EXPORT void ung_mouse_get(int* x, int* y, int* dx, int* dy)
{
    *x = state->input.mouse_x;
    *y = state->input.mouse_y;
    *dx = state->input.mouse_dx;
    *dy = state->input.mouse_dy;
}

EXPORT size_t ung_get_gamepads(ung_gamepad_id* gamepads, size_t max_num_gamepads)
{
    size_t n = 0;
    const auto& gps = state->input.gamepads;
    for (uint32_t i = 0; i < gps.sm.capacity; ++i) {
        const auto key = ung_slotmap_get_key(&gps.sm, i);
        if (key) {
            gamepads[n++] = { key };
        }
    }
    return n;
}

EXPORT ung_gamepad_id ung_gamepad_get_any()
{
    return { state->input.last_active_gamepad };
}

EXPORT SDL_GameController* ung_gamepad_get_sdl(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->controller;
}

EXPORT ung_gamepad_id ung_get_gamepad_from_event(uint32_t type, int32_t which)
{
    assert(which >= 0);

    int32_t device_index = -1, instance_id = -1;
    if (type == SDL_CONTROLLERDEVICEADDED) {
        device_index = which;
    } else {
        instance_id = which;
    }

    const auto& gps = state->input.gamepads;
    for (u32 i = 0; i < gps.capacity(); ++i) {
        const auto key = gps.get_key(i);
        if (!key) {
            continue;
        }
        if (gps.data[i].device_index == device_index || gps.data[i].instance_id == instance_id) {
            return { key };
        }
    }
    return { 0 };
}

EXPORT int32_t ung_gamepad_instance_id(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->instance_id;
}

EXPORT bool ung_gamepad_is_connected(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->connected;
}

EXPORT const ung_gamepad_info* ung_gamepad_get_info(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return &gp->info;
}

static float axis_to_float(int16_t v)
{
    return v > 0 ? (float)v / 32767.0f : (float)v / 32768.0f;
}

EXPORT float ung_gamepad_axis(ung_gamepad_id gamepad, uint8_t axis)
{
    const auto gp = get_gamepad(gamepad.id);
    const auto v
        = axis_to_float(SDL_GameControllerGetAxis(gp->controller, (SDL_GameControllerAxis)axis));
    if (std::fabs(v) < gp->deadzone_inner) {
        return 0.0f;
    } else if (v > gp->deadzone_outer) {
        return 1.0f;
    } else if (v < -gp->deadzone_outer) {
        return -1.0f;
    }
    return v;
}

static SDL_GameControllerButton map_button(uint8_t type, uint8_t button)
{
    if (button >= UNG_GAMEPAD_ACTION_CONFIRM) {
        assert(button <= UNG_GAMEPAD_ACTION_QUATERNARY);
        const size_t button_idx = button - UNG_GAMEPAD_ACTION_CONFIRM;
        // confirm, cancel, primary, secondary, tertiary, quaternary
        constexpr std::array<SDL_GameControllerButton, 6> xbox
            = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A,
                  SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_B };
        constexpr std::array<SDL_GameControllerButton, 6> ps
            = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A,
                  SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y };
        constexpr std::array<SDL_GameControllerButton, 6> nintendo
            = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A,
                  SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_B };

        switch (type) {
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
        case SDL_CONTROLLER_TYPE_PS5:
            return ps[button_idx];
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            return nintendo[button_idx];
        default: // default is xbox
            return xbox[button_idx];
        }
    }
    return (SDL_GameControllerButton)button;
}

EXPORT bool ung_gamepad_button_down(ung_gamepad_id gamepad, uint8_t button)
{
    const auto gp = get_gamepad(gamepad.id);
    return SDL_GameControllerGetButton(gp->controller, map_button(gp->type, button)) > 0;
}

EXPORT uint32_t ung_gamepad_button_pressed(ung_gamepad_id gamepad, uint8_t button)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->button_pressed.at((size_t)map_button(gp->type, button));
}

EXPORT uint32_t ung_gamepad_button_released(ung_gamepad_id gamepad, uint8_t button)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->button_released.at((size_t)map_button(gp->type, button));
}

EXPORT int ung_gamepad_get_player_index(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return SDL_GameControllerGetPlayerIndex(gp->controller);
}

EXPORT void ung_gamepad_set_player_index(ung_gamepad_id gamepad, int player_index)
{
    const auto gp = get_gamepad(gamepad.id);
    SDL_GameControllerSetPlayerIndex(gp->controller, player_index);
}

EXPORT void ung_gamepad_rumble(
    ung_gamepad_id gamepad, uint16_t low_freq, uint16_t high_freq, uint32_t duration_ms)
{
    const auto gp = get_gamepad(gamepad.id);
    SDL_GameControllerRumble(gp->controller, low_freq, high_freq, duration_ms);
}

EXPORT void ung_gamepad_rumble_triggers(
    ung_gamepad_id gamepad, uint16_t left, uint16_t right, uint32_t duration_ms)
{
    const auto gp = get_gamepad(gamepad.id);
    SDL_GameControllerRumbleTriggers(gp->controller, left, right, duration_ms);
}

EXPORT void ung_gamepad_set_led(ung_gamepad_id gamepad, uint8_t red, uint8_t green, uint8_t blue)
{
    const auto gp = get_gamepad(gamepad.id);
    SDL_GameControllerSetLED(gp->controller, red, green, blue);
}

EXPORT void ung_gamepad_axis_deadzone(
    ung_gamepad_id gamepad, uint8_t axis, float inner, float outer)
{
    const auto gp = get_gamepad(gamepad.id);
    gp->deadzone_inner = inner;
    gp->deadzone_outer = outer;
}

Transform* get_transform(u64 key)
{
    return get(state->transforms, key);
}

EXPORT ung_transform_id ung_transform_create()
{
    const auto [id, trafo] = state->transforms.insert();
    trafo->orientation = um_quat_identity();
    trafo->scale = um_vec3 { 1.0f, 1.0f, 1.0f };
    trafo->uniform_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
        .size = sizeof(UTransform),
    });
    trafo->local_matrix_dirty = true;
    return { id };
}

static void link(u64 prev_key, u64 next_key)
{
    if (!prev_key && !next_key) {
        return; // nothing to do
    } else if (!prev_key) {
        get_transform(next_key)->prev_sibling = 0;
    } else if (!next_key) {
        get_transform(prev_key)->next_sibling = 0;
    } else {
        get_transform(prev_key)->next_sibling = next_key;
        get_transform(next_key)->prev_sibling = prev_key;
    }
}

EXPORT void ung_transform_destroy(ung_transform_id transform)
{
    auto trafo = get_transform(transform.id);
    if (trafo->first_child) {
        if (trafo->parent) {
            // Reparent all children to the parent
            auto child_key = trafo->first_child;
            while (child_key) {
                auto child = get_transform(child_key);
                child->parent = trafo->parent;
                if (!child->next_sibling) {
                    break;
                }
                child_key = child->next_sibling;
            }
            const auto last_sibling = child_key;

            // Insert children between surrounding siblings
            auto parent = get_transform(trafo->parent);
            if (parent->first_child == transform.id) {
                assert(!get_transform(trafo->first_child)->prev_sibling);
                parent->first_child = trafo->first_child;
            } else {
                assert(trafo->prev_sibling);
                link(trafo->prev_sibling, trafo->first_child);
            }
            link(last_sibling, trafo->next_sibling);
        } else {
            // If the node has children, but no parent, unparent all children
            auto child_key = trafo->first_child;
            while (child_key) {
                auto child = get_transform(child_key);
                child_key = child->next_sibling;

                child->parent = 0;
                child->prev_sibling = 0;
                child->next_sibling = 0;
            }
        }
    } else if (trafo->parent) {
        // If the node has no children, but a parent, fix up siblings
        auto parent = get_transform(trafo->parent);
        link(trafo->prev_sibling, trafo->next_sibling);
        if (parent->first_child == transform.id) {
            parent->first_child = trafo->next_sibling;
        }
    }
    state->transforms.remove(transform.id);
}

EXPORT void ung_transform_set_position(ung_transform_id transform, float x, float y, float z)
{
    auto trafo = get_transform(transform.id);
    trafo->position = { x, y, z };
    trafo->local_matrix_dirty = true;
}

EXPORT void ung_transform_get_position(ung_transform_id transform, float position[3])
{
    const auto trafo = get_transform(transform.id);
    position[0] = trafo->position.x;
    position[1] = trafo->position.y;
    position[2] = trafo->position.z;
}

EXPORT void ung_transform_set_orientation(
    ung_transform_id transform, float w, float x, float y, float z)
{

    auto trafo = get_transform(transform.id);
    trafo->orientation = { w, x, y, z };
    trafo->local_matrix_dirty = true;
}

EXPORT void ung_transform_get_orientation(ung_transform_id transform, float quat[4])
{
    const auto trafo = get_transform(transform.id);
    quat[0] = trafo->orientation.w;
    quat[1] = trafo->orientation.x;
    quat[2] = trafo->orientation.y;
    quat[3] = trafo->orientation.z;
}

EXPORT void ung_transform_set_scale(ung_transform_id transform, float x, float y, float z)
{
    auto trafo = get_transform(transform.id);
    trafo->scale = { x, y, z };
    trafo->local_matrix_dirty = true;
}

EXPORT void ung_transform_get_scale(ung_transform_id transform, float scale[3])
{
    const auto trafo = get_transform(transform.id);
    scale[0] = trafo->scale.x;
    scale[1] = trafo->scale.y;
    scale[2] = trafo->scale.z;
}

static void look_at(Transform* trafo, um_vec3 at, um_vec3 up)
{
    const auto look_at = um_mat_look_at(trafo->position, at, up);
    trafo->orientation = um_quat_normalized(um_quat_conjugate(um_quat_from_matrix(look_at)));
    trafo->local_matrix_dirty = true;
}

EXPORT void ung_transform_look_at(ung_transform_id transform, float x, float y, float z)
{
    auto trafo = get_transform(transform.id);
    const um_vec3 at = { x, y, z };
    // We will guess an up vector by first calculating a right vector and from that an up vector
    const auto look = um_vec3_sub(at, trafo->position);
    const auto right = um_vec3_cross(look, { 0.0f, 1.0f, 0.0f });
    const auto up = um_vec3_normalized(um_vec3_cross(look, right));
    look_at(trafo, at, up);
}

EXPORT void ung_transform_look_at_up(
    ung_transform_id transform, float x, float y, float z, float up_x, float up_y, float up_z)
{
    auto trafo = get_transform(transform.id);
    look_at(trafo, { x, y, z }, { up_x, up_y, up_z });
}

static const um_mat& get_local_matrix(Transform* trafo)
{
    if (trafo->local_matrix_dirty) {
        const auto t = um_mat_translate(trafo->position);
        const auto r = um_mat_from_quat(trafo->orientation);
        const auto s = um_mat_scale(trafo->scale);
        trafo->local_matrix = um_mat_mul(um_mat_mul(t, r), s);
        trafo->local_matrix_dirty = false;
    }
    return trafo->local_matrix;
}

static const um_mat get_world_matrix(Transform* trafo)
{
    const auto& local_matrix = get_local_matrix(trafo);
    if (trafo->parent) {
        const auto parent = state->transforms.find(trafo->parent);
        return um_mat_mul(get_world_matrix(parent), local_matrix);
    }
    return local_matrix;
}

EXPORT void ung_transform_get_local_matrix(ung_transform_id transform, float matrix[16])
{
    const auto trafo = get_transform(transform.id);
    const auto& m = get_local_matrix(trafo);
    std::memcpy(matrix, &m, sizeof(float) * 16);
}

EXPORT void ung_transform_get_world_matrix(ung_transform_id transform, float matrix[16])
{
    // TODO: Consider going over all transforms in ung_begin_frame, calculating all local matrices
    // and then going over all of them again to calculate world transforms. This keeps a tight loop,
    // though it might calculate more than is necessary. And requires that transforms are constant
    // between ung_begin_frame and ung_end_frame.
    const auto trafo = get_transform(transform.id);
    const auto m = get_world_matrix(trafo);
    std::memcpy(matrix, &m, sizeof(float) * 16);
}

EXPORT void ung_transform_local_to_world(ung_transform_id transform, float dir[3])
{
    const auto trafo = get_transform(transform.id);
    const auto world = um_quat_mul_vec3(trafo->orientation, { dir[0], dir[1], dir[2] });
    std::memcpy(dir, &world, sizeof(float) * 3);
}

static void unparent(u64 child_key, Transform* child)
{
    auto parent = get_transform(child->parent);
    link(child->prev_sibling, child->next_sibling);
    if (parent->first_child == child_key) {
        assert(!child->prev_sibling);
        parent->first_child = child->next_sibling;
    }
    child->parent = 0;
    child->prev_sibling = 0;
    child->next_sibling = 0;
}

EXPORT void ung_transform_set_parent(ung_transform_id transform, ung_transform_id parent)
{
    // NOTE: I explicitly do not check for loops, because it's too expensive to check every time and
    // you'll notice quick (infinite loops).
    // NOTE: I also shortcut a redundant set_parent with the current parent of transform,
    // because I consider this a mistake, which you should pay for.
    assert(transform.id != parent.id);
    auto trafo = get_transform(transform.id);

    if (trafo->parent) {
        unparent(transform.id, trafo);
    }

    if (parent.id) {
        auto parent_trafo = get_transform(parent.id);
        trafo->parent = parent.id;
        link(transform.id, parent_trafo->first_child);
        parent_trafo->first_child = transform.id;
    }
}

EXPORT ung_transform_id ung_transform_get_parent(ung_transform_id transform)
{
    const auto trafo = state->transforms.find(transform.id);
    assert(trafo);
    return { trafo->parent };
}

EXPORT ung_transform_id ung_transform_get_first_child(ung_transform_id transform)
{
    const auto trafo = state->transforms.find(transform.id);
    assert(trafo);
    return { trafo->first_child };
}

EXPORT ung_transform_id ung_transform_get_next_sibling(ung_transform_id transform)
{
    const auto trafo = state->transforms.find(transform.id);
    assert(trafo);
    return { trafo->next_sibling };
}

EXPORT ung_shader_id ung_shader_create(mugfx_shader_create_params params)
{
    const auto sh = mugfx_shader_create(params);
    if (!sh.id) {
        std::exit(1);
    }
    const auto [id, shader] = state->shaders.insert();
    shader->shader = sh;
    return { id };
}

EXPORT void ung_shader_recreate(ung_shader_id shader, mugfx_shader_create_params params)
{
    const auto sh = mugfx_shader_create(params);
    if (!sh.id) {
        return;
    }
    get(state->shaders, shader.id)->shader = sh;
}

static std::string_view ltrim(std::string_view str)
{
    const auto p = str.find_first_not_of(" \t\n\r");
    return str.substr(p != std::string_view::npos ? p : str.size());
}

static bool expect(std::string_view& src, std::string_view str)
{
    src = ltrim(src);
    if (src.starts_with(str)) {
        src = src.substr(str.size());
        return true;
    }
    return false;
}

static u32 parse_number(std::string_view str)
{
    u32 res = 0;
    const auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), res);
    if (ec == std::errc()) {
        return res;
    }
    return 0xffff'ffff;
}

static bool parse_shader_bindings(std::string_view src, mugfx_shader_create_params& params)
{
    // This is so primitive, but its enough for now and I will make it better as I go
    usize binding_idx = 0;
    while (src.size()) {
        const auto nl = src.find('\n');
        auto line = ltrim(src.substr(0, nl));
        if (expect(line, "layout") && expect(line, "(") && expect(line, "binding")
            && expect(line, "=")) {
            line = ltrim(line);
            const auto num_end = line.find_first_not_of("0123456789");
            const auto num_str = line.substr(0, num_end);
            const auto num = parse_number(num_str);
            if (num == 0xffff'ffff) {
                return false;
            }

            const auto uniform = line.find("uniform");
            if (uniform == std::string_view::npos) {
                return false;
            }
            line = line.substr(uniform + 7);
            if (expect(line, "sampler")) {
                params.bindings[binding_idx].type = MUGFX_SHADER_BINDING_TYPE_SAMPLER;
            } else {
                params.bindings[binding_idx].type = MUGFX_SHADER_BINDING_TYPE_UNIFORM;
            }
            params.bindings[binding_idx].binding = num;
            binding_idx++;
        }
        if (nl == std::string_view::npos) {
            return true;
        }
        src = src.substr(nl + 1);
    }
    return true;
}

mugfx_shader_id load_shader(mugfx_shader_stage stage, const char* path)
{
    usize size = 0;
    const auto data = ung_read_whole_file(path, &size);
    if (!data) {
        std::printf("Could not read '%s': %s\n", path, SDL_GetError());
        return { 0 };
    }
    mugfx_shader_create_params params;
    params.stage = stage;
    params.source = data;
    if (params.bindings[0].type == MUGFX_SHADER_BINDING_TYPE_NONE) {
        // TODO: Try to load from <path>.meta first
        // TODO: Check data is not SPIR-V
        if (!parse_shader_bindings(std::string_view(data, size), params)) {
            std::printf("Could not parse shader bindings\n");
            ung_free_file_data(data, size);
            return { 0 };
        }
    }
    const auto shader = mugfx_shader_create(params);
    ung_free_file_data(data, size);
    return shader;
}

EXPORT ung_shader_id ung_shader_load(mugfx_shader_stage stage, const char* path)
{
    const auto sh = load_shader(stage, path);
    if (!sh.id) {
        std::exit(1);
    }
    const auto [id, shader] = state->shaders.insert();
    shader->shader = sh;
    return { id };
}

EXPORT void ung_shader_reload(ung_shader_id shader, mugfx_shader_stage stage, const char* path)
{
    const auto sh = load_shader(stage, path);
    if (!sh.id) {
        return;
    }
    get(state->shaders, shader.id)->shader = sh;
}

EXPORT ung_texture_id ung_texture_create(mugfx_texture_create_params params)
{
    const auto t = mugfx_texture_create(params);
    if (!t.id) {
        std::exit(1);
    }
    const auto [id, texture] = state->textures.insert();
    texture->texture = t;
    return { id };
}

EXPORT void ung_texture_recreate(ung_texture_id texture, mugfx_texture_create_params params)
{
    const auto t = mugfx_texture_create(params);
    if (!t.id) {
        return;
    }
    get(state->textures, texture.id)->texture = t;
}

static mugfx_texture_id load_texture(
    const char* path, bool flip_y, mugfx_texture_create_params params)
{
    mugfx_pixel_format pixel_formats[] {
        MUGFX_PIXEL_FORMAT_DEFAULT,
        MUGFX_PIXEL_FORMAT_R8,
        MUGFX_PIXEL_FORMAT_RG8,
        MUGFX_PIXEL_FORMAT_RGB8,
        MUGFX_PIXEL_FORMAT_RGBA8,
    };
    int width, height, comp;
    stbi_set_flip_vertically_on_load(flip_y);
    auto data = stbi_load(path, &width, &height, &comp, 0);
    if (!data) {
        std::fprintf(stderr, "Could not load texture: %s\n", stbi_failure_reason());
        std::exit(1);
    }
    assert(width > 0 && height > 0 && comp > 0);
    assert(comp <= 4);
    params.width = static_cast<usize>(width);
    params.height = static_cast<usize>(height);
    params.data.data = data;
    params.data.length = static_cast<usize>(width * height * comp);
    params.format = pixel_formats[comp];
    params.data_format = pixel_formats[comp];
    const auto texture = mugfx_texture_create(params);
    stbi_image_free(data);
    return texture;
}

EXPORT ung_texture_id ung_texture_load(
    const char* path, bool flip_y, mugfx_texture_create_params params)
{
    const auto t = load_texture(path, flip_y, params);
    if (!t.id) {
        std::exit(1);
    }
    const auto [id, texture] = state->textures.insert();
    texture->texture = t;
    return { id };
}

EXPORT void ung_texture_reload(
    ung_texture_id texture, const char* path, bool flip_y, mugfx_texture_create_params params)
{
    const auto t = load_texture(path, flip_y, params);
    if (!t.id) {
        return;
    }
    get(state->textures, texture.id)->texture = t;
}

Material* get_material(u64 key)
{
    return get(state->materials, key);
}

EXPORT ung_material_id ung_material_create(ung_material_create_params params)
{
    assert(params.mugfx.vert_shader.id == 0);
    assert(params.mugfx.frag_shader.id == 0);
    const auto vert = get(state->shaders, params.vert.id);
    const auto frag = get(state->shaders, params.frag.id);
    params.mugfx.vert_shader = vert->shader;
    params.mugfx.frag_shader = frag->shader;

    const auto [id, material] = state->materials.insert();
    material->material = mugfx_material_create(params.mugfx);
    material->bindings.append() = {
        .type = MUGFX_BINDING_TYPE_UNIFORM_DATA,
        .uniform_data = { .binding = 0, .id = state->constant_data },
    };
    material->bindings.append() = {
        .type = MUGFX_BINDING_TYPE_UNIFORM_DATA,
        .uniform_data = { .binding = 1, .id = state->frame_data },
    };
    material->bindings.append() = {
        .type = MUGFX_BINDING_TYPE_UNIFORM_DATA,
        .uniform_data = { .binding = 2, .id = state->camera_data },
    };
    material->bindings.append() = {
        .type = MUGFX_BINDING_TYPE_UNIFORM_DATA,
        .uniform_data = { .binding = 3, .id = { 0 } }, // Transform, replaced before draw
    };
    if (params.constant_data_size) {
        material->constant_data = mugfx_uniform_data_create({
            .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_CONSTANT,
            .size = params.constant_data_size,
        });
        if (params.constant_data) {
            std::memcpy(mugfx_uniform_data_get_ptr(material->constant_data), params.constant_data,
                params.constant_data_size);
        }
        material->bindings.append() = {
            .type = MUGFX_BINDING_TYPE_UNIFORM_DATA,
            .uniform_data = { .binding = 8, .id = material->constant_data },
        };
    }
    if (params.dynamic_data_size) {
        material->dynamic_data = mugfx_uniform_data_create({
            .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
            .size = params.dynamic_data_size,
        });
        material->bindings.append() = {
            .type = MUGFX_BINDING_TYPE_UNIFORM_DATA,
            .uniform_data = { .binding = 9, .id = material->dynamic_data },
        };
    }
    return { id };
}

EXPORT ung_material_id ung_material_load(
    const char* vert_path, const char* frag_path, ung_material_create_params params)
{
    const auto vert = ung_shader_load(MUGFX_SHADER_STAGE_VERTEX, vert_path);
    const auto frag = ung_shader_load(MUGFX_SHADER_STAGE_FRAGMENT, frag_path);
    assert(params.vert.id == 0);
    params.vert = vert;
    assert(params.frag.id == 0);
    params.frag = frag;
    return ung_material_create(params);
}

EXPORT void ung_material_destroy(ung_material_id material)
{
    auto mat = get_material(material.id);
    if (mat->constant_data.id) {
        mugfx_uniform_data_destroy(mat->constant_data);
    }
    if (mat->dynamic_data.id) {
        mugfx_uniform_data_destroy(mat->dynamic_data);
    }
    mugfx_material_destroy(mat->material);
    state->materials.remove(material.id);
}

static bool is_same_binding(const mugfx_draw_binding& a, const mugfx_draw_binding& b)
{
    if (a.type != b.type) {
        return false;
    }
    switch (a.type) {
    case MUGFX_BINDING_TYPE_UNIFORM_DATA:
        return a.uniform_data.binding == b.uniform_data.binding;
    case MUGFX_BINDING_TYPE_TEXTURE:
        return a.texture.binding == b.texture.binding;
    case MUGFX_BINDING_TYPE_BUFFER:
        return a.buffer.binding == b.buffer.binding;
    default:
        return false;
    }
}

EXPORT void ung_material_set_binding(ung_material_id material, mugfx_draw_binding binding)
{
    auto mat = get_material(material.id);
    for (auto& b : mat->bindings) {
        if (is_same_binding(b, binding)) {
            b = binding;
            return;
        }
    }
    mat->bindings.append() = binding;
}

EXPORT void ung_material_set_uniform_data(
    ung_material_id material, u32 binding, mugfx_uniform_data_id uniform_data)
{
    ung_material_set_binding(material,
        {
            .type = MUGFX_BINDING_TYPE_UNIFORM_DATA,
            .uniform_data = { .binding = binding, .id = uniform_data },
        });
}

EXPORT void ung_material_set_texture(ung_material_id material, u32 binding, ung_texture_id texture)
{
    const auto tex = get(state->textures, texture.id);
    ung_material_set_binding(material,
        {
            .type = MUGFX_BINDING_TYPE_TEXTURE,
            .texture = { .binding = binding, .id = tex->texture },
        });
}

EXPORT void* ung_material_get_dynamic_data(ung_material_id material)
{
    auto mat = get_material(material.id);
    return mat->dynamic_data.id ? mugfx_uniform_data_get_ptr(mat->dynamic_data) : nullptr;
}

EXPORT void ung_material_update(ung_material_id material)
{
    auto mat = get_material(material.id);
    if (mat->dynamic_data.id) {
        mugfx_uniform_data_update(mat->dynamic_data);
    }
}

EXPORT char* ung_read_whole_file(const char* path, usize* size)
{
    return (char*)SDL_LoadFile(path, size);
}

EXPORT void ung_free_file_data(char* data, usize)
{
    SDL_free(data);
}

// https://www.khronos.org/opengl/wiki/Normalized_Integer
static constexpr u32 pack1010102(float x, float y, float z, u8 w = 0)
{
    x = std::fmin(std::fmax(x, -1.0f), 1.0f);
    y = std::fmin(std::fmax(y, -1.0f), 1.0f);
    z = std::fmin(std::fmax(z, -1.0f), 1.0f);

    constexpr u32 maxv = 511; // MAX=2^(B-1)-1, B=10
    const auto xi = static_cast<i32>(std::round(x * maxv));
    const auto yi = static_cast<i32>(std::round(y * maxv));
    const auto zi = static_cast<i32>(std::round(z * maxv));

    // Convert to 10-bit unsigned representations
    // For negative values: apply two's complement, because in twos complement a number and its
    // negative sum to 2^N and x + -x = 2^N <=> 2^N - x = -x.
    // wiki: "The defining property of being a complement to a number with respect to
    // 2N is simply that the summation of this number with the original produce 2N."
    const auto xu = static_cast<u32>(xi < 0 ? (1024 + xi) : xi);
    const auto yu = static_cast<u32>(yi < 0 ? (1024 + yi) : yi);
    const auto zu = static_cast<u32>(zi < 0 ? (1024 + zi) : zi);

    const auto wu = static_cast<u32>(w & 0b11); // limit to two bits

    return (xu & 0x3FF) | // X in bits 0-9
        ((yu & 0x3FF) << 10) | // Y in bits 10-19
        ((zu & 0x3FF) << 20) | // Z in bits 20-29
        (wu << 30); // W in bits 30-31
}

struct Vertex {
    float x, y, z;
    u16 u, v;
    u32 n; // MUGFX_VERTEX_ATTRIBUTE_TYPE_I10_10_10_2_NORM
    u8 r, g, b, a;
};

EXPORT ung_geometry_id ung_geometry_box(float w, float h, float d)
{
    const auto n_px = pack1010102(1.0f, 0.0f, 0.0f);
    const auto n_nx = pack1010102(-1.0f, 0.0f, 0.0f);
    const auto n_py = pack1010102(0.0f, 1.0f, 0.0f);
    const auto n_ny = pack1010102(0.0f, -1.0f, 0.0f);
    const auto n_pz = pack1010102(0.0f, 0.0f, 1.0f);
    const auto n_nz = pack1010102(0.0f, 0.0f, -1.0f);

    // clang-format off
    std::array vertices = {
        // +x
        Vertex { 1.0f,  1.0f,  1.0f, 0x0000, 0x0000, n_px, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f,  1.0f, 0x0000, 0xffff, n_px, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f, -1.0f, 0xffff, 0xffff, n_px, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f,  1.0f, -1.0f, 0xffff, 0x0000, n_px, 0xff, 0xff, 0xff, 0xff },

        // -x
        Vertex {-1.0f,  1.0f, -1.0f, 0x0000, 0x0000, n_nx, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f, -1.0f, 0x0000, 0xffff, n_nx, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f,  1.0f, 0xffff, 0xffff, n_nx, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f,  1.0f,  1.0f, 0xffff, 0x0000, n_nx, 0xff, 0xff, 0xff, 0xff },
        
        // +y
        Vertex {-1.0f,  1.0f, -1.0f, 0x0000, 0x0000, n_py, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f,  1.0f,  1.0f, 0x0000, 0xffff, n_py, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f,  1.0f,  1.0f, 0xffff, 0xffff, n_py, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f,  1.0f, -1.0f, 0xffff, 0x0000, n_py, 0xff, 0xff, 0xff, 0xff },

        // -y
        Vertex {-1.0f, -1.0f,  1.0f, 0x0000, 0x0000, n_ny, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f, -1.0f, 0x0000, 0xffff, n_ny, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f, -1.0f, 0xffff, 0xffff, n_ny, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f,  1.0f, 0xffff, 0x0000, n_ny, 0xff, 0xff, 0xff, 0xff },

        // +z
        Vertex {-1.0f,  1.0f,  1.0f, 0x0000, 0x0000, n_pz, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f,  1.0f, 0x0000, 0xffff, n_pz, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f,  1.0f, 0xffff, 0xffff, n_pz, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f,  1.0f,  1.0f, 0xffff, 0x0000, n_pz, 0xff, 0xff, 0xff, 0xff },

        // -z
        Vertex { 1.0f,  1.0f, -1.0f, 0x0000, 0x0000, n_nz, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f, -1.0f, 0x0000, 0xffff, n_nz, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f, -1.0f, 0xffff, 0xffff, n_nz, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f,  1.0f, -1.0f, 0xffff, 0x0000, n_nz, 0xff, 0xff, 0xff, 0xff },
    };
    // clang-format on

    for (auto& v : vertices) {
        v.x *= w / 2.0f;
        v.y *= h / 2.0f;
        v.z *= d / 2.0f;
    }

    static constexpr u8 face_indices[] = { 0, 1, 2, 0, 2, 3 };

    std::array<u8, 6 * 2 * 3> indices; // 6 sides, 2 tris/side, 3 indices/tri
    for (u8 side = 0; side < 6; ++side) {
        for (u8 vertex = 0; vertex < 6; ++vertex) {
            indices[side * 6 + vertex] = static_cast<u8>(4 * side + face_indices[vertex]);
        }
    }

    const auto vertex_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_ARRAY,
        .data = { vertices.data(), vertices.size() * sizeof(Vertex) },
    });

    const auto index_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_INDEX,
        .data = { indices.data(), indices.size() * sizeof(indices[0]) },
    });

    const auto geometry = ung_geometry_create({
        .vertex_buffers = {
            {
                .buffer = vertex_buffer,
                .attributes = {
                    {.location = 0, .components = 3, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_F32}, // position
                    {.location = 1, .components = 2, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U16_NORM}, // texcoord
                    {.location = 2, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_I10_10_10_2_NORM}, // normal
                    {.location = 3, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U8_NORM}, // color
                },
            },
        },
        .index_buffer = index_buffer,
        .index_type = MUGFX_INDEX_TYPE_U8,
    });

    return geometry;
}

static float clamp(float v, float min, float max)
{
    return std::fmin(std::fmax(v, min), max);
}

static float saturate(float v)
{
    return clamp(v, 0.0f, 1.0f);
}

static u16 f2u16norm(float v)
{
    return (u16)(65535.0f * saturate(v));
}

static u8 f2u8norm(float v)
{
    return (u8)(255.0f * saturate(v));
}

EXPORT ung_geometry_data ung_geometry_data_load(const char* path)
{
    auto mesh = fast_obj_read(path);
    if (!mesh) {
        std::printf("Failed to load geometry '%s'\n", path);
        return {};
    }

    ung_geometry_data gdata = {};

    usize vidx = 0;
    bool normals = false, texcoords = false, colors = false;
    for (unsigned int face = 0; face < mesh->face_count; ++face) {
        if (mesh->face_materials[face] < mesh->material_count) {
            colors = true;
        }

        gdata.num_vertices += mesh->face_vertices[face];

        if (mesh->face_vertices[face] == 3) {
            gdata.num_indices += 3;
        } else if (mesh->face_vertices[face] == 4) {
            gdata.num_indices += 6; // two triangles
        } else {
            std::printf("Only triangles and quads are supported - '%s'\n", path);
            return {};
        }

        for (unsigned int vtx = 0; vtx < mesh->face_vertices[face]; ++vtx) {
            const auto [p, t, n] = mesh->indices[vidx++];
            if (t) {
                texcoords = true;
            }
            if (n) {
                normals = true;
            }
        }
    }

    gdata.positions
        = allocate<float>(gdata.num_vertices * (3 + normals * 3 + texcoords * 2 + colors * 4));
    if (normals) {
        gdata.normals = gdata.positions + gdata.num_vertices * 3;
    }
    if (texcoords) {
        gdata.texcoords = gdata.positions + gdata.num_vertices * (3 + normals * 3);
    }
    if (colors) {
        gdata.colors = gdata.positions + gdata.num_vertices * (3 + normals * 3 + texcoords * 2);
    }

    vidx = 0;
    for (unsigned int face = 0; face < mesh->face_count; ++face) {
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        if (mesh->face_materials[face] < mesh->material_count) {
            const auto& mat = mesh->materials[mesh->face_materials[face]];
            r = mat.Kd[0];
            g = mat.Kd[1];
            b = mat.Kd[2];
            a = mat.d;
        }

        for (unsigned int vtx = 0; vtx < mesh->face_vertices[face]; ++vtx) {
            const auto [p, t, n] = mesh->indices[vidx];

            gdata.positions[vidx * 3 + 0] = mesh->positions[3 * p + 0]; // x
            gdata.positions[vidx * 3 + 1] = mesh->positions[3 * p + 1]; // y
            gdata.positions[vidx * 3 + 2] = mesh->positions[3 * p + 2]; // z

            // t or n might be zero, but we don't have to check, becaust fast_obj will add a
            // dummy element that's all zeros.

            if (gdata.texcoords) {
                gdata.texcoords[vidx * 2 + 0] = mesh->texcoords[2 * t + 0]; // u
                gdata.texcoords[vidx * 2 + 1] = mesh->texcoords[2 * t + 1]; // v
            }

            if (gdata.normals) {
                gdata.normals[vidx * 3 + 0] = mesh->normals[3 * n + 0]; // x
                gdata.normals[vidx * 3 + 1] = mesh->normals[3 * n + 1]; // y
                gdata.normals[vidx * 3 + 2] = mesh->normals[3 * n + 2]; // z
            }

            if (gdata.colors) {
                gdata.colors[vidx * 4 + 0] = r; // x
                gdata.colors[vidx * 4 + 1] = g; // y
                gdata.colors[vidx * 4 + 2] = b; // z
                gdata.colors[vidx * 4 + 3] = a; // z
            }

            vidx++;
        }
    }

    // TODO: Generate normals if none a present!

    gdata.indices = allocate<uint32_t>(gdata.num_indices);

    auto indices_it = gdata.indices;
    uint32_t base_vtx = 0;
    for (unsigned int face = 0; face < mesh->face_count; ++face) {
        if (mesh->face_vertices[face] == 3) {
            *(indices_it++) = base_vtx + 0;
            *(indices_it++) = base_vtx + 1;
            *(indices_it++) = base_vtx + 2;
        } else if (mesh->face_vertices[face] == 4) {
            *(indices_it++) = base_vtx + 0;
            *(indices_it++) = base_vtx + 1;
            *(indices_it++) = base_vtx + 2;

            *(indices_it++) = base_vtx + 0;
            *(indices_it++) = base_vtx + 2;
            *(indices_it++) = base_vtx + 3;
        }
        base_vtx += mesh->face_vertices[face];
    }

    fast_obj_destroy(mesh);

    return gdata;
}

EXPORT void ung_geometry_data_destroy(ung_geometry_data gdata)
{
    const bool normals = gdata.normals;
    const bool texcoords = gdata.texcoords;
    const bool colors = gdata.colors;
    const auto num_floats = gdata.num_vertices * (3 + normals * 3 + texcoords * 2 + colors * 4);
    deallocate(gdata.positions, num_floats);
    deallocate(gdata.indices, gdata.num_indices);
}

EXPORT ung_geometry_id ung_geometry_create(mugfx_geometry_create_params params)
{
    const auto geom = mugfx_geometry_create(params);
    if (!geom.id) {
        std::exit(1);
    }

    const auto [id, geometry] = state->geometries.insert();
    geometry->geometry = geom;
    return { id };
}

EXPORT void ung_geometry_recreate(ung_geometry_id geometry_id, mugfx_geometry_create_params params)
{
    const auto geom = mugfx_geometry_create(params);
    if (!geom.id) {
        return;
    }

    auto geometry = get(state->geometries, geometry_id.id);
    geometry->geometry = geom;
}

static mugfx_geometry_id create_geometry(
    Vertex* vertices, usize num_vertices, u32* indices, usize num_indices)
{
    mugfx_buffer_id vertex_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_ARRAY,
        .data = { vertices, num_vertices * sizeof(Vertex) },
    });

    mugfx_buffer_id index_buffer = { 0 };
    if (indices) {
        index_buffer = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_INDEX,
            .data = { indices, num_indices * sizeof(u32) },
        });
    }

    mugfx_geometry_create_params geometry_params = {
        .vertex_buffers = {
            {
                .buffer = vertex_buffer,
                .attributes = {
                    {.location = 0, .components = 3, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_F32}, // position
                    {.location = 1, .components = 2, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U16_NORM}, // texcoord
                    {.location = 2, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_I10_10_10_2_NORM}, // normal
                    {.location = 3, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U8_NORM}, // color
                },
            },
        },
    };
    if (index_buffer.id) {
        geometry_params.index_buffer = index_buffer;
        geometry_params.index_type = MUGFX_INDEX_TYPE_U32;
    }

    return mugfx_geometry_create(geometry_params);
}

static Vertex* build_vertex_buffer_data(ung_geometry_data gdata)
{
    auto vertices = allocate<Vertex>(gdata.num_vertices);
    for (size_t i = 0; i < gdata.num_vertices; ++i) {
        const auto x = gdata.positions[i * 3 + 0];
        const auto y = gdata.positions[i * 3 + 1];
        const auto z = gdata.positions[i * 3 + 2];

        const auto u = f2u16norm(gdata.texcoords ? gdata.texcoords[i * 2 + 0] : 0.0f);
        const auto v = f2u16norm(gdata.texcoords ? gdata.texcoords[i * 2 + 1] : 0.0f);

        const auto nx = gdata.normals ? gdata.normals[i * 3 + 0] : 0.0f;
        const auto ny = gdata.normals ? gdata.normals[i * 3 + 1] : 0.0f;
        const auto nz = gdata.normals ? gdata.normals[i * 3 + 2] : 0.0f;

        const auto r = f2u8norm(gdata.colors ? gdata.colors[i * 4 + 0] : 1.0f);
        const auto g = f2u8norm(gdata.colors ? gdata.colors[i * 4 + 1] : 1.0f);
        const auto b = f2u8norm(gdata.colors ? gdata.colors[i * 4 + 2] : 1.0f);
        const auto a = f2u8norm(gdata.colors ? gdata.colors[i * 4 + 3] : 1.0f);

        vertices[i] = { x, y, z, u, v, pack1010102(nx, ny, nz), r, g, b, a };
    }
    return vertices;
}

static mugfx_geometry_id geometry_from_data(ung_geometry_data gdata)
{
    // We cannot build a proper indexed mesh trivially, because a face will reference different
    // position, texcoord and normal indices, so you would have to generate all used combinations
    // and build new indices. It's too bothersome and will not be fast.

    auto vertices = build_vertex_buffer_data(gdata);

    const auto geom
        = create_geometry(vertices, gdata.num_vertices, gdata.indices, gdata.num_indices);

    deallocate(vertices, gdata.num_vertices);

    return geom;
}

EXPORT ung_geometry_id ung_geometry_create_from_data(ung_geometry_data gdata)
{
    const auto geom = geometry_from_data(gdata);
    if (!geom.id) {
        std::exit(1);
    }

    const auto [id, geometry] = state->geometries.insert();
    geometry->geometry = geom;
    return { id };
}

mugfx_geometry_id load_geometry(const char* path)
{
    const auto gdata = ung_geometry_data_load(path);
    const auto geom = geometry_from_data(gdata);
    ung_geometry_data_destroy(gdata);
    return geom;
}

EXPORT ung_geometry_id ung_geometry_load(const char* path)
{
    const auto geom = load_geometry(path);
    if (!geom.id) {
        std::exit(1);
    }

    const auto [id, geometry] = state->geometries.insert();
    geometry->geometry = geom;
    return { id };
}

EXPORT void ung_geometry_reload(ung_geometry_id geometry_id, const char* path)
{
    const auto geom = load_geometry(path);
    if (!geom.id) {
        return;
    }

    const auto geometry = get(state->geometries, geometry_id.id);
    geometry->geometry = geom;
}

static mugfx_texture_id get_texture(ung_material_id material, uint32_t binding)
{
    auto mat = get_material(material.id);
    for (const auto& b : mat->bindings) {
        if (b.type == MUGFX_BINDING_TYPE_TEXTURE && b.texture.binding == binding) {
            return b.texture.id;
        }
    }
    return { 0 };
}

EXPORT void ung_sprite_set_material(ung_material_id mat)
{
    if (mat.id != state->sprite_renderer.current_material.id) {
        ung_sprite_flush();
        state->sprite_renderer.current_material = mat;
        const auto tex = get_texture(mat, 0);
        mugfx_texture_get_size(tex, &state->sprite_renderer.current_tex_width,
            &state->sprite_renderer.current_tex_height);
    }
}

EXPORT uint16_t ung_sprite_add_vertex(float x, float y, float u, float v, ung_color color)
{
    assert(state->sprite_renderer.vertex_offset < state->sprite_renderer.num_vertices);
    state->sprite_renderer.vertices[state->sprite_renderer.vertex_offset] = SpriteRenderer::Vertex {
        x,
        y,
        f2u16norm(u),
        f2u16norm(v),
        f2u8norm(color.r),
        f2u8norm(color.g),
        f2u8norm(color.b),
        f2u8norm(color.a),
    };
    return (u16)state->sprite_renderer.vertex_offset++;
}

EXPORT void ung_sprite_add_index(uint16_t idx)
{
    assert(state->sprite_renderer.index_offset < state->sprite_renderer.num_indices);
    state->sprite_renderer.indices[state->sprite_renderer.index_offset++] = idx;
}

EXPORT void ung_sprite_add_quad(
    float x, float y, float w, float h, ung_texture_region texture, ung_color color)
{
    const auto tl = ung_sprite_add_vertex(x, y, texture.x, texture.y, color);
    const auto bl = ung_sprite_add_vertex(x, y + h, texture.x, texture.y + texture.h, color);
    const auto tr = ung_sprite_add_vertex(x + w, y, texture.x + texture.w, texture.y, color);
    const auto br
        = ung_sprite_add_vertex(x + w, y + h, texture.x + texture.w, texture.y + texture.h, color);

    ung_sprite_add_index(tl);
    ung_sprite_add_index(bl);
    ung_sprite_add_index(tr);

    ung_sprite_add_index(bl);
    ung_sprite_add_index(br);
    ung_sprite_add_index(tr);
}

struct vec2 {
    float x, y;
};

static vec2 transform_vec2(ung_transform_2d t, float x, float y)
{
    // offset
    x += t.offset_x;
    y += t.offset_y;

    // scale
    x *= t.scale_x;
    y *= t.scale_y;

    // rotate
    const auto s = sinf(t.rotation);
    const auto c = cosf(t.rotation);
    const auto rx = x * c - y * s;
    const auto ry = x * s + y * c;

    // translate
    x = rx + t.x;
    y = ry + t.y;

    return vec2 { x, y };
}

static u16 add_vertex(
    const vec2& p, ung_transform_2d transform, ung_texture_region region, ung_color color)
{
    const auto pos = transform_vec2(transform,
        p.x * (float)state->sprite_renderer.current_tex_width * region.w,
        p.y * (float)state->sprite_renderer.current_tex_height * region.h);
    const auto u = region.x + p.x * region.w;
    const auto v = region.y + p.y * region.h;
    return ung_sprite_add_vertex(pos.x, pos.y, u, v, color);
}

EXPORT void ung_sprite_add(
    ung_material_id mat, ung_transform_2d transform, ung_texture_region texture, ung_color color)
{
    transform.scale_x = transform.scale_x != 0.0f ? transform.scale_x : 1.0f;
    transform.scale_y = transform.scale_y != 0.0f ? transform.scale_y : 1.0f;

    ung_sprite_set_material(mat);

    const auto tl = add_vertex({ 0.0f, 0.0f }, transform, texture, color);
    const auto bl = add_vertex({ 0.0f, 1.0f }, transform, texture, color);
    const auto tr = add_vertex({ 1.0f, 0.0f }, transform, texture, color);
    const auto br = add_vertex({ 1.0f, 1.0f }, transform, texture, color);

    ung_sprite_add_index(tl);
    ung_sprite_add_index(bl);
    ung_sprite_add_index(tr);

    ung_sprite_add_index(bl);
    ung_sprite_add_index(br);
    ung_sprite_add_index(tr);
}

EXPORT void ung_sprite_flush()
{
    if (state->sprite_renderer.index_offset > 0) {
        const auto geom = get(state->geometries, state->sprite_renderer.geometry.id);
        mugfx_buffer_update(state->sprite_renderer.vertex_buffer, 0,
            { .data = state->sprite_renderer.vertices,
                .length = sizeof(SpriteRenderer::Vertex) * state->sprite_renderer.vertex_offset });
        mugfx_buffer_update(state->sprite_renderer.index_buffer, 0,
            { .data = state->sprite_renderer.indices,
                .length = sizeof(u16) * state->sprite_renderer.index_offset });
        mugfx_geometry_set_index_range(geom->geometry, 0, state->sprite_renderer.index_offset);
        ung_draw(state->sprite_renderer.current_material, state->sprite_renderer.geometry,
            state->identity_trafo);
        state->sprite_renderer.vertex_offset = 0;
        state->sprite_renderer.index_offset = 0;
    }
}

EXPORT void ung_font_load_ttf(ung_font* font, ung_font_load_ttf_param params)
{
    font->font = utxt_font_load_ttf(ung_get_utxt_alloc(), params.ttf_path, params.load_params);

    uint32_t atlas_width, atlas_height, atlas_channels;
    const auto atlas_data
        = utxt_get_atlas(font->font, &atlas_width, &atlas_height, &atlas_channels);
    assert(atlas_channels == 1);
    font->texture = ung_texture_create({
        .width = atlas_width,
        .height = atlas_height,
        .format = MUGFX_PIXEL_FORMAT_R8,
        .data = { atlas_data, atlas_width * atlas_height },
        .data_format = MUGFX_PIXEL_FORMAT_R8,
    });

    auto& gfxparams = params.material_params.mugfx;
    gfxparams.depth_func = gfxparams.depth_func ? gfxparams.depth_func : MUGFX_DEPTH_FUNC_ALWAYS;
    gfxparams.write_mask = gfxparams.write_mask ? gfxparams.write_mask : MUGFX_WRITE_MASK_RGBA,
    gfxparams.cull_face = gfxparams.cull_face ? gfxparams.cull_face : MUGFX_CULL_FACE_MODE_NONE,
    gfxparams.src_blend = gfxparams.src_blend ? gfxparams.src_blend : MUGFX_BLEND_FUNC_SRC_ALPHA,
    gfxparams.dst_blend = gfxparams.dst_blend ? gfxparams.dst_blend : MUGFX_BLEND_FUNC_ONE_MINUS_SRC_ALPHA,

    font->material = ung_material_load(params.vert_path, params.frag_path, {
        .mugfx = {
            .depth_func = MUGFX_DEPTH_FUNC_ALWAYS,
            .write_mask = MUGFX_WRITE_MASK_RGBA,
            .cull_face = MUGFX_CULL_FACE_MODE_NONE,
            .src_blend = MUGFX_BLEND_FUNC_SRC_ALPHA,
            .dst_blend = MUGFX_BLEND_FUNC_ONE_MINUS_SRC_ALPHA,
        },
    });
    ung_material_set_texture(font->material, 0, font->texture);
}

EXPORT void ung_font_draw_quad(const utxt_quad* q, ung_color color)
{
    ung_sprite_add_quad(
        q->x, q->y, q->w, q->h, { q->u0, q->v0, q->u1 - q->u0, q->v1 - q->v0 }, color);
}

EXPORT void ung_font_draw_quads(
    const ung_font* font, const utxt_quad* quads, size_t num_quads, ung_color color)
{
    ung_sprite_set_material(font->material);
    for (size_t i = 0; i < num_quads; ++i) {
        ung_font_draw_quad(quads + i, color);
    }
}

Camera* get_camera(u64 key)
{
    return get(state->cameras, key);
}

EXPORT ung_camera_id ung_camera_create()
{
    const auto [id, camera] = state->cameras.insert();
    camera->transform = ung_transform_create();
    return { id };
}

EXPORT void ung_camera_destroy(ung_camera_id camera)
{
    auto cam = get_camera(camera.id);
    ung_transform_destroy(cam->transform);
    state->cameras.remove(camera.id);
}

static void set_projection(Camera* camera, const um_mat& proj)
{
    camera->projection = proj;
    camera->projection_inv = um_mat_invert(proj);
}

EXPORT void ung_camera_set_projection(ung_camera_id camera, const float matrix[16])
{
    um_mat proj;
    std::memcpy(&proj, matrix, sizeof(float) * 16);
    set_projection(get_camera(camera.id), proj);
}

EXPORT void ung_camera_set_perspective(
    ung_camera_id camera, float fov, float aspect, float near, float far)
{
    const auto proj = um_mat_perspective(fov, aspect, near, far);
    set_projection(get_camera(camera.id), proj);
}

EXPORT void ung_camera_set_orthographic_fullscreen(ung_camera_id camera)
{
    const auto proj
        = um_mat_ortho(0.0f, (float)state->win_width, (float)state->win_height, 0.0f, -1.0f, 1.0f);
    set_projection(get_camera(camera.id), proj);
}

EXPORT void ung_camera_set_orthographic(
    ung_camera_id camera, float left, float right, float bottom, float top)
{
    const auto proj = um_mat_ortho(left, right, bottom, top, -1.0f, 1.0f);
    set_projection(get_camera(camera.id), proj);
}

EXPORT void ung_camera_set_orthographic_z(
    ung_camera_id camera, float left, float right, float bottom, float top, float near, float far)
{
    const auto proj = um_mat_ortho(left, right, bottom, top, near, far);
    set_projection(get_camera(camera.id), proj);
}

EXPORT ung_transform_id ung_camera_get_transform(ung_camera_id camera)
{
    return get_camera(camera.id)->transform;
}

EXPORT void ung_camera_get_view_matrix(ung_camera_id camera, float matrix[16])
{
    const auto trafo = get_transform(get_camera(camera.id)->transform.id);
    const auto view = um_mat_invert(get_world_matrix(trafo));
    std::memcpy(matrix, &view, sizeof(float) * 16);
}

EXPORT void ung_camera_get_projection_matrix(ung_camera_id camera, float matrix[16])
{
    std::memcpy(matrix, &get_camera(camera.id)->projection, sizeof(float) * 16);
}

EXPORT void ung_begin_frame()
{
    files::begin_frame();
    mugfx_begin_frame();
    state->u_frame.time.x = 0.0f;
    state->u_frame.time.y = 0.0f;
    mugfx_uniform_data_update(state->frame_data);
    sound::begin_frame();
}

EXPORT void ung_begin_pass(mugfx_render_target_id target, ung_camera_id camera)
{
    mugfx_begin_pass(target);

    auto cam = get_camera(camera.id);
    state->u_camera.projection = cam->projection;
    state->u_camera.projection_inv = cam->projection_inv;
    auto trafo = get_transform(cam->transform.id);
    state->u_camera.view_inv = get_world_matrix(trafo);
    state->u_camera.view = um_mat_invert(state->u_camera.view_inv);
    state->u_camera.view_projection = um_mat_mul(state->u_camera.projection, state->u_camera.view);
    state->u_camera.view_projection_inv = um_mat_invert(state->u_camera.view_projection);
    mugfx_uniform_data_update(state->camera_data);

    if (!target.id) {
        mugfx_set_viewport(0, 0, state->win_width, state->win_height);
    }
}

EXPORT void ung_draw(ung_material_id material, ung_geometry_id geometry, ung_transform_id transform)
{
    static UTransform u_trafo;
    auto mat = get_material(material.id);
    auto geom = get(state->geometries, geometry.id);
    auto trafo = get_transform(transform.id);

    u_trafo.model = get_world_matrix(trafo);
    u_trafo.model_inv = um_mat_invert(u_trafo.model);
    u_trafo.model_view = um_mat_mul(state->u_camera.view, u_trafo.model);
    u_trafo.model_view_projection = um_mat_mul(state->u_camera.projection, u_trafo.model_view);
    std::memcpy(mugfx_uniform_data_get_ptr(trafo->uniform_data), &u_trafo, sizeof(UTransform));
    mugfx_uniform_data_update(trafo->uniform_data);

    assert(mat->bindings[3].uniform_data.binding == 3);
    mat->bindings[3].uniform_data.id = trafo->uniform_data;

    mugfx_draw(mat->material, geom->geometry, mat->bindings.data(), mat->bindings.size());
}

EXPORT void ung_end_pass()
{
    mugfx_end_pass();
}

EXPORT void ung_end_frame()
{
    mugfx_end_frame();
    SDL_GL_SwapWindow(state->window);
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
