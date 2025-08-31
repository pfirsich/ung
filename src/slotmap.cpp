#include <cstdio>
#include <cstdlib>

#include "types.hpp"

#define EXPORT extern "C"

namespace ung {
static constexpr u32 MaxIdx = 0xFF'FFFF;
static constexpr u64 FreeMask = 0xFF00'0000'0000'0000;

// Keys array is used for free list too. If an element is free the index has the upper 8 bits
// set. The rest of the index contains the index to the next free element.
// Key 0 is always invalid

static u64 make_key(u32 idx, u32 gen)
{
    return gen << 24 | idx;
}

EXPORT uint32_t ung_slotmap_get_index(uint64_t key)
{
    return (u32)(key & MaxIdx);
}

EXPORT uint32_t ung_slotmap_get_generation(uint64_t key)
{
    return (u32)((key & 0xFFFF'FF00'0000) >> 24);
}

EXPORT void ung_slotmap_init(ung_slotmap* s)
{
    assert(s->keys);
    assert(s->capacity < 0xFF'FFFF);
    for (u32 i = 0; i < s->capacity; ++i) {
        // We invalidate on removal and we want to start with generation 1, so we init with 1
        s->keys[i] = FreeMask | make_key(i + 1, 1);
    }
    s->free_list_head = 0;
}

EXPORT uint64_t ung_slotmap_insert(ung_slotmap* s, uint32_t* oidx)
{
    assert(oidx);
    const auto idx = s->free_list_head;
    assert(idx < s->capacity);
    if (idx >= s->capacity) {
        return 0;
    }
    assert(s->keys[idx] & FreeMask);
    s->free_list_head = ung_slotmap_get_index(s->keys[idx]);
    const auto gen = ung_slotmap_get_generation(s->keys[idx]);
    s->keys[idx] = make_key(idx, gen);
    *oidx = idx;
    return s->keys[idx];
}

EXPORT uint64_t ung_slotmap_get_key(const ung_slotmap* s, uint32_t idx)
{
    // We just have to check the index here because for valid keys the index will be
    // equal to it's index in the array (that is clear).
    // But for free keys the index will always point to another index, lest the free
    // list would contain a cycle.
    return idx < s->capacity && ung_slotmap_get_index(s->keys[idx]) == idx ? s->keys[idx] : 0;
}

EXPORT uint32_t ung_slotmap_next_alive(const ung_slotmap* s, uint32_t min_index)
{
    for (uint32_t i = min_index; i < s->capacity; ++i) {
        if (ung_slotmap_get_key(s, i)) {
            return i;
        }
    }
    return s->capacity; // past the end
}

EXPORT bool ung_slotmap_contains(const ung_slotmap* s, uint64_t key)
{
    const auto idx = ung_slotmap_get_index(key);
    return (key & FreeMask) == 0 && idx < s->capacity && s->keys[idx] == key;
}

EXPORT bool ung_slotmap_remove(ung_slotmap* s, uint64_t key)
{
    assert(ung_slotmap_contains(s, key));
    if (!ung_slotmap_contains(s, key)) {
        return false;
    }
    const auto idx = ung_slotmap_get_index(key);
    const auto gen = ung_slotmap_get_generation(key);
    assert(s->free_list_head < s->capacity);
    s->keys[idx] = FreeMask | make_key(s->free_list_head, gen + 1);
    s->free_list_head = idx;
    return true;
}
}