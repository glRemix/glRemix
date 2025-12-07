#include "arena.h"
#include "shared/math_utils.h"

using namespace glRemix;

void Arena::init(const size_t capacity)
{
    m_memory = std::make_unique<UINT8[]>(capacity);
    m_capacity = capacity;
    m_offset = 0;
}

void Arena::reset()
{
    m_offset = 0;
}

void* Arena::alloc(const size_t size, const size_t alignment)
{
    assert(size);
    assert(alignment);

    const auto current_address = reinterpret_cast<size_t>(m_memory.get()) + m_offset;
    const auto aligned_address = align_u64(current_address, alignment);
    const auto padding = aligned_address - current_address;
    const auto total_size = size + padding;
    if (m_offset + total_size > m_capacity)
    {
        return nullptr;
    }
    m_offset += padding;
    void* ptr = m_memory.get() + m_offset;
    m_offset += size;

    std::memset(ptr, 0, size);
    return ptr;
}

// Simple method using thread local storage to obtain per thread arena, proper solution would need a
// pool of arenas (would need to be lock free to be efficient)
thread_local Arena arena;
thread_local bool initialized;

Arena& glRemix::get_arena(const size_t capacity)
{
    if (!initialized)
    {
        arena.init(capacity);
        initialized = true;
    }
    // Not allowed to change capacity after first use
    assert(capacity <= arena.capacity);
    return arena;
}
