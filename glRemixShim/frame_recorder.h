#pragma once

#include <vector>
#include <mutex>

#include <shared/ipc_protocol.h>
#include <shared/gl_commands.h>

namespace glRemix
{
class FrameRecorder
{
    struct GLCommandObject
    {
        GLCommandUnifs cmd_unifs;
        std::vector<UINT8> data;
    };

    IPCProtocol m_ipc;

    std::mutex m_mutex;
    bool m_recording = false;

    GLFrameUnifs m_frame_unifs;
    std::vector<GLCommandObject> m_commands;
    std::vector<UINT8> m_buffer;

public:
    bool initialize();

    void record(GLCommandType type, const void* payload, UINT32 size);

    void start_frame();

    void end_frame();
};

extern FrameRecorder g_recorder;

}  // namespace glRemix
