#pragma once

#include <shared/ipc_protocol.h>

#include "framework.h"

namespace glRemix
{
extern glRemix::IPCProtocol g_ipc;

namespace gl
{
extern HANDLE g_renderer_process;

/**
 * @brief
 * @return Will still return true if initialization already occurred.
 * i.e. it is still the first frame.
 */
bool initialize();

/**
 * @brief
 * @return Will still return true if renderer already launched.
 * i.e. it is after the first frame.
 */
bool launch_renderer();

// Add function pointer to hooks map using name as key
void register_hook(const char* name, PROC proc);

// Try to return hooked function pointer using name as hook map lookup
PROC find_hook(const char* name);

// Print out missing function name to debug output
void report_missing_function(const char* name);

/**
 * @brief
 * @return Will return false if shutdown did not occur.
 * i.e. it is still the first frame.
 */
bool shutdown();
}  // namespace gl
}  // namespace glRemix
