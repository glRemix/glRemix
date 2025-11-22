#pragma once

#include <shared/ipc_protocol.h>
#include <shared/gl_commands.h>

#include "gl_state.h"

#include <vector>
#include <array>

namespace glRemix
{
constexpr size_t NUM_COMMANDS = static_cast<size_t>(GLCommandType::END_COMMANDS);
class glDriver;  // forward declare
class glState;

// represents view of command within shared memory
struct GLCommandView
{
    GLCommandType type;
    UINT32 data_size;
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
    glState m_state;
    IPCProtocol m_ipc;
    std::vector<UINT8> m_command_buffer;

    using GLCommandHandler = void (*)(const GLCommandContext&, const void* data);
    std::array<GLCommandHandler, NUM_COMMANDS> gl_command_handlers{};

    void init();
    void init_handlers();
    bool read_next_command(const UINT8* buffer, size_t buffer_size, size_t& offset,
                           GLCommandView& out);

public:
    void process_stream();
    void read_buffer(const GLCommandContext& ctx, const UINT8* buffer, size_t buffer_size,
                     size_t& offset);

    glState& get_state()
    {
        return m_state;
    }

    const UINT8* get_command_buffer_data() const
    {
        return m_command_buffer.data();
    }

    glDriver();
    ~glDriver() = default;
};
}  // namespace glRemix
