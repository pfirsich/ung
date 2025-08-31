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

#define EXPORT extern "C"

namespace ung {

float clamp(float v, float min, float max);
float saturate(float v);
u16 f2u16norm(float v);
u8 f2u8norm(float v);

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

void assign(Array<char>& arr, const char* str);

template <typename T>
struct Vector {
    static_assert(std::is_trivially_copyable_v<T>);

    T* data;
    u32 size;
    u32 capacity;

    void init(u32 c)
    {
        assert(!data);
        data = allocate<T>(c);
        capacity = c;
        size = 0;
    }

    void push(T v)
    {
        if (size == capacity) {
            auto temp = allocate<T>(capacity * 2);
            std::memcpy(temp, data, size * sizeof(T));
            deallocate(data, capacity);
            data = temp;
            capacity = capacity * 2;
        }
        data[size++] = std::move(v);
    }

    void remove(u32 idx)
    {
        assert(idx < size);
        for (size_t i = idx; i < size - 1; ++i) {
            data[i] = data[i + 1];
        }
        size--;
    }

    void clear() { size = 0; }

    void free()
    {
        deallocate(data, capacity);
        data = nullptr;
        size = 0;
        capacity = 0;
    }

    T& operator[](u32 idx) { return data[idx]; }
    const T& operator[](u32 idx) const { return data[idx]; }
};

template <typename Container, typename T>
u32 find(const Container& c, const T& v)
{
    static_assert(std::is_trivially_copyable_v<T>);
    for (u32 i = 0; i < c.size; ++i) {
        if (std::memcmp(&c.data[i], &v, sizeof(T)) == 0) {
            return i;
        }
    }
    return (u32)-1;
}

template <typename Container>
void remove(Container& c, u32 idx)
{
    assert(idx < c.size);
    for (size_t i = idx; i < c.size - 1; ++i) {
        c.data[i] = c.data[i + 1];
    }
    c.size--;
}

template <typename Container, typename T>
void remove_v(Container& c, const T& v)
{
    const auto idx = find(c, v);
    assert(idx < c.size);
    remove(c, idx);
}

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