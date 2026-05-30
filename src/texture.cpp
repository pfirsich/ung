#include "state.hpp"

#include <cstdio>
#include <span>
#include <utility>

#ifdef UNG_STB_IMAGE
#include <stb_image.h>
#endif

namespace ung {

// This is everything you need to keep around while a texture is loading and that you can throw away
// once it has finished loading.
// It will only live until either decode has failed or upload has finished (failed or succeeded).
struct TexturePending {
    // Non-owning pointers
    std::span<const u8> encoded_data;
    const u8* decoded_data;
    const char* cache_path;
    const char* error;

    // Owning pointers
    char* file_data; // just for freeing
    size_t file_size;
    std::span<u8> allocated_data;
#ifdef UNG_STB_IMAGE
    u8* stbi_data; // just for freeing
#endif

    u32 width;
    u32 height;
    u32 components;

    void free()
    {
        if (file_data) {
            ung_free_file_data(file_data, file_size);
        }
        if (allocated_data.size()) {
            deallocate(allocated_data.data(), allocated_data.size());
        }
#ifdef UNG_STB_IMAGE
        if (stbi_data) {
            stbi_image_free(stbi_data);
        }
#endif
    }
};

// This stores everything you need to keep around to reload a texture later, i.e.
// for as long as the texture itself.
struct TextureResource {
    ung_texture_id texture;
    Array<char> path;
    ung_texture_type type;
    ung_texture_load_params params;
    TexturePending* pending;
};

static void format_texture_cache_path(Formatter& fmt, const void* data, usize size, bool flip_y)
{
    fmt.append(".ungcache/");
    fmt.append_hash(data, size);
    if (flip_y) {
        fmt.append("-flip");
    }
    fmt.append("-v1.tex");
}

static const char* format_texture_cache_path(const void* data, usize size, bool flip_y)
{
    thread_local char path_buf[128];
    Formatter fmt { path_buf };
    format_texture_cache_path(fmt, data, size, flip_y);
    return path_buf;
}

static void format_texture_cache_path(Formatter& fmt, const char* path, bool flip_y)
{
    const auto mtime = ung_file_get_mtime(path);
    fmt.append(".ungcache/");
    fmt.append_hash(path, strlen(path));
    fmt.append("-");
    fmt.append_hex_obj(mtime);
    if (flip_y) {
        fmt.append("-flip");
    }
    fmt.append("-v1.tex");
}

static const char* format_texture_cache_path(const char* path, bool flip_y)
{
    thread_local char path_buf[128];
    Formatter fmt { path_buf };
    format_texture_cache_path(fmt, path, flip_y);
    return path_buf;
}

struct CacheTexHeader {
    u32 width, height, components;
};

static void write_texture_cache_file(TexturePending* pending)
{
    auto file = fopen(pending->cache_path, "wb");
    if (!file) {
        fprintf(stderr, "Could not open texture cache file for writing: %s\n", pending->cache_path);
        return;
    }
    CacheTexHeader hdr { pending->width, pending->height, pending->components };
    fwrite(&hdr, sizeof(CacheTexHeader), 1, file);
    fwrite(pending->decoded_data, 1, hdr.width * hdr.height * hdr.components, file);
    fclose(file);
}

static bool load_cache(TexturePending* pending)
{
    size_t file_size = 0;
    const auto file_data = ung_read_whole_file(pending->cache_path, &file_size, false);
    if (!file_data) {
        return false; // couldn't load from cache
    }

    pending->file_data = file_data;
    pending->file_size = file_size;

    CacheTexHeader hdr;
    memcpy(&hdr, file_data, sizeof(CacheTexHeader));
    pending->decoded_data = (u8*)file_data + sizeof(CacheTexHeader);
    pending->width = hdr.width;
    pending->height = hdr.height;
    pending->components = hdr.components;

    return true;
}

static bool load_path(TextureResource* res, TexturePending* pending)
{
    ung_resource_depend_file(res->path.data);

    if (state->load_cache) {
        pending->cache_path = format_texture_cache_path(res->path.data, res->params.flip_y);
        if (load_cache(pending)) {
            return true;
        }
    }

    pending->file_data = ung_read_whole_file(res->path.data, &pending->file_size, false);
    if (!pending->file_data) {
        fprintf(stderr, "Could not open texture file: %s\n", res->path.data);
        pending->error = "Could not open file";
        return false;
    }

    pending->encoded_data = { (const u8*)pending->file_data, pending->file_size };
    return true;
}

static bool load_buffer(TextureResource* res, TexturePending* pending)
{
    assert(pending->encoded_data.data());
    if (state->load_cache) {
        pending->cache_path = format_texture_cache_path(
            pending->encoded_data.data(), pending->encoded_data.size(), res->params.flip_y);
        if (load_cache(pending)) {
            return true;
        }
    }
    return true;
}

static bool res_texture_decode(ung_resource_id self, void* instance)
{
    auto res = (TextureResource*)instance;

    if (!res->pending) {
        res->pending = allocate<TexturePending>();
    }
    auto pending = res->pending;

    if (res->path.size) {
        if (!load_path(res, pending)) {
            return false;
        }
    } else {
        if (!load_buffer(res, pending)) {
            return false;
        }
    }

    if (pending->error) {
        return false;
    }

    if (!pending->decoded_data) {
        assert(pending->encoded_data.data());
#ifdef UNG_STB_IMAGE
        stbi_set_flip_vertically_on_load_thread(res->params.flip_y);
        int w = 0, h = 0, c = 0;
        pending->stbi_data = stbi_load_from_memory(
            pending->encoded_data.data(), (int)pending->encoded_data.size(), &w, &h, &c, 0);
        if (!pending->stbi_data) {
            pending->error = stbi_failure_reason();
            return false;
        }
        pending->decoded_data = pending->stbi_data;
        pending->width = (u32)w;
        pending->height = (u32)h;
        pending->components = (u32)c;

        if (state->load_cache && pending->decoded_data) {
            write_texture_cache_file(pending);
        }
#else
        ung_panicf("Texture loading requires stb_image (UNG_STB_IMAGE=ON)");
#endif
    }

    return true;
}

static bool res_texture_upload(ung_resource_id self, void* instance)
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

    auto res = (TextureResource*)instance;
    auto pending = res->pending;
    auto tex = get(state->textures, res->texture.id);

    assert(pending->decoded_data);
    assert(pending->width > 0 && pending->height > 0 && pending->components > 0);
    assert(pending->components <= 4);

    auto gfx_params = res->params.mugfx;
    gfx_params.width = (u32)pending->width;
    gfx_params.height = (u32)pending->height;
    gfx_params.data = {
        pending->decoded_data,
        (usize)(pending->width * pending->height * pending->components),
    };
    if (gfx_params.format == MUGFX_PIXEL_FORMAT_DEFAULT) {
        if (res->type == UNG_TEXTURE_COLOR) {
            assert(pending->components >= 3); // TODO: Implement expansion (R -> RGB, LA -> RGBA)
            gfx_params.format = pixel_formats_srgb[pending->components];
        } else if (res->type == UNG_TEXTURE_DATA) {
            gfx_params.format = pixel_formats_linear[pending->components];
        }
    }
    gfx_params.data_format = pixel_formats_linear[pending->components];

    if (res->path.size) {
        gfx_params.debug_label = res->path.data;
    }

    const auto mugfx_tex = mugfx_texture_create(gfx_params);
    if (!mugfx_tex.id) {
        pending->error = "Could not create mugfx texture";
        return false;
    }

    // commit
    if (tex->texture.id) {
        mugfx_texture_destroy(tex->texture);
    }
    tex->texture = mugfx_tex;

    return true;
}

const char* res_texture_get_error(void* instance)
{
    auto res = (TextureResource*)instance;
    return res->pending ? res->pending->error : nullptr;
}

static void res_texture_cleanup_load(ung_resource_id self, void* instance)
{
    auto res = (TextureResource*)instance;
    if (res->pending) {
        res->pending->free();
        deallocate(res->pending);
        res->pending = nullptr;
    }
}

void res_texture_destroy(ung_resource_id self, void* instance)
{
    auto res = (TextureResource*)instance;
    const auto tex_id = res->texture.id;
    auto tex = get(state->textures, tex_id);

    res->path.free();
    deallocate(res);

    if (tex->texture.id) {
        mugfx_texture_destroy(tex->texture);
    }
    state->textures.remove(tex_id);
}

EXPORT ung_texture_id ung_texture_create(mugfx_texture_create_params params)
{
    const auto t = mugfx_texture_create(params);
    if (!t.id) {
        ung_panic("Failed to create texture");
    }

    const auto [id, texture] = state->textures.insert();
    texture->texture = t;
    return { id };
}

static ung_resource_type_id texture_resource()
{
    static ung_resource_type_id res_type = {};
    if (!res_type.id) {
        res_type = ung_resource_type_register({
            .type_name = "texture",
            .decode = res_texture_decode,
            .upload = res_texture_upload,
            .get_error = res_texture_get_error,
            .cleanup_load = res_texture_cleanup_load,
            .destroy = res_texture_destroy,
        });
    }
    return res_type;
}

// Pass a fully initialized TextureResource, becaude decode might kick off right away!
static ung_texture_id load_texture(TextureResource* tex_res, const char* key)
{
    // We have to insert and assign the texture to the resource before we load, because
    // it might kick off the decode immediately, which might want to access the texture.
    const auto [id, tex] = state->textures.insert();
    tex_res->texture = { id };
    const auto [res, created] = ung_resource_load(texture_resource(), key, tex_res);

    if (!created) {
        state->textures.remove(id);
        if (tex_res->path.size) {
            tex_res->path.free();
        }
        if (tex_res->pending) {
            tex_res->pending->free();
            deallocate(tex_res->pending);
        }
        deallocate(tex_res);
        return ((TextureResource*)ung_resource_instance(res))->texture;
    }

    tex->resource = res;

    return { id };
}

EXPORT ung_texture_id ung_texture_load(
    const char* path, ung_texture_type type, ung_texture_load_params params)
{
    assert(type);

    char key_buf[512];
    Formatter fmt { key_buf };
    fmt.append(path);
    fmt.append("-");
    fmt.append_hex_obj((u8)type);
    fmt.append("-");
    fmt.append_hash_obj(params);

    auto tex_res = allocate<TextureResource>();
    assign(tex_res->path, path);
    tex_res->type = type;
    tex_res->params = params;

    return load_texture(tex_res, fmt.data());
}

EXPORT ung_texture_id ung_texture_load_buffer(
    const void* buffer, size_t size, ung_texture_type type, ung_texture_load_params params)
{
    assert(type);

    auto tex_res = allocate<TextureResource>();
    tex_res->type = type;
    tex_res->params = params;
    tex_res->pending = allocate<TexturePending>();
    tex_res->pending->allocated_data = { allocate<u8>(size), size };
    std::memcpy(tex_res->pending->allocated_data.data(), buffer, size);
    tex_res->pending->encoded_data = tex_res->pending->allocated_data;

    return load_texture(tex_res, nullptr);
}

EXPORT void ung_texture_swap(ung_texture_id dst_id, ung_texture_id src_id)
{
    assert(src_id.id);
    assert(dst_id.id);
    assert(dst_id.id != src_id.id);
    auto dst = get(state->textures, dst_id.id);
    auto src = get(state->textures, src_id.id);
    // We could actually handle the mixed cases, but I think that is mostly dumb, so I will assert
    // instead.
    assert((dst->resource.id && src->resource.id) || (!dst->resource.id && !src->resource.id));
    std::swap(dst->texture, src->texture);
    if (dst->resource.id) {
        ung_resource_swap(dst->resource, src->resource);
        ((TextureResource*)ung_resource_instance(dst->resource))->texture = dst_id;
        ((TextureResource*)ung_resource_instance(src->resource))->texture = src_id;
    }
}

EXPORT void ung_texture_destroy(ung_texture_id id)
{
    auto texture = get(state->textures, id.id);
    if (texture->resource.id) {
        ung_resource_destroy_new(texture->resource);
    } else {
        mugfx_texture_destroy(texture->texture);
        state->textures.remove(id.id);
    }
}

EXPORT ung_dimensions ung_texture_get_size(ung_texture_id id)
{
    auto texture = get(state->textures, id.id);
    if (texture->resource.id) {
        ung_resource_wait_ready(texture->resource);
    }
    u32 width = 0, height = 0;
    mugfx_texture_get_size(texture->texture, &width, &height);
    return { width, height };
}

EXPORT void ung_texture_set_data(
    ung_texture_id id, mugfx_slice data, mugfx_pixel_format data_format)
{
    auto texture = get(state->textures, id.id);
    mugfx_texture_set_data(texture->texture, data, data_format);
}

EXPORT ung_resource_id ung_texture_resource(ung_texture_id id)
{
    return get(state->textures, id.id)->resource;
}

}