#include "gl_loader.h"

#include <mutex>
#include <string>
#include <tsl/robin_map.h>

#include "frame_recorder.h"

namespace glRemix
{
namespace gl
{
// Both WGL and OpenGL functions may be called from multiple threads hence the mutex
std::mutex g_hook_mutex;

// Function pointers for our custom hook implementations
tsl::robin_map<std::string, PROC> g_hooks;

HANDLE g_renderer_process = nullptr;

std::once_flag g_initialize_flag;

void initialize()
{
    // create lambda function for `std::call_once`
    auto initialize_once_fn = []
    {
        if (!g_recorder.initialize())
        {
            OutputDebugStringA("glRemix: Recorder init failed.\n");
        }
        else
        {
            OutputDebugStringA("glRemix: Recorder ready.\n");
        }
        g_recorder.start_frame();

        // Start the renderer as a subprocess
        // Get the DLL path then expect to find "glRemix_renderer.exe" alongside it
        std::array<char, MAX_PATH> dll_path{};
        HMODULE h_module;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                   | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&initialize), &h_module)
            && GetModuleFileNameA(h_module, dll_path.data(), MAX_PATH))
        {
            // Truncate at last backslash to get directory
            *strrchr(dll_path.data(), '\\') = '\0';
            // Append renderer executable name
            char* end = strrchr(dll_path.data(), '\0');
            const size_t remaining = MAX_PATH - (end - dll_path.data());
            strcpy_s(end, remaining, "\\glRemix_renderer.exe");

#ifdef GLREMIX_CUSTOM_RENDERER_EXE_PATH
            // override `dll_path`
            std::array renderer_path = std::to_array(GLREMIX_CUSTOM_RENDERER_EXE_PATH);
            strcpy_s(dll_path.data(), dll_path.size(), renderer_path.data());
#endif
            STARTUPINFOA si{ .cb = sizeof(STARTUPINFOA) };
            PROCESS_INFORMATION pi;
            if (CreateProcessA(nullptr, dll_path.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &si, &pi))
            {
                OutputDebugStringA("glRemix: Renderer started.\n");
                // Store handle to terminate renderer on DLL unload
                g_renderer_process = pi.hProcess;
                CloseHandle(pi.hThread);
            }
            else
            {
                OutputDebugStringA("glRemix: Failed to start renderer.\n");
                // TODO: Treat as critical error?
            }
        }
    };

    std::call_once(g_initialize_flag, initialize_once_fn);
}

void register_hook(const char* name, const PROC proc)
{
    if (name == nullptr)
    {
        return;
    }

    std::scoped_lock lock(g_hook_mutex);
    if (proc == nullptr)
    {
        g_hooks.erase(name);
        return;
    }

    g_hooks[name] = proc;
}

PROC find_hook(const char* name)
{
    if (name == nullptr)
    {
        return nullptr;
    }

    std::scoped_lock lock(g_hook_mutex);
    if (g_hooks.contains(name))
    {
        return g_hooks[name];
    }

    return nullptr;
}

void report_missing_function(const char* name)
{
    if (name == nullptr)
    {
        return;
    }

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "glRemix ERROR: missing OpenGL symbol: %s\n", name);

    OutputDebugStringA(buffer);
}
}  // namespace gl
}  // namespace glRemix
