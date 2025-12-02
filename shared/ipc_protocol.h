#pragma once

#include <windows.h>

#include "gl_commands.h"
#include "shared_memory.h"

#include <stdexcept>

namespace glRemix
{
// Local keyword allows to stay per-session, Global requires elevated permissions
// wchar_t is standard for file mapping names in windows (though can maybe switch with TCHAR*)
constexpr const wchar_t* k_MAP_A = L"Local\\glRemix_Map_A";
constexpr const wchar_t* k_WRITE_EVENT_A = L"Local\\glRemix_WriteEvent_A";
constexpr const wchar_t* k_READ_EVENT_A = L"Local\\glRemix_ReadEvent_A";

constexpr const wchar_t* k_MAP_B = L"Local\\glRemix_Map_B";
constexpr const wchar_t* k_WRITE_EVENT_B = L"Local\\glRemix_WriteEvent_B";
constexpr const wchar_t* k_READ_EVENT_B = L"Local\\glRemix_ReadEvent_B";

constexpr UINT32 k_MAX_IPC_PAYLOAD = k_DEFAULT_CAPACITY - sizeof(GLFrameHeader);

constexpr DWORD k_READER_INIT_TIMEOUT_MS = 60000;  // 1 minute timeout for graphics debugger workflow
constexpr DWORD k_READER_INIT_RETRY_MS = 50;

constexpr DWORD k_GLOBAL_TIMEOUT_MS = 5000;  // 5 second timeout for all `WaitForXXX` operations

// For now, a simple manager for `SharedMemory
class IPCProtocol
{
public:
    // for shim
    void init_writer();
    void start_frame_or_wait();  // uses `WaitForMultipleObjects` to stall thread here

    /*
     * Writes `GLFrameHeader` at 0 offset of IPC file-mapped object.
     * Signals write event when complete
     */
    void end_frame();

    // for renderer
    void init_reader();
    // uses `WaitForMultipleObjects` to stall thread here. signals read event when complete.
    void consume_frame_or_wait(void* payload, UINT32* frame_index, UINT32* frame_bytes);

    void write_simple(const void* ptr, SIZE_T bytes);

#include "ipc_protocol.inl"

private:
    struct MemorySlot
    {
        SharedMemory smem;
        UINT32 frame_index = 0;  // each smem keeps track of their own frame index for now

        inline bool operator<(const MemorySlot& other) const
        {
            return frame_index < other.frame_index;
        }
    };

    // double buffers
    MemorySlot m_slot_A;
    MemorySlot m_slot_B;

    UINT32 g_frame_index = 0;

    MemorySlot* m_curr_slot = nullptr;

    UINT32 m_offset = 0;
};
}  // namespace glRemix
