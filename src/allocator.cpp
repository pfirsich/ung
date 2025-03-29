#include "allocator.hpp"

#include <cstdlib>

namespace ung {

void* default_allocate(size_t size, void*)
{
    return std::malloc(size);
}

void* default_reallocate(void* ptr, size_t, size_t new_size, void*)
{
    return std::realloc(ptr, new_size);
}

void default_deallocate(void* ptr, size_t, void*)
{
    return std::free(ptr);
}

ung_allocator allocator = {
    .allocate = default_allocate,
    .reallocate = default_reallocate,
    .deallocate = default_deallocate,
    .ctx = nullptr,
};

void* mugfx_allocate(size_t size, void* ctx)
{
    return allocator.allocate(size, ctx);
}

void* mugfx_reallocate(void* ptr, size_t old_size, size_t new_size, void* ctx)
{
    return allocator.reallocate(ptr, old_size, new_size, ctx);
}

void mugfx_deallocate(void* ptr, size_t size, void* ctx)
{
    return allocator.deallocate(ptr, size, ctx);
}

mugfx_allocator mugfx_alloc {
    .allocate = mugfx_allocate,
    .reallocate = mugfx_reallocate,
    .deallocate = mugfx_deallocate,
    .ctx = nullptr,
};

}