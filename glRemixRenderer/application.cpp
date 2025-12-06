#include "application.h"

#include <system_error>

#include <shared/debug_utils.h>

using namespace glRemix;

void Application::run_with_hwnd(const bool enable_debug_layer)
{
    // Setup and create default resources
    THROW_IF_FALSE(m_context.create(enable_debug_layer));

    THROW_IF_FALSE(m_context.create_queue(D3D12_COMMAND_LIST_TYPE_DIRECT, &m_gfx_queue));

    D3D12_DESCRIPTOR_HEAP_DESC desc{ .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                     .NumDescriptors = 32,
                                     .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
                                     .NodeMask = 0 };
    THROW_IF_FALSE(m_context.create_descriptor_heap(desc, &m_rtv_heap, "default rtv heap"));

    // Swapchain creation is deferred until we receive HWND from command stream

    THROW_IF_FALSE(
        m_context.create_fence(&m_fence_frame_ready, m_fence_frame_ready_val[get_frame_index()]));

    create();

    while (!m_quit)
    {
        // Process window messages for ImGui input
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
            {
                m_quit = true;
            }
        }

        try
        {
            render();
        }
        catch (const std::system_error& e)
        {
            if (e.code().value() == ERROR_SHUTDOWN_IN_PROGRESS)
            {
                DBG_PRINT("GLRemixApp - Renderer exited gracefully from shim shutdown.");

                m_quit = true;
                continue;
            }
            throw;  // continue error propogation
        }
    }

#if 1
    // TODO: add some other custom goodbye? or remove
    MessageBoxTimeoutW(nullptr, L"Thanks for using glRemix!", L"glRemixRenderer",
                       MB_OK | MB_ICONINFORMATION, 0, 5000);
#endif

    THROW_IF_FALSE(m_context.wait_idle(&m_gfx_queue));
    destroy();
}
