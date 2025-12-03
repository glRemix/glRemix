#include "override_modules/common_includes.h"

#include "gl_loader.h"

namespace glRemix::hooks
{
// Window procedure to capture input events and forward to IPC
static LRESULT CALLBACK s_input_capture_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    bool should_send = false;
    switch (msg)
    {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDBLCLK:

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:

        case WM_SETFOCUS:
        case WM_KILLFOCUS:

        case WM_SETCURSOR:
        case WM_MOUSELEAVE: should_send = true; break;
    }

    if (should_send)
    {
        const WGLInputEventCommand payload{
            .msg = msg,
            .wparam = wparam,
            .lparam = static_cast<UINT64>(lparam),
        };

        g_ipc.write_command(GLCommandType::WGLCMD_INPUT_EVENT, payload);
    }

    // Call the original window procedure
    if (g_original_wndproc)
    {
        return CallWindowProc(g_original_wndproc, hwnd, msg, wparam, lparam);
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

BOOL WINAPI wgl_swap_buffers_ovr(HDC dc)
{
    if (!gl::launch_renderer())
    {
        return FALSE;
    }

    // Install window procedure hook to capture input events
    if (!g_original_wndproc)
    {
        // Derive HWND from HDC for swapchain creation
        HWND hwnd = WindowFromDC(dc);
        assert(hwnd);

        g_original_wndproc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(hwnd, GWLP_WNDPROC,
                             reinterpret_cast<LONG_PTR>(s_input_capture_wnd_proc)));

        GLRemixSendHWNDCommand payload{ hwnd };

        g_ipc.write_command(GLCommandType::GLREMIXCMD_SEND_HWND, payload);
    }

    g_ipc.end_frame();
    g_ipc.start_frame_or_wait();

    return TRUE;
}

FakePixelFormat s_create_default_pixel_format(const PIXELFORMATDESCRIPTOR* requested)
{
    if (requested == nullptr)
    {
        // Standard 32-bit color 24-bit depth 8-bit stencil double buffered format
        return { .descriptor = { .nSize = sizeof(PIXELFORMATDESCRIPTOR),
                                 .nVersion = 1,
                                 .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL
                                            | PFD_DOUBLEBUFFER,
                                 .iPixelType = PFD_TYPE_RGBA,
                                 .cColorBits = 32,
                                 .cRedBits = 8,
                                 .cRedShift = 16,
                                 .cGreenBits = 8,
                                 .cGreenShift = 8,
                                 .cBlueBits = 8,
                                 .cBlueShift = 0,
                                 .cAlphaBits = 8,
                                 .cAlphaShift = 24,
                                 .cAccumBits = 0,
                                 .cAccumRedBits = 0,
                                 .cAccumGreenBits = 0,
                                 .cAccumBlueBits = 0,
                                 .cAccumAlphaBits = 0,
                                 .cDepthBits = 24,
                                 .cStencilBits = 8,
                                 .cAuxBuffers = 0,
                                 .iLayerType = PFD_MAIN_PLANE,
                                 .bReserved = 0,
                                 .dwLayerMask = 0,
                                 .dwVisibleMask = 0,
                                 .dwDamageMask = 0 } };
    }
    FakePixelFormat result;
    result.descriptor = *requested;
    result.descriptor.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    result.descriptor.nVersion = 1;
    return result;
}

int WINAPI wgl_choose_pixel_format_ovr(HDC dc, const PIXELFORMATDESCRIPTOR* descriptor)
{
    if (dc == nullptr)
    {
        return 0;
    }

    g_pixel_formats[dc] = s_create_default_pixel_format(descriptor);
    return g_pixel_formats[dc].id;
}

int WINAPI wgl_describe_pixel_format_ovr(HDC dc, int pixel_format, UINT bytes,
                                         LPPIXELFORMATDESCRIPTOR descriptor)
{
    if (dc == nullptr || pixel_format <= 0)
    {
        return 0;
    }

    // When descriptor is NULL, return the maximum pixel format index
    if (descriptor == nullptr)
    {
        return 1;  // We only support one pixel format
    }

    // Only pixel format 1 is valid for us
    if (pixel_format > 1)
    {
        return 0;  // Invalid pixel format index
    }

    // Fill in the descriptor with our default pixel format
    // Use the stored format if available, otherwise use default
    auto it = g_pixel_formats.find(dc);
    if (it != g_pixel_formats.end())
    {
        *descriptor = it.value().descriptor;
    }
    else
    {
        *descriptor = s_create_default_pixel_format(nullptr).descriptor;
    }

    return 1;  // Return max number of formats available
}

int WINAPI wgl_get_pixel_format_ovr(HDC dc)
{
    if (dc == nullptr)
    {
        return 0;
    }

    if (!g_pixel_formats.contains(dc))
    {
        return 0;
    }

    return g_pixel_formats[dc].id;
}

BOOL WINAPI wgl_set_pixel_format_ovr(HDC dc, int pixel_format,
                                     const PIXELFORMATDESCRIPTOR* descriptor)
{
    if (dc == nullptr || pixel_format <= 0)
    {
        return FALSE;
    }

    g_pixel_formats[dc] = s_create_default_pixel_format(descriptor);
    g_pixel_formats[dc].id = pixel_format;

    return TRUE;
}

HGLRC WINAPI wgl_create_context_ovr(HDC dc)
{
    if (!gl::initialize())
    {
        // this allows the host app to attempt a fallback if they have one
        // as we've properly error-handled in `initialize()`, this is appropriate.
        return nullptr;
    }

    return reinterpret_cast<HGLRC>(static_cast<UINT_PTR>(0xDEADBEEF));  // Dummy context handle
}

BOOL WINAPI wgl_delete_context_ovr(HGLRC context)
{
    return gl::shutdown();
}

HGLRC WINAPI wgl_get_current_context_ovr()
{
    return g_current_context;
}

HDC WINAPI wgl_get_current_dc_ovr()
{
    return g_current_dc;
}

BOOL WINAPI wgl_make_current_ovr(HDC dc, HGLRC context)
{
    g_current_dc = dc;
    g_current_context = context;
    return TRUE;
}

BOOL WINAPI wgl_share_lists_ovr(HGLRC, HGLRC)
{
    return TRUE;
}

BOOL WINAPI wgl_swap_interval_EXT_ovr(int interval)
{
    return TRUE;
}

const char* WINAPI wgl_get_extensions_string_EXT_ovr()
{
    return k_EXTENSIONS;
}

const char* WINAPI wgl_get_extensions_string_ARB_ovr(HDC hdc)
{
    return k_EXTENSIONS;
}

}  // namespace glRemix::hooks
