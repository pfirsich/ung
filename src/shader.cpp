#include "state.hpp"

#include <charconv>
#include <cstdio>
#include <span>

#include <SDL.h>

namespace ung {

static std::string_view ltrim(std::string_view str)
{
    const auto p = str.find_first_not_of(" \t\n\r");
    return str.substr(p != std::string_view::npos ? p : str.size());
}

static std::string_view rtrim(std::string_view str)
{
    const auto p = str.find_last_not_of(" \t\n\r");
    return str.substr(0, p != std::string_view::npos ? p + 1 : str.size());
}

static std::string_view trim(std::string_view str)
{
    return rtrim(ltrim(str));
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

static size_t append(std::span<char> buf, size_t offset, std::string_view str)
{
    if (offset == SIZE_MAX || offset + str.size() >= buf.size()) {
        return SIZE_MAX;
    }
    memcpy(buf.data() + offset, str.data(), str.size());
    buf[offset + str.size()] = '\0';
    return offset + str.size();
}

static constexpr std::string_view UngFrame = R"(layout (binding = 0, std140) uniform UngFrame {
    vec4 time; // x: seconds since game started, y: frame counter
};
)";

static constexpr std::string_view UngPass = R"(layout (binding = 1, std140) uniform UngPass {
    mat4 view;
    mat4 view_inv;
    mat4 projection;
    mat4 projection_inv;
    mat4 view_projection;
    mat4 view_projection_inv;
    vec4 view_dimensions; // xy: size, zw: reciprocal size
};
)";

static constexpr std::string_view UngTransform
    = R"(layout (binding = 2, std140) uniform UngTransform {
    mat4 model;
    mat4 model_view;
    mat4 model_view_projection;
    mat4 normal_matrix;
};
)";

static std::string_view line_directive(u32 line_num)
{
    thread_local char buf[256];
    const auto n = snprintf(buf, sizeof(buf), "#line %u\n", line_num);
    return { buf, (size_t)n };
}

static const char* process_pragmas(std::string_view src)
{
    thread_local std::array<char, 8 * 1024> expanded;
    expanded[0] = '\0'; // clear
    size_t offset = 0;
    u32 line_num = 1;
    while (src.size()) {
        const auto nl = src.find('\n');
        auto line = ltrim(src.substr(0, nl));
        if (expect(line, "#pragma ung-")) {
            if (expect(line, "include ")) {
                const auto name = trim(line);
                if (name == "UngFrame") {
                    offset = append(expanded, offset, UngFrame);
                    offset = append(expanded, offset, line_directive(line_num + 1));
                } else if (name == "UngPass") {
                    offset = append(expanded, offset, UngPass);
                    offset = append(expanded, offset, line_directive(line_num + 1));
                } else if (name == "UngTransform") {
                    offset = append(expanded, offset, UngTransform);
                    offset = append(expanded, offset, line_directive(line_num + 1));
                } else {
                    std::printf("Unknown ung-include '%.*s'\n", (int)name.size(), name.data());
                    return nullptr;
                }
            } else {
                std::printf("Unknown ung pragma '%.*s'\n", (int)line.size(), line.data());
                return nullptr;
            }
        } else {
            if (nl == std::string_view::npos) {
                offset = append(expanded, offset, src);
            } else {
                offset = append(expanded, offset, src.substr(0, nl + 1)); // include newline!
            }
        }

        if (offset == SIZE_MAX) {
            std::printf("Shader source buffer of size %zu exhausted\n", expanded.size());
            return nullptr;
        }

        if (nl == std::string_view::npos) {
            break;
        }
        src = src.substr(nl + 1);
        line_num++;
    }
    return expanded.data();
}

EXPORT ung_shader_id ung_shader_create(mugfx_shader_create_params params)
{
    const auto source = process_pragmas(params.source);
    if (!source) {
        ung_panic("Failed to process pragmas");
    }
    params.source = source;

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
    const auto source = process_pragmas(params.source);
    if (!source) {
        return;
    }
    params.source = source;

    const auto sh = mugfx_shader_create(params);
    if (!sh.id) {
        return;
    }

    auto shader = get(state->shaders, shader_id.id);
    assert(shader->stage == params.stage);
    mugfx_shader_destroy(shader->shader);
    shader->shader = sh;
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
    char* data = ung_read_whole_file(path, &size, false);
    ung_load_profiler_pop("io");
    if (!data) {
        std::printf("Could not read '%s': %s\n", path, SDL_GetError());
        return { 0 };
    }
    const auto source = process_pragmas(std::string_view(data, size));
    ung_free_file_data(data, size);
    if (!source) {
        return { 0 };
    }
    mugfx_shader_create_params params = {
        .stage = stage,
        .source = source,
        .debug_label = path,
    };
    // TODO: Try to load from <path>.meta first
    // TODO: Check data is not SPIR-V
    if (!parse_shader_bindings(source, params)) {
        std::printf("Could not parse shader bindings\n");
        return { 0 };
    }
    ung_load_profiler_push("create");
    const auto shader = mugfx_shader_create(params);
    ung_load_profiler_pop("create");
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