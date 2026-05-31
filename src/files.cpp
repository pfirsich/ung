#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <SDL.h>

#include "state.hpp"

namespace ung::files {
struct Watch {
    Array<char*> paths;
    Array<uint64_t> last_mtime;
    ung_file_watch_cb cb;
    void* ctx;
};

struct State {
    Pool<Watch> watches;
    float next_file_watch_check;
};

State* state;

void init(ung_init_params params)
{
    assert(!state);
    state = allocate<State>();
    std::memset(state, 0, sizeof(State));

    state->watches.init(params.max_num_file_watches ? params.max_num_file_watches : 128);
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

EXPORT uint32_t ung_parse_kv_file(
    const char* pdata, size_t size, ung_kv_pair* pairs, usize max_num_pairs)
{
    std::string_view section;
    uint32_t p = 0;

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

template <typename T>
bool parse(ung_string str, T* ptr, usize num)
{
    size_t cursor = 0;
    for (usize n = 0; n < num; ++n) {
        if (cursor >= str.length) {
            return false;
        }

        const auto [p, ec]
            = std::from_chars(str.data + cursor, str.data + str.length - cursor, ptr[n]);
        if (ec != std::errc()) {
            return false;
        }

        assert(p >= str.data);
        const auto val_len = (size_t)(p - (str.data + cursor));
        assert(val_len <= str.length);
        cursor += val_len;

        while (cursor < str.length && is_skip_char(str.data[cursor])) {
            cursor++;
        }
    }
    return true;
}

EXPORT bool ung_parse_float(ung_string str, float* ptr, usize num)
{
    return parse(str, ptr, num);
}

EXPORT bool ung_parse_int(ung_string str, int64_t* ptr, usize num)
{
    return parse(str, ptr, num);
}

EXPORT ung_file_watch_id ung_file_watch_create(
    const char* const* paths, usize num_paths, ung_file_watch_cb cb, void* ctx)
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
