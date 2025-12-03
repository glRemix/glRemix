#include "gl_hooks.h"

#include <tsl/robin_map.h>
#include <mutex>

#include <shared/gl_utils.h>

#include "gl_loader.h"

namespace glRemix::hooks
{
UINT32 g_active_texture_unit = 0;
UINT32 g_client_active_texcoord_unit = 0;

GLRemixTextureUnitInterface g_texture_units[k_MAX_TEXTURE_UNITS] = {};

UINT32 g_gen_lists_count = 1;
UINT32 g_gen_textures_count = 1;

std::array<GLRemixClientArrayInterface, NUM_CLIENT_ARRAYS> g_client_arrays = {};
UINT32 g_enabled_client_arrays_count = 0;

tsl::robin_map<HDC, FakePixelFormat> g_pixel_formats = {};

thread_local HGLRC g_current_context = nullptr;
thread_local HDC g_current_dc = nullptr;

// Window procedure subclassing
WNDPROC g_original_wndproc = nullptr;

/* CORE IMMEDIATE MODE */
void APIENTRY gl_begin_ovr(GLenum mode)
{
    GLBeginCommand payload{ mode };
    g_ipc.write_command(GLCommandType::GLCMD_BEGIN, payload);
}

void APIENTRY gl_end_ovr()
{
    GLEmptyCommand payload{};  // init with default 0 value
    g_ipc.write_command(GLCommandType::GLCMD_END, payload);
}

void APIENTRY gl_vertex2f_ovr(GLfloat x, GLfloat y)
{
    GLVertex2fCommand payload{ x, y };
    g_ipc.write_command(GLCommandType::GLCMD_VERTEX2F, payload);
}

void APIENTRY gl_vertex3f_ovr(GLfloat x, GLfloat y, GLfloat z)
{
    GLVertex3fCommand payload{ x, y, z };
    g_ipc.write_command(GLCommandType::GLCMD_VERTEX3F, payload);
}

void APIENTRY gl_vertex3fv_ovr(const GLfloat* v)
{
    GLVertex3fCommand payload{ v[0], v[1], v[2] };
    g_ipc.write_command(GLCommandType::GLCMD_VERTEX3F, payload);
}

void APIENTRY gl_color3f_ovr(GLfloat r, GLfloat g, GLfloat b)
{
    GLColor3fCommand payload{ r, g, b };
    g_ipc.write_command(GLCommandType::GLCMD_COLOR3F, payload);
}

void APIENTRY gl_color4f_ovr(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GLColor4fCommand payload{ r, g, b, a };
    g_ipc.write_command(GLCommandType::GLCMD_COLOR4F, payload);
}

void APIENTRY gl_normal3f_ovr(GLfloat nx, GLfloat ny, GLfloat nz)
{
    GLNormal3fCommand payload{ nx, ny, nz };
    g_ipc.write_command(GLCommandType::GLCMD_NORMAL3F, payload);
}

void APIENTRY gl_tex_coord2f_ovr(GLfloat s, GLfloat t)
{
    GLTexCoord2fCommand payload{ s, t };
    g_ipc.write_command(GLCommandType::GLCMD_TEXCOORD2F, payload);
}

/* DISPLAY LISTS */
void APIENTRY gl_call_list_ovr(GLuint list)
{
    GLCallListCommand payload{ list };
    g_ipc.write_command(GLCommandType::GLCMD_CALL_LIST, payload);
}

void APIENTRY gl_new_list_ovr(GLuint list, GLenum mode)
{
    GLNewListCommand payload{ list, mode };
    g_ipc.write_command(GLCommandType::GLCMD_NEW_LIST, payload);
}

void APIENTRY gl_end_list_ovr()
{
    GLEndListCommand payload{};
    g_ipc.write_command(GLCommandType::GLCMD_END_LIST, payload);
}

GLuint APIENTRY gl_gen_lists_ovr(GLsizei range)
{
    // fetchandadd
    GLuint base = static_cast<GLuint>(g_gen_lists_count);
    g_gen_lists_count += range;

    return base;
}

/* CLIENT STATE */
extern void APIENTRY gl_enable_client_state_ovr(GLenum array);
extern void APIENTRY gl_disable_client_state_ovr(GLenum array);

extern void APIENTRY gl_vertex_pointer_ovr(GLint size, GLenum type, GLsizei stride,
                                           const GLvoid* pointer);
extern void APIENTRY gl_normal_pointer_ovr(GLenum type, GLsizei stride, const GLvoid* pointer);
extern void APIENTRY gl_color_pointer_ovr(GLint size, GLenum type, GLsizei stride,
                                          const GLvoid* pointer);
extern void APIENTRY gl_index_pointer_ovr(GLenum type, GLsizei stride, const GLvoid* pointer);
extern void APIENTRY gl_edge_flag_pointer_ovr(GLsizei stride, const GLvoid* pointer);
extern void APIENTRY gl_tex_coord_pointer_ovr(GLint size, GLenum type, GLsizei stride,
                                              const GLvoid* pointer);

extern void APIENTRY gl_draw_arrays_ovr(GLenum mode, GLint first, GLsizei count);
extern void APIENTRY gl_draw_elements_ovr(GLenum mode, GLsizei count, GLenum type,
                                          const GLvoid* indices);
extern void APIENTRY gl_draw_range_elements_ovr(GLenum mode, GLuint start, GLuint end,
                                                GLsizei count, GLenum type, const GLvoid* indices);

/* MATRIX OPERATIONS */
void APIENTRY gl_matrix_mode_ovr(GLenum mode)
{
    GLMatrixModeCommand payload{ mode };
    g_ipc.write_command(GLCommandType::GLCMD_MATRIX_MODE, payload);
}

void APIENTRY gl_load_identity_ovr()
{
    GLLoadIdentityCommand payload{};
    g_ipc.write_command(GLCommandType::GLCMD_LOAD_IDENTITY, payload);
}

void APIENTRY gl_load_matrixf_ovr(const GLfloat* m)
{
    GLLoadMatrixCommand payload{};
    memcpy(payload.m, m, sizeof(payload.m));
    g_ipc.write_command(GLCommandType::GLCMD_LOAD_MATRIX, payload);
}

void APIENTRY gl_mult_matrixf_ovr(const GLfloat* m)
{
    GLMultMatrixCommand payload{};
    memcpy(payload.m, m, sizeof(payload.m));
    g_ipc.write_command(GLCommandType::GLCMD_MULT_MATRIX, payload);
}

void APIENTRY gl_push_matrix_ovr()
{
    GLPushMatrixCommand payload{};
    g_ipc.write_command(GLCommandType::GLCMD_PUSH_MATRIX, payload);
}

void APIENTRY gl_pop_matrix_ovr()
{
    GLPopMatrixCommand payload{};
    g_ipc.write_command(GLCommandType::GLCMD_POP_MATRIX, payload);
}

void APIENTRY gl_translatef_ovr(GLfloat x, GLfloat y, GLfloat z)
{
    GLTranslateCommand payload{ { x, y, z } };
    g_ipc.write_command(GLCommandType::GLCMD_TRANSLATE, payload);
}

void APIENTRY gl_rotatef_ovr(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    GLRotateCommand payload{ angle, { x, y, z } };
    g_ipc.write_command(GLCommandType::GLCMD_ROTATE, payload);
}

void APIENTRY gl_scalef_ovr(GLfloat x, GLfloat y, GLfloat z)
{
    GLScaleCommand payload{ { x, y, z } };
    g_ipc.write_command(GLCommandType::GLCMD_SCALE, payload);
}

void APIENTRY gl_viewport_ovr(GLint x, GLint y, GLsizei width, GLsizei height)
{
    GLViewportCommand payload{ x, y, width, height };
    g_ipc.write_command(GLCommandType::GLCMD_VIEWPORT, payload);
}

void APIENTRY gl_ortho_ovr(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                           GLdouble zNear, GLdouble zFar)
{
    GLOrthoCommand payload{ left, right, bottom, top, zNear, zFar };
    g_ipc.write_command(GLCommandType::GLCMD_ORTHO, payload);
}

void APIENTRY gl_frustum_ovr(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                             GLdouble zNear, GLdouble zFar)
{
    GLFrustumCommand payload{ left, right, bottom, top, zNear, zFar };
    g_ipc.write_command(GLCommandType::GLCMD_FRUSTUM, payload);
}

/* RENDERING */
void APIENTRY gl_clear_ovr(GLbitfield mask)
{
    GLClearCommand payload{ mask };
    g_ipc.write_command(GLCommandType::GLCMD_CLEAR, payload);
}

void APIENTRY gl_clear_color_ovr(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GLClearColorCommand payload{ { r, g, b, a } };
    g_ipc.write_command(GLCommandType::GLCMD_CLEAR_COLOR, payload);
}

void APIENTRY gl_flush_ovr()
{
    GLFlushCommand payload{};
    g_ipc.write_command(GLCommandType::GLCMD_FLUSH, payload);
}

void APIENTRY gl_finish_ovr()
{
    GLFinishCommand payload{};
    g_ipc.write_command(GLCommandType::GLCMD_FINISH, payload);
}

void APIENTRY gl_bind_texture_ovr(GLenum target, GLuint texture)
{
    GLBindTextureCommand payload{ target, texture };
    g_ipc.write_command(GLCommandType::GLCMD_BIND_TEXTURE, payload);
}

void APIENTRY gl_gen_textures_ovr(GLsizei n, GLuint* textures)
{
    GLGenTexturesCommand payload{ .n = static_cast<UINT32>(n) };

    for (GLsizei i = 0; i < n; i++)
    {
        textures[i] = g_gen_textures_count++;
        payload.ids[i] = textures[i];
    }

    g_ipc.write_command(GLCommandType::GLCMD_GEN_TEXTURES, payload);
}

void APIENTRY gl_delete_textures_ovr(GLsizei n, const GLuint* textures)
{
    GLDeleteTexturesCommand payload{ .n = static_cast<UINT32>(n) };

    for (GLsizei i = 0; i < n; i++)
    {
        payload.ids[i] = textures[i];
    }

    g_ipc.write_command(GLCommandType::GLCMD_DELETE_TEXTURES, payload);
}

/*
 *   - Declare struct normally
 *   - Copy pixel blob through IPCProtocol::write_command(..., true, pixels, bytes)
 */
void APIENTRY gl_tex_image_2d_ovr(GLenum target, GLint level, GLint internalFormat, GLsizei width,
                                  GLsizei height, GLint border, GLenum format, GLenum type,
                                  const void* pixels)
{
    GLTexImage2DCommand payload{};
    payload.target = target;
    payload.level = level;
    payload.internalFormat = internalFormat;
    payload.width = width;
    payload.height = height;
    payload.border = border;
    payload.format = format;
    payload.type = type;

    const UINT32 pixels_bytes = utils::ComputePixelDataSize(width, height, format, type);

    g_ipc.write_command(GLCommandType::GLCMD_TEX_IMAGE_2D, payload, pixels_bytes, pixels != nullptr,
                        pixels);
}

void APIENTRY gl_tex_parameterf_ovr(GLenum target, GLenum pname, GLfloat param)
{
    GLTexParameterCommand payload{ target, pname, param };
    g_ipc.write_command(GLCommandType::GLCMD_TEX_PARAMETER, payload);
}

void APIENTRY gl_tex_envf_ovr(GLenum target, GLenum pname, GLfloat param)
{
    GLTexEnvfCommand payload{ target, pname, param };
    g_ipc.write_command(GLCommandType::GLCMD_TEX_ENV_F, payload);
}

void APIENTRY gl_tex_envi_ovr(GLenum target, GLenum pname, GLint param)
{
    GLTexEnviCommand payload{ target, pname, static_cast<UINT32>(param) };
    g_ipc.write_command(GLCommandType::GLCMD_TEX_ENV_I, payload);
}

/* FIXED FUNCTION */
void APIENTRY gl_lightf_ovr(GLenum light, GLenum pname, GLfloat param)
{
    GLLightCommand payload{ light, pname, param };
    g_ipc.write_command(GLCommandType::GLCMD_LIGHTF, payload);
}

void APIENTRY gl_lightfv_ovr(GLenum light, GLenum pname, const GLfloat* params)
{
    GLLightfvCommand payload{ light, pname, { params[0], params[1], params[2], params[3] } };
    g_ipc.write_command(GLCommandType::GLCMD_LIGHTFV, payload);
}

void APIENTRY gl_materiali_ovr(GLenum face, GLenum pname, GLint param)
{
    GLMaterialiCommand payload{ face, pname, param };
    g_ipc.write_command(GLCommandType::GLCMD_MATERIALI, payload);
}

void APIENTRY gl_materialf_ovr(GLenum face, GLenum pname, GLfloat param)
{
    GLMaterialfCommand payload{ face, pname, param };
    g_ipc.write_command(GLCommandType::GLCMD_MATERIALF, payload);
}

void APIENTRY gl_materialiv_ovr(GLenum face, GLenum pname, const GLint* params)
{
    GLMaterialivCommand payload{ face,
                                 pname,
                                 { static_cast<float>(params[0]), static_cast<float>(params[1]),
                                   static_cast<float>(params[2]), static_cast<float>(params[3]) } };
    g_ipc.write_command(GLCommandType::GLCMD_MATERIALIV, payload);
}

void APIENTRY gl_materialfv_ovr(GLenum face, GLenum pname, const GLfloat* params)
{
    GLMaterialfvCommand payload{ face, pname, { params[0], params[1], params[2], params[3] } };
    g_ipc.write_command(GLCommandType::GLCMD_MATERIALFV, payload);
}

void APIENTRY gl_alpha_func_ovr(GLenum func, GLclampf ref)
{
    GLAlphaFuncCommand payload{ func, ref };
    g_ipc.write_command(GLCommandType::GLCMD_ALPHA_FUNC, payload);
}

/* STATE MANAGEMENT */
void APIENTRY gl_enable_ovr(GLenum cap)
{
    GLEnableCommand payload{ cap };
    g_ipc.write_command(GLCommandType::GLCMD_ENABLE, payload);
}

void APIENTRY gl_disable_ovr(GLenum cap)
{
    GLDisableCommand payload{ cap };
    g_ipc.write_command(GLCommandType::GLCMD_DISABLE, payload);
}

void APIENTRY gl_color_mask_ovr(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    GLColorMaskCommand payload{ (UINT8)r, (UINT8)g, (UINT8)b, (UINT8)a };
    g_ipc.write_command(GLCommandType::GLCMD_COLOR_MASK, payload);
}

void APIENTRY gl_depth_mask_ovr(GLboolean flag)
{
    GLDepthMaskCommand payload{ (UINT8)flag };
    g_ipc.write_command(GLCommandType::GLCMD_DEPTH_MASK, payload);
}

void APIENTRY gl_blend_func_ovr(GLenum sfactor, GLenum dfactor)
{
    GLBlendFuncCommand payload{ sfactor, dfactor };
    g_ipc.write_command(GLCommandType::GLCMD_BLEND_FUNC, payload);
}

void APIENTRY gl_point_size_ovr(GLfloat size)
{
    GLPointSizeCommand payload{ size };
    g_ipc.write_command(GLCommandType::GLCMD_POINT_SIZE, payload);
}

void APIENTRY gl_polygon_offset_ovr(GLfloat factor, GLfloat units)
{
    GLPolygonOffsetCommand payload{ factor, units };
    g_ipc.write_command(GLCommandType::GLCMD_POLYGON_OFFSET, payload);
}

void APIENTRY gl_cull_face_ovr(GLenum mode)
{
    GLCullFaceCommand payload{ mode };
    g_ipc.write_command(GLCommandType::GLCMD_CULL_FACE, payload);
}

void APIENTRY gl_stencil_mask_ovr(GLuint mask)
{
    GLStencilMaskCommand payload{ mask };
    g_ipc.write_command(GLCommandType::GLCMD_STENCIL_MASK, payload);
}

void APIENTRY gl_stencil_func_ovr(GLenum func, GLint ref, GLuint mask)
{
    GLStencilFuncCommand payload{ func, ref, mask };
    g_ipc.write_command(GLCommandType::GLCMD_STENCIL_FUNC, payload);
}

void APIENTRY gl_stencil_op_ovr(GLenum sfail, GLenum dpfail, GLenum dppass)
{
    GLStencilOpCommand payload{ sfail, dpfail, dppass };
    g_ipc.write_command(GLCommandType::GLCMD_STENCIL_OP, payload);
}

void APIENTRY gl_stencil_op_separate_ATI_ovr(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass)
{
    GLStencilOpSeparateATICommand payload{ face, sfail, dpfail, dppass };
    g_ipc.write_command(GLCommandType::GLCMD_STENCIL_OP_SEPARATE_ATI, payload);
}

/* WGL (WINDOWS GRAPHICS LIBRARY) */

extern int WINAPI wgl_choose_pixel_format_ovr(HDC hdc, const PIXELFORMATDESCRIPTOR* ppfd);
extern int WINAPI wgl_describe_pixel_format_ovr(HDC hdc, int fmt, UINT bytes,
                                                LPPIXELFORMATDESCRIPTOR desc);
extern int WINAPI wgl_get_pixel_format_ovr(HDC hdc);
extern BOOL WINAPI wgl_set_pixel_format_ovr(HDC hdc, int fmt, const PIXELFORMATDESCRIPTOR* desc);
extern BOOL WINAPI wgl_swap_buffers_ovr(HDC hdc);

extern HGLRC WINAPI wgl_create_context_ovr(HDC dc);
extern BOOL WINAPI wgl_delete_context_ovr(HGLRC ctx);
extern HGLRC WINAPI wgl_get_current_context_ovr(void);
extern HDC WINAPI wgl_get_current_dc_ovr(void);
extern BOOL WINAPI wgl_make_current_ovr(HDC dc, HGLRC ctx);
extern BOOL WINAPI wgl_share_lists_ovr(HGLRC src, HGLRC dst);

extern BOOL WINAPI wgl_swap_interval_EXT_ovr(int interval);
extern const char* WINAPI wgl_get_extensions_string_ARB_ovr(HDC hdc);
extern const char* WINAPI wgl_get_extensions_string_EXT_ovr(void);

/* STATE QUERY */
extern const GLubyte* APIENTRY gl_get_string_ovr(GLenum name);
extern void APIENTRY gl_get_integerv_ovr(GLenum pname, GLint* data);
extern GLenum APIENTRY gl_get_error_ovr();

/* MULTITEXTURE */
extern void APIENTRY gl_active_texture_ARB_ovr(GLenum texture);
extern void APIENTRY gl_client_active_texture_ARB_ovr(GLenum texture);

extern void APIENTRY gl_multi_texcoord2f_ARB_ovr(GLenum unit, GLfloat s, GLfloat t);
extern void APIENTRY gl_multi_texcoord2fv_ARB_ovr(GLenum unit, const GLfloat* v);

std::once_flag g_install_flag;

void install_overrides()
{
    // create lambda function for `std::call_once`
    auto register_all_hooks_once_fn = []
    {
        /* CORE IMMEDIATE MODE */
        gl::register_hook("glBegin", reinterpret_cast<PROC>(&gl_begin_ovr));
        gl::register_hook("glEnd", reinterpret_cast<PROC>(&gl_end_ovr));
        gl::register_hook("glVertex2f", reinterpret_cast<PROC>(&gl_vertex2f_ovr));
        gl::register_hook("glVertex3f", reinterpret_cast<PROC>(&gl_vertex3f_ovr));
        gl::register_hook("glVertex3fv", reinterpret_cast<PROC>(&gl_vertex3fv_ovr));
        gl::register_hook("glColor3f", reinterpret_cast<PROC>(&gl_color3f_ovr));
        gl::register_hook("glColor4f", reinterpret_cast<PROC>(&gl_color4f_ovr));
        gl::register_hook("glNormal3f", reinterpret_cast<PROC>(&gl_normal3f_ovr));
        gl::register_hook("glTexCoord2f", reinterpret_cast<PROC>(&gl_tex_coord2f_ovr));

        /* DISPLAY LISTS */
        gl::register_hook("glCallList", reinterpret_cast<PROC>(&gl_call_list_ovr));
        gl::register_hook("glNewList", reinterpret_cast<PROC>(&gl_new_list_ovr));
        gl::register_hook("glEndList", reinterpret_cast<PROC>(&gl_end_list_ovr));
        gl::register_hook("glGenLists", reinterpret_cast<PROC>(&gl_gen_lists_ovr));

        /* CLIENT STATE */
        gl::register_hook("glEnableClientState",
                          reinterpret_cast<PROC>(&gl_enable_client_state_ovr));
        gl::register_hook("glDisableClientState",
                          reinterpret_cast<PROC>(&gl_disable_client_state_ovr));
        gl::register_hook("glVertexPointer", reinterpret_cast<PROC>(&gl_vertex_pointer_ovr));
        gl::register_hook("glNormalPointer", reinterpret_cast<PROC>(&gl_normal_pointer_ovr));
        gl::register_hook("glTexCoordPointer", reinterpret_cast<PROC>(&gl_tex_coord_pointer_ovr));
        gl::register_hook("glColorPointer", reinterpret_cast<PROC>(&gl_color_pointer_ovr));
        gl::register_hook("glIndexPointer", reinterpret_cast<PROC>(&gl_index_pointer_ovr));
        gl::register_hook("glEdgeFlagPointer", reinterpret_cast<PROC>(&gl_edge_flag_pointer_ovr));
        gl::register_hook("glDrawArrays", reinterpret_cast<PROC>(&gl_draw_arrays_ovr));
        gl::register_hook("glDrawElements", reinterpret_cast<PROC>(&gl_draw_elements_ovr));
        gl::register_hook("glDrawRangeElements",
                          reinterpret_cast<PROC>(&gl_draw_range_elements_ovr));

        /* MATRIX OPERATIONS */
        gl::register_hook("glMatrixMode", reinterpret_cast<PROC>(&gl_matrix_mode_ovr));
        gl::register_hook("glLoadIdentity", reinterpret_cast<PROC>(&gl_load_identity_ovr));
        gl::register_hook("glLoadMatrixf", reinterpret_cast<PROC>(&gl_load_matrixf_ovr));
        gl::register_hook("glMultMatrixf", reinterpret_cast<PROC>(&gl_mult_matrixf_ovr));
        gl::register_hook("glPushMatrix", reinterpret_cast<PROC>(&gl_push_matrix_ovr));
        gl::register_hook("glPopMatrix", reinterpret_cast<PROC>(&gl_pop_matrix_ovr));
        gl::register_hook("glTranslatef", reinterpret_cast<PROC>(&gl_translatef_ovr));
        gl::register_hook("glRotatef", reinterpret_cast<PROC>(&gl_rotatef_ovr));
        gl::register_hook("glScalef", reinterpret_cast<PROC>(&gl_scalef_ovr));
        gl::register_hook("glViewport", reinterpret_cast<PROC>(&gl_viewport_ovr));
        gl::register_hook("glOrtho", reinterpret_cast<PROC>(&gl_ortho_ovr));
        gl::register_hook("glFrustum", reinterpret_cast<PROC>(&gl_frustum_ovr));

        /* RENDERING */
        gl::register_hook("glClear", reinterpret_cast<PROC>(&gl_clear_ovr));
        gl::register_hook("glClearColor", reinterpret_cast<PROC>(&gl_clear_color_ovr));
        gl::register_hook("glFlush", reinterpret_cast<PROC>(&gl_flush_ovr));
        gl::register_hook("glFinish", reinterpret_cast<PROC>(&gl_finish_ovr));
        gl::register_hook("glBindTexture", reinterpret_cast<PROC>(&gl_bind_texture_ovr));
        gl::register_hook("glGenTextures", reinterpret_cast<PROC>(&gl_gen_textures_ovr));
        gl::register_hook("glDeleteTextures", reinterpret_cast<PROC>(&gl_delete_textures_ovr));
        gl::register_hook("glTexImage2D", reinterpret_cast<PROC>(&gl_tex_image_2d_ovr));
        gl::register_hook("glTexParameterf", reinterpret_cast<PROC>(&gl_tex_parameterf_ovr));
        gl::register_hook("glTexEnvi", reinterpret_cast<PROC>(&gl_tex_envi_ovr));
        gl::register_hook("glTexEnvf", reinterpret_cast<PROC>(&gl_tex_envf_ovr));

        /* FIXED FUNCTION */
        gl::register_hook("glLightf", reinterpret_cast<PROC>(&gl_lightf_ovr));
        gl::register_hook("glLightfv", reinterpret_cast<PROC>(&gl_lightfv_ovr));
        gl::register_hook("glMateriali", reinterpret_cast<PROC>(&gl_materiali_ovr));
        gl::register_hook("glMaterialf", reinterpret_cast<PROC>(&gl_materialf_ovr));
        gl::register_hook("glMaterialiv", reinterpret_cast<PROC>(&gl_materialiv_ovr));
        gl::register_hook("glMaterialfv", reinterpret_cast<PROC>(&gl_materialfv_ovr));
        gl::register_hook("glAlphaFunc", reinterpret_cast<PROC>(&gl_alpha_func_ovr));

        /* STATE MANAGEMENT */
        gl::register_hook("glEnable", reinterpret_cast<PROC>(&gl_enable_ovr));
        gl::register_hook("glDisable", reinterpret_cast<PROC>(&gl_disable_ovr));
        gl::register_hook("glColorMask", reinterpret_cast<PROC>(&gl_color_mask_ovr));
        gl::register_hook("glDepthMask", reinterpret_cast<PROC>(&gl_depth_mask_ovr));
        gl::register_hook("glBlendFunc", reinterpret_cast<PROC>(&gl_blend_func_ovr));
        gl::register_hook("glPointSize", reinterpret_cast<PROC>(&gl_point_size_ovr));
        gl::register_hook("glPolygonOffset", reinterpret_cast<PROC>(&gl_polygon_offset_ovr));
        gl::register_hook("glCullFace", reinterpret_cast<PROC>(&gl_cull_face_ovr));
        gl::register_hook("glStencilMask", reinterpret_cast<PROC>(&gl_stencil_mask_ovr));
        gl::register_hook("glStencilFunc", reinterpret_cast<PROC>(&gl_stencil_func_ovr));
        gl::register_hook("glStencilOp", reinterpret_cast<PROC>(&gl_stencil_op_ovr));
        gl::register_hook("glStencilOpSeparateATI",
                          reinterpret_cast<PROC>(&gl_stencil_op_separate_ATI_ovr));

        /* WGL (WINDOWS GRAPHICS LIBRARY) */
        gl::register_hook("wglChoosePixelFormat",
                          reinterpret_cast<PROC>(&wgl_choose_pixel_format_ovr));
        gl::register_hook("wglDescribePixelFormat",
                          reinterpret_cast<PROC>(&wgl_describe_pixel_format_ovr));
        gl::register_hook("wglGetPixelFormat", reinterpret_cast<PROC>(&wgl_get_pixel_format_ovr));
        gl::register_hook("wglSetPixelFormat", reinterpret_cast<PROC>(&wgl_set_pixel_format_ovr));
        gl::register_hook("wglSwapBuffers", reinterpret_cast<PROC>(&wgl_swap_buffers_ovr));
        gl::register_hook("wglCreateContext", reinterpret_cast<PROC>(&wgl_create_context_ovr));
        gl::register_hook("wglDeleteContext", reinterpret_cast<PROC>(&wgl_delete_context_ovr));
        gl::register_hook("wglGetCurrentContext",
                          reinterpret_cast<PROC>(&wgl_get_current_context_ovr));
        gl::register_hook("wglGetCurrentDC", reinterpret_cast<PROC>(&wgl_get_current_dc_ovr));
        gl::register_hook("wglMakeCurrent", reinterpret_cast<PROC>(&wgl_make_current_ovr));
        gl::register_hook("wglShareLists", reinterpret_cast<PROC>(&wgl_share_lists_ovr));
        gl::register_hook("wglSwapIntervalEXT", reinterpret_cast<PROC>(&wgl_swap_interval_EXT_ovr));
        gl::register_hook("wglGetExtensionsStringARB",
                          reinterpret_cast<PROC>(&wgl_get_extensions_string_ARB_ovr));
        gl::register_hook("wglGetExtensionsStringEXT",
                          reinterpret_cast<PROC>(&wgl_get_extensions_string_EXT_ovr));

        /* STATE QUERY */
        gl::register_hook("glGetString", reinterpret_cast<PROC>(&gl_get_string_ovr));
        gl::register_hook("glGetIntegerv", reinterpret_cast<PROC>(&gl_get_integerv_ovr));
        gl::register_hook("glGetError", reinterpret_cast<PROC>(&gl_get_error_ovr));

        /* MULTITEXTURE */
        gl::register_hook("glActiveTextureARB", reinterpret_cast<PROC>(&gl_active_texture_ARB_ovr));
        gl::register_hook("glClientActiveTexture",
                          reinterpret_cast<PROC>(&gl_client_active_texture_ARB_ovr));
        gl::register_hook("glClientActiveTextureARB",
                          reinterpret_cast<PROC>(&gl_client_active_texture_ARB_ovr));
        gl::register_hook("glMultiTexCoord2fARB",
                          reinterpret_cast<PROC>(&gl_multi_texcoord2f_ARB_ovr));
        gl::register_hook("glMultiTexCoord2fvARB",
                          reinterpret_cast<PROC>(&gl_multi_texcoord2fv_ARB_ovr));
    };

    std::call_once(g_install_flag, register_all_hooks_once_fn);
}
}  // namespace glRemix::hooks
