#include "state.hpp"

#include <charconv>
#include <cstdio>
#include <span>

#include <SDL.h>

namespace ung {

struct ShaderPending {
    char* source_data;
    size_t source_size;

    mugfx_shader_create_params params;
    const char* error;

    void free() { deallocate(source_data, source_size); }
};

struct ShaderResource {
    ung_shader_id shader;
    Array<char> path;
    mugfx_shader_stage stage;
    ShaderPending* pending;
};

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

static void append(Formatter& fmt, std::string_view str)
{
    if (!fmt.fits(str.size())) {
        auto new_size = std::max(fmt.buf.size() * 2, fmt.buf.size() + str.size() + 1);
        auto new_data = allocate<char>(new_size);
        memcpy(new_data, fmt.buf.data(), fmt.offset + 1);
        deallocate(fmt.buf.data(), fmt.buf.size());
        fmt.buf = { new_data, new_size };
    }
    fmt.append(str);
}

static void append_line_directive(Formatter& fmt, u32 line_num)
{
    thread_local char buf[256];
    const auto n = snprintf(buf, sizeof(buf), "#line %u\n", line_num);
    append(fmt, { buf, (size_t)n });
}

static const char* nullterm(std::string_view str)
{
    thread_local char buf[512];
    assert(str.size() < sizeof(buf));
    memcpy(buf, str.data(), str.size());
    buf[str.size()] = 0;
    return buf;
}

// Returns an owning span
static std::span<char> process_pragmas(std::string_view src)
{
    const auto buf_size = src.size() + 1;
    Formatter fmt { std::span { allocate<char>(buf_size), buf_size } };
    u32 line_num = 1;
    while (src.size()) {
        const auto nl = src.find('\n');
        auto line = ltrim(src.substr(0, nl));
        if (expect(line, "#pragma ung-")) {
            if (expect(line, "include ")) {
                const auto name = trim(line);
                if (name == "UngFrame") {
                    append(fmt, UngFrame);
                    append_line_directive(fmt, line_num + 1);
                } else if (name == "UngPass") {
                    append(fmt, UngPass);
                    append_line_directive(fmt, line_num + 1);
                } else if (name == "UngTransform") {
                    append(fmt, UngTransform);
                    append_line_directive(fmt, line_num + 1);
                } else if (name.size() > 2 && name[0] == '"') {
                    const auto path = nullterm(name.substr(1, name.size() - 2));
                    ung_resource_depend_file(path);
                    size_t file_size = 0;
                    const auto file_data = ung_read_whole_file(path, &file_size, true);
                    append_line_directive(fmt, 1);
                    append(fmt, std::string_view(file_data, file_size));
                    ung_free_file_data(file_data, file_size);
                    append(fmt, "\n");
                    append_line_directive(fmt, line_num + 1);
                } else {
                    std::printf("Unknown ung-include '%.*s'\n", (int)name.size(), name.data());
                    deallocate(fmt.buf.data(), fmt.buf.size());
                    return {};
                }
            } else {
                std::printf("Unknown ung pragma '%.*s'\n", (int)line.size(), line.data());
                deallocate(fmt.buf.data(), fmt.buf.size());
                return {};
            }
        } else {
            if (nl == std::string_view::npos) {
                append(fmt, src);
            } else {
                append(fmt, src.substr(0, nl + 1)); // include newline!
            }
        }

        if (nl == std::string_view::npos) {
            break;
        }
        src = src.substr(nl + 1);
        line_num++;
    }
    return fmt.buf;
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
    // TODO: Try to load from <path>.meta first
    // TODO: Check data is not SPIR-V
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

static bool res_shader_decode(ung_resource_id self, void* instance)
{
    auto res = (ShaderResource*)instance;
    assert(!res->pending);
    res->pending = allocate<ShaderPending>();
    auto pending = res->pending;

    ung_resource_depend_file(res->path.data);

    size_t file_size = 0;
    const auto file_data = ung_read_whole_file(res->path.data, &file_size, false);
    if (!file_data) {
        // TODO: Copy SDL_Error() in here
        pending->error = "Could not read file";
        return false;
    }

    if (file_size == 0) {
        pending->error = "Shader is empty";
        return false;
    }

    const auto processed = process_pragmas(std::string_view(file_data, file_size));
    ung_free_file_data(file_data, file_size);
    if (processed.empty()) {
        pending->error = "Could not process pragmas";
        return false;
    }

    pending->source_size = processed.size();
    pending->source_data = processed.data();
    pending->params.stage = res->stage;
    pending->params.source = pending->source_data;
    pending->params.debug_label = res->path.data;

    if (!parse_shader_bindings({ pending->source_data, pending->source_size }, pending->params)) {
        pending->error = "Could not parse shader bindings";
        return false;
    }

    return true;
}

static bool res_shader_upload(ung_resource_id self, void* instance)
{
    auto res = (ShaderResource*)instance;
    auto pending = (ShaderPending*)res->pending;
    auto shader = get(state->shaders, res->shader.id);

    const auto mugfx_shader = mugfx_shader_create(pending->params);
    if (!mugfx_shader.id) {
        pending->error = "Could not create shader";
        return false;
    }

    if (shader->shader.id) {
        mugfx_shader_destroy(shader->shader);
    }
    shader->shader = mugfx_shader;

    return true;
}

static const char* res_shader_get_error(void* instance)
{
    auto res = (ShaderResource*)instance;
    return res->pending ? res->pending->error : nullptr;
}

static void res_shader_cleanup_load(ung_resource_id self, void* instance)
{
    auto res = (ShaderResource*)instance;
    if (res->pending) {
        res->pending->free();
        deallocate(res->pending);
        res->pending = nullptr;
    }
}

static void res_shader_destroy(ung_resource_id self, void* instance)
{
    auto res = (ShaderResource*)instance;
    const auto sh_id = res->shader.id;
    auto sh = get(state->shaders, sh_id);

    res->path.free();
    deallocate(res);

    if (sh->shader.id) {
        mugfx_shader_destroy(sh->shader);
    }
    state->shaders.remove(sh_id);
}

static ung_resource_type_id shader_resource()
{
    static ung_resource_type_id res_type = {};
    if (!res_type.id) {
        res_type = ung_resource_type_register({
            .type_name = "shader",
            .decode = res_shader_decode,
            .upload = res_shader_upload,
            .get_error = res_shader_get_error,
            .cleanup_load = res_shader_cleanup_load,
            .destroy = res_shader_destroy,
        });
    }
    return res_type;
}

EXPORT ung_shader_id ung_shader_create(mugfx_shader_create_params params)
{
    const auto processed = process_pragmas(params.source);
    if (processed.empty()) {
        ung_panic("Failed to process pragmas");
    }
    params.source = processed.data();

    const auto sh = mugfx_shader_create(params);
    if (!sh.id) {
        ung_panic("Failed to create shader");
    }

    deallocate(processed.data(), processed.size());

    const auto [id, shader] = state->shaders.insert();
    shader->shader = sh;
    shader->stage = params.stage;
    return { id };
}

EXPORT ung_shader_id ung_shader_load(mugfx_shader_stage stage, const char* path)
{
    char key_buf[512];
    Formatter fmt { key_buf };
    fmt.append(path);
    fmt.append("-");
    fmt.append_hex_obj((u8)stage);

    auto sh_res = allocate<ShaderResource>();
    assign(sh_res->path, path);
    sh_res->stage = stage;

    const auto [id, shader] = state->shaders.insert();
    sh_res->shader = { id };
    const auto [res, created] = ung_resource_load(shader_resource(), fmt.data(), sh_res);

    if (!created) {
        state->shaders.remove(id);
        if (sh_res->path.size) {
            sh_res->path.free();
        }
        deallocate(sh_res);
        return ((ShaderResource*)ung_resource_instance(res))->shader;
    }

    shader->resource = res;

    return { id };
}

EXPORT void ung_shader_destroy(ung_shader_id shader)
{
    auto sh = get(state->shaders, shader.id);

    if (sh->resource.id) {
        ung_resource_destroy(sh->resource);
    } else {
        mugfx_shader_destroy(sh->shader);
        state->shaders.remove(shader.id);
    }
}

EXPORT ung_resource_id ung_shader_resource(ung_shader_id id)
{
    return get(state->shaders, id.id)->resource;
}

}