#include "gl_loader.h"

// This .inl marks WGL functions for export
#include "wgl_export_aliases.inl"
#include "export_macros.h"

// This .inl defines the wrappers for each WGL function using the macros in export_macros.h
// The wrapper tries to call the hook if it exists, otherwise reports missing function
#include "wgl_wrappers.inl"
#include "undef_export_macros.h"

// Custom implementation of wglGetProcAddress to return our hooked functions
// prefix with `glRemix_` to avoid conflicting symbols
extern "C" __declspec(dllexport) PROC WINAPI glRemix_wglGetProcAddress(LPCSTR name)
{
    if (name == nullptr)
    {
        return nullptr;
    }

    // Find a hook we defined
    if (PROC hook_proc = glRemix::gl::find_hook(name); hook_proc != nullptr)
    {
        return hook_proc;
    }

    glRemix::gl::report_missing_function(name);
    return nullptr;
}
