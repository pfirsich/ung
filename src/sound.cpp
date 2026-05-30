#include <cfloat>
#include <cstdio>
#include <cstdlib>

#include <miniaudio.h>

#include "state.hpp"
#include "types.hpp"
#include "um.h"

/*
 * I realize this seems much more complicated than it could be, but I go through all this trouble
 * so I can avoid initializing sounds as much as possible.
 * One principle of my engine is to not require allocations in the game's main loop.
 * Unfortunately it is not possible to do this with an interface like ung has it (sources + sounds).
 * See here: https://github.com/mackron/miniaudio/discussions/1025
 * So instead of avoiding allocations altogether I try to reduce them.
 * I keep a list per-source of idle sounds and a global last recently used list of sounds.
 * and whenever I want to play a sound, I take sounds from those.
 * Loaded sounds are reused as much as possible and at some point it should converge so that
 * no ung_sound_play should allocate.
 */

namespace ung::sound {
struct Sound;

struct SoundSourcePending {
    const char* error;
    void* pcm;
    ma_uint64 frame_count;
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sample_rate;

    void free()
    {
        if (pcm) {
            ma_free(pcm, nullptr);
        }
    }
};

struct SoundSourceResource {
    ung_sound_source_id source;
    // We duplicate some fields here with SoundSource, so we don't have to touch SoundSource in a
    // decode thread.
    Array<char> path;
    u32 flags;
    u32 num_prewarm_sounds;
    SoundSourcePending* pending;
};

// We don't have a separate reload context, because the source saves the path already.
struct SoundSource {
    Array<char> path;

    void* pcm;
    ma_uint64 frame_count;
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sample_rate;

    u32 flags;
    u8 group;

    // This represents a list of idle sounds for this source.
    // A sound in this list, should be in sounds_idle_lru as well (and vice-versa)!
    Sound* source_idle_head;

    ung_resource_id resource;
    ung_sound_spatial_params spatial_params;
    bool use_spatial_params;
};

struct Sound {
    ma_sound sound;
    ma_audio_buffer buffer;

    bool sound_loaded = false;
    bool buffer_loaded = false;

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
    bool paused;
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

    ung_sound_spatial_params spatial_params;
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
        ung_panicf("Error initializing audio engine: %s", ma_result_description(ma_res));
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
            ung_panicf("Error initializing sound group: %s", ma_result_description(ma_res));
        }
    }

    state->spatial_params = {
        .attenuation_model = UNG_SOUND_ATTENUATION_INVERSE,
        .min_distance = 1,
        .max_distance = FLT_MAX,
        .rolloff = 1.0f,
        .directional_attenuation_factor = 1.0f,
        .doppler_factor = 1.0f,
    };
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
        if (sound->in_use && !sound->paused && !ma_sound_is_playing(&sound->sound)) {
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

EXPORT ung_sound_spatial_params ung_sound_get_default_spatial_params(void)
{
    return state->spatial_params;
}

EXPORT void ung_sound_set_default_spatial_params(ung_sound_spatial_params params)
{
    state->spatial_params = params;
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

static void unload_sound(Sound* sound)
{
    if (sound->sound_loaded) {
        ma_sound_stop(&sound->sound);
        ma_sound_uninit(&sound->sound);
        sound->sound_loaded = false;
    }

    if (sound->buffer_loaded) {
        ma_audio_buffer_uninit(&sound->buffer);
        sound->buffer_loaded = false;
    }
}

static void unset_source(Sound* sound)
{
    assert(sound->source);
    unload_sound(sound);
    source_idle_remove(sound->source, sound);
    sound->source = nullptr;
}

static bool load_sound(Sound* sound)
{
    const auto source = sound->source;

    if (source->flags & MA_SOUND_FLAG_STREAM) {
        const auto r = ma_sound_init_from_file(&state->sound_engine, source->path.data,
            source->flags, &state->sound_groups[source->group], nullptr, &sound->sound);
        sound->sound_loaded = r == MA_SUCCESS;
        return r == MA_SUCCESS;
    }

    auto cfg = ma_audio_buffer_config_init(
        source->format, source->channels, source->frame_count, source->pcm, nullptr);
    cfg.sampleRate = source->sample_rate;

    auto r = ma_audio_buffer_init(&cfg, &sound->buffer);
    if (r != MA_SUCCESS) {
        return false;
    }
    sound->buffer_loaded = true;

    r = ma_sound_init_from_data_source(&state->sound_engine, (ma_data_source*)&sound->buffer,
        source->flags, &state->sound_groups[source->group], &sound->sound);

    if (r != MA_SUCCESS) {
        ma_audio_buffer_uninit(&sound->buffer);
        sound->buffer_loaded = false;
        return false;
    }

    sound->sound_loaded = true;
    return true;
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
    sound->source = source;

    if (!load_sound(sound)) {
        ung_panicf("Error loading sound '%s'", source->path.data);
    }
}

static bool res_source_decode(ung_resource_id self, void* instance)
{
    auto res = (SoundSourceResource*)instance;

    ung_resource_depend_file(res->path.data);

    res->pending = allocate<SoundSourcePending>();
    auto pending = res->pending;

    if (res->flags & MA_SOUND_FLAG_STREAM) {
        // TODO: Maybe validate?
        return true;
    }

    auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
    const auto result
        = ma_decode_file(res->path.data, &config, &pending->frame_count, &pending->pcm);
    if (result != MA_SUCCESS) {
        pending->error = ma_result_description(result);
        return false;
    }

    pending->format = config.format;
    pending->channels = config.channels;
    pending->sample_rate = config.sampleRate;
    return true;
}

// Add back to free list (sounds without a source)
static void set_free(Sound* sound)
{
    if (!sound->in_use) {
        sounds_idle_lru_remove(sound);
    } else {
        sound->in_use = false;
        sound->generation++; // invalidate active user handle
    }

    unset_source(sound);

    sound->free_sounds_next = state->free_sounds_head;
    state->free_sounds_head = sound;
}

static void unset_all(SoundSource* source)
{
    // Playing sounds are annoying, because we don't have a direct way of getting at them.
    // We can't simply wait until they stop playing and clean them up properly in begin_frame/
    // sound_set_idle, because there might be a new SoundSource at the same address as this one
    // (the one being destroyed). So we have to loop!
    for (u32 i = 0; i < state->sounds.size; ++i) {
        auto sound = &state->sounds[i];
        if (source == sound->source) {
            set_free(sound);
        }
    }
}

static void prewarm(SoundSource* source, u32 num_prewarm_sounds)
{
    for (size_t i = 0; i < num_prewarm_sounds; ++i) {
        auto sound = get_idle_sound();
        if (!sound) {
            ung_panicf("No idle sounds to prewarm '%s'", source->path.data);
        }
        set_source(sound, source);
        source_idle_push(source, sound);
        sounds_idle_lru_push_front(sound);
    }
}

static bool res_source_upload(ung_resource_id self, void* instance)
{
    auto res = (SoundSourceResource*)instance;
    auto source = get(state->sound_sources, res->source.id);
    auto pending = res->pending;

    // Stop and unload all playing and idle sounds for this source
    unset_all(source);

    if (source->pcm) {
        ma_free(source->pcm, nullptr);
    }

    source->pcm = pending->pcm;
    source->frame_count = pending->frame_count;
    source->format = pending->format;
    source->channels = pending->channels;
    source->sample_rate = pending->sample_rate;

    pending->pcm = nullptr;

    prewarm(source, res->num_prewarm_sounds);
    return true;
}

const char* res_source_get_error(void* instance)
{
    auto res = (SoundSourceResource*)instance;
    return res->pending ? res->pending->error : nullptr;
}

static void res_source_cleanup_load(ung_resource_id self, void* instance)
{
    auto res = (SoundSourceResource*)instance;
    if (res->pending) {
        res->pending->free();
        deallocate(res->pending);
        res->pending = nullptr;
    }
}

void res_source_destroy(ung_resource_id self, void* instance)
{
    auto res = (SoundSourceResource*)instance;
    auto source = get(state->sound_sources, res->source.id);

    unset_all(source);

    if (source->pcm) {
        ma_free(source->pcm, nullptr);
    }

    source->path.free();
    state->sound_sources.remove(res->source.id);

    res->path.free();
    deallocate(res);
}

static ung_resource_type_id source_resource()
{
    static ung_resource_type_id res_type = {};
    if (!res_type.id) {
        res_type = ung_resource_type_register({
            .type_name = "sound_source",
            .decode = res_source_decode,
            .upload = res_source_upload,
            .get_error = res_source_get_error,
            .cleanup_load = res_source_cleanup_load,
            .destroy = res_source_destroy,
        });
    }
    return res_type;
}

EXPORT ung_sound_source_id ung_sound_source_load(
    const char* path, ung_sound_source_load_params params)
{
    char key_buf[512];
    Formatter fmt { key_buf };
    fmt.append(path);
    fmt.append("-");
    // left out stream and num prewarm on purpose. those are just loading hints.
    fmt.append_hex_obj(params.group);
    if (params.spatial_params) {
        fmt.append_hex_obj(*params.spatial_params);
    }

    auto source_res = allocate<SoundSourceResource>();
    assign(source_res->path, path);
    source_res->flags = MA_SOUND_FLAG_DECODE;
    if (params.stream) {
        source_res->flags |= MA_SOUND_FLAG_STREAM;
    }
    // prewarm at least one, so we make sure the sound file exists and is
    // decodeable
    source_res->num_prewarm_sounds = params.num_prewarm_sounds ? params.num_prewarm_sounds : 1;

    const auto [id, source] = state->sound_sources.insert();
    source_res->source = { id };
    const auto [res, created] = ung_resource_load(source_resource(), fmt.data(), source_res);

    if (!created) {
        state->sound_sources.remove(id);
        source_res->path.free();
        deallocate(source_res);
        return ((SoundSourceResource*)ung_resource_instance(res))->source;
    }

    assign(source->path, path);
    source->flags = source_res->flags;
    source->group = params.group;

    if (params.spatial_params) {
        std::memcpy(
            &source->spatial_params, params.spatial_params, sizeof(ung_sound_spatial_params));
        source->use_spatial_params = true;
    }

    source->resource = res;

    return { id };
}

EXPORT void ung_sound_source_destroy(ung_sound_source_id src_id)
{
    auto source = get(state->sound_sources, src_id.id);
    ung_resource_destroy_new(source->resource);
}

static ma_attenuation_model get_atten_model(ung_sound_attenuation_model model)
{
    switch (model) {
    case UNG_SOUND_ATTENUATION_NONE:
        return ma_attenuation_model_none;
    case UNG_SOUND_ATTENUATION_LINEAR:
        return ma_attenuation_model_linear;
    case UNG_SOUND_ATTENUATION_INVERSE:
        return ma_attenuation_model_inverse;
    case UNG_SOUND_ATTENUATION_EXPONENTIAL:
        return ma_attenuation_model_exponential;
    }
    return ma_attenuation_model_none;
}

EXPORT ung_sound_id ung_sound_play(ung_sound_source_id src_id, ung_sound_play_params params)
{
    auto source = get(state->sound_sources, src_id.id);
    ung_resource_wait_ready(source->resource);

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
        const ung_sound_spatial_params& spatial_params
            = source->use_spatial_params ? source->spatial_params : state->spatial_params;
        ma_sound_set_attenuation_model(
            &sound->sound, get_atten_model(spatial_params.attenuation_model));
        ma_sound_set_min_distance(&sound->sound, spatial_params.min_distance);
        ma_sound_set_max_distance(&sound->sound, spatial_params.max_distance);
        ma_sound_set_rolloff(&sound->sound, spatial_params.rolloff);
        ma_sound_set_directional_attenuation_factor(
            &sound->sound, spatial_params.directional_attenuation_factor);
        ma_sound_set_doppler_factor(&sound->sound, spatial_params.doppler_factor);
    }

    sound->paused = false;
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

EXPORT void ung_sound_update(ung_sound_id snd_id, const float position[3], const float velocity[3])
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

    sound->paused = paused;

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

    sound->paused = false;
    ma_sound_stop(&sound->sound);
    ma_sound_seek_to_pcm_frame(&sound->sound, 0);
    sound_set_idle(sound);
}

EXPORT void ung_update_listener(
    const float position[3], const float orientation_quat[4], const float velocity[3])
{
    ma_engine_listener_set_position(&state->sound_engine, 0, position[0], position[1], position[2]);
    const um_quat q = um_quat_from_ptr(orientation_quat);
    const auto fwd = um_quat_mul_vec3(q, { 0.0f, 0.0f, -1.0f });
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