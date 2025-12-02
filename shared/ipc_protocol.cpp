#include "ipc_protocol.h"

#include "debug_utils.h"

#include <thread>
#include <chrono>

#include <system_error>

void glRemix::IPCProtocol::init_writer()
{
    if (!m_slot_A.smem.create_for_writer(k_MAP_A, k_WRITE_EVENT_A, k_READ_EVENT_A))
    {
        throw std::runtime_error("IPCProtocol.WRITER - Failed to create SharedMemory A");
    }
    if (!m_slot_B.smem.create_for_writer(k_MAP_B, k_WRITE_EVENT_B, k_READ_EVENT_B))
    {
        throw std::runtime_error("IPCProtocol.WRITER - Failed to create SharedMemory B");
    }
}

void glRemix::IPCProtocol::start_frame_or_wait()
{
    {
        MemorySlot* oldest;  // shared memory that has the smaller recorded time frame
        MemorySlot* latest;  // shared memory that has the larger recorded time frame
        if (m_slot_A < m_slot_B)
        {
            oldest = &m_slot_A;
            latest = &m_slot_B;
        }
        else
        {
            oldest = &m_slot_B;
            latest = &m_slot_A;
        }

        HANDLE tmp_events[2] = { oldest->smem.get_read_event(), latest->smem.get_read_event() };

        DWORD dw_wait_result = WaitForMultipleObjects(2, tmp_events, false, k_GLOBAL_TIMEOUT_MS);

        switch (dw_wait_result)
        {
            case WAIT_OBJECT_0:
                m_curr_slot = oldest;
                DBG_PRINT("IPCProtocol.WRITER - Oldest SharedMemory slot became available to "
                          "write before latest.");  // theoretically should not occur
                break;
            case WAIT_OBJECT_0 + 1: m_curr_slot = latest; break;
            case WAIT_TIMEOUT:
                throw std::system_error(
                    ERROR_TIMEOUT, std::system_category(),
                    "IPCProtocol.WRITER - Timed out while waiting for IPC memory to become "
                    "available to write. Check the READER process for stalls.");
            default:
                // this is a runtime_error as it is unpredictable to my knowledge
                throw std::runtime_error(FSTR(
                    "IPCProtocol.WRITER - WaitForMultipleObjects for writer failed. Error Code: {}",
                    dw_wait_result));
        }
    }

    g_frame_index++;
    m_curr_slot->frame_index = g_frame_index;

    m_offset = sizeof(GLFrameHeader);  // reset to just the size of GLFrameHeader
}

void glRemix::IPCProtocol::end_frame()
{
    if (!m_curr_slot)
    {
        throw std::logic_error("IPCProtocol.WRITER - `m_curr_slot` is null at time of frame end.");
    }

    GLFrameHeader header = { .frame_index = m_curr_slot->frame_index,
                             .frame_bytes = m_offset - sizeof(GLFrameHeader) };

    m_curr_slot->smem.write(&header, 0, sizeof(GLFrameHeader));

    m_curr_slot->smem.signal_write_event();
}

void glRemix::IPCProtocol::init_reader()
{
    constexpr UINT16 MAX_WAIT_MS = 60000;  // 1 minute timeout for graphics debugger workflow
    constexpr UINT16 RETRY_MS = 50;

    UINT16 elapsed = 0;
    while (elapsed < MAX_WAIT_MS)
    {
        bool result_A = m_slot_A.smem.open_for_reader(k_MAP_A, k_WRITE_EVENT_A, k_READ_EVENT_A);
        bool result_B = m_slot_B.smem.open_for_reader(k_MAP_B, k_WRITE_EVENT_B, k_READ_EVENT_B);

        if (result_A && result_B)
        {
            m_curr_slot = &m_slot_A;
            return;  // success
        }
        else
        {
            if (!result_A)
            {
                DBG_PRINT(
                    "IPCProtocol.READER - Failed to open SharedMemory A after %u milliseconds.",
                    elapsed);
            }
            if (!result_B)
            {
                DBG_PRINT(
                    "IPCProtocol.READER - Failed to open SharedMemory B after %u milliseconds.",
                    elapsed);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_MS));

        elapsed += RETRY_MS;
    }
    // this is a runtime_error as it should not happen if shim and renderer are correctly launched
    throw std::runtime_error("IPCProtocol.READER - Timed out waiting for writer initialization.");
}

void glRemix::IPCProtocol::consume_frame_or_wait(void* payload, UINT32* frame_index,
                                                 UINT32* frame_bytes)
{
    {
        MemorySlot* oldest;  // shared memory that has the smaller recorded time frame
        MemorySlot* latest;  // shared memory that has the larger recorded time frame
        if (m_slot_A < m_slot_B)
        {
            oldest = &m_slot_A;
            latest = &m_slot_B;
        }
        else
        {
            oldest = &m_slot_B;
            latest = &m_slot_A;
        }

        HANDLE tmp_events[2] = { oldest->smem.get_write_event(), latest->smem.get_write_event() };

        DWORD dw_wait_result = WaitForMultipleObjects(2, tmp_events, false, k_GLOBAL_TIMEOUT_MS);

        switch (dw_wait_result)
        {
            case WAIT_OBJECT_0: m_curr_slot = oldest; break;
            case WAIT_OBJECT_0 + 1:
                m_curr_slot = latest;
                DBG_PRINT("IPCProtocol.READER - Latest SharedMemory slot became available to read "
                          "before oldest.");
                break;  // theoretically should not occur
            case WAIT_TIMEOUT:
                throw std::system_error(
                    ERROR_TIMEOUT, std::system_category(),
                    "IPCProtocol.READER - Timed out while waiting for IPC memory to become "
                    "available to read. Check the WRITER process for stalls.");
            default:
                // this is a runtime_error as it is unpredictable to my knowledge
                throw std::runtime_error(FSTR(
                    "IPCProtocol.READER - WaitForMultipleObjects for reader failed. Error Code: {}",
                    dw_wait_result));
        }
    }

    thread_local GLFrameHeader frame_header;

    m_curr_slot->smem.read(&frame_header, 0, sizeof(GLFrameHeader));

    *frame_index = frame_header.frame_index;
    *frame_bytes = frame_header.frame_bytes;

    UINT32 payload_bytes_read = m_curr_slot->smem.read(payload, sizeof(GLFrameHeader), *frame_bytes);

    if (payload_bytes_read != *frame_bytes)
    {
        // this is a logic error as this should really never happen.
        // if it occurs, writer logic is incorrect
        throw std::logic_error(FSTR("IPCProtocol.READER - Tried to read {} but only read {}",
                                    *frame_bytes, payload_bytes_read));
    }

    m_curr_slot->smem.signal_read_event();
}

void glRemix::IPCProtocol::write_simple(const void* ptr, SIZE_T bytes)
{
    m_curr_slot->smem.write(ptr, m_offset, bytes);
    m_offset += bytes;
}
