#pragma once
#include <ipc_protocol.h>  // TODO: per-project prefixes
#include <gl_commands.h>
#include <vector>
#include <mutex>

namespace glRemix
{
class FrameRecorder
{
public:
    inline bool Initialize()
    {
        return m_ipc.InitWriter();
    }

    inline void Record(const GLCommandType type, const void* payload, uint32_t size)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_recording)
        {
            GLCommandObject cmd;
            cmd.cmdUnifs = {type, size};
            cmd.data.resize(size);
            std::memcpy(cmd.data.data(), payload, size);

            m_frameUnifs.payloadSize += sizeof(cmd.cmdUnifs) + size;

            m_commands.push_back(std::move(cmd));
        }
    }

    inline void StartFrame()
    {
        m_recording = true;
    }

    inline void EndFrame()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_recording)
        {
            return;
        }

        m_recording = false;

        std::vector<uint8_t> buffer;
        buffer.reserve(sizeof(m_frameUnifs) + m_frameUnifs.payloadSize);

        buffer.insert(buffer.end(),
                      reinterpret_cast<uint8_t*>(&m_frameUnifs),
                      reinterpret_cast<uint8_t*>(&m_frameUnifs) + sizeof(m_frameUnifs));

        if (!m_commands.empty())
        {
            for (auto& cmd : m_commands)
            {
                buffer.insert(buffer.end(),
                              reinterpret_cast<uint8_t*>(&cmd.cmdUnifs),
                              reinterpret_cast<uint8_t*>(&cmd.cmdUnifs) + sizeof(cmd.cmdUnifs));
                buffer.insert(buffer.end(), cmd.data.begin(), cmd.data.end());
            }
        }

        m_ipc.SendFrame(buffer.data(), static_cast<uint32_t>(buffer.size()));

        m_commands.clear();
        m_frameUnifs.payloadSize = 0;
        m_frameUnifs.frameIndex++;
    }

    inline uint8_t* GetScratchBuffer(size_t requiredSize)
    {
        static thread_local std::vector<uint8_t> scratch;
        if (scratch.size() < requiredSize)
            scratch.resize(requiredSize);
        return scratch.data();
    }

private:
    struct GLCommandObject
    {
        GLCommandUnifs cmdUnifs;
        std::vector<uint8_t> data;
    };

    IPCProtocol m_ipc;

    std::mutex m_mutex;
    bool m_recording = false;

    GLFrameUnifs m_frameUnifs;
    std::vector<GLCommandObject> m_commands;
};
}  // namespace glRemix
