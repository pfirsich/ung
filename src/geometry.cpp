#include "state.hpp"

#include <cmath>
#include <cstdio>

#ifdef UNG_FAST_OBJ
#include <fast_obj.h>
#endif

namespace ung {

// https://www.khronos.org/opengl/wiki/Normalized_Integer
static constexpr u32 pack1010102(float x, float y, float z, u8 w = 0)
{
    x = std::fmin(std::fmax(x, -1.0f), 1.0f);
    y = std::fmin(std::fmax(y, -1.0f), 1.0f);
    z = std::fmin(std::fmax(z, -1.0f), 1.0f);

    constexpr u32 maxv = 511; // MAX=2^(B-1)-1, B=10
    const auto xi = static_cast<i32>(std::round(x * maxv));
    const auto yi = static_cast<i32>(std::round(y * maxv));
    const auto zi = static_cast<i32>(std::round(z * maxv));

    // Convert to 10-bit unsigned representations
    // For negative values: apply two's complement, because in twos complement a number and its
    // negative sum to 2^N and x + -x = 2^N <=> 2^N - x = -x.
    // wiki: "The defining property of being a complement to a number with respect to
    // 2N is simply that the summation of this number with the original produce 2N."
    const auto xu = static_cast<u32>(xi < 0 ? (1024 + xi) : xi);
    const auto yu = static_cast<u32>(yi < 0 ? (1024 + yi) : yi);
    const auto zu = static_cast<u32>(zi < 0 ? (1024 + zi) : zi);

    const auto wu = static_cast<u32>(w & 0b11); // limit to two bits

    return (xu & 0x3FF) | // X in bits 0-9
        ((yu & 0x3FF) << 10) | // Y in bits 10-19
        ((zu & 0x3FF) << 20) | // Z in bits 20-29
        (wu << 30); // W in bits 30-31
}

struct Vertex {
    float x, y, z;
    u16 u, v;
    u32 n; // MUGFX_VERTEX_ATTRIBUTE_TYPE_I10_10_10_2_NORM
    u8 r, g, b, a;
};

EXPORT ung_geometry_id ung_geometry_box(float w, float h, float d)
{
    const auto n_px = pack1010102(1.0f, 0.0f, 0.0f);
    const auto n_nx = pack1010102(-1.0f, 0.0f, 0.0f);
    const auto n_py = pack1010102(0.0f, 1.0f, 0.0f);
    const auto n_ny = pack1010102(0.0f, -1.0f, 0.0f);
    const auto n_pz = pack1010102(0.0f, 0.0f, 1.0f);
    const auto n_nz = pack1010102(0.0f, 0.0f, -1.0f);

    // clang-format off
    std::array vertices = {
        // +x
        Vertex { 1.0f,  1.0f,  1.0f, 0x0000, 0x0000, n_px, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f,  1.0f, 0x0000, 0xffff, n_px, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f, -1.0f, 0xffff, 0xffff, n_px, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f,  1.0f, -1.0f, 0xffff, 0x0000, n_px, 0xff, 0xff, 0xff, 0xff },

        // -x
        Vertex {-1.0f,  1.0f, -1.0f, 0x0000, 0x0000, n_nx, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f, -1.0f, 0x0000, 0xffff, n_nx, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f,  1.0f, 0xffff, 0xffff, n_nx, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f,  1.0f,  1.0f, 0xffff, 0x0000, n_nx, 0xff, 0xff, 0xff, 0xff },
        
        // +y
        Vertex {-1.0f,  1.0f, -1.0f, 0x0000, 0x0000, n_py, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f,  1.0f,  1.0f, 0x0000, 0xffff, n_py, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f,  1.0f,  1.0f, 0xffff, 0xffff, n_py, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f,  1.0f, -1.0f, 0xffff, 0x0000, n_py, 0xff, 0xff, 0xff, 0xff },

        // -y
        Vertex {-1.0f, -1.0f,  1.0f, 0x0000, 0x0000, n_ny, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f, -1.0f, 0x0000, 0xffff, n_ny, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f, -1.0f, 0xffff, 0xffff, n_ny, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f,  1.0f, 0xffff, 0x0000, n_ny, 0xff, 0xff, 0xff, 0xff },

        // +z
        Vertex {-1.0f,  1.0f,  1.0f, 0x0000, 0x0000, n_pz, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f,  1.0f, 0x0000, 0xffff, n_pz, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f,  1.0f, 0xffff, 0xffff, n_pz, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f,  1.0f,  1.0f, 0xffff, 0x0000, n_pz, 0xff, 0xff, 0xff, 0xff },

        // -z
        Vertex { 1.0f,  1.0f, -1.0f, 0x0000, 0x0000, n_nz, 0xff, 0xff, 0xff, 0xff },
        Vertex { 1.0f, -1.0f, -1.0f, 0x0000, 0xffff, n_nz, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f, -1.0f, -1.0f, 0xffff, 0xffff, n_nz, 0xff, 0xff, 0xff, 0xff },
        Vertex {-1.0f,  1.0f, -1.0f, 0xffff, 0x0000, n_nz, 0xff, 0xff, 0xff, 0xff },
    };
    // clang-format on

    for (auto& v : vertices) {
        v.x *= w / 2.0f;
        v.y *= h / 2.0f;
        v.z *= d / 2.0f;
    }

    static constexpr u8 face_indices[] = { 0, 1, 2, 0, 2, 3 };

    std::array<u8, 6 * 2 * 3> indices; // 6 sides, 2 tris/side, 3 indices/tri
    for (u8 side = 0; side < 6; ++side) {
        for (u8 vertex = 0; vertex < 6; ++vertex) {
            indices[side * 6 + vertex] = static_cast<u8>(4 * side + face_indices[vertex]);
        }
    }

    const auto vertex_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_ARRAY,
        .data = { vertices.data(), vertices.size() * sizeof(Vertex) },
        .debug_label = "box.vbuf",
    });

    const auto index_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_INDEX,
        .data = { indices.data(), indices.size() * sizeof(indices[0]) },
        .debug_label = "box.ibuf",
    });

    const auto geometry = ung_geometry_create({
        .vertex_buffers = {
            {
                .buffer = vertex_buffer,
                .attributes = {
                    {.location = 0, .components = 3, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_F32}, // position
                    {.location = 1, .components = 2, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U16_NORM}, // texcoord
                    {.location = 2, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_I10_10_10_2_NORM}, // normal
                    {.location = 3, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U8_NORM}, // color
                },
            },
        },
        .index_buffer = index_buffer,
        .index_type = MUGFX_INDEX_TYPE_U8,
        .debug_label = "box.geom",
    });

    return geometry;
}

ung_geometry_data geometry_data_load(const char* path)
{
#ifndef UNG_FAST_OBJ
    (void)path;
    ung_panicf("Geometry loading requires fast_obj (UNG_FAST_OBJ=ON)");
    return {};
#else
    auto mesh = fast_obj_read(path);
    if (!mesh) {
        std::printf("Failed to load geometry '%s'\n", path);
        return {};
    }

    ung_geometry_data gdata = {};

    usize vidx = 0;
    bool normals = false, texcoords = false, colors = false;
    for (unsigned int face = 0; face < mesh->face_count; ++face) {
        if (mesh->face_materials[face] < mesh->material_count) {
            colors = true;
        }

        gdata.num_vertices += mesh->face_vertices[face];

        if (mesh->face_vertices[face] == 3) {
            gdata.num_indices += 3;
        } else if (mesh->face_vertices[face] == 4) {
            gdata.num_indices += 6; // two triangles
        } else {
            std::printf("Only triangles and quads are supported - '%s'\n", path);
            return {};
        }

        for (unsigned int vtx = 0; vtx < mesh->face_vertices[face]; ++vtx) {
            const auto [p, t, n] = mesh->indices[vidx++];
            if (t) {
                texcoords = true;
            }
            if (n) {
                normals = true;
            }
        }
    }

    gdata.positions
        = allocate<float>(gdata.num_vertices * (3 + normals * 3 + texcoords * 2 + colors * 4));
    if (normals) {
        gdata.normals = gdata.positions + gdata.num_vertices * 3;
    }
    if (texcoords) {
        gdata.texcoords = gdata.positions + gdata.num_vertices * (3 + normals * 3);
    }
    if (colors) {
        gdata.colors = gdata.positions + gdata.num_vertices * (3 + normals * 3 + texcoords * 2);
    }

    vidx = 0;
    for (unsigned int face = 0; face < mesh->face_count; ++face) {
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        if (mesh->face_materials[face] < mesh->material_count) {
            const auto& mat = mesh->materials[mesh->face_materials[face]];
            r = mat.Kd[0];
            g = mat.Kd[1];
            b = mat.Kd[2];
            a = mat.d;
        }

        for (unsigned int vtx = 0; vtx < mesh->face_vertices[face]; ++vtx) {
            const auto [p, t, n] = mesh->indices[vidx];

            gdata.positions[vidx * 3 + 0] = mesh->positions[3 * p + 0]; // x
            gdata.positions[vidx * 3 + 1] = mesh->positions[3 * p + 1]; // y
            gdata.positions[vidx * 3 + 2] = mesh->positions[3 * p + 2]; // z

            // t or n might be zero, but we don't have to check, becaust fast_obj will add a
            // dummy element that's all zeros.

            if (gdata.texcoords) {
                gdata.texcoords[vidx * 2 + 0] = mesh->texcoords[2 * t + 0]; // u
                gdata.texcoords[vidx * 2 + 1] = mesh->texcoords[2 * t + 1]; // v
            }

            if (gdata.normals) {
                gdata.normals[vidx * 3 + 0] = mesh->normals[3 * n + 0]; // x
                gdata.normals[vidx * 3 + 1] = mesh->normals[3 * n + 1]; // y
                gdata.normals[vidx * 3 + 2] = mesh->normals[3 * n + 2]; // z
            }

            if (gdata.colors) {
                gdata.colors[vidx * 4 + 0] = r; // x
                gdata.colors[vidx * 4 + 1] = g; // y
                gdata.colors[vidx * 4 + 2] = b; // z
                gdata.colors[vidx * 4 + 3] = a; // z
            }

            vidx++;
        }
    }

    // TODO: Generate normals if none are present!

    gdata.indices = allocate<uint32_t>(gdata.num_indices);

    auto indices_it = gdata.indices;
    uint32_t base_vtx = 0;
    for (unsigned int face = 0; face < mesh->face_count; ++face) {
        if (mesh->face_vertices[face] == 3) {
            *(indices_it++) = base_vtx + 0;
            *(indices_it++) = base_vtx + 1;
            *(indices_it++) = base_vtx + 2;
        } else if (mesh->face_vertices[face] == 4) {
            *(indices_it++) = base_vtx + 0;
            *(indices_it++) = base_vtx + 1;
            *(indices_it++) = base_vtx + 2;

            *(indices_it++) = base_vtx + 0;
            *(indices_it++) = base_vtx + 2;
            *(indices_it++) = base_vtx + 3;
        }
        base_vtx += mesh->face_vertices[face];
    }

    fast_obj_destroy(mesh);

    return gdata;
#endif
}

EXPORT ung_geometry_data ung_geometry_data_load(const char* path)
{
    LoadProfScope s(path);
    return geometry_data_load(path);
}

EXPORT void ung_geometry_data_destroy(ung_geometry_data gdata)
{
    const bool normals = gdata.normals;
    const bool texcoords = gdata.texcoords;
    const bool colors = gdata.colors;
    const auto num_floats = gdata.num_vertices * (3 + normals * 3 + texcoords * 2 + colors * 4);
    deallocate(gdata.positions, num_floats);
    deallocate(gdata.indices, gdata.num_indices);
}

EXPORT ung_geometry_id ung_geometry_create(mugfx_geometry_create_params params)
{
    const auto geom = mugfx_geometry_create(params);
    if (!geom.id) {
        ung_panicf("Error creating geometry");
    }

    const auto [id, geometry] = state->geometries.insert();
    geometry->geometry = geom;
    return { id };
}

EXPORT void ung_geometry_recreate(ung_geometry_id geometry_id, mugfx_geometry_create_params params)
{
    const auto geom = mugfx_geometry_create(params);
    if (!geom.id) {
        return;
    }

    auto geometry = get(state->geometries, geometry_id.id);
    mugfx_geometry_destroy(geometry->geometry);
    geometry->geometry = geom;
}

EXPORT void ung_geometry_set_vertex_range(
    ung_geometry_id geometry_id, uint32_t offset, uint32_t count)
{
    const auto geometry = get(state->geometries, geometry_id.id);
    mugfx_geometry_set_vertex_range(geometry->geometry, offset, count);
}

EXPORT void ung_geometry_set_index_range(
    ung_geometry_id geometry_id, uint32_t offset, uint32_t count)
{
    const auto geometry = get(state->geometries, geometry_id.id);
    mugfx_geometry_set_index_range(geometry->geometry, offset, count);
}

static mugfx_geometry_id create_geometry(Vertex* vertices, usize num_vertices, u32* indices,
    usize num_indices, const char* debug_label = nullptr)
{
    mugfx_buffer_id vertex_buffer = mugfx_buffer_create({
        .target = MUGFX_BUFFER_TARGET_ARRAY,
        .data = { vertices, num_vertices * sizeof(Vertex) },
        .debug_label = debug_label,
    });

    mugfx_buffer_id index_buffer = { 0 };
    if (indices) {
        index_buffer = mugfx_buffer_create({
            .target = MUGFX_BUFFER_TARGET_INDEX,
            .data = { indices, num_indices * sizeof(u32) },
            .debug_label = debug_label,
        });
    }

    mugfx_geometry_create_params geometry_params = {
        .vertex_buffers = {
            {
                .buffer = vertex_buffer,
                .attributes = {
                    {.location = 0, .components = 3, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_F32}, // position
                    {.location = 1, .components = 2, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U16_NORM}, // texcoord
                    {.location = 2, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_I10_10_10_2_NORM}, // normal
                    {.location = 3, .components = 4, .type = MUGFX_VERTEX_ATTRIBUTE_TYPE_U8_NORM}, // color
                },
            },
        },
        .debug_label = debug_label,
    };
    if (index_buffer.id) {
        geometry_params.index_buffer = index_buffer;
        geometry_params.index_type = MUGFX_INDEX_TYPE_U32;
    }

    return mugfx_geometry_create(geometry_params);
}

static Vertex* build_vertex_buffer_data(ung_geometry_data gdata)
{
    auto vertices = allocate<Vertex>(gdata.num_vertices);
    for (size_t i = 0; i < gdata.num_vertices; ++i) {
        const auto x = gdata.positions[i * 3 + 0];
        const auto y = gdata.positions[i * 3 + 1];
        const auto z = gdata.positions[i * 3 + 2];

        const auto u = f2u16norm(gdata.texcoords ? gdata.texcoords[i * 2 + 0] : 0.0f);
        const auto v = f2u16norm(gdata.texcoords ? gdata.texcoords[i * 2 + 1] : 0.0f);

        const auto nx = gdata.normals ? gdata.normals[i * 3 + 0] : 0.0f;
        const auto ny = gdata.normals ? gdata.normals[i * 3 + 1] : 0.0f;
        const auto nz = gdata.normals ? gdata.normals[i * 3 + 2] : 0.0f;

        const auto r = f2u8norm(gdata.colors ? gdata.colors[i * 4 + 0] : 1.0f);
        const auto g = f2u8norm(gdata.colors ? gdata.colors[i * 4 + 1] : 1.0f);
        const auto b = f2u8norm(gdata.colors ? gdata.colors[i * 4 + 2] : 1.0f);
        const auto a = f2u8norm(gdata.colors ? gdata.colors[i * 4 + 3] : 1.0f);

        vertices[i] = { x, y, z, u, v, pack1010102(nx, ny, nz), r, g, b, a };
    }
    return vertices;
}

static mugfx_geometry_id geometry_from_data(
    ung_geometry_data gdata, const char* debug_label = nullptr)
{
    // We cannot build a proper indexed mesh trivially, because a face will reference different
    // position, texcoord and normal indices, so you would have to generate all used
    // combinations and build new indices. It's too bothersome and will not be fast.

    auto vertices = build_vertex_buffer_data(gdata);

    const auto geom = create_geometry(
        vertices, gdata.num_vertices, gdata.indices, gdata.num_indices, debug_label);

    deallocate(vertices, gdata.num_vertices);

    return geom;
}

EXPORT ung_geometry_id ung_geometry_create_from_data(ung_geometry_data gdata)
{
    const auto geom = geometry_from_data(gdata);
    if (!geom.id) {
        ung_panicf("Error creating geometry");
    }

    const auto [id, geometry] = state->geometries.insert();
    geometry->geometry = geom;
    return { id };
}

mugfx_geometry_id load_geometry(const char* path)
{
    LoadProfScope s(path);
    ung_load_profiler_push("load");
    const auto gdata = ung_geometry_data_load(path);
    ung_load_profiler_pop("load");
    ung_load_profiler_push("upload");
    const auto geom = geometry_from_data(gdata, path);
    ung_load_profiler_pop("upload");
    ung_geometry_data_destroy(gdata);
    return geom;
}

static bool reload_geometry(Geometry* geometry, const char* path)
{
    const auto geom = load_geometry(path);
    if (!geom.id) {
        return false;
    }

    mugfx_geometry_destroy(geometry->geometry);
    geometry->geometry = geom;
    return true;
}

static bool geometry_reload_cb(void* userdata)
{
    auto ctx = (GeometryReloadCtx*)userdata;
    std::fprintf(stderr, "Reloading geometry: %s\n", ctx->path.data);
    auto geometry = get(state->geometries, ctx->geometry.id);
    return reload_geometry(geometry, ctx->path.data);
}

EXPORT ung_geometry_id ung_geometry_load(const char* path)
{
    const auto geom = load_geometry(path);
    if (!geom.id) {
        ung_panicf("Error loading geometry '%s'", path);
    }

    const auto [id, geometry] = state->geometries.insert();
    geometry->geometry = geom;

    if (state->auto_reload) {
        geometry->reload_ctx = allocate<GeometryReloadCtx>();
        geometry->reload_ctx->geometry = { id };
        assign(geometry->reload_ctx->path, path);
        geometry->resource = ung_resource_create(geometry_reload_cb, geometry->reload_ctx);
        ung_resource_set_deps(geometry->resource, &path, 1, nullptr, 0);
    }

    return { id };
}

EXPORT bool ung_geometry_reload(ung_geometry_id geometry_id, const char* path)
{
    const auto geometry = get(state->geometries, geometry_id.id);

    if (state->auto_reload) {
        geometry->reload_ctx->path.free();
        assign(geometry->reload_ctx->path, path);
        ung_resource_set_deps(geometry->resource, &path, 1, nullptr, 0);
    }

    return reload_geometry(geometry, path);
}

}