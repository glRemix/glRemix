#pragma once

#include <shared/ipc_protocol.h>
#include <shared/gl_commands.h>

#include "gl_state.h"

#include <array>
#include <vector>
#include <memory>

namespace glRemix
{
constexpr size_t NUM_COMMANDS = static_cast<size_t>(GLCommandType::_COUNT);
constexpr SIZE_T NUM_CLIENT_ARRAYS = static_cast<SIZE_T>(GLRemixClientArrayType::_COUNT);

class glDriver;  // forward declare
struct glState;

// represents view of command within shared memory
struct GLCommandView
{
    GLCommandType type;
    UINT32 cmd_bytes;
    const void* data;
};

// passed in to static handlers to allow them to affect persistent gl state
struct GLCommandContext
{
    glState& state;
    glDriver& driver;  // required for recursive calls like call lists
};

class glDriver
{
    std::unique_ptr<glState> m_state = nullptr;  // glState (and resources) are owned and released
    IPCProtocol m_ipc;

    /* must be heap-allocated due to it's target size (k_MAX_PAYLOAD_SIZE) being upwards of 16mib.
    however, can call ::reserve to ensure allocation only happens once */
    std::vector<UINT8> m_command_buffer{};

    using GLCommandHandler = void (*)(const GLCommandContext&, const void* data);
    std::array<GLCommandHandler, NUM_COMMANDS> gl_command_handlers{};

    void init_handlers();
    bool read_next_command(const UINT8* buffer, size_t buffer_size, size_t& offset,
                           GLCommandView& out);

public:
    void process_stream();
    void read_buffer(const GLCommandContext& ctx, const UINT8* buffer, size_t buffer_size,
                     size_t& offset);

    inline glState& get_state()
    {
        return *m_state;
    }

    inline const UINT8* get_command_buffer_data() const
    {
        return m_command_buffer.data();
    }

    void init(const HWND local_hwnd);

    inline void destroy()
    {
        m_ipc.shutdown_reader();
        m_state.reset();
    };

    inline bool shim_is_running()
    {
        return !m_ipc.has_writer_shutdown();
    }

    glDriver() = default;
    ~glDriver() = default;
};

UINT64 compute_mesh_hash(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices);
}  // namespace glRemix
