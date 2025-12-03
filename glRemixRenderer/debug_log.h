#pragma once
#include <atomic>
#include <basetsd.h>

namespace glRemix
{
struct DebugLogEntry
{
    UINT64 seq;
    char text[256];
};

// Lock free debug log using circular buffer
struct DebugLog
{
    static constexpr UINT32 k_capacity = 1024;
    std::atomic<UINT64> write_seq{ 0 };
    DebugLogEntry buffer[k_capacity]{};
    DebugLog() = default;
    DebugLog(const DebugLog&) = delete;
    DebugLog& operator=(const DebugLog&) = delete;
};

DebugLog& get_debug_log();
void dbglog_push(const char* msg);
UINT64 dbglog_current_seq();

}  // namespace glRemix
