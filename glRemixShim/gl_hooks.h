#pragma once

#include <tsl/robin_map.h>

#include <shared/ipc_protocol.h>
#include <shared/gl_commands.h>

#include <framework.h>

namespace glRemix::hooks
{
struct FakePixelFormat
{
    PIXELFORMATDESCRIPTOR descriptor{};
    int id = 1;
};

struct FakeContext
{
    HDC last_dc = nullptr;
};

struct GLRemixClientArrayInterface
{
    GLRemixClientArrayHeader ipc_payload;
    bool enabled = false;
    const void* ptr = nullptr;
};

struct GLRemixTextureUnitInterface
{
    UINT32 binding_2d = 0;               // what texture is currently bound to GL_TEXTURE_2D
    float texcoord[2] = { 0.0f, 0.0f };  // last glMultiTexCoord2fARB/glMultiTexCoord2fvARB value
};

#ifndef GLREMIX_EXT
#define GLREMIX_EXT(x) x " "
#endif
constexpr char k_EXTENSIONS[] =
#include "gl_extensions.inl"
    "";
#undef GLREMIX_EXT

extern UINT32 g_gen_lists_count;      // monotonic int, passed back to host app in `glGenLists`
extern UINT32 g_gen_textures_count;   // monotonic int, passed back in `glGenTextures`

extern UINT32 g_active_texture_unit;  // active texture unit for server-side texture state
extern UINT32 g_client_active_texcoord_unit;  // active texture unit for client-side texcoord arrays

extern GLRemixTextureUnitInterface g_texture_units[];

constexpr SIZE_T NUM_CLIENT_ARRAYS = static_cast<SIZE_T>(GLRemixClientArrayType::_COUNT);

extern thread_local std::array<GLRemixClientArrayInterface, NUM_CLIENT_ARRAYS> g_client_arrays;
extern UINT32 g_enabled_client_arrays_count;  // count of currently enabled client arrays

// Assume WGL/OpenGL not called from multiple threads

// wglSetPixelFormat will only be called once per context
// Or if there are multiple contexts they can share the same format since they're fake anyway...
extern tsl::robin_map<HDC, FakePixelFormat> g_pixel_formats;

extern thread_local HGLRC g_current_context;
extern thread_local HDC g_current_dc;

extern WNDPROC g_original_wndproc;  // window procedure subclassing

void install_overrides();
}  // namespace glRemix::hooks
