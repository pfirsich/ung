#include "types.hpp"

#include <cstdio>
#include <cstdlib>

#define EXPORT extern "C"

namespace ung::files {
struct Watch {
    Array<Array<char>> paths;
    Array<uint64_t> last_mtime;
    ung_file_watch_cb cb;
    void* ctx;
};

struct State {
    Pool<Watch> watches;
    float next_check;
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
    if (state->next_check > ung_get_time()) {
        return;
    }

    for (u32 w = 0; w < state->watches.keys.size; ++w) {
        if (state->watches.contains(state->watches.keys[w])) {
            auto& watch = state->watches.data[w];
            for (u32 p = 0; p < watch.paths.size; ++p) {
                const auto mtime = ung_file_get_mtime(watch.paths[p].data);
                if (mtime > watch.last_mtime[p]) {
                    watch.cb(watch.ctx, watch.paths[p].data);
                    watch.last_mtime[p] = mtime;
                }
            }
        }
    }

    state->next_check = ung_get_time() + 2.0f;
}

EXPORT ung_file_watch_id ung_file_watch_create(
    const char* const* paths, size_t num_paths, ung_file_watch_cb cb, void* ctx)
{
    const auto [id, watch] = state->watches.insert();

    watch->paths.init((u32)num_paths);
    watch->last_mtime.init((u32)num_paths);
    for (u32 i = 0; i < (u32)num_paths; ++i) {
        watch->paths[i].init((uint32_t)std::strlen(paths[i]) + 1);
        std::strcpy(watch->paths[i].data, paths[i]);
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
        watch->paths[p].free();
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