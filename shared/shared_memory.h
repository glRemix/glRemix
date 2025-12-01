#pragma once

#include <windows.h>

#include "math_utils.h"

namespace glRemix
{
constexpr UINT32 k_DEFAULT_CAPACITY = 16 * MEGABYTE;

class SharedMemory
{
public:
    SharedMemory() = default;
    ~SharedMemory();

    // writer creates or opens existing mapping and initializes frame header.
    bool create_for_writer(const wchar_t* map_name, const wchar_t* write_event_name,
                           const wchar_t* read_event_name);

    // reader opens existing mapping and maps view.
    bool open_for_reader(const wchar_t* map_name, const wchar_t* write_event_name,
                         const wchar_t* read_event_name);

    // returns true if write success
    bool write(const void* src, const UINT32 offset, const UINT32 bytes_to_write);

    // returns true if read success
    UINT32 read(void* dst, const UINT32 offset, const UINT32 bytes_to_read) const;

    inline UINT32 get_capacity() const
    {
        return m_capacity;
    }

    // accesssors
    inline HANDLE get_write_event() const
    {
        return m_write_event;
    }

    inline HANDLE get_read_event() const
    {
        return m_read_event;
    }

    inline bool signal_write_event() const
    {
        return SetEvent(m_write_event);
    }

    inline bool signal_read_event() const
    {
        return SetEvent(m_read_event);
    }

private:
    HANDLE m_map = nullptr;  // windows provides `HANDLE` typedef
    LPVOID m_view = nullptr;
    UINT8* m_payload = nullptr;

    // currently, this is never modified.
    // Only foreseeable refactors is if we want to allow expansion of memory capacity
    UINT32 m_capacity = k_DEFAULT_CAPACITY;

    HANDLE m_write_event = nullptr;
    HANDLE m_read_event = nullptr;

    // helpers
    bool map_common(HANDLE h_map);
    void close_all();

    inline DWORD max_object_size()
    {
        return static_cast<DWORD>(m_capacity);
    }
};
}  // namespace glRemix
