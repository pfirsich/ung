#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;
using uptr = uintptr_t;
using ssize = ptrdiff_t;
using usize = size_t;

#include "allocator.hpp"

namespace ung {

template <typename T, size_t Capacity>
struct StaticVector {
    std::array<T, Capacity> data_;
    usize size_;

    T& append()
    {
        assert(size_ < Capacity);
        return data_[size_++];
    }

    void clear() { size_ = 0; }

    T& operator[](usize idx)
    {
        assert(idx < size_);
        return data_[idx];
    }

    const T& operator[](usize idx) const
    {
        assert(idx < size_);
        return data_[idx];
    }

    auto data() { return data_.data(); }
    auto data() const { return data_.data(); }
    auto size() const { return size_; }
    auto begin() { return data(); }
    auto begin() const { return data(); }
    auto end() { return begin() + size_; }
    auto end() const { return begin() + size_; }
};

template <typename T>
struct Array {
    T* data;
    u32 size;

    void init(u32 s)
    {
        assert(!data);
        data = allocate<T>(s);
        size = s;
    }

    void free()
    {
        deallocate(data, size);
        data = nullptr;
        size = 0;
    }

    T& operator[](u32 idx) { return data[idx]; }
    const T& operator[](u32 idx) const { return data[idx]; }
};

template <typename T>
struct Pool {
    Array<u64> keys;
    Array<T> data;
    ung_slotmap sm;

    void init(u32 capacity)
    {
        keys.init(capacity);
        data.init(capacity);
        sm = ung_slotmap { keys.data, capacity, 0 };
        ung_slotmap_init(&sm);
    }

    void free()
    {
        keys.free();
        data.free();
    }

    std::pair<u64, T*> insert()
    {
        u32 idx = 0;
        const auto id = ung_slotmap_insert(&sm, &idx);
        if (id == 0) {
            return { 0, nullptr };
        }
        T* obj = &data[idx];
        std::memset(obj, 0, sizeof(T));
        return { id, obj };
    }

    T* find(u64 key)
    {
        return ung_slotmap_contains(&sm, key) ? &data[ung_slotmap_get_index(key)] : nullptr;
    }

    void remove(u64 key) { ung_slotmap_remove(&sm, key); }

    u32 capacity() const { return sm.capacity; }

    u64 get_key(u32 idx) const { return ung_slotmap_get_key(&sm, idx); }

    bool contains(u64 key) { return ung_slotmap_contains(&sm, key); }
};

template <typename T>
auto get(Pool<T>& pool, u64 key)
{
    auto obj = pool.find(key);
    assert(obj);
    return obj;
}

}