#include "types.hpp"

#include <cstdio>
#include <cstdlib>

#include <miniaudio.h>

#include "um.h"

/*
 * I realize this seems much more complicated than it could be, but I go through all this trouble
 * so I can avoid initializing sounds as much as possible.
 * One principle of my engine is to not require allocations in the game's main loop.
 * Unfortunately it is not possible to do this with an interface like ung has it (sources + sounds).
 * See here: https://github.com/mackron/miniaudio/discussions/1025
 * So intead of avoiding allocations altogether I try to reduce them.
 * I keep a list per-source of idle sounds and a global last recently used list of sounds.
 * and whenever I want to play a sound, I take sounds from those.
 * Loaded sounds are reused as much as possible and at some point it should converge so that
 * no ung_sound_play should allocate.
 */

namespace ung::sound {
struct Sound;

struct SoundSource {
    ma_sound sound;
    Array<char> path;
    // This represents a list of idle sounds for this source.
    // A sound in this list, should be in sounds_idle_lru as well (and vice-versa)!
    Sound* source_idle_head;
    u32 flags;
    u8 group;
};

struct Sound {
    ma_sound sound;
    SoundSource* source;
    Sound* source_idle_next;

    Sound* sounds_idle_lru_next;
    Sound* sounds_idle_lru_prev;

    Sound* free_sounds_next;

    // in_use is true if the sound is not idle.
    // in_use is true when the user is possibly currently keeping a ung_sound_id handle,
    // either because the sound is playing or paused (but not stopped).
    // when in_use is false, the sound should be in the free list or in both idle lists
    // (source and lru).
    uint32_t generation;
    bool in_use; // !idle
};

struct State {
    ma_engine sound_engine;
    Pool<SoundSource> sound_sources;
    Array<Sound> sounds;
    Array<ma_sound_group> sound_groups;

    // These (and sounds_idle_lru_next/sounds_idle_lru_next in Sound) represent a list of idle
    // sounds, sorted by most recent usage.
    Sound* sounds_idle_lru_head; // most recently used
    Sound* sounds_idle_lru_tail; // least recently used

    // This list (see Sound::free_sounds_next) keeps track of all sounds that do not have
    // a source yet (or anymore). This is a separate list, so we can always prioritize
    // sounds that don't have a source over those that do.
    Sound* free_sounds_head; // unused sounds (remove only)
};

State* state;

static void sounds_idle_lru_remove(Sound* sound)
{
    // Unlink from prev
    if (sound->sounds_idle_lru_prev) {
        sound->sounds_idle_lru_prev->sounds_idle_lru_next = sound->sounds_idle_lru_next;
    } else if (state->sounds_idle_lru_head == sound) {
        state->sounds_idle_lru_head = sound->sounds_idle_lru_next;
    }

    // Unlink from next
    if (sound->sounds_idle_lru_next) {
        sound->sounds_idle_lru_next->sounds_idle_lru_prev = sound->sounds_idle_lru_prev;
    } else if (state->sounds_idle_lru_tail == sound) {
        state->sounds_idle_lru_tail = sound->sounds_idle_lru_prev;
    }

    sound->sounds_idle_lru_prev = nullptr;
    sound->sounds_idle_lru_next = nullptr;
}

static void sounds_idle_lru_push_front(Sound* sound)
{
    sound->sounds_idle_lru_prev = nullptr;
    sound->sounds_idle_lru_next = state->sounds_idle_lru_head;

    if (state->sounds_idle_lru_head) {
        state->sounds_idle_lru_head->sounds_idle_lru_prev = sound;
    } else {
        // list was empty
        state->sounds_idle_lru_tail = sound;
    }

    state->sounds_idle_lru_head = sound;
}

static Sound* sounds_idle_lru_pop_back()
{
    auto sound = state->sounds_idle_lru_tail;
    if (sound) {
        assert(!sound->in_use);
        sounds_idle_lru_remove(sound);
    }
    return sound;
}

static void source_idle_push(SoundSource* source, Sound* sound)
{
    sound->source_idle_next = source->source_idle_head;
    source->source_idle_head = sound;
}

static Sound* source_idle_pop(SoundSource* source)
{
    auto sound = source->source_idle_head;
    if (sound) {
        source->source_idle_head = sound->source_idle_next;
        sound->source_idle_next = nullptr;
    }
    return sound;
}

static void source_idle_remove(SoundSource* source, Sound* sound)
{
    Sound* cur = source->source_idle_head;
    Sound* prev = nullptr;

    while (cur && cur != sound) {
        prev = cur;
        cur = cur->source_idle_next;
    }

    if (!cur) {
        // not found (possible because we might be calling this for a playing sound in
        // ung_sound_source_destroy)
        return;
    }

    assert(cur == sound);
    if (prev) {
        prev->source_idle_next = sound->source_idle_next;
    } else {
        assert(sound == source->source_idle_head);
        source->source_idle_head = sound->source_idle_next;
    }

    sound->source_idle_next = nullptr;
}

void init(ung_init_params params)
{
    assert(!state);
    state = allocate<State>();
    std::memset(state, 0, sizeof(State));

    auto ma_res = ma_engine_init(nullptr, &state->sound_engine);
    if (ma_res != MA_SUCCESS) {
        std::fprintf(
            stderr, "Could not initialize audio engine: %s\n", ma_result_description(ma_res));
        std::exit(1);
    }

    state->sound_sources.init(params.max_num_sound_sources ? params.max_num_sound_sources : 64);

    state->sounds.init(params.max_num_sounds ? params.max_num_sounds : 64);
    for (u32 i = 0; i < state->sounds.size - 1; ++i) {
        state->sounds[i].free_sounds_next = &state->sounds[i + 1];
    }
    state->free_sounds_head = &state->sounds[0];

    state->sound_groups.init(params.num_sound_groups ? params.num_sound_groups : 4);
    for (u32 i = 0; i < state->sound_groups.size; ++i) {
        ma_res = ma_sound_group_init(&state->sound_engine, 0, nullptr, &state->sound_groups[i]);
        if (ma_res != MA_SUCCESS) {
            std::fprintf(
                stderr, "Could not initialize sound group: %s\n", ma_result_description(ma_res));
            std::exit(1);
        }
    }
}

static void sound_set_idle(Sound* sound)
{
    sound->in_use = false;
    sound->generation++;
    // It's possible the sound source has been destroyed since the sound was started
    if (sound->source) {
        source_idle_push(sound->source, sound);
    }
    sounds_idle_lru_push_front(sound);
}

void begin_frame()
{
    for (u32 i = 0; i < state->sounds.size; ++i) {
        auto sound = &state->sounds[i];
        if (sound->in_use && !ma_sound_is_playing(&sound->sound)) {
            sound_set_idle(sound);
        }
    }
}

void shutdown()
{
    if (!state) {
        return;
    }

    // destroying the sources should take care of de-initing the sounds
    for (u32 i = 0; i < state->sound_sources.keys.size; ++i) {
        if (state->sound_sources.contains(state->sound_sources.keys[i])) {
            ung_sound_source_destroy({ state->sound_sources.keys[i] });
        }
    }
    state->sounds.free();
    state->sound_sources.free();

    for (u32 i = 0; i < state->sound_groups.size; ++i) {
        ma_sound_group_uninit(&state->sound_groups[i]);
    }
    state->sound_groups.free();

    ma_engine_uninit(&state->sound_engine);
}

static Sound* get_idle_sound()
{
    if (state->free_sounds_head) {
        auto sound = state->free_sounds_head;
        assert(!sound->in_use);
        state->free_sounds_head = sound->free_sounds_next;
        sound->free_sounds_next = nullptr;
        sound->generation++; // the first generation we hand out is 1 (so 0 can be invalid)
        return sound;
    }
    return sounds_idle_lru_pop_back(); // may be nullptr
}

static void unset_source(Sound* sound)
{
    assert(sound->source);
    ma_sound_stop(&sound->sound);
    ma_sound_uninit(&sound->sound);
    source_idle_remove(sound->source, sound);
    sound->source = nullptr;
}

static void load_sound(const char* path, uint32_t flags, ma_sound_group* group, ma_sound* sound)
{
    const auto res
        = ma_sound_init_from_file(&state->sound_engine, path, flags, group, nullptr, sound);
    if (res != MA_SUCCESS) {
        std::fprintf(stderr, "Could not load sound '%s': %s\n", path, ma_result_description(res));
        std::exit(1);
    }
}

static void set_source(Sound* sound, SoundSource* source)
{
    assert(!sound->in_use); // should only be called on idle sounds
    if (sound->source == source) {
        return;
    }

    if (sound->source) {
        unset_source(sound);
    }

    if (source->flags & MA_SOUND_FLAG_STREAM) {
        load_sound(
            source->path.data, source->flags, &state->sound_groups[source->group], &sound->sound);
    } else {
        const auto res = ma_sound_init_copy(&state->sound_engine, &source->sound, source->flags,
            &state->sound_groups[source->group], &sound->sound);
        if (res != MA_SUCCESS) {
            std::fprintf(stderr, "Could not copy sound '%s': %s\n", source->path.data,
                ma_result_description(res));
            std::exit(1);
        }
    }

    sound->source = source;
}

EXPORT ung_sound_source_id ung_sound_source_load(
    const char* path, ung_sound_source_load_params params)
{
    const auto [id, source] = state->sound_sources.insert();

    assign(source->path, path);
    assert(source->path.size);
    source->flags = MA_SOUND_FLAG_DECODE;
    if (params.stream) {
        source->flags |= MA_SOUND_FLAG_STREAM;
    }
    source->group = params.group;

    if (!params.stream) {
        load_sound(source->path.data, source->flags, nullptr, &source->sound);
    }

    // prewarm at least one, so we make sure the sound file exists and is
    // decodeable
    params.num_prewarm_sounds = params.num_prewarm_sounds ? params.num_prewarm_sounds : 1;
    for (size_t i = 0; i < params.num_prewarm_sounds; ++i) {
        auto sound = get_idle_sound();
        if (!sound) {
            std::fprintf(stderr, "No idle sounds to prewarm '%s'\n", path);
            std::exit(1);
        }
        set_source(sound, source);
        source_idle_push(source, sound);
        sounds_idle_lru_push_front(sound);
    }

    return { id };
}

EXPORT void ung_sound_source_destroy(ung_sound_source_id src_id)
{
    // This is super expensive
    auto source = get(state->sound_sources, src_id.id);

    // Playing sounds are annoying, because we don't have a direct way of getting at them.
    // We can't simply wait until they stop playing and clean them up properly in begin_frame/
    // sound_set_idle, because there might be a new SoundSource at the same address as this one
    // (the one being destroyed). So we have to loop!
    for (u32 i = 0; i < state->sounds.size; ++i) {
        auto sound = &state->sounds[i];
        if (source == sound->source) {
            // Add back to free list (sounds without a source)
            unset_source(sound);
            sound->free_sounds_next = state->free_sounds_head;
            state->free_sounds_head = sound;
        }
    }

    // For all idle sounds for this source, we have to unset their source
    auto cur = source->source_idle_head;
    while (cur) {
        auto next = cur->source_idle_next;
        unset_source(cur); // this resets source_idle_next
        cur = next;
    }

    if ((source->flags & MA_SOUND_FLAG_STREAM) == 0) {
        ma_sound_uninit(&source->sound);
    }
    source->path.free();
    state->sound_sources.remove(src_id.id);
}

EXPORT ung_sound_id ung_sound_play(ung_sound_source_id src_id, ung_sound_play_params params)
{
    auto source = get(state->sound_sources, src_id.id);

    auto sound = source_idle_pop(source);

    if (sound) {
        sounds_idle_lru_remove(sound);
    }

    if (!sound && params.fail_if_no_idle) {
        // Do not get a sound from the free list either, because this allows limiting the
        // number of sounds per source by passing num_prewarm_sounds and fail_if_no_idle.
        return { 0 };
    }

    if (!sound) {
        sound = get_idle_sound();
        if (!sound) {
            std::fprintf(stderr, "No idle sounds\n");
            return { 0 }; // no idle sounds at all
        }
        set_source(sound, source);
    }

    ma_sound_set_volume(&sound->sound, params.volume != 0.0f ? params.volume : 1.0f);
    ma_sound_set_pitch(&sound->sound, params.pitch != 0.0f ? params.pitch : 1.0f);
    ma_sound_set_looping(&sound->sound, params.loop);
    ma_sound_set_spatialization_enabled(&sound->sound, params.spatial);
    if (params.spatial) {
        ma_sound_set_position(
            &sound->sound, params.position[0], params.position[1], params.position[2]);
    }

    ma_sound_start(&sound->sound);

    sound->in_use = true;
    const auto idx = sound - state->sounds.data;
    const uint64_t id = (u64)sound->generation << 24 | (u64)idx;
    return { id };
}

static Sound* get_sound(u64 id)
{
    const auto idx = (u32)(id & 0xFF'FFFF);
    const auto gen = (u32)((id & 0xFFFF'FF00'0000) >> 24);
    assert(idx < state->sounds.size);
    if (state->sounds[idx].generation != gen) {
        return nullptr;
    }
    return &state->sounds[idx];
}

EXPORT void ung_sound_update(ung_sound_id snd_id, float position[3], float velocity[3])
{
    auto sound = get_sound(snd_id.id);
    if (!sound) {
        return;
    }
    ma_sound_set_position(&sound->sound, position[0], position[1], position[2]);
    ma_sound_set_velocity(&sound->sound, velocity[0], velocity[1], velocity[2]);
}

EXPORT bool ung_sound_is_playing(ung_sound_id snd_id)
{
    auto sound = get_sound(snd_id.id);
    if (!sound) {
        return false;
    }
    return ma_sound_is_playing(&sound->sound);
}

EXPORT void ung_sound_set_paused(ung_sound_id snd_id, bool paused)
{
    auto sound = get_sound(snd_id.id);
    if (!sound) {
        return;
    }

    if (paused) {
        ma_sound_stop(&sound->sound);
    } else {
        ma_sound_start(&sound->sound);
    }
}

EXPORT void ung_sound_stop(ung_sound_id snd_id)
{
    auto sound = get_sound(snd_id.id);
    if (!sound) {
        return;
    }
    ma_sound_stop(&sound->sound);
    sound_set_idle(sound);
}

EXPORT void ung_update_listener(float position[3], float orientation_quat[4], float velocity[3])
{
    ma_engine_listener_set_position(&state->sound_engine, 0, position[0], position[1], position[2]);
    const um_quat q
        = { orientation_quat[0], orientation_quat[1], orientation_quat[2], orientation_quat[3] };
    const auto fwd = um_quat_mul_vec3(q, { -1.0f, 0.0f, 0.0f });
    ma_engine_listener_set_direction(&state->sound_engine, 0, fwd.x, fwd.y, fwd.z);
    ma_engine_listener_set_velocity(&state->sound_engine, 0, velocity[0], velocity[1], velocity[2]);
}

EXPORT float ung_sound_get_volume()
{
    return ma_engine_get_volume(&state->sound_engine);
}

EXPORT void ung_sound_set_volume(float vol)
{
    ma_engine_set_volume(&state->sound_engine, vol);
}

EXPORT float ung_sound_group_get_volume(uint8_t group)
{
    return ma_sound_group_get_volume(&state->sound_groups[group]);
}

EXPORT void ung_sound_group_set_volume(uint8_t group, float vol)
{
    ma_sound_group_set_volume(&state->sound_groups[group], vol);
}

EXPORT void ung_sound_group_set_paused(uint8_t group, bool paused)
{
    if (paused) {
        ma_sound_group_stop(&state->sound_groups[group]);
    } else {
        ma_sound_group_start(&state->sound_groups[group]);
    }
}
}