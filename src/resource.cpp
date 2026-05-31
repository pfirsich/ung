#include "state.hpp"
#include "types.hpp"
#include "ung.h"

#include <um.hpp>

#include <condition_variable>
#include <mutex>
#include <thread>

// Some general notes about multi-threading in here.
// The only operation run in another thread is a .find on the resources Pool and the decode
// function. From the decode function we should only expect ung_resource_load,
// ung_resource_depend_*. So we should expect insertion of new resources from any thread.
// So from decode we should only expect insertion, which should lock a mutex.
// Destroy (removal) will only happen from the main thread.
// Iteration still requires locking a mutex, because insertion might partially initialize the Pool.
// The resource key map needs to be protected as well.

namespace ung::resource {

static bool is_main_thread()
{
    static auto main_thread_id = std::this_thread::get_id();
    return std::this_thread::get_id() == main_thread_id;
}

struct DecodeThreadPool {
    struct Task {
        decltype(ung_resource_type_desc::decode) decode;
        ung_resource_id self;
    };

    Array<std::jthread> threads;
    Vector<Task> tasks;
    std::condition_variable_any tasks_cv;
    std::mutex tasks_mtx;

    void worker(std::stop_token stop);
    void start();
    void stop();
    void push(Task task);
};

struct ResourceType {
    const char* name;
    decltype(ung_resource_type_desc::decode) decode;
    decltype(ung_resource_type_desc::upload) upload;
    decltype(ung_resource_type_desc::get_error) get_error;
    decltype(ung_resource_type_desc::cleanup_load) cleanup_load;
    decltype(ung_resource_type_desc::destroy) destroy;
    FlatMap<std::string_view, ung_resource_id> map; // put this into state instead?
};

struct FileDep {
    char* path;
    u64 mtime;
};

struct Resource {
    enum class State {
        DecodeQueued,
        Decoding,
        Decoded,
        Error,
        Ready, // reload can be triggered again
    };

    ResourceType* type;
    char* key;
    void* instance;
    u32 version;
    bool ready;
    Vector<FileDep> file_deps;
    Vector<ung_resource_id> res_deps;
    Vector<ung_resource_id> dependents;
    bool needs_reload;
    std::atomic<u32> refcount;

    // written from decode thread
    std::atomic<State> pending_state;
    Vector<FileDep> pending_file_deps;
    Vector<ung_resource_id> pending_res_deps;
};

struct State {
    Pool<ResourceType> resource_types;
    Pool<Resource> resources;
    std::mutex resource_mtx;
    std::condition_variable decoded_cv;
    DecodeThreadPool decode_pool;
    float next_reload_check;
};

State* state = nullptr;

struct ResourceLock {
    std::unique_lock<std::mutex> lock;
    ResourceLock() : lock(state->resource_mtx) { }
};

static auto& current_resource_stack()
{
    thread_local StaticVector<Resource*, 8> stack;
    return stack;
}

static Resource* current_resource()
{
    const auto size = current_resource_stack().size();
    return size ? current_resource_stack()[size - 1] : nullptr;
}

static void push_current_resource(Resource* res)
{
    current_resource_stack().append() = res;
}

static void pop_current_resource()
{
    current_resource_stack().pop();
}

void DecodeThreadPool::worker(std::stop_token stop)
{
    while (!stop.stop_requested()) {
        Task task;
        {
            std::unique_lock lock(tasks_mtx);
            const auto has_task = tasks_cv.wait(lock, stop, [this] { return tasks.size > 0; });
            if (!has_task) { // stop requested
                return;
            }

            task = std::move(tasks[0]);
            remove(tasks, 0);
        }

        void* instance = nullptr;
        {
            ResourceLock lock;
            const auto res = state->resources.find(task.self.id);
            if (!res) {
                continue; // likely destroyed
            }
            // Do not modify state if this is a reload.
            res->pending_state = Resource::State::Decoding;
            instance = res->instance;
            push_current_resource(res);
        }

        auto decoded = task.decode(task.self, instance);

        {
            pop_current_resource();
            ResourceLock lock;
            const auto res = state->resources.find(task.self.id);
            if (!res) {
                continue; // likely destroyed
            }
            res->pending_state = decoded ? Resource::State::Decoded : Resource::State::Error;
            state->decoded_cv.notify_all(); // only one thread should be waiting
        }
    }
}

void DecodeThreadPool::start()
{
    const auto num_threads = um::max(1u, std::thread::hardware_concurrency());
    threads.init(num_threads);
    tasks.init(64);
    for (u32 i = 0; i < num_threads; ++i) {
        threads[i] = std::jthread([this](std::stop_token st) { worker(st); });
    }
}

void DecodeThreadPool::stop()
{
    threads.free(); // calls ~jthread
}

void DecodeThreadPool::push(Task task)
{
    {
        std::lock_guard lock(tasks_mtx);
        tasks.push(std::move(task));
    }
    tasks_cv.notify_one();
}

static const char* resource_name(Resource& res)
{
    return res.key ? res.key : res.type->name;
}

static Resource& get_res(ung_resource_id res)
{
    return *get(state->resources, res.id);
}

static void wait_for_decode(ResourceLock& lock, Resource& res)
{
    // This is just insurance, should not be needed.
    if (res.pending_state == Resource::State::Ready) {
        return;
    }
    state->decoded_cv.wait(lock.lock, [&res]() {
        const auto state = res.pending_state.load();
        return state == Resource::State::Decoded || state == Resource::State::Error;
    });
}

static void free(Vector<FileDep>& file_deps)
{
    for (u32 i = 0; i < file_deps.size; ++i) {
        deallocate(file_deps[i].path);
    }
    file_deps.free();
}

static void free(Vector<ung_resource_id>& res_deps)
{
    for (auto& dep : res_deps) {
        ung_resource_decref(dep);
    }
    res_deps.free();
}

static void update_dependencies(ung_resource_id id, Resource& res)
{
    for (u32 i = 0; i < res.res_deps.size; ++i) {
        get_res(res.res_deps[i]).dependents.push(id);
    }
}

static void remove_deps(ung_resource_id id, Resource& res)
{
    free(res.file_deps);

    for (auto& dep : res.res_deps) {
        remove_v(get_res(dep).dependents, id);
    }
    free(res.res_deps);
}

static void destroy(ResourceLock& lock, ung_resource_id id, Resource& res)
{
    assert(is_main_thread());
    assert(res.refcount == 0); // this might race, but it helps and it's just a debug assert

    if (res.key) {
        res.type->map.remove(res.key);
        deallocate_string(res.key);
    }

    // This will destroy the instance, which the decode thread might be using right now, so we need
    // to wait. If we are uninitialized yet, decoding has not started and we don't need to wait (and
    // it cannot start, because it needs the mutex).
    if (res.pending_state == Resource::State::Decoding) {
        wait_for_decode(lock, res);
    }

    // Relax mutex for callbacks (to avoid deadlocks)
    lock.lock.unlock();
    if (res.type->cleanup_load) {
        res.type->cleanup_load(id, res.instance);
    }
    if (res.type->destroy) {
        res.type->destroy(id, res.instance);
    }
    // We can run this without a lock, because we waited for decode to finish.
    remove_deps(id, res);
    free(res.pending_file_deps);
    free(res.pending_res_deps);
    lock.lock.lock();

    state->resources.remove(id.id);
}

static uint32_t decref(ResourceLock& lock, ung_resource_id id, Resource& res)
{
    const auto count = --res.refcount;
    if (count == 0) {
        destroy(lock, id, res);
    }
    return count;
}

static void commit_deps(ung_resource_id id, Resource& res)
{
    // We have already increased the refcount for all deps in pending_res_deps.
    // So we just decrease for the commited deps (remove_deps) and copy over.

    remove_deps(id, res);

    res.file_deps = res.pending_file_deps;
    res.pending_file_deps = {};
    res.res_deps = res.pending_res_deps;
    res.pending_res_deps = {};
    update_dependencies(id, res);
}

static void upload(ung_resource_id id, Resource& res)
{
    assert(res.pending_state.load() == Resource::State::Decoded);
    if (res.type->upload) {
        push_current_resource(&res);
        const auto success = res.type->upload(id, res.instance);
        pop_current_resource();

        if (!success) {
            if (res.ready) {
                fprintf(stderr, "Upload failed for '%s': %s\n", resource_name(res),
                    res.type->get_error(res.instance));
            } else {
                ung_panicf("Upload failed for '%s': %s", resource_name(res),
                    res.type->get_error(res.instance));
            }
            return;
        }
    }

    commit_deps(id, res);
    res.version++;
    res.pending_state = Resource::State::Ready;
    res.ready = true;
}

static void finish_load(ung_resource_id id, Resource& res)
{
    assert(is_main_thread());
    const auto rstate = res.pending_state.load();
    if (rstate == Resource::State::Decoded || rstate == Resource::State::Error) {
        if (rstate == Resource::State::Decoded) {
            upload(id, res);
        } else if (rstate == Resource::State::Error) {
            if (res.ready) {
                fprintf(stderr, "Decode failed for '%s': %s\n", resource_name(res),
                    res.type->get_error(res.instance));
            } else {
                ung_panicf("Decode failed for '%s': %s", resource_name(res),
                    res.type->get_error(res.instance));
            }
        }

        if (res.type->cleanup_load) {
            res.type->cleanup_load(id, res.instance);
        }

        // Throw pending deps away if they were not commited
        free(res.pending_file_deps);
        free(res.pending_res_deps);

        // We use this pending state to signal that we can trigger reloads
        res.pending_state = Resource::State::Ready;
    }
}

static void mark_needs_reload(Resource& res)
{
    res.needs_reload = true;
    for (u32 d = 0; d < res.dependents.size; ++d) {
        mark_needs_reload(get_res(res.dependents[d]));
    }
}

static bool is_up_to_date(const Resource& res)
{
    return !res.needs_reload && res.pending_state == Resource::State::Ready;
}

static bool deps_up_to_date(const Resource& res)
{
    for (u32 d = 0; d < res.res_deps.size; ++d) {
        if (!is_up_to_date(get_res(res.res_deps[d]))) {
            return false;
        }
    }
    return true;
}

static void start_load(ung_resource_id id, Resource& res)
{
    res.pending_file_deps.init(1);
    res.pending_res_deps.init(1);

    if (res.type->decode) {
        res.pending_state = Resource::State::DecodeQueued;
        if (ung::state->async_decode) {
            state->decode_pool.push({ res.type->decode, { id } });
        } else {
            res.pending_state = Resource::State::Decoding;
            push_current_resource(&res);
            auto decoded = res.type->decode(id, res.instance);
            pop_current_resource();
            res.pending_state = decoded ? Resource::State::Decoded : Resource::State::Error;
        }
    } else {
        // We don't do upload here, because start_load might be called from ung_resource_load, which
        // could be called from any thread.
        res.pending_state = Resource::State::Decoded;
    }
    res.needs_reload = false;
}

void init(ung_init_params params)
{
    assert(!state);
    state = allocate<State>();

    params.max_num_resource_types
        = params.max_num_resource_types ? params.max_num_resource_types : 16;

    if (!params.max_num_resources) {
        params.max_num_resources = params.max_num_textures + params.max_num_shaders
            + params.max_num_geometries + params.max_num_materials + params.max_num_sound_sources;
    }

    state->resource_types.init(params.max_num_resource_types);
    state->resources.init(params.max_num_resources);

    state->decode_pool.start();
}

static void check_reload()
{
    ResourceLock lock;
    for (u32 i = 0; i < state->resources.keys.size; ++i) {
        if (state->resources.contains(state->resources.keys[i])) {
            auto& res = state->resources.data[i];
            for (u32 f = 0; f < res.file_deps.size; ++f) {
                const auto mtime = ung_file_get_mtime(res.file_deps[f].path);
                if (res.file_deps[f].mtime != mtime) {
                    printf("File changed: %s\n", res.file_deps[f].path);
                    res.file_deps[f].mtime = mtime;
                    mark_needs_reload(res);
                }
            }
        }
    }

    for (u32 i = 0; i < state->resources.keys.size; ++i) {
        if (state->resources.contains(state->resources.keys[i])) {
            auto& res = state->resources.data[i];
            if (res.needs_reload && res.pending_state == Resource::State::Ready
                && deps_up_to_date(res)) {
                printf("Reloading: %s\n", resource_name(res));
                start_load({ state->resources.get_key(i) }, res);
                // Reload at most one resource per frame
                break;
            }
        }
    }
}

static void finish_resources()
{
    // Some resources are reloaded and then never waited on again (e.g. shaders), so we have to
    // finish them here.
    StaticVector<ung_resource_id, 64> finish_resources = {};
    {
        // decode might insert into resources, so when we iterate them, we need a lock.
        ResourceLock lock;
        for (u32 i = 0; i < state->resources.keys.size; ++i) {
            finish_resources.append() = { state->resources.get_key(i) };
            if (finish_resources.size() == finish_resources.capacity()) {
                break; // let's get them next time
            }
        }
    }
    for (auto& res_id : finish_resources) {
        // It might have been destroyed by an earlier iteration of finish_load
        if (auto res = state->resources.find(res_id.id)) {
            finish_load(res_id, *res);
        }
    }
}

void begin_frame()
{
    assert(is_main_thread());

    if (ung::state->auto_reload && state->next_reload_check <= ung_get_time()) {
        state->next_reload_check = ung_get_time();
        check_reload();
    }

    // We finish resources even when auto_reload is false, because swapping resources might trigger
    // reloads of dependents.
    finish_resources();
}

void shutdown()
{
    state->decode_pool.stop();
}

EXPORT ung_resource_type_id ung_resource_type_register(ung_resource_type_desc desc)
{
    assert(is_main_thread());
    const auto [id, type] = state->resource_types.insert();
    type->name = desc.type_name;
    type->decode = desc.decode;
    type->upload = desc.upload;
    type->get_error = desc.get_error;
    type->cleanup_load = desc.cleanup_load;
    type->destroy = desc.destroy;
    type->map.init(256);
    return { id };
}

EXPORT ung_resource_load_result ung_resource_load(
    ung_resource_type_id res_type, const char* key, void* instance)
{
    ResourceLock lock;
    const auto type = state->resource_types.find(res_type.id);
    assert(type);
    if (key) {
        if (const auto id = type->map.find(key)) {
            if (current_resource()) {
                ung_resource_depend_res(*id);
            } else {
                get_res(*id).refcount++;
            }
            return { *id, false };
        }
    }
    const auto [id, res] = state->resources.insert();
    res->type = type;
    res->instance = instance;
    res->key = key ? allocate_string(key) : nullptr;
    res->pending_state = Resource::State::DecodeQueued;
    res->version = 0;
    res->dependents.init(1);
    if (key) {
        type->map.insert(res->key, { id });
    }

    res->refcount.store(0);
    if (current_resource()) {
        ung_resource_depend_res({ id });
    } else {
        res->refcount.store(1);
    }

    start_load({ id }, *res);
    return { { id }, true };
}

EXPORT void* ung_resource_instance(ung_resource_id res)
{
    return get_res(res).instance;
}

EXPORT ung_resource_id ung_resource_get(ung_resource_type_id res_type, const char* key)
{
    ResourceLock lock; // only for ResourceType::map
    const auto type = get(state->resource_types, res_type.id);
    const auto res = type->map.find(key);
    return res ? *res : ung_resource_id {};
}

EXPORT uint32_t ung_resource_incref(ung_resource_id id)
{
    return ++get_res(id).refcount;
}

EXPORT uint32_t ung_resource_decref(ung_resource_id id)
{
    ResourceLock lock;
    return decref(lock, id, get_res(id));
}

EXPORT void ung_resource_destroy(ung_resource_id id)
{
    ResourceLock lock;
    auto& res = get_res(id);
    [[maybe_unused]] const auto count = --res.refcount;
    assert(count == 0);
    destroy(lock, id, res);
}

EXPORT void ung_resource_depend_file(const char* path)
{
    assert(current_resource());
    current_resource()->pending_file_deps.push({
        allocate_string(path),
        ung_file_get_mtime(path),
    });
}

EXPORT void ung_resource_depend_res(ung_resource_id res)
{
    assert(current_resource());
    auto cur_res = current_resource();
    if (find(cur_res->pending_res_deps, res) == UINT32_MAX) {
        get_res(res).refcount++;
        cur_res->pending_res_deps.push(res);
    }
}

EXPORT uint32_t ung_resource_version(ung_resource_id res)
{
    return get_res(res).version;
}

EXPORT void ung_resource_wait_ready(ung_resource_id id)
{
    assert(is_main_thread());

    auto& res = get_res(id);

    // If a reload is in progress, let's upload/commit now
    finish_load(id, res);

    // If we were ever ready, we don't have to wait anymore
    if (res.ready) {
        return;
    } else {
        ResourceLock lock;
        wait_for_decode(lock, res);
    }

    finish_load(id, res);
}

static void replace_dependents(
    Vector<ung_resource_id> dependencies, ung_resource_id find, ung_resource_id replace)
{
    for (auto dep_id : dependencies) {
        auto& dep = get_res(dep_id);
        remove_v(dep.dependents, find);
        dep.dependents.push(replace);
    }
}

EXPORT void ung_resource_swap(ung_resource_id dst_id, ung_resource_id src_id)
{
    assert(is_main_thread());

    // We need to wait for pending reloads to be done
    ung_resource_wait_ready(dst_id);
    ung_resource_wait_ready(src_id);

    ResourceLock lock;
    auto& dst = get_res(dst_id);
    auto& src = get_res(src_id);

    assert(dst.pending_state == Resource::State::Ready);
    assert(src.pending_state == Resource::State::Ready);
    assert(dst.type == src.type);

    std::swap(dst.key, src.key);
    std::swap(dst.instance, src.instance);
    src.version++;
    dst.version++;
    std::swap(dst.ready, src.ready);
    std::swap(dst.file_deps, src.file_deps);
    std::swap(dst.res_deps, src.res_deps);
    // don't swap dependents. those belong to the id!
    replace_dependents(dst.res_deps, src_id, dst_id);
    replace_dependents(src.res_deps, dst_id, src_id);
    std::swap(dst.needs_reload, src.needs_reload);
    // keep refcount!
    // all pending stuff should be empty/not in use, because we waited for decode

    // dst.key was previously src.key and referred to src_id! we need to fix it up.
    if (src.key) {
        *dst.type->map.find(src.key) = src_id;
    }
    if (dst.key) {
        *dst.type->map.find(dst.key) = dst_id;
    }

    for (auto id : src.dependents) {
        mark_needs_reload(get_res(id));
    }

    for (auto id : dst.dependents) {
        mark_needs_reload(get_res(id));
    }
}

}
