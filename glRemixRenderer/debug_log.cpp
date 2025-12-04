#include "debug_log.h"

#include <cstring>

namespace glRemix
{
static DebugLog g_log;

DebugLog& get_debug_log()
{
    return g_log;
}

void dbglog_push(const char* msg)
{
    if (!msg)
    {
        return;
    }
    DebugLog& log = get_debug_log();
    const auto seq = log.write_seq.fetch_add(1, std::memory_order_relaxed);
    const auto idx = static_cast<UINT32>(seq % DebugLog::k_capacity);

    DebugLogEntry& e = log.buffer[idx];
    std::strncpy(e.text, msg, sizeof(e.text) - 1);
    e.text[sizeof(e.text) - 1] = '\0';

    e.seq = seq + 1;
}

UINT64 dbglog_current_seq()
{
    return get_debug_log().write_seq.load(std::memory_order_relaxed);
}

}  // namespace glRemix
