#include "state.hpp"

#include <charconv>
#include <cstdio>

#include <SDL.h>

namespace ung {

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
    LoadProfScope lpscope("parse shader bindings");
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
    LoadProfScope lpscope(path);
    usize size = 0;
    ung_load_profiler_push("io");
    const auto data = ung_read_whole_file(path, &size, false);
    ung_load_profiler_pop("io");
    if (!data) {
        std::printf("Could not read '%s': %s\n", path, SDL_GetError());
        return { 0 };
    }
    mugfx_shader_create_params params = {
        .stage = stage,
        .source = data,
        .debug_label = path,
    };
    // TODO: Try to load from <path>.meta first
    // TODO: Check data is not SPIR-V
    if (!parse_shader_bindings(std::string_view(data, size), params)) {
        std::printf("Could not parse shader bindings\n");
        ung_free_file_data(data, size);
        return { 0 };
    }
    ung_load_profiler_push("create");
    const auto shader = mugfx_shader_create(params);
    ung_load_profiler_pop("create");
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

}