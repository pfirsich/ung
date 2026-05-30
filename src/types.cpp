#include "types.hpp"

#include <cmath>

namespace ung {

float clamp(float v, float min, float max)
{
    return std::fmin(std::fmax(v, min), max);
}

float saturate(float v)
{
    return clamp(v, 0.0f, 1.0f);
}

u16 f2u16norm(float v)
{
    return (u16)(65535.0f * saturate(v));
}

u8 f2u8norm(float v)
{
    return (u8)(255.0f * saturate(v));
}

void assign(Array<char>& arr, const char* str)
{
    if (arr.data == str) {
        return;
    }
    if (arr.data) {
        arr.free();
    }
    arr.init((uint32_t)std::strlen(str) + 1);
    std::strcpy(arr.data, str);
}

void StrPool::init(u32 ps)
{
    offset = 0;
    page_size = ps;
    pages.init(4);
    pages.push(allocate<char>(page_size));
}

std::string_view StrPool::insert(std::string_view str)
{
    assert(str.size() + 1 <= page_size);
    if (offset + str.size() + 1 > page_size) {
        // insert new page
        pages.push(allocate<char>(page_size));
        offset = 0;
    }
    auto dest = pages.last() + offset;
    std::memcpy(dest, str.data(), str.size());
    offset += str.size();
    pages.last()[offset] = '\0';
    offset++;
    return { dest, str.size() };
}

bool is_same_binding(const mugfx_draw_binding& a, const mugfx_draw_binding& b)
{
    if (a.type != b.type) {
        return false;
    }
    switch (a.type) {
    case MUGFX_BINDING_TYPE_TEXTURE:
        return a.texture.binding == b.texture.binding;
    case MUGFX_BINDING_TYPE_BUFFER:
        return a.buffer.binding == b.buffer.binding;
    default:
        return false;
    }
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

bool Formatter::append(std::string_view str)
{
    if (offset + str.size() >= buf.size() - 1) {
        offset = SIZE_MAX; // mark this full
        return false;
    }
    memcpy(buf.data() + offset, str.data(), str.size());
    offset += str.size();
    buf[offset] = '\0';
    return true;
}

bool Formatter::append_hex(const void* data, size_t size)
{
    if (offset + size * 2 >= buf.size() - 1) {
        offset = SIZE_MAX; // mark this full
        return false;
    }
    fmt_hex(buf.data() + offset, data, size);
    offset += size * 2;
    buf[offset] = '\0';
    return true;
}

}