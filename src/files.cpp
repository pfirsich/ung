#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "state.hpp"
#include "types.hpp"

namespace ung::files {
struct Watch {
    Array<char*> paths;
    Array<uint64_t> last_mtime;
    ung_file_watch_cb cb;
    void* ctx;
};

struct Resource {
    ung_resource_reload_cb cb;
    void* ctx;
    Array<char*> files_deps;
    ung_file_watch_id file_watch;
    Vector<ung_resource_id> res_deps;
    Vector<ung_resource_id> dependent_resources;
    uint32_t version;
};

struct State {
    Pool<Watch> watches;
    float next_file_watch_check;
    Pool<Resource> resources;
};

State* state;

void init(ung_init_params params)
{
    assert(!state);
    state = allocate<State>();
    std::memset(state, 0, sizeof(State));

    state->watches.init(params.max_num_file_watches ? params.max_num_file_watches : 128);
    if (ung::state->auto_reload) {
        if (!params.max_num_resources) {
            params.max_num_resources = params.max_num_textures + params.max_num_shaders
                + params.max_num_geometries + params.max_num_materials;
        }
        state->resources.init(params.max_num_resources);
    }
}

void shutdown()
{
    if (!state) {
        return;
    }

    for (u32 i = 0; i < state->watches.keys.size; ++i) {
        if (state->watches.contains(state->watches.keys[i])) {
            ung_file_watch_destroy({ state->watches.keys[i] });
        }
    }
    state->watches.free();

    deallocate(state, 1);
    state = nullptr;
}

void begin_frame()
{
    if (state->next_file_watch_check > ung_get_time()) {
        return;
    }

    for (u32 w = 0; w < state->watches.keys.size; ++w) {
        if (state->watches.contains(state->watches.keys[w])) {
            auto& watch = state->watches.data[w];
            for (u32 p = 0; p < watch.paths.size; ++p) {
                const auto mtime = ung_file_get_mtime(watch.paths[p]);
                // File might also have been replaced with an older file, so we use !=
                if (mtime != watch.last_mtime[p]) {
                    watch.cb(watch.ctx, watch.paths[p]);
                    watch.last_mtime[p] = mtime;
                }
            }
        }
    }

    state->next_file_watch_check = ung_get_time() + 0.5f;
}

static std::string_view trim(std::string_view str)
{
    constexpr std::string_view ws = " \t\n\r\f\v";

    const auto start = str.find_first_not_of(ws);
    if (start == std::string_view::npos) { // the view is entirely whitespace
        return {};
    }
    str.remove_prefix(start);

    const auto end = str.find_last_not_of(ws);
    str.remove_suffix(str.size() - end - 1);

    return str;
}

EXPORT size_t ung_parse_kv_file(
    const char* pdata, size_t size, ung_kv_pair* pairs, size_t max_num_pairs)
{
    std::string_view section;
    size_t p = 0;

    std::string_view data(pdata, size);
    while (data.size()) {
        const auto nl = data.find_first_of("\n");
        auto line = trim(data.substr(0, nl));

        if (nl == std::string_view::npos) {
            data = data.substr(data.size());
        } else {
            data = data.substr(nl + 1);
        }

        if (line.empty() || line[0] == '#') {
            continue;
        }

        if (line[0] == '[') { // section
            const auto end = line.find(']');
            section = line.substr(1, end - 1);
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            ung_panicf("Missing '=' in '%.*s'", (int)line.size(), line.data());
        }

        const auto name = trim(line.substr(0, eq));
        const auto value = trim(line.substr(eq + 1));
        pairs[p++] = {
            .section = { section.data(), section.size() },
            .key = { name.data(), name.size() },
            .value = { value.data(), value.size() },
        };
    }

    return p;
}

static bool is_skip_char(char c)
{
    return c == ' ' || c == '\t' || c == ',';
}

EXPORT bool ung_parse_float(ung_string str, float* fptr, size_t num)
{
    size_t cursor = 0;
    for (size_t n = 0; n < num; ++n) {
        if (cursor >= str.length) {
            return false;
        }

        const auto [ptr, ec]
            = std::from_chars(str.data + cursor, str.data + str.length - cursor, fptr[n]);
        if (ec != std::errc()) {
            return false;
        }

        assert(ptr >= str.data);
        const auto val_len = (size_t)(ptr - (str.data + cursor));
        assert(val_len <= str.length);
        cursor += val_len;

        while (cursor < str.length && is_skip_char(str.data[cursor])) {
            cursor++;
        }
    }
    return true;
}

EXPORT ung_file_watch_id ung_file_watch_create(
    const char* const* paths, size_t num_paths, ung_file_watch_cb cb, void* ctx)
{
    const auto [id, watch] = state->watches.insert();

    watch->paths.init((u32)num_paths);
    watch->last_mtime.init((u32)num_paths);
    for (u32 i = 0; i < (u32)num_paths; ++i) {
        watch->paths[i] = allocate_string(paths[i]);
        watch->last_mtime[i] = ung_file_get_mtime(paths[i]);
    }
    watch->cb = cb;
    watch->ctx = ctx;

    return { id };
}

EXPORT void ung_file_watch_destroy(ung_file_watch_id watch_id)
{
    auto watch = get(state->watches, watch_id.id);

    for (u32 p = 0; p < watch->paths.size; ++p) {
        deallocate_string(watch->paths[p]);
    }
    watch->paths.free();
    watch->last_mtime.free();
}

EXPORT ung_resource_id ung_resource_create(ung_resource_reload_cb cb, void* ctx)
{
    if (!ung::state->auto_reload) {
        return { 0 };
    }

    const auto [id, res] = state->resources.insert();
    res->cb = cb;
    res->ctx = ctx;
    return { id };
}

static void free_files_deps(Resource* res)
{
    if (res->files_deps.size) {
        for (u32 f = 0; f < res->files_deps.size; ++f) {
            deallocate_string(res->files_deps[f]);
        }
        res->files_deps.free();
        ung_file_watch_destroy(res->file_watch);
    }
}

static void reload(Resource* res)
{
    if (!res->cb(res->ctx)) {
        return;
    }
    res->version++;
    for (u32 r = 0; r < res->dependent_resources.size; ++r) {
        auto dep = get(state->resources, res->dependent_resources[r].id);
        reload(dep);
    }
}

static void resource_file_watch_cb(void* ctx, const char* changed_path)
{
    std::fprintf(stderr, "File changed: %s\n", changed_path);
    reload((Resource*)ctx);
}

static void undepend(ung_resource_id id, Resource* res)
{
    // First remove ourselves from dependent resources
    for (u32 r = 0; r < res->res_deps.size; ++r) {
        auto dep = get(state->resources, res->res_deps[r].id);
        remove_v(dep->dependent_resources, id);
    }
    res->res_deps.clear();

    // Remove us from dependents
    for (u32 r = 0; r < res->dependent_resources.size; ++r) {
        const auto dep = get(state->resources, res->dependent_resources[r].id);
        remove_v(dep->res_deps, id);
    }
    res->dependent_resources.clear();
}

static void depend(ung_resource_id dependent_id, Resource* dependent, ung_resource_id dependency_id)
{
    dependent->res_deps.push(dependency_id);
    auto dependency = get(state->resources, dependency_id.id);
    dependency->dependent_resources.push(dependent_id);
}

EXPORT void ung_resource_set_deps(ung_resource_id resource_id, const char* const* files_deps,
    size_t num_files_deps, const ung_resource_id* res_deps, size_t num_res_deps)
{
    if (!ung::state->auto_reload) {
        return;
    }

    auto res = get(state->resources, resource_id.id);

    free_files_deps(res);

    if (num_files_deps) {
        res->files_deps.init((u32)num_files_deps);
        for (u32 f = 0; f < res->files_deps.size; ++f) {
            res->files_deps[f] = allocate_string(files_deps[f]);
        }
        res->file_watch = ung_file_watch_create(
            res->files_deps.data, res->files_deps.size, resource_file_watch_cb, res);
    }

    undepend(resource_id, res);

    if (num_res_deps) {
        res->res_deps.init((u32)num_res_deps);
        for (u32 r = 0; r < num_res_deps; ++r) {
            depend(resource_id, res, res_deps[r]);
        }
    }
}

EXPORT uint32_t ung_resource_get_version(ung_resource_id resource_id)
{
    if (!ung::state->auto_reload) {
        return 0;
    }
    auto res = get(state->resources, resource_id.id);
    return res->version;
}

EXPORT void ung_resource_destroy(ung_resource_id resource_id)
{
    if (!ung::state->auto_reload) {
        return;
    }

    auto res = get(state->resources, resource_id.id);

    undepend(resource_id, res);

    free_files_deps(res);
    res->files_deps.free();
    res->res_deps.free();
    res->dependent_resources.free();
}

EXPORT ung_resource_id ung_shader_get_resource(ung_shader_id shader_id)
{
    const auto sh = get(ung::state->shaders, shader_id.id);
    return sh->resource;
}

EXPORT ung_resource_id ung_texture_get_resource(ung_texture_id texture_id)
{
    const auto t = get(ung::state->textures, texture_id.id);
    return t->resource;
}

EXPORT ung_resource_id ung_geometry_get_resource(ung_geometry_id geometry_id)
{
    const auto g = get(ung::state->geometries, geometry_id.id);
    return g->resource;
}

EXPORT ung_resource_id ung_material_get_resource(ung_material_id material_id)
{
    const auto m = get(ung::state->materials, material_id.id);
    return m->resource;
}

}

#if defined(__EMSCRIPTEN__) // WASM
EXPORT std::uint64_t ung_file_get_mtime(const char* /*path*/)
{
    return 0;
}

#elif defined(_WIN32) // Windows
// ------------------ Windows -------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

EXPORT uint64_t ung_file_get_mtime(const char* path)
{
    assert(path);

    // Convert UTF-8 -> UTF-16 without heap allocation.
    // Typical MAX_PATH is 260; 1024 gives headroom. Increase if you need very long paths.
    std::array<wchar_t, 1024> wbuf;

    const auto wlen
        = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wbuf.data(), wbuf.size());
    if (wlen <= 0 || wlen > wbuf.size()) {
        return 0;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(wbuf.data(), GetFileExInfoStandard, &fad)) {
        return 0;
    }

    ULARGE_INTEGER u {};
    u.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    u.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    return u.QuadPart; // 100-ns ticks since 1601-01-01 (UTC)
}

#else // POSIX
#include <sys/stat.h>

EXPORT uint64_t ung_file_get_mtime(const char* path)
{
    assert(path);

    struct stat st {};
    if (stat(path, &st) != 0) {
        return 0;
    }

#if defined(__APPLE__) || defined(__MACH__)
    const auto s = (uint64_t)(st.st_mtimespec.tv_sec);
    const auto ns = (uint64_t)(st.st_mtimespec.tv_nsec);
#else
    const auto s = (uint64_t)(st.st_mtim.tv_sec);
    const auto ns = (uint64_t)(st.st_mtim.tv_nsec);
#endif

    return s * 1000ULL + ns / 1000000ULL; // ms since UNIX epoch
}
#endif
