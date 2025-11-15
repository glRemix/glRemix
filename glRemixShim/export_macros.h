#pragma once

#ifndef GLREMIX_GL_VOID_WRAPPER
#define GLREMIX_GL_VOID_WRAPPER(name, params, args) \
    extern "C" __declspec(dllexport) void APIENTRY glRemix_##name params \
    { \
        using FnType = void(APIENTRY*) params; \
        if (auto override_fn = reinterpret_cast<FnType>(glRemix::gl::find_hook(#name))) \
        { \
            override_fn args; \
            return; \
        } \
        glRemix::gl::report_missing_function(#name); \
    }
#endif

#ifndef GLREMIX_GL_RETURN_WRAPPER
#define GLREMIX_GL_RETURN_WRAPPER(ret, name, params, args) \
    extern "C" __declspec(dllexport) ret APIENTRY glRemix_##name params \
    { \
        using FnType = ret(APIENTRY*) params; \
        if (auto override_fn = reinterpret_cast<FnType>(glRemix::gl::find_hook(#name))) \
        { \
            return override_fn args; \
        } \
        glRemix::gl::report_missing_function(#name); \
        return {}; \
    }
#endif

#ifndef GLREMIX_WGL_RETURN_WRAPPER
#define GLREMIX_WGL_RETURN_WRAPPER(retType, name, params, args, default_value) \
    extern "C" __declspec(dllexport) retType WINAPI glRemix_##name params \
    { \
        using FnType = retType(WINAPI*) params; \
        if (auto override_fn = reinterpret_cast<FnType>(glRemix::gl::find_hook(#name))) \
        { \
            return override_fn args; \
        } \
        glRemix::gl::report_missing_function(#name); \
        return default_value; \
    }
#endif
