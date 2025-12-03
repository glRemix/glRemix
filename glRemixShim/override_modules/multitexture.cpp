#include "override_modules/common_includes.h"

namespace glRemix::hooks
{

void APIENTRY gl_active_texture_ARB_ovr(GLenum texture)
{
    return;
}

void APIENTRY gl_client_active_texture_ARB_ovr(GLenum texture)
{
    return;
}

void APIENTRY gl_multi_texcoord2f_ARB_ovr(GLenum target, float s, float t)
{
    return;
}

void APIENTRY gl_multi_texcoord2fv_ARB_ovr(GLenum target, const float* v)
{
    return;
}

}  // namespace glRemix::hooks
