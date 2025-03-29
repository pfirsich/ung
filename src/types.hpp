#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

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
    T* data_ = nullptr;
    u32 size_ = 0;

    void init(u32 size)
    {
        assert(!data_);
        data_ = allocate<T>(size);
        size_ = size;
    }

    ~Array() { deallocate(data_, size_); }

    T& operator[](u32 idx) { return data_[idx]; }
    const T& operator[](u32 idx) const { return data_[idx]; }
};

struct SlotMap {
    struct Key {
        static constexpr u32 MaxIdx = 0xFF'FFFF;
        static constexpr u32 FreeMask = 0xFF00'0000;
        // Both of these use only 24 bits each
        u32 idx;
        u32 gen;

        static Key empty() { return { 0, 0 }; }

        static Key create(u64 key)
        {
            return {
                .idx = static_cast<u32>(key & MaxIdx),
                .gen = static_cast<u32>((key & 0xFFFF'FF00'0000) >> 24),
            };
        }

        u64 combine() const { return gen << 24 | idx; }

        bool is_free() const { return idx > MaxIdx; }

        u32 get_free() const { return idx & MaxIdx; }

        void set_free(u32 next_free)
        {
            idx = FreeMask | next_free;
            gen++;
            assert(gen <= MaxIdx);
        }

        explicit operator bool() const { return gen != 0; }
        bool operator!=(const Key& o) const { return idx != o.idx || gen != o.gen; }
        bool operator==(const Key& o) const { return idx == o.idx && gen == o.gen; }
    };

    Key* keys_ = nullptr;
    u32 capacity_ = 0;
    u32 free_list_head_ = 0;

    void init(u32 capacity)
    {
        assert(!keys_);
        assert(capacity <= 0xFF'FFFF);
        keys_ = allocate<Key>(capacity);
        capacity_ = capacity;
        for (u32 i = 0; i < capacity_; ++i) {
            // We invalidate on removal and we want to start with generation 1, so we init with 1
            keys_[i].set_free(i + 1);
        }
    }

    ~SlotMap() { deallocate(keys_, capacity_); }

    Key insert()
    {
        const auto idx = free_list_head_;
        assert(idx < capacity_);
        if (idx >= capacity_) {
            return { 0, 0 };
        }
        assert(keys_[idx].is_free());
        free_list_head_ = keys_[idx].get_free();
        keys_[idx].idx = idx;
        return keys_[idx];
    }

    Key get_key(usize idx) const
    {
        return idx < static_cast<usize>(capacity_) && keys_[idx].idx == idx ? keys_[idx]
                                                                            : Key { 0, 0 };
    }

    bool contains(Key key) const { return key.idx < capacity_ && keys_[key.idx] == key; }

    bool remove(Key key)
    {
        assert(contains(key));
        if (!contains(key)) {
            return false;
        }
        keys_[key.idx].set_free(free_list_head_);
        free_list_head_ = key.idx;
        return true;
    }

    u32 capacity() const { return capacity_; }
};

template <typename T>
struct Pool {
    SlotMap keys;
    Array<T> data;

    void init(u32 capacity)
    {
        keys.init(capacity);
        data.init(capacity);
    }

    std::pair<SlotMap::Key, T*> insert()
    {
        const auto id = keys.insert();
        T* obj = &data[id.idx];
        std::memset(obj, 0, sizeof(T));
        return { id, obj };
    }

    T* find(SlotMap::Key key) { return keys.contains(key) ? &data[key.idx] : nullptr; }
    T* find(u64 key) { return find(SlotMap::Key::create(key)); }
    void remove(u64 key) { keys.remove(SlotMap::Key::create(key)); }
};

}