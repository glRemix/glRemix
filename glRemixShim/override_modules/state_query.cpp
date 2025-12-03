#include "override_modules/common_includes.h"

#include "generated/glext_enums.inl"

namespace glRemix::hooks
{
const GLubyte* APIENTRY gl_get_string_ovr(GLenum name)
{
    switch (name)
    {
        case GL_EXTENSIONS: return reinterpret_cast<const GLubyte*>(k_EXTENSIONS);
        case GL_VERSION: return reinterpret_cast<const GLubyte*>("1.3");  // TODO: define in CMake
        case GL_VENDOR: return reinterpret_cast<const GLubyte*>("glRemix");
        case GL_RENDERER: return reinterpret_cast<const GLubyte*>("glRemixRenderer");
        default: return reinterpret_cast<const GLubyte*>("");
    }
}

void APIENTRY gl_get_integerv_ovr(GLenum pname, GLint* data)
{
    switch (pname)
    {
        case GL_MAX_TEXTURE_SIZE: *data = 4096; return;
        case GL_MAX_TEXTURE_UNITS_ARB: *data = k_MAX_TEXTURE_UNITS; return;
        case GL_ACTIVE_TEXTURE_ARB: *data = g_active_texture_unit; return;
        case GL_CLIENT_ACTIVE_TEXTURE_ARB: *data = g_client_active_texcoord_unit; return;
        case GL_TEXTURE_BINDING_1D:
            *data = g_texture_units[g_active_texture_unit].binding_1d;
            return;
        case GL_TEXTURE_BINDING_2D:
            *data = g_texture_units[g_active_texture_unit].binding_2d;
            return;
        case GL_TEXTURE_MATRIX:
            *data = g_texture_units[g_active_texture_unit].texture_matrix_mode;
            return;
        default: *data = 0; return;
    }
}

GLenum APIENTRY gl_get_error_ovr()
{
    return GL_NO_ERROR;
}

}  // namespace glRemix::hooks
