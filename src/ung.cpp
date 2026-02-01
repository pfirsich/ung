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

namespace input {
    void init(ung_init_params params);
    void reset();
    void process_event(SDL_Event* event);
    void shutdown();
}

namespace transform {
    void init(ung_init_params params);
    void shutdown();
    um_mat get_world_matrix(ung_transform_id trafo);
    mugfx_uniform_data_id get_uniform_data(ung_transform_id trafo);
}

namespace sprite_renderer {
    void init(ung_init_params params);
    void shutdown();
}

State* state = nullptr;

void assign(Array<char>& arr, const char* str)
{
    if (arr.data == str) {
        return;
    }
    if (arr.data) {
        arr.free();
    }
    arr.init((uint32_t)std::strlen(str) + 1);
    std::strcpy(arr.data, str);
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
        ung_panicf("Error creating window: %s", SDL_GetError());
    }

    // Context
    state->context = SDL_GL_CreateContext(state->window);
    if (!state->context) {
        ung_panicf("Error creating GL context: %s", SDL_GetError());
    }

    printf("SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver());

    SDL_GL_SetSwapInterval(params.window_mode.vsync);

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
    state->materials.init(params.max_num_materials ? params.max_num_materials : 1024);
    state->cameras.init(params.max_num_cameras ? params.max_num_cameras : 8);

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
        .debug_label = "UngConstant",
    });

    state->frame_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
        .size = sizeof(UFrame),
        .cpu_buffer = &state->u_frame,
        .debug_label = "UngFrame",
    });

    state->camera_data = mugfx_uniform_data_create({
        .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
        .size = sizeof(UCamera),
        .cpu_buffer = &state->u_camera,
        .debug_label = "UngCamera",
    });

    state->auto_reload = params.auto_reload;
    state->load_cache = params.load_cache;

    files::init(params);

    input::init(params);

    transform::init(params);

    sound::init(params);

    random::init();

    animation::init(params);

    sprite_renderer::init(params);

    state->identity_trafo = ung_transform_create();
}

EXPORT void ung_shutdown()
{
    if (!state) {
        return;
    }

    ung_transform_destroy(state->identity_trafo);

    sprite_renderer::shutdown();

    animation::shutdown();

    sound::shutdown();

    transform::shutdown();

    input::shutdown();

    mugfx_shutdown();

    if (state->context) {
        SDL_GL_DeleteContext(state->context);
    }
    if (state->window) {
        SDL_DestroyWindow(state->window);
    }

    state->materials.free();
    state->cameras.free();

    files::shutdown();

    deallocate(state);
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

EXPORT bool ung_poll_events()
{
    input::reset();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
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

EXPORT ung_shader_id ung_shader_create(mugfx_shader_create_params params)
{
    const auto sh = mugfx_shader_create(params);
    if (!sh.id) {
        ung_panic("Failed to create shader");
    }

    const auto [id, shader] = state->shaders.insert();
    shader->shader = sh;
    shader->stage = params.stage;
    return { id };
}

EXPORT void ung_shader_recreate(ung_shader_id shader_id, mugfx_shader_create_params params)
{
    const auto sh = mugfx_shader_create(params);
    if (!sh.id) {
        return;
    }

    auto shader = get(state->shaders, shader_id.id);
    assert(shader->stage == params.stage);
    mugfx_shader_destroy(shader->shader);
    shader->shader = sh;
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
    const auto data = ung_read_whole_file(path, &size, false);
    if (!data) {
        std::printf("Could not read '%s': %s\n", path, SDL_GetError());
        return { 0 };
    }
    mugfx_shader_create_params params;
    params.stage = stage;
    params.source = data;
    params.debug_label = path;
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

static bool reload_shader(Shader* shader, const char* path)
{
    const auto sh = load_shader(shader->stage, path);
    if (!sh.id) {
        return false;
    }

    mugfx_shader_destroy(shader->shader);
    shader->shader = sh;
    return true;
}

static bool shader_reload_cb(void* userdata)
{
    auto ctx = (ShaderReloadCtx*)userdata;
    std::fprintf(stderr, "Reloading shader: %s\n", ctx->path.data);
    auto shader = get(state->shaders, ctx->shader.id);
    return reload_shader(shader, ctx->path.data);
}

EXPORT ung_shader_id ung_shader_load(mugfx_shader_stage stage, const char* path)
{
    const auto sh = load_shader(stage, path);
    if (!sh.id) {
        ung_panicf("Error loading shader '%s'", path);
    }

    const auto [id, shader] = state->shaders.insert();
    shader->shader = sh;
    shader->stage = stage;

    if (state->auto_reload) {
        shader->reload_ctx = allocate<ShaderReloadCtx>();
        shader->reload_ctx->shader = { id };
        assign(shader->reload_ctx->path, path);
        shader->resource = ung_resource_create(shader_reload_cb, shader->reload_ctx);
        ung_resource_set_deps(shader->resource, &path, 1, nullptr, 0);
    }

    return { id };
}

EXPORT bool ung_shader_reload(ung_shader_id shader_id, const char* path)
{
    const auto shader = get(state->shaders, shader_id.id);

    if (shader->reload_ctx) {
        shader->reload_ctx->path.free();
        assign(shader->reload_ctx->path, path);
        ung_resource_set_deps(shader->resource, &path, 1, nullptr, 0);
    }

    return reload_shader(shader, path);
}

EXPORT ung_texture_id ung_texture_create(mugfx_texture_create_params params)
{
    const auto t = mugfx_texture_create(params);
    if (!t.id) {
        ung_panic("Failed to create shader");
    }

    const auto [id, texture] = state->textures.insert();
    texture->texture = t;
    return { id };
}

EXPORT void ung_texture_recreate(ung_texture_id texture_id, mugfx_texture_create_params params)
{
    const auto t = mugfx_texture_create(params);
    if (!t.id) {
        return;
    }

    auto texture = get(state->textures, texture_id.id);
    mugfx_texture_destroy(texture->texture);
    texture->texture = t;
}

static mugfx_texture_id create_texture(
    const void* data, int width, int height, int comp, mugfx_texture_create_params& params)
{
    static constexpr mugfx_pixel_format pixel_formats[] {
        MUGFX_PIXEL_FORMAT_DEFAULT,
        MUGFX_PIXEL_FORMAT_R8,
        MUGFX_PIXEL_FORMAT_RG8,
        MUGFX_PIXEL_FORMAT_RGB8,
        MUGFX_PIXEL_FORMAT_RGBA8,
    };
    assert(width > 0 && height > 0 && comp > 0);
    assert(comp <= 4);
    params.width = (usize)width;
    params.height = (usize)height;
    params.data = { data, (usize)(width * height * comp) };
    params.format = pixel_formats[comp];
    params.data_format = pixel_formats[comp];
    return mugfx_texture_create(params);
}

char* append(char* pathbuf, std::string_view str)
{
    memcpy(pathbuf, str.data(), str.size());
    pathbuf[str.size()] = '\0';
    return pathbuf + str.size();
}

char* fmt_hex(char* buf, const void* data, usize size)
{
    constexpr static char digits[16]
        = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
    auto bytes = (const u8*)data;
    for (size_t i = 0; i < size; ++i) {
        *(buf++) = digits[0xF & (bytes[i]) >> 4];
        *(buf++) = digits[0xF & bytes[i]];
    }
    return buf;
}

void format_texture_cache_path(char* pathbuf, u64 hash, bool flip_y)
{
    pathbuf = append(pathbuf, ".ungcache/");
    pathbuf = fmt_hex(pathbuf, &hash, sizeof(hash));
    if (flip_y) {
        pathbuf = append(pathbuf, "-flip");
    }
    append(pathbuf, "-v1.tex");
}

struct TexDecode {
    void* free_data = nullptr;
    usize free_size = 0;
    u8* texture_data = nullptr;
    int width = 0;
    int height = 0;
    int components = 0;

    TexDecode() = default;
    TexDecode(TexDecode&) = delete;
    TexDecode(TexDecode&& r)
        : free_data(r.free_data)
        , free_size(r.free_size)
        , texture_data(r.texture_data)
        , width(r.width)
        , height(r.height)
        , components(r.components)
    {
        r.free_data = nullptr;
        r.texture_data = nullptr;
    }

    ~TexDecode()
    {
        if (free_data && texture_data) {
            if (free_data == texture_data) {
                stbi_image_free(free_data);
            } else {
                ung_free_file_data((char*)free_data, free_size);
            }
        }
    }
};

struct CacheTexHeader {
    u32 width, height, components;
};

void write_texture_cache_file(const char* path, const TexDecode& tex)
{
    auto file = fopen(path, "wb");
    if (!file) {
        std::fprintf(stderr, "Could not open texture cache file for writing: %s\n", path);
        return;
    }
    CacheTexHeader hdr { (u32)tex.width, (u32)tex.height, (u32)tex.components };
    fwrite(&hdr, sizeof(CacheTexHeader), 1, file);
    fwrite(tex.texture_data, 1, hdr.width * hdr.height * hdr.components, file);
    fclose(file);
}

TexDecode load_cached_texture(const char* cache_path)
{
    TexDecode ret = {};
    ret.free_data = ung_read_whole_file(cache_path, &ret.free_size, false);
    if (!ret.free_data) {
        return ret;
    }

    ret.texture_data = (u8*)ret.free_data + sizeof(CacheTexHeader);

    CacheTexHeader hdr;
    memcpy(&hdr, ret.free_data, sizeof(CacheTexHeader));

    ret.width = (int)hdr.width;
    ret.height = (int)hdr.height;
    ret.components = (int)hdr.components;

    return ret;
}

TexDecode decode_texture(const u8* data, usize size, bool flip_y)
{
    char cache_path[128];
    if (state->load_cache) {
        format_texture_cache_path(cache_path, ung_fnv1a(data, size), flip_y);

        auto cached = load_cached_texture(cache_path);

        if (cached.free_size) {
            return cached;
        }
    }

    TexDecode ret;
    stbi_set_flip_vertically_on_load(flip_y);
    ret.texture_data
        = stbi_load_from_memory(data, (int)size, &ret.width, &ret.height, &ret.components, 0);
    ret.free_data = ret.texture_data;

    if (state->load_cache) {
        write_texture_cache_file(cache_path, ret);
    }

    return ret;
}

static mugfx_texture_id load_texture(
    const char* path, bool flip_y, mugfx_texture_create_params& params)
{
    usize size = 0;
    const auto data = ung_read_whole_file(path, &size, false);
    if (!data) {
        return { 0 };
    }
    const auto decoded = decode_texture((const u8*)data, size, flip_y);
    ung_free_file_data(data, size);
    if (!decoded.texture_data) {
        return { 0 };
    }
    params.debug_label = path;
    const auto texture = create_texture(
        decoded.texture_data, decoded.width, decoded.height, decoded.components, params);
    return texture;
}

static bool reload_texture(
    Texture* texture, const char* path, bool flip_y, mugfx_texture_create_params& params)
{
    const auto tex = load_texture(path, flip_y, params);
    if (!tex.id) {
        return false;
    }

    mugfx_texture_destroy(texture->texture);
    texture->texture = tex;
    return true;
}

static bool texture_reload_cb(void* userdata)
{
    auto ctx = (TextureReloadCtx*)userdata;
    std::fprintf(stderr, "Reloading texture %#lx: %s\n", ctx->texture.id, ctx->path.data);
    auto texture = get(state->textures, ctx->texture.id);
    return reload_texture(texture, ctx->path.data, ctx->flip_y, ctx->params);
}

EXPORT ung_texture_id ung_texture_load(
    const char* path, bool flip_y, mugfx_texture_create_params params)
{
    const auto t = load_texture(path, flip_y, params);
    if (!t.id) {
        ung_panicf("Error loading texture '%s'", path);
    }

    const auto [id, texture] = state->textures.insert();
    texture->texture = t;

    if (state->auto_reload) {
        texture->reload_ctx = allocate<TextureReloadCtx>();
        texture->reload_ctx->texture = { id };
        assign(texture->reload_ctx->path, path);
        texture->reload_ctx->flip_y = flip_y;
        texture->reload_ctx->params = params;
        texture->resource = ung_resource_create(texture_reload_cb, texture->reload_ctx);
        ung_resource_set_deps(texture->resource, &path, 1, nullptr, 0);
    }

    return { id };
}

EXPORT ung_texture_id ung_texture_load_buffer(
    const void* buffer, size_t size, bool flip_y, mugfx_texture_create_params params)
{

    const auto decoded = decode_texture((const u8*)buffer, size, flip_y);
    if (!decoded.texture_data) {
        ung_panicf("Error loading texture: %s", stbi_failure_reason());
    }
    const auto tex = create_texture(
        decoded.texture_data, decoded.width, decoded.height, decoded.components, params);

    if (!tex.id) {
        ung_panicf("Error loading texture");
    }

    const auto [id, texture] = state->textures.insert();
    texture->texture = tex;
    return { id };
}

EXPORT bool ung_texture_reload(
    ung_texture_id texture_id, const char* path, bool flip_y, mugfx_texture_create_params params)
{
    const auto texture = get(state->textures, texture_id.id);

    if (texture->reload_ctx) {
        texture->reload_ctx->path.free();
        assign(texture->reload_ctx->path, path);
        texture->reload_ctx->flip_y = flip_y;
        texture->reload_ctx->params = params;
        ung_resource_set_deps(texture->resource, &path, 1, nullptr, 0);
    }

    return reload_texture(texture, path, flip_y, params);
}

Material* get_material(u64 key)
{
    return get(state->materials, key);
}

static bool recreate_material(Material* mat, MaterialReloadCtx* ctx, Shader* vert, Shader* frag)
{
    auto params = ctx->params;
    params.vert_shader = vert->shader;
    params.frag_shader = frag->shader;

    const auto new_mat = mugfx_material_create(params);
    if (!new_mat.id) {
        return false;
    }

    mugfx_material_destroy(mat->material);
    mat->material = new_mat;

    return true;
}

static void update_texture_bindings(Material* mat, MaterialReloadCtx* ctx)
{
    for (size_t i = 0; i < mat->bindings.size(); ++i) {
        if (mat->bindings[i].type == MUGFX_BINDING_TYPE_TEXTURE) {
            ung_material_set_texture(
                ctx->material, mat->bindings[i].texture.binding, ctx->textures[i]);
        }
    }
}

static bool reload_material(Material* mat, MaterialReloadCtx* ctx)
{
    const auto vert = get(state->shaders, ctx->vert.id);
    const auto frag = get(state->shaders, ctx->frag.id);
    const auto recreate = ung_resource_get_version(vert->resource) != ctx->vert_version
        || ung_resource_get_version(frag->resource) != ctx->frag_version;

    bool res = true;
    if (recreate) {
        res = recreate_material(mat, ctx, vert, frag);
    }

    update_texture_bindings(mat, ctx);
    return res;
}

static bool material_reload_cb(void* userdata)
{
    auto ctx = (MaterialReloadCtx*)userdata;
    std::fprintf(stderr, "Reloading material\n");
    auto mat = get(state->materials, ctx->material.id);
    return reload_material(mat, ctx);
}

static void update_deps(Material* mat)
{
    StaticVector<ung_resource_id, 32> deps = {};
    deps.append() = ung_shader_get_resource(mat->reload_ctx->vert);
    deps.append() = ung_shader_get_resource(mat->reload_ctx->frag);
    for (const auto tex : mat->reload_ctx->textures) {
        if (tex.id) {
            const auto res = ung_texture_get_resource(tex);
            if (res.id) {
                deps.append() = res;
            }
        }
    }
    ung_resource_set_deps(mat->resource, nullptr, 0, deps.data(), deps.size());
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
            .debug_label = "mat.constant",
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
            .debug_label = "mat.dynamic",
        });
        material->bindings.append() = {
            .type = MUGFX_BINDING_TYPE_UNIFORM_DATA,
            .uniform_data = { .binding = 9, .id = material->dynamic_data },
        };
    }

    if (state->auto_reload) {
        material->reload_ctx = allocate<MaterialReloadCtx>();
        material->reload_ctx->material = { id };
        material->reload_ctx->params = params.mugfx;
        material->reload_ctx->constant_data_size = params.constant_data_size;
        material->reload_ctx->dynamic_data_size = params.dynamic_data_size;

        material->reload_ctx->vert = params.vert;
        material->reload_ctx->vert_version
            = ung_resource_get_version(ung_shader_get_resource(params.vert));
        material->reload_ctx->frag = params.frag;
        material->reload_ctx->frag_version
            = ung_resource_get_version(ung_shader_get_resource(params.frag));
        material->resource = ung_resource_create(material_reload_cb, material->reload_ctx);

        update_deps(material);
    }

    return { id };
}

EXPORT ung_material_id ung_material_load(
    const char* vert_path, const char* frag_path, ung_material_create_params params)
{
    assert(params.vert.id == 0);
    assert(params.frag.id == 0);
    params.vert = ung_shader_load(MUGFX_SHADER_STAGE_VERTEX, vert_path);
    params.frag = ung_shader_load(MUGFX_SHADER_STAGE_FRAGMENT, frag_path);

    const auto mat_id = ung_material_create(params);
    auto mat = get_material(mat_id.id);
    mat->vert = params.vert;
    mat->frag = params.frag;

    if (state->auto_reload) {
        assign(mat->reload_ctx->vert_path, vert_path);
        assign(mat->reload_ctx->frag_path, frag_path);
    }

    return mat_id;
}

EXPORT bool ung_material_recreate(ung_material_id material_id, ung_material_create_params params)
{
    auto mat = get_material(material_id.id);

    assert(params.mugfx.vert_shader.id == 0);
    assert(params.mugfx.frag_shader.id == 0);
    auto vert = get(state->shaders, params.vert.id);
    auto frag = get(state->shaders, params.frag.id);
    params.mugfx.vert_shader = vert->shader;
    params.mugfx.frag_shader = frag->shader;

    const auto new_mat = mugfx_material_create(params.mugfx);
    if (!new_mat.id) {
        return false;
    }

    mugfx_material_destroy(mat->material);
    mat->material = new_mat;

    if (params.constant_data_size) {
        if (mat->constant_data.id) {
            mugfx_uniform_data_destroy(mat->constant_data);
        }
        mat->constant_data = mugfx_uniform_data_create({
            .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_CONSTANT,
            .size = params.constant_data_size,
            .debug_label = "mat.constant",
        });
        if (params.constant_data) {
            std::memcpy(mugfx_uniform_data_get_ptr(mat->constant_data), params.constant_data,
                params.constant_data_size);
        }
        for (auto& b : mat->bindings) {
            if (b.type == MUGFX_BINDING_TYPE_UNIFORM_DATA && b.uniform_data.binding == 8) {
                b.uniform_data.id = mat->constant_data;
                break;
            }
        }
    }

    if (params.dynamic_data_size) {
        if (mat->dynamic_data.id) {
            mugfx_uniform_data_destroy(mat->dynamic_data);
        }
        mat->dynamic_data = mugfx_uniform_data_create({
            .usage_hint = MUGFX_UNIFORM_DATA_USAGE_HINT_FRAME,
            .size = params.dynamic_data_size,
            .debug_label = "mat.dynamic",
        });
        for (auto& b : mat->bindings) {
            if (b.type == MUGFX_BINDING_TYPE_UNIFORM_DATA && b.uniform_data.binding == 9) {
                b.uniform_data.id = mat->dynamic_data;
                break;
            }
        }
    }

    if (mat->reload_ctx) {
        mat->reload_ctx->params = params.mugfx;
        mat->reload_ctx->constant_data_size = params.constant_data_size;
        mat->reload_ctx->dynamic_data_size = params.dynamic_data_size;
        mat->reload_ctx->vert = params.vert;
        mat->reload_ctx->vert_version = ung_resource_get_version(vert->resource);
        mat->reload_ctx->frag = params.frag;
        mat->reload_ctx->frag_version = ung_resource_get_version(frag->resource);

        update_deps(mat);
    }

    return true;
}

EXPORT bool ung_material_reload(ung_material_id material_id, const char* vert_path,
    const char* frag_path, ung_material_create_params params)
{
    assert(params.vert.id == 0);
    assert(params.frag.id == 0);

    auto mat = get_material(material_id.id);

    if (mat->vert.id) {
        ung_shader_reload(mat->vert, vert_path);
    } else {
        mat->vert = ung_shader_load(MUGFX_SHADER_STAGE_VERTEX, vert_path);
    }

    if (mat->frag.id) {
        ung_shader_reload(mat->frag, frag_path);
    } else {
        mat->frag = ung_shader_load(MUGFX_SHADER_STAGE_FRAGMENT, frag_path);
    }

    params.vert = mat->vert;
    params.frag = mat->frag;

    const auto res = ung_material_recreate(material_id, params);
    if (!res) {
        return false;
    }

    if (mat->reload_ctx) {
        assign(mat->reload_ctx->vert_path, vert_path);
        assign(mat->reload_ctx->frag_path, frag_path);
    }

    return true;
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

    if (mat->resource.id) {
        ung_resource_destroy(mat->resource);
    }

    if (mat->reload_ctx) {
        mat->reload_ctx->vert_path.free();
        mat->reload_ctx->frag_path.free();
        deallocate(mat->reload_ctx);
    }

    /* ung_shader_destroy is not implemented yet
    if (mat->vert.id) {
        ung_shader_destroy(mat->vert);
    }
    if (mat->frag.id) {
        ung_shader_destroy(mat->frag);
    }
    */

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

static void store_texture(
    Material* mat, MaterialReloadCtx* ctx, u32 binding, ung_texture_id texture)
{
    for (size_t i = 0; i < mat->bindings.size(); ++i) {
        if (mat->bindings[i].type == MUGFX_BINDING_TYPE_TEXTURE
            && mat->bindings[i].texture.binding == binding) {
            ctx->textures[i] = texture;
        }
    }
}

EXPORT void ung_material_set_texture(ung_material_id material, u32 binding, ung_texture_id texture)
{
    const auto tex = get(state->textures, texture.id);

    ung_material_set_binding(material,
        {
            .type = MUGFX_BINDING_TYPE_TEXTURE,
            .texture = { .binding = binding, .id = tex->texture },
        });

    if (state->auto_reload) {
        auto mat = get_material(material.id);
        if (mat->reload_ctx) {
            store_texture(mat, mat->reload_ctx, binding, texture);
            update_deps(mat);
        }
    }
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

EXPORT char* ung_read_whole_file(const char* path, usize* size, bool panic_on_error)
{
    auto data = (char*)SDL_LoadFile(path, size);
    if (panic_on_error && !data) {
        ung_panicf("Error reading file '%s': %s", path, SDL_GetError());
    }
    return data;
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
        .debug_label = "box.vbuf",
    });

    const auto index_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_INDEX,
        .data = { indices.data(), indices.size() * sizeof(indices[0]) },
        .debug_label = "box.ibuf",
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
        .debug_label = "box.geom",
    });

    return geometry;
}

float clamp(float v, float min, float max)
{
    return std::fmin(std::fmax(v, min), max);
}

float saturate(float v)
{
    return clamp(v, 0.0f, 1.0f);
}

u16 f2u16norm(float v)
{
    return (u16)(65535.0f * saturate(v));
}

u8 f2u8norm(float v)
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
        ung_panicf("Error creating geometry");
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
    mugfx_geometry_destroy(geometry->geometry);
    geometry->geometry = geom;
}

static mugfx_geometry_id create_geometry(Vertex* vertices, usize num_vertices, u32* indices,
    usize num_indices, const char* debug_label = nullptr)
{
    mugfx_buffer_id vertex_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_ARRAY,
        .data = { vertices, num_vertices * sizeof(Vertex) },
        .debug_label = debug_label,
    });

    mugfx_buffer_id index_buffer = { 0 };
    if (indices) {
        index_buffer = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_INDEX,
            .data = { indices, num_indices * sizeof(u32) },
            .debug_label = debug_label,
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
        .debug_label = debug_label,
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

static mugfx_geometry_id geometry_from_data(
    ung_geometry_data gdata, const char* debug_label = nullptr)
{
    // We cannot build a proper indexed mesh trivially, because a face will reference different
    // position, texcoord and normal indices, so you would have to generate all used combinations
    // and build new indices. It's too bothersome and will not be fast.

    auto vertices = build_vertex_buffer_data(gdata);

    const auto geom = create_geometry(
        vertices, gdata.num_vertices, gdata.indices, gdata.num_indices, debug_label);

    deallocate(vertices, gdata.num_vertices);

    return geom;
}

EXPORT ung_geometry_id ung_geometry_create_from_data(ung_geometry_data gdata)
{
    const auto geom = geometry_from_data(gdata);
    if (!geom.id) {
        ung_panicf("Error creating geometry");
    }

    const auto [id, geometry] = state->geometries.insert();
    geometry->geometry = geom;
    return { id };
}

mugfx_geometry_id load_geometry(const char* path)
{
    const auto gdata = ung_geometry_data_load(path);
    const auto geom = geometry_from_data(gdata, path);
    ung_geometry_data_destroy(gdata);
    return geom;
}

static bool reload_geometry(Geometry* geometry, const char* path)
{
    const auto geom = load_geometry(path);
    if (!geom.id) {
        return false;
    }

    mugfx_geometry_destroy(geometry->geometry);
    geometry->geometry = geom;
    return true;
}

static bool geometry_reload_cb(void* userdata)
{
    auto ctx = (GeometryReloadCtx*)userdata;
    std::fprintf(stderr, "Reloading geometry: %s\n", ctx->path.data);
    auto geometry = get(state->geometries, ctx->geometry.id);
    return reload_geometry(geometry, ctx->path.data);
}

EXPORT ung_geometry_id ung_geometry_load(const char* path)
{
    const auto geom = load_geometry(path);
    if (!geom.id) {
        ung_panicf("Error loading geometry '%s'", path);
    }

    const auto [id, geometry] = state->geometries.insert();
    geometry->geometry = geom;

    if (state->auto_reload) {
        geometry->reload_ctx = allocate<GeometryReloadCtx>();
        geometry->reload_ctx->geometry = { id };
        assign(geometry->reload_ctx->path, path);
        geometry->resource = ung_resource_create(geometry_reload_cb, geometry->reload_ctx);
        ung_resource_set_deps(geometry->resource, &path, 1, nullptr, 0);
    }

    return { id };
}

EXPORT bool ung_geometry_reload(ung_geometry_id geometry_id, const char* path)
{
    const auto geometry = get(state->geometries, geometry_id.id);

    if (state->auto_reload) {
        geometry->reload_ctx->path.free();
        assign(geometry->reload_ctx->path, path);
        ung_resource_set_deps(geometry->resource, &path, 1, nullptr, 0);
    }

    return reload_geometry(geometry, path);
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
    ung_camera_id camera, float fovy_deg, float aspect, float near, float far)
{
    const auto proj = um_mat_perspective(um_deg { fovy_deg }, aspect, near, far);
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
    auto cam = get_camera(camera.id);
    const auto view = um_mat_invert(transform::get_world_matrix(cam->transform));
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
    state->u_camera.view_inv = transform::get_world_matrix(cam->transform);
    state->u_camera.view = um_mat_invert(state->u_camera.view_inv);
    state->u_camera.view_projection = um_mat_mul(state->u_camera.projection, state->u_camera.view);
    state->u_camera.view_projection_inv = um_mat_invert(state->u_camera.view_projection);
    mugfx_uniform_data_update(state->camera_data);

    if (!target.id) {
        mugfx_set_viewport(0, 0, state->win_width, state->win_height);
    }
}

static mugfx_uniform_data_id update_uniform_data(ung_transform_id transform)
{
    if (transform.id == 0) {
        transform = state->identity_trafo;
    }
    UTransform u_trafo;
    u_trafo.model = transform::get_world_matrix(transform);
    u_trafo.model_inv = um_mat_invert(u_trafo.model);
    u_trafo.model_view = um_mat_mul(state->u_camera.view, u_trafo.model);
    u_trafo.model_view_projection = um_mat_mul(state->u_camera.projection, u_trafo.model_view);
    const auto uniform_data = transform::get_uniform_data(transform);
    std::memcpy(mugfx_uniform_data_get_ptr(uniform_data), &u_trafo, sizeof(UTransform));
    mugfx_uniform_data_update(uniform_data);
    return uniform_data;
}

EXPORT void ung_draw(ung_material_id material, ung_geometry_id geometry, ung_transform_id transform)
{
    ung_draw_instanced(material, geometry, transform, 0);
}

EXPORT void ung_draw_instanced(ung_material_id material, ung_geometry_id geometry,
    ung_transform_id transform, size_t instance_count)
{
    auto mat = get_material(material.id);
    auto geom = get(state->geometries, geometry.id);

    assert(mat->bindings[3].uniform_data.binding == 3);
    mat->bindings[3].uniform_data.id = update_uniform_data(transform);

    mugfx_draw_instanced(
        mat->material, geom->geometry, mat->bindings.data(), mat->bindings.size(), instance_count);
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
