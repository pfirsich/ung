#pragma once

#include <new> // needed for placement new

#include "ung.h"

namespace ung {

extern ung_allocator allocator;
extern mugfx_allocator mugfx_alloc;

template <typename T>
T* allocate(size_t count = 1)
{
    const auto ptr = reinterpret_cast<T*>(allocator.allocate(sizeof(T) * count, allocator.ctx));
    for (size_t i = 0; i < count; ++i) {
        new (ptr + i) T {};
    }
    return ptr;
}

inline void* reallocate(void* ptr, size_t old_size, size_t new_size)
{
    return allocator.reallocate(ptr, old_size, new_size, allocator.ctx);
}

template <typename T>
void deallocate(T* ptr, size_t count = 1)
{
    if (!ptr) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        (ptr + i)->~T();
    }
    allocator.deallocate(ptr, sizeof(T) * count, allocator.ctx);
}

char* allocate_string(const char* str);
void deallocate_string(char* str);

}