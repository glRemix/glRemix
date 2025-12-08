#pragma once

#include <windows.h>
#include <cstdint>
#include <cassert>

constexpr UINT64 MEGABYTE = 1024ull * 1024ull;
constexpr UINT CB_ALIGNMENT = 256u;

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
