#include "override_modules/common_includes.h"

#include "gl_loader.h"

#include "generated/glext_enums.inl"

namespace glRemix::hooks
{

void APIENTRY gl_active_texture_ARB_ovr(GLenum texture)
{
    UINT32 unit = texture - GL_TEXTURE0_ARB;

    if (unit >= k_MAX_TEXTURE_UNITS)
    {
        unit = 0;  // clamp to valid range for sanity
    }

    g_active_texture_unit = unit;

    GLActiveTextureARBCommand payload{ texture };
    g_ipc.write_command(GLCommandType::GLCMD_ACTIVE_TEXTURE_ARB, payload);
}

void APIENTRY gl_client_active_texture_ARB_ovr(GLenum texture)
{
    UINT32 unit = texture - GL_TEXTURE0_ARB;

    if (unit >= k_MAX_TEXTURE_UNITS)
    {
        unit = 0;
    }

    g_client_active_texcoord_unit = unit;

    // do not send via ipc
}

void APIENTRY gl_multi_texcoord2f_ARB_ovr(GLenum target, float s, float t)
{
    UINT32 unit = target - GL_TEXTURE0_ARB;

    if (unit < k_MAX_TEXTURE_UNITS)
    {
        g_texture_units[unit].texcoord[0] = s;
        g_texture_units[unit].texcoord[1] = t;
    }

    GLMultiTexCoord2fARBCommand payload{ target, s, t };
    g_ipc.write_command(GLCommandType::GLCMD_MULTI_TEXCOORD2F_ARB, payload);
}

void APIENTRY gl_multi_texcoord2fv_ARB_ovr(GLenum target, const float* v)
{
    UINT32 unit = target - GL_TEXTURE0_ARB;

    if (unit < k_MAX_TEXTURE_UNITS)
    {
        g_texture_units[unit].texcoord[0] = v[0];
        g_texture_units[unit].texcoord[1] = v[1];
    }

    GLMultiTexCoord2fARBCommand payload{ target, v[0], v[1] };
    g_ipc.write_command(GLCommandType::GLCMD_MULTI_TEXCOORD2F_ARB, payload);
}

}  // namespace glRemix::hooks
