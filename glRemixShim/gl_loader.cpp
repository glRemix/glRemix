#include "gl_loader.h"

#include <mutex>
#include <string>
#include <tsl/robin_map.h>

#include <shared/utils/debug_utils.h>

namespace glRemix
{
glRemix::IPCProtocol g_ipc{};

std::once_flag g_renderer_flag;
HANDLE g_renderer_process = nullptr;

namespace gl
{
// Both WGL and OpenGL functions may be called from multiple threads hence the mutex
std::mutex g_hook_mutex;

// Function pointers for our custom hook implementations
tsl::robin_map<std::string, PROC> g_hooks;

std::once_flag g_initialize_flag;

bool initialize()
{
    if (glRemix::g_renderer_process)
    {
#ifdef _DEBUG
        // renderer only launched after first swap buffers call
        throw std::runtime_error("GLRemixLoader - Single context assumption proven wrong.");
#endif
        DBG_PRINT("GLRemixLoader - Host app attempted to create a context after the first frame");
        return false;
    }

    static bool cached_success = false;

    // create lambda function for `std::call_once`
    auto initialize_once_fn = []() -> bool
    {
        try
        {
            g_ipc.init_writer();  // initialize shim as IPC writer
            g_ipc.start_frame_or_wait();

            return true;
        }
        catch (
            const std::exception& e)  // All attempts to initialize IPC failed. Nothing will work.
        {
#ifdef _DEBUG
            throw;  // only let error propogate in development
#endif
            return false;
        }
    };

    std::call_once(g_initialize_flag, [&] { cached_success = initialize_once_fn(); });

    return cached_success;
}

bool launch_renderer()
{
    if (glRemix::g_renderer_process)
    {
        return true;  // return as early as possible
    }

    static bool cached_success = false;

    auto launch_renderer_once_fn = []() -> bool
    {
#ifdef GLREMIX_AUTO_LAUNCH_RENDERER
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
            char debug_msg[512];
            std::snprintf(debug_msg, sizeof(debug_msg), "glRemix: Using custom renderer path: %s\n",
                          dll_path.data());
            OutputDebugStringA(debug_msg);

            STARTUPINFOA si{ .cb = sizeof(STARTUPINFOA) };
            PROCESS_INFORMATION pi;
            if (CreateProcessA(nullptr, dll_path.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &si, &pi))
            {
                OutputDebugStringA("glRemix: Renderer started.\n");
                // Store handle to terminate renderer on DLL unload
                glRemix::g_renderer_process = pi.hProcess;
                CloseHandle(pi.hThread);
                return true;
            }
            else
            {
                OutputDebugStringA("glRemix: Failed to start renderer.\n");
                // propogate down to swap buffer call to let host app handle
                return false;
            }
        }

        OutputDebugStringA("glRemix: Failed to find renderer executable.\n");
        return false;
#else
        return true;  // return success if `GLREMIX_AUTO_LAUNCH_RENDERER` is OFF
#endif
    };

    std::call_once(glRemix::g_renderer_flag, [&] { cached_success = launch_renderer_once_fn(); });

    return cached_success;
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

bool shutdown()
{
    if (!glRemix::g_renderer_process)
    {
        // prevent shutdown during first frame
        DBG_PRINT("GLRemixLoader - Renderer not launched yet. Shutdown will not occur.");
        return false;
    }
    if (glRemix::g_renderer_process)
    {
        TerminateProcess(glRemix::g_renderer_process, 0);
        CloseHandle(glRemix::g_renderer_process);
        glRemix::g_renderer_process = nullptr;

        DBG_PRINT("GLRemixLoader - Renderer termination cached_success.");
    }

    // TODO: implement global disabled state that affects `find_hook` and `export_macros.h`
    {
        std::scoped_lock lock(g_hook_mutex);
        g_hooks.clear();
    }

    g_ipc.shutdown_writer();

#if 1
    // TODO: add some other custom goodbye? or remove
    MessageBoxTimeoutW(nullptr, L"Thanks for using glRemix!", L"glRemixShim",
                       MB_OK | MB_ICONINFORMATION, 0, 5000);
#endif

    return true;
}

}  // namespace gl
}  // namespace glRemix
