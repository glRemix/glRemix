#include "gl_hooks.h"

#include <gl_loader.h>

#include <GL/gl.h>

#include <mutex>
#include <tsl/robin_map.h>

namespace glRemix::hooks
{
// If forwarding a call is needed for whatever reason, you can write a function to load the true
// DLL, then use GetProcAddress or wglGetProcAddress to store the real function pointer. But we are
// not using OpenGL functions, so this does not apply. Might be useful for DLL chaining though.

struct FakePixelFormat
{
    PIXELFORMATDESCRIPTOR descriptor{};
    int id = 1;
};

struct FakeContext
{
    HDC last_dc = nullptr;
};

// WGL/OpenGL might be called from multiple threads
std::mutex g_mutex;

static GLuint g_list_id_counter;  // monotonic id used in `glGenLists` and passed back to host app

// wglSetPixelFormat will only be called once per context
// Or if there are multiple contexts they can share the same format since they're fake anyway...
// TODO: Make the above assumption?
tsl::robin_map<HDC, FakePixelFormat> g_pixel_formats;

thread_local HGLRC g_current_context = nullptr;
thread_local HDC g_current_dc = nullptr;

FakePixelFormat create_default_pixel_format(const PIXELFORMATDESCRIPTOR* requested)
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
                                 .cDepthBits = 24,
                                 .cStencilBits = 8,
                                 .iLayerType = PFD_MAIN_PLANE } };
    }
    FakePixelFormat result;
    result.descriptor = *requested;
    result.descriptor.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    result.descriptor.nVersion = 1;
    return result;
}

void APIENTRY gl_begin_ovr(GLenum mode)
{
    GLBeginCommand payload{ mode };
    g_recorder.record(GLCommandType::GLCMD_BEGIN, &payload, sizeof(payload));
}

void APIENTRY gl_end_ovr()
{
    GLEmptyCommand payload{};  // init with default 0 value
    g_recorder.record(GLCommandType::GLCMD_END, &payload, sizeof(payload));
}

/* Basic Commands */
void APIENTRY gl_vertex2f_ovr(GLfloat x, GLfloat y)
{
    GLVertex2fCommand payload{ x, y };
    g_recorder.record(GLCommandType::GLCMD_VERTEX2F, &payload, sizeof(payload));
}

void APIENTRY gl_vertex3f_ovr(GLfloat x, GLfloat y, GLfloat z)
{
    GLVertex3fCommand payload({ x, y, z });
    g_recorder.record(GLCommandType::GLCMD_VERTEX3F, &payload, sizeof(payload));
}

void APIENTRY gl_color3f_ovr(GLfloat r, GLfloat g, GLfloat b)
{
    GLColor3fCommand payload{ r, g, b };
    g_recorder.record(GLCommandType::GLCMD_COLOR3F, &payload, sizeof(payload));
}

void APIENTRY gl_color4f_ovr(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GLColor4fCommand payload{ r, g, b, a };
    g_recorder.record(GLCommandType::GLCMD_COLOR4F, &payload, sizeof(payload));
}

void APIENTRY gl_normal3f_ovr(GLfloat nx, GLfloat ny, GLfloat nz)
{
    GLNormal3fCommand payload{ nx, ny, nz };
    g_recorder.record(GLCommandType::GLCMD_NORMAL3F, &payload, sizeof(payload));
}

void APIENTRY gl_tex_coord2f_ovr(GLfloat s, GLfloat t)
{
    GLTexCoord2fCommand payload{ s, t };
    g_recorder.record(GLCommandType::GLCMD_TEXCOORD2F, &payload, sizeof(payload));
}

/* Matrix Operations */
void APIENTRY gl_matrix_mode_ovr(GLenum mode)
{
    GLMatrixModeCommand payload{ mode };
    g_recorder.record(GLCommandType::GLCMD_MATRIX_MODE, &payload, sizeof(payload));
}

void APIENTRY gl_load_identity_ovr()
{
    GLLoadIdentityCommand payload{};
    g_recorder.record(GLCommandType::GLCMD_LOAD_IDENTITY, &payload, sizeof(payload));
}

void APIENTRY gl_load_matrixf_ovr(const GLfloat* m)
{
    GLLoadMatrixCommand payload{};
    memcpy(payload.m, m, sizeof(payload.m));
    g_recorder.record(GLCommandType::GLCMD_LOAD_MATRIX, &payload, sizeof(payload));
}

void APIENTRY gl_mult_matrixf_ovr(const GLfloat* m)
{
    GLMultMatrixCommand payload{};
    memcpy(payload.m, m, sizeof(payload.m));
    g_recorder.record(GLCommandType::GLCMD_MULT_MATRIX, &payload, sizeof(payload));
}

void APIENTRY gl_push_matrix_ovr()
{
    GLPushMatrixCommand payload{};
    g_recorder.record(GLCommandType::GLCMD_PUSH_MATRIX, &payload, sizeof(payload));
}

void APIENTRY gl_pop_matrix_ovr()
{
    GLPopMatrixCommand payload{};
    g_recorder.record(GLCommandType::GLCMD_POP_MATRIX, &payload, sizeof(payload));
}

void APIENTRY gl_translatef_ovr(GLfloat x, GLfloat y, GLfloat z)
{
    GLTranslateCommand payload{ { x, y, z } };
    g_recorder.record(GLCommandType::GLCMD_TRANSLATE, &payload, sizeof(payload));
}

void APIENTRY gl_rotatef_ovr(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    GLRotateCommand payload{ angle, { x, y, z } };
    g_recorder.record(GLCommandType::GLCMD_ROTATE, &payload, sizeof(payload));
}

void APIENTRY gl_scalef_ovr(GLfloat x, GLfloat y, GLfloat z)
{
    GLScaleCommand payload{ { x, y, z } };
    g_recorder.record(GLCommandType::GLCMD_SCALE, &payload, sizeof(payload));
}

/* Texture Operations */
void APIENTRY gl_bind_texture_ovr(GLenum target, GLuint texture)
{
    GLBindTextureCommand payload{ target, texture };
    g_recorder.record(GLCommandType::GLCMD_BIND_TEXTURE, &payload, sizeof(payload));
}

void APIENTRY gl_gen_textures_ovr(GLsizei n, GLuint*)
{
    GLGenTexturesCommand payload{ static_cast<UINT32>(n) };
    g_recorder.record(GLCommandType::GLCMD_GEN_TEXTURES, &payload, sizeof(payload));
}

void APIENTRY gl_delete_textures_ovr(GLsizei n, const GLuint*)
{
    GLDeleteTexturesCommand payload{ static_cast<UINT32>(n) };
    g_recorder.record(GLCommandType::GLCMD_DELETE_TEXTURES, &payload, sizeof(payload));
}

void APIENTRY gl_tex_image_2d_ovr(GLenum target, GLint level, GLint internalFormat, GLsizei width,
                                  GLsizei height, GLint border, GLenum format, GLenum type,
                                  const void* pixels)
{
    GLTexImage2DCommand payload;
    payload.target = target;
    payload.level = level;
    payload.internalFormat = internalFormat;
    payload.width = (UINT32)width;
    payload.height = (UINT32)height;
    payload.border = border;
    payload.format = format;
    payload.type = type;
    payload.dataPtr = reinterpret_cast<UINT64>(pixels);

    g_recorder.record(GLCommandType::GLCMD_TEX_IMAGE_2D, &payload, sizeof(payload));
}

void APIENTRY gl_tex_parameterf_ovr(GLenum target, GLenum pname, GLfloat param)
{
    GLTexParameterCommand payload{ target, pname, param };
    g_recorder.record(GLCommandType::GLCMD_TEX_PARAMETER, &payload, sizeof(payload));
}

/* Lighting */
void APIENTRY gl_enable_ovr(GLenum cap)
{
    GLEnableCommand payload{ cap };
    g_recorder.record(GLCommandType::GLCMD_ENABLE, &payload, sizeof(payload));
}

void APIENTRY gl_disable_ovr(GLenum cap)
{
    GLDisableCommand payload{ cap };
    g_recorder.record(GLCommandType::GLCMD_DISABLE, &payload, sizeof(payload));
}

void APIENTRY gl_lightf_ovr(GLenum light, GLenum pname, GLfloat param)
{
    GLLightCommand payload{ light, pname, param };
    g_recorder.record(GLCommandType::GLCMD_LIGHTF, &payload, sizeof(payload));
}

void APIENTRY gl_lightfv_ovr(GLenum light, GLenum pname, const GLfloat* params)
{
    GLLightfvCommand payload{ light, pname, { params[0], params[1], params[2], params[3] } };
    g_recorder.record(GLCommandType::GLCMD_LIGHTFV, &payload, sizeof(payload));
}

void APIENTRY gl_materialf_ovr(GLenum face, GLenum pname, GLfloat param)
{
    GLMaterialCommand payload{ face, pname, param };
    g_recorder.record(GLCommandType::GLCMD_MATERIALF, &payload, sizeof(payload));
}

void APIENTRY gl_materialfv_ovr(GLenum face, GLenum pname, const GLfloat* params)
{
    GLMaterialfvCommand payload{ face, pname, { params[0], params[1], params[2], params[3] } };
    g_recorder.record(GLCommandType::GLCMD_MATERIALFV, &payload, sizeof(payload));
}

/* Buffer Operations */
void APIENTRY gl_clear_ovr(GLbitfield mask)
{
    GLClearCommand payload{ mask };
    g_recorder.record(GLCommandType::GLCMD_CLEAR, &payload, sizeof(payload));
}

void APIENTRY gl_clear_color_ovr(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GLClearColorCommand payload{ { r, g, b, a } };
    g_recorder.record(GLCommandType::GLCMD_CLEAR_COLOR, &payload, sizeof(payload));
}

void APIENTRY gl_flush_ovr()
{
    GLFlushCommand payload{};
    g_recorder.record(GLCommandType::GLCMD_FLUSH, &payload, sizeof(payload));
}

void APIENTRY gl_finish_ovr()
{
    GLFinishCommand payload{};
    g_recorder.record(GLCommandType::GLCMD_FINISH, &payload, sizeof(payload));
}

/* Viewport & Projection */
void APIENTRY gl_viewport_ovr(GLint x, GLint y, GLsizei width, GLsizei height)
{
    GLViewportCommand payload{ x, y, width, height };
    g_recorder.record(GLCommandType::GLCMD_VIEWPORT, &payload, sizeof(payload));
}

void APIENTRY gl_ortho_ovr(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                           GLdouble zNear, GLdouble zFar)
{
    GLOrthoCommand payload{ left, right, bottom, top, zNear, zFar };
    g_recorder.record(GLCommandType::GLCMD_ORTHO, &payload, sizeof(payload));
}

void APIENTRY gl_frustum_ovr(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                             GLdouble zNear, GLdouble zFar)
{
    GLFrustumCommand payload{ left, right, bottom, top, zNear, zFar };
    g_recorder.record(GLCommandType::GLCMD_FRUSTUM, &payload, sizeof(payload));
}

/* Other */

void APIENTRY gl_shutdown_ovr()
{
    GLShutdownCommand payload{};
    g_recorder.record(GLCommandType::GLCMD_SHUTDOWN, &payload, sizeof(payload));
}

/* Display Lists */

void APIENTRY gl_call_list_ovr(GLuint list)
{
    GLCallListCommand payload{ list };
    g_recorder.record(GLCommandType::GLCMD_CALL_LIST, &payload, sizeof(payload));
}

void APIENTRY gl_new_list_ovr(GLuint list, GLenum mode)
{
    GLNewListCommand payload{ list, mode };
    g_recorder.record(GLCommandType::GLCMD_NEW_LIST, &payload, sizeof(payload));
}

void APIENTRY gl_end_list_ovr()
{
    GLEndListCommand payload{};
    g_recorder.record(GLCommandType::GLCMD_END_LIST, &payload, sizeof(payload));
}

GLuint APIENTRY gl_gen_lists_ovr(GLsizei range)
{
    // fetchandadd
    GLuint base = g_list_id_counter;
    g_list_id_counter += range;

    return base;
}

/* WGL (Windows Graphics Library) overrides */

BOOL WINAPI swap_buffers_ovr(HDC)
{
    g_recorder.end_frame();
    g_recorder.start_frame();

    return TRUE;
}

int WINAPI choose_pixel_format_ovr(HDC dc, const PIXELFORMATDESCRIPTOR* descriptor)
{
    if (dc == nullptr)
    {
        return 0;
    }

    std::scoped_lock lock(g_mutex);
    g_pixel_formats[dc] = create_default_pixel_format(descriptor);
    return g_pixel_formats[dc].id;
}

int WINAPI describe_pixel_format_ovr(HDC dc, int pixel_format, UINT bytes,
                                     LPPIXELFORMATDESCRIPTOR descriptor)
{
    if (dc == nullptr || pixel_format <= 0 || descriptor == nullptr)
    {
        return 0;
    }

    std::scoped_lock lock(g_mutex);
    if (!g_pixel_formats.contains(dc))
    {
        *descriptor = create_default_pixel_format(nullptr).descriptor;
        return pixel_format;
    }

    *descriptor = g_pixel_formats[dc].descriptor;
    return pixel_format;
}

int WINAPI get_pixel_format_ovr(HDC dc)
{
    if (dc == nullptr)
    {
        return 0;
    }

    std::scoped_lock lock(g_mutex);
    if (!g_pixel_formats.contains(dc))
    {
        return 0;
    }

    return g_pixel_formats[dc].id;
}

BOOL WINAPI set_pixel_format_ovr(HDC dc, int pixel_format, const PIXELFORMATDESCRIPTOR* descriptor)
{
    if (dc == nullptr || pixel_format <= 0)
    {
        return FALSE;
    }

    std::scoped_lock lock(g_mutex);
    g_pixel_formats[dc] = create_default_pixel_format(descriptor);
    g_pixel_formats[dc].id = pixel_format;
    return TRUE;
}

HGLRC WINAPI create_context_ovr(HDC dc)
{
    // Derive HWND from HDC for swapchain creation
    HWND hwnd = WindowFromDC(dc);
    assert(hwnd);

    g_recorder.record(GLCommandType::GLCMD_CREATE, &hwnd, sizeof(HWND));

    return reinterpret_cast<HGLRC>(static_cast<UINT_PTR>(0xDEADBEEF));  // Dummy context handle
}

BOOL WINAPI delete_context_ovr(HGLRC context)
{
    return TRUE;
}

HGLRC WINAPI get_current_context_ovr()
{
    return g_current_context;
}

HDC WINAPI get_current_dc_ovr()
{
    return g_current_dc;
}

BOOL WINAPI make_current_ovr(HDC dc, HGLRC context)
{
    g_current_dc = dc;
    return TRUE;
}

BOOL WINAPI share_lists_ovr(HGLRC, HGLRC)
{
    return TRUE;
}

std::once_flag g_install_flag;

void install_overrides()
{
    // create lambda function for `std::call_once`
    auto register_all_hooks_once_fn = []
    {
        gl::register_hook("glBegin", reinterpret_cast<PROC>(&gl_begin_ovr));
        gl::register_hook("glEnd", reinterpret_cast<PROC>(&gl_end_ovr));
        gl::register_hook("glVertex2f", reinterpret_cast<PROC>(&gl_vertex2f_ovr));
        gl::register_hook("glVertex3f", reinterpret_cast<PROC>(&gl_vertex3f_ovr));
        gl::register_hook("glColor3f", reinterpret_cast<PROC>(&gl_color3f_ovr));
        gl::register_hook("glColor4f", reinterpret_cast<PROC>(&gl_color4f_ovr));
        gl::register_hook("glNormal3f", reinterpret_cast<PROC>(&gl_normal3f_ovr));
        gl::register_hook("glTexCoord2f", reinterpret_cast<PROC>(&gl_tex_coord2f_ovr));
        gl::register_hook("glMatrixMode", reinterpret_cast<PROC>(&gl_matrix_mode_ovr));
        gl::register_hook("glLoadIdentity", reinterpret_cast<PROC>(&gl_load_identity_ovr));
        gl::register_hook("glLoadMatrixf", reinterpret_cast<PROC>(&gl_load_matrixf_ovr));
        gl::register_hook("glMultMatrixf", reinterpret_cast<PROC>(&gl_mult_matrixf_ovr));
        gl::register_hook("glPushMatrix", reinterpret_cast<PROC>(&gl_push_matrix_ovr));
        gl::register_hook("glPopMatrix", reinterpret_cast<PROC>(&gl_pop_matrix_ovr));
        gl::register_hook("glTranslatef", reinterpret_cast<PROC>(&gl_translatef_ovr));
        gl::register_hook("glRotatef", reinterpret_cast<PROC>(&gl_rotatef_ovr));
        gl::register_hook("glScalef", reinterpret_cast<PROC>(&gl_scalef_ovr));
        gl::register_hook("glBindTexture", reinterpret_cast<PROC>(&gl_bind_texture_ovr));
        gl::register_hook("glGenTextures", reinterpret_cast<PROC>(&gl_gen_textures_ovr));
        gl::register_hook("glDeleteTextures", reinterpret_cast<PROC>(&gl_delete_textures_ovr));
        gl::register_hook("glTexImage2D", reinterpret_cast<PROC>(&gl_tex_image_2d_ovr));
        gl::register_hook("glTexParameterf", reinterpret_cast<PROC>(&gl_tex_parameterf_ovr));
        gl::register_hook("glEnable", reinterpret_cast<PROC>(&gl_enable_ovr));
        gl::register_hook("glDisable", reinterpret_cast<PROC>(&gl_disable_ovr));
        gl::register_hook("glLightf", reinterpret_cast<PROC>(&gl_lightf_ovr));
        gl::register_hook("glLightfv", reinterpret_cast<PROC>(&gl_lightfv_ovr));
        gl::register_hook("glMaterialf", reinterpret_cast<PROC>(&gl_materialf_ovr));
        gl::register_hook("glMaterialfv", reinterpret_cast<PROC>(&gl_materialfv_ovr));
        gl::register_hook("glClear", reinterpret_cast<PROC>(&gl_clear_ovr));
        gl::register_hook("glClearColor", reinterpret_cast<PROC>(&gl_clear_color_ovr));
        gl::register_hook("glFlush", reinterpret_cast<PROC>(&gl_flush_ovr));
        gl::register_hook("glFinish", reinterpret_cast<PROC>(&gl_finish_ovr));
        gl::register_hook("glViewport", reinterpret_cast<PROC>(&gl_viewport_ovr));
        gl::register_hook("glOrtho", reinterpret_cast<PROC>(&gl_ortho_ovr));
        gl::register_hook("glFrustum", reinterpret_cast<PROC>(&gl_frustum_ovr));

        gl::register_hook("glCallList", reinterpret_cast<PROC>(&gl_call_list_ovr));
        gl::register_hook("glNewList", reinterpret_cast<PROC>(&gl_new_list_ovr));
        gl::register_hook("glEndList", reinterpret_cast<PROC>(&gl_end_list_ovr));
        gl::register_hook("glGenLists", reinterpret_cast<PROC>(&gl_gen_lists_ovr));

        // Override WGL for app to work. Return success and try to do nothing.
        gl::register_hook("wglChoosePixelFormat", reinterpret_cast<PROC>(&choose_pixel_format_ovr));
        gl::register_hook("wglDescribePixelFormat",
                          reinterpret_cast<PROC>(&describe_pixel_format_ovr));
        gl::register_hook("wglGetPixelFormat", reinterpret_cast<PROC>(&get_pixel_format_ovr));
        gl::register_hook("wglSetPixelFormat", reinterpret_cast<PROC>(&set_pixel_format_ovr));
        gl::register_hook("wglSwapBuffers", reinterpret_cast<PROC>(&swap_buffers_ovr));
        gl::register_hook("wglCreateContext", reinterpret_cast<PROC>(&create_context_ovr));
        gl::register_hook("wglDeleteContext", reinterpret_cast<PROC>(&delete_context_ovr));
        gl::register_hook("wglGetCurrentContext", reinterpret_cast<PROC>(&get_current_context_ovr));
        gl::register_hook("wglGetCurrentDC", reinterpret_cast<PROC>(&get_current_dc_ovr));
        gl::register_hook("wglMakeCurrent", reinterpret_cast<PROC>(&make_current_ovr));
        gl::register_hook("wglShareLists", reinterpret_cast<PROC>(&share_lists_ovr));
    };

    std::call_once(g_install_flag, register_all_hooks_once_fn);
}
}  // namespace glRemix::hooks
