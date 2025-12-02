#include "shared_memory.h"

#include "debug_utils.h"

glRemix::SharedMemory::~SharedMemory()
{
    close_all();
}

// `CreateFileMapping`
bool glRemix::SharedMemory::create_for_writer(const wchar_t* map_name,
                                              const wchar_t* write_event_name,
                                              const wchar_t* read_event_name)
{
    close_all();

    const auto h_map_file
        = CreateFileMappingW(INVALID_HANDLE_VALUE,  // use paging file
                             nullptr,               // default security
                             PAGE_READWRITE,        // rw access
                             0,                     // maximum object size (high-order DWORD)
                             max_object_size(),     // maximum object size (low-order DWORD)
                             map_name);             // name of mapping object

    if (!h_map_file)
    {
        DBG_PRINT("SharedMemory.WRITER - `CreateFileMappingW()` failed. Error Code: %u",
                  GetLastError());
        return false;
    }

    if (!map_common(h_map_file))
    {
        DBG_PRINT("SharedMemory.WRITER - Could not map common view. Error Code: %u", GetLastError());

        CloseHandle(h_map_file);
        return false;
    }

    // create named events
    m_write_event = CreateEventW(nullptr, false, false, write_event_name);
    m_read_event = CreateEventW(nullptr, false, true, read_event_name);

    return true;
}

bool glRemix::SharedMemory::open_for_reader(const wchar_t* map_name,
                                            const wchar_t* write_event_name,
                                            const wchar_t* read_event_name)
{
    close_all();

    HANDLE h_map_file = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, map_name);
    if (!h_map_file)
    {
        DBG_PRINT("SharedMemory.READER - `OpenFileMappingW()` failed. Error Code: %u",
                  GetLastError());
        return false;
    }

    if (!map_common(h_map_file))
    {
        DBG_PRINT("SharedMemory.READER - Could not map common view. Error Code: %u", GetLastError());

        CloseHandle(h_map_file);
        return false;
    }

    // Try to open events (safe if they don't exist yet)
    m_write_event = OpenEventW(EVENT_ALL_ACCESS, FALSE, write_event_name);
    m_read_event = OpenEventW(EVENT_ALL_ACCESS, FALSE, read_event_name);

    return true;
}

bool glRemix::SharedMemory::write(const void* src, const UINT32 offset, const UINT32 bytes_to_write)
{
    if (offset + bytes_to_write > this->get_capacity())
    {
#ifdef _DEBUG
        // TODO: Expand SharedMemory dynamically
        // throw std::runtime_error(
        //    "SharedMemory.WRITER - Writer has exceeded current `k_DEFAULT_CAPACITY`.");
#endif
        DBG_PRINT("SharedMemory.WRITER - Writer has exceeded capacity of allocated memory space. "
                  "Will not write_simple.");
        return false;
    }
    memcpy(m_payload + offset, src, bytes_to_write);

    return true;
}

UINT32 glRemix::SharedMemory::read(void* dst, const UINT32 offset, const UINT32 bytes_to_read) const
{
    UINT32 bytes_read = bytes_to_read;
    if (offset + bytes_to_read > this->get_capacity())
    {
        // error handle externally. for now just update bytes_read
        bytes_read = (this->get_capacity() - offset);
    }

    memcpy(dst, m_payload + offset, bytes_read);

    return bytes_read;
}

bool glRemix::SharedMemory::map_common(const HANDLE h_map_file)
{
    m_view = MapViewOfFile(h_map_file,           // handle to map object
                           FILE_MAP_ALL_ACCESS,  // rw permission
                           0, 0, 0);

    if (m_view == nullptr)
    {
        CloseHandle(h_map_file);

        return false;
    }

    m_map = h_map_file;
    m_payload = reinterpret_cast<UINT8*>(m_view);

    return true;
}

void glRemix::SharedMemory::close_all()
{
    if (m_view)
    {
        UnmapViewOfFile(m_view);
        m_view = nullptr;
    }
    if (m_map)
    {
        CloseHandle(m_map);
        m_map = nullptr;
    }
    if (m_write_event)
    {
        CloseHandle(m_write_event);
        m_write_event = nullptr;
    }
    if (m_read_event)
    {
        CloseHandle(m_read_event);
        m_read_event = nullptr;
    }

    // host side buffer
    m_payload = nullptr;
}
