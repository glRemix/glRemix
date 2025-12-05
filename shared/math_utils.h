#pragma once

#include <cstdint>
#include <cassert>

constexpr UINT64 MEGABYTE = 1024ul * 1024ul;
constexpr UINT CB_ALIGNMENT = 256;

inline bool u64_overflows_u32(const UINT64 v)
{
    return v > UINT32_MAX;
}

inline UINT64 ceil_div(const UINT64 a, const UINT64 b)
{
    return (a + b - 1) / b;
}

inline bool is_power_of_two(const UINT32 value)
{
    return (value & (value - 1)) == 0;
}

inline bool is_power_of_two(const UINT64 value)
{
    return (value & (value - 1)) == 0;
}

inline bool is_multiple_of_power_of_two(const UINT32 value, const UINT32 divisor)
{
    assert(is_power_of_two(divisor));
    return (value & (divisor - 1)) == 0;
}

inline bool is_multiple_of_power_of_two(const UINT64 value, const UINT64 divisor)
{
    assert(is_power_of_two(divisor));
    return (value & divisor - 1) == 0;
}

inline UINT32 align_u32(const UINT32 size, const UINT32 alignment)
{
    assert(is_power_of_two(alignment));
    return size + alignment - 1 & ~(alignment - 1);
}

inline UINT64 align_u64(const UINT64 size, const UINT64 alignment)
{
    return size + alignment - 1 & ~(alignment - 1);
}

inline void utf8_to_wide(const char* utf8, WCHAR* wide, size_t wide_size)
{
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, static_cast<int>(wide_size));
}

static void wide_to_utf8(const WCHAR* wide, char* utf8, size_t utf8_size)
{
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, static_cast<int>(utf8_size), NULL, NULL);
}
