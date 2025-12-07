#pragma once

#include <basetsd.h>
#include <memory>

#include "math_utils.h"

namespace glRemix
{
class Arena
{
    std::unique_ptr<UINT8[]> m_memory = nullptr;
    size_t m_capacity = 0;
    size_t m_offset = 0;

public:
    const size_t& capacity = m_capacity;
    void init(size_t capacity);
    // Resets offset. Does not zero memory.
    void reset();

    // Returns zeroed block of given size and alignment. Returns null if arena is full.
    void* alloc(size_t size, size_t alignment = alignof(std::max_align_t));

    // Returns default initialized array of given type. Returns null if arena is full.
    template<typename T>
    T* alloc_array(const size_t count)
    {
        static_assert(std::is_default_constructible_v<T>);
        auto mem = alloc(count * sizeof(T), alignof(T));
        if (!mem)
        {
            return nullptr;
        }
        auto arr = static_cast<T*>(mem);
        for (size_t i = 0; i < count; i++)
        {
            new (&arr[i]) T();  // Invoke default constructor
        }
        return arr;
    }
};

// Use the same argument per thread, we don't allow reallocation.
Arena& get_arena(size_t capacity = 64 * MEGABYTE);

}  // namespace glRemix
