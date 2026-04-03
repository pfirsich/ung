#include "state.hpp"

#include <cstdio>
#include <utility>

#ifdef UNG_STB_IMAGE
#include <stb_image.h>
#endif

namespace ung {

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

static mugfx_texture_id create_texture(const void* data, int width, int height, int comp,
    ung_texture_type type, mugfx_texture_create_params& params)
{
    static constexpr mugfx_pixel_format pixel_formats_linear[] {
        MUGFX_PIXEL_FORMAT_DEFAULT,
        MUGFX_PIXEL_FORMAT_R8,
        MUGFX_PIXEL_FORMAT_RG8,
        MUGFX_PIXEL_FORMAT_RGB8,
        MUGFX_PIXEL_FORMAT_RGBA8,
    };
    static constexpr mugfx_pixel_format pixel_formats_srgb[] {
        MUGFX_PIXEL_FORMAT_DEFAULT,
        MUGFX_PIXEL_FORMAT_SRGB8,
        MUGFX_PIXEL_FORMAT_SRGB8,
        MUGFX_PIXEL_FORMAT_SRGB8,
        MUGFX_PIXEL_FORMAT_SRGB8_ALPHA8,
    };
    LoadProfScope lpscope("upload");
    assert(width > 0 && height > 0 && comp > 0);
    assert(comp <= 4);
    params.width = (u32)width;
    params.height = (u32)height;
    params.data = { data, (usize)(width * height * comp) };
    if (params.format == MUGFX_PIXEL_FORMAT_DEFAULT) {
        if (type == UNG_TEXTURE_COLOR) {
            assert(comp >= 3); // TODO: Implement expansion (R -> RGB, LA -> RGBA)
            params.format = pixel_formats_srgb[comp];
        } else if (type == UNG_TEXTURE_DATA) {
            params.format = pixel_formats_linear[comp];
        }
    }
    params.data_format = pixel_formats_linear[comp];
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
    enum class Type {
        Invalid,
        Cached,
#ifdef UNG_STB_IMAGE
        Stbi,
#endif
    };

    Type type = Type::Invalid;
    void* data = nullptr;
    size_t size = 0;
    u8* decoded = nullptr;
    int width = 0;
    int height = 0;
    int components = 0;
    const char* error = nullptr;

    TexDecode() = default;
    TexDecode(TexDecode&) = delete;
    TexDecode(TexDecode&& r)
        : type(std::exchange(r.type, Type::Invalid))
        , data(std::exchange(r.data, nullptr))
        , size(std::exchange(r.size, 0))
        , decoded(std::exchange(r.decoded, nullptr))
        , width(std::exchange(r.width, 0))
        , height(std::exchange(r.height, 0))
        , components(std::exchange(r.components, 0))
        , error(std::exchange(r.error, nullptr))
    {
    }

    ~TexDecode()
    {
        switch (type) {
        case Type::Cached:
            ung_free_file_data((char*)data, size);
            break;
#ifdef UNG_STB_IMAGE
        case Type::Stbi:
            stbi_image_free(data);
            break;
#endif
        default:
            break;
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
    fwrite(tex.decoded, 1, hdr.width * hdr.height * hdr.components, file);
    fclose(file);
}

TexDecode load_cached_texture(const char* cache_path)
{
    TexDecode ret;
    ret.type = TexDecode::Type::Cached;
    {
        LoadProfScope s("read cached");
        ret.data = ung_read_whole_file(cache_path, &ret.size, false);
    }
    if (!ret.data) {
        return ret;
    }

    ret.decoded = (u8*)ret.data + sizeof(CacheTexHeader);

    CacheTexHeader hdr;
    memcpy(&hdr, ret.data, sizeof(CacheTexHeader));

    ret.width = (int)hdr.width;
    ret.height = (int)hdr.height;
    ret.components = (int)hdr.components;

    return ret;
}

TexDecode decode_texture(const u8* data, usize size, bool flip_y)
{
    LoadProfScope lpscope("decode");
#ifdef UNG_STB_IMAGE
    char cache_path[128];
    if (state->load_cache) {
        LoadProfScope s("load cache");
        format_texture_cache_path(cache_path, ung_fnv1a(data, size), flip_y);

        auto cached = load_cached_texture(cache_path);

        if (cached.data) {
            return cached;
        }
    }

    TexDecode ret;
    ret.type = TexDecode::Type::Stbi;
    stbi_set_flip_vertically_on_load(flip_y);
    {
        LoadProfScope s("stbi_load_from_memory");
        ret.data
            = stbi_load_from_memory(data, (int)size, &ret.width, &ret.height, &ret.components, 0);
    }
    ret.decoded = (u8*)ret.data;
    if (!ret.decoded) {
        ret.error = stbi_failure_reason();
    }

    if (state->load_cache && ret.decoded) {
        LoadProfScope s("write cache");
        write_texture_cache_file(cache_path, ret);
    }

    return ret;
#else
    (void)data;
    (void)size;
    (void)flip_y;
    ung_panicf("Texture loading requires stb_image (UNG_STB_IMAGE=ON)");
#endif
}

static mugfx_texture_id load_texture(
    const char* path, ung_texture_type type, ung_texture_load_params& params)
{
    LoadProfScope lpscope(path);
    usize size = 0;

    ung_load_profiler_push("io");
    const auto data = ung_read_whole_file(path, &size, false);
    ung_load_profiler_pop("io");
    if (!data) {
        return { 0 };
    }

    const auto decoded = decode_texture((const u8*)data, size, params.flip_y);
    ung_free_file_data(data, size);
    if (!decoded.decoded) {
        return { 0 };
    }

    params.mugfx.debug_label = path;
    const auto texture = create_texture(
        decoded.decoded, decoded.width, decoded.height, decoded.components, type, params.mugfx);

    return texture;
}

static bool reload_texture(
    Texture* texture, const char* path, ung_texture_type type, ung_texture_load_params& params)
{
    const auto tex = load_texture(path, type, params);
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
    return reload_texture(texture, ctx->path.data, ctx->type, ctx->params);
}

EXPORT ung_texture_id ung_texture_load(
    const char* path, ung_texture_type type, ung_texture_load_params params)
{
    assert(type);
    const auto t = load_texture(path, type, params);
    if (!t.id) {
        ung_panicf("Error loading texture '%s'", path);
    }

    const auto [id, texture] = state->textures.insert();
    texture->texture = t;

    if (state->auto_reload) {
        texture->reload_ctx = allocate<TextureReloadCtx>();
        texture->reload_ctx->texture = { id };
        assign(texture->reload_ctx->path, path);
        texture->reload_ctx->type = type;
        texture->reload_ctx->params = params;
        texture->resource = ung_resource_create(texture_reload_cb, texture->reload_ctx);
        ung_resource_set_deps(texture->resource, &path, 1, nullptr, 0);
    }

    return { id };
}

EXPORT ung_texture_id ung_texture_load_buffer(
    const void* buffer, size_t size, ung_texture_type type, ung_texture_load_params params)
{
    assert(type);
    const auto decoded = decode_texture((const u8*)buffer, size, params.flip_y);
    if (!decoded.decoded) {
        assert(decoded.error);
        ung_panicf("Error loading texture: %s", decoded.error);
    }
    const auto tex = create_texture(
        decoded.decoded, decoded.width, decoded.height, decoded.components, type, params.mugfx);

    if (!tex.id) {
        ung_panicf("Error loading texture");
    }

    const auto [id, texture] = state->textures.insert();
    texture->texture = tex;
    return { id };
}

EXPORT void ung_texture_destroy(ung_texture_id texture_id)
{
    auto texture = get(state->textures, texture_id.id);

    mugfx_texture_destroy(texture->texture);

    if (texture->resource.id) {
        ung_resource_destroy(texture->resource);
    }

    if (texture->reload_ctx) {
        texture->reload_ctx->path.free();
        deallocate(texture->reload_ctx);
    }

    state->textures.remove(texture_id.id);
}

EXPORT ung_dimensions ung_texture_get_size(ung_texture_id texture_id)
{
    auto texture = get(state->textures, texture_id.id);
    u32 width = 0, height = 0;
    mugfx_texture_get_size(texture->texture, &width, &height);
    return { width, height };
}

}