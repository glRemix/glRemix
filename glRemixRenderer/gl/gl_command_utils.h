#pragma once

#include <DirectXMath.h>

namespace glRemix
{
inline XMFLOAT2 fv_to_xmf2(const GLVec2f& fv)
{
    return XMFLOAT2{ fv.x, fv.y };
}

inline XMFLOAT3 fv_to_xmf3(const GLVec3f& fv)
{
    return XMFLOAT3{ fv.x, fv.y, fv.z };
}

inline XMFLOAT4 fv_to_xmf4(const GLVec4f& fv)
{
    return XMFLOAT4{ fv.x, fv.y, fv.z, fv.w };
}

inline XMFLOAT2 f_to_xmf2(const float f)
{
    return fv_to_xmf2(GLVec2f{ f, f });
}

inline XMFLOAT3 f_to_xmf3(const float f)
{
    return fv_to_xmf3(GLVec3f{ f, f, f });
}

inline XMFLOAT4 f_to_xmf4(const float f)
{
    return fv_to_xmf4(GLVec4f{ f, f, f, f });
}

DXGI_FORMAT gl_format_to_dxgi(UINT32 format, UINT32 type)
{
    // promote GL_BYTE, GL_INT, GL_UNSIGNED_INT to float for now
    if (type == GL_FLOAT || type == GL_BYTE || type == GL_INT || type == GL_UNSIGNED_INT)
    {
        switch (format)
        {
            case GL_RED:
            case GL_GREEN:
            case GL_BLUE:
            case GL_ALPHA:
            case GL_LUMINANCE: return DXGI_FORMAT_R32_FLOAT;
            case GL_LUMINANCE_ALPHA: return DXGI_FORMAT_R32G32_FLOAT;
            case GL_RGB: return DXGI_FORMAT_R32G32B32_FLOAT;
            case GL_RGBA: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            default: return DXGI_FORMAT_UNKNOWN;
        }
    }

    if (type == GL_UNSIGNED_BYTE)
    {
        switch (format)
        {
            case GL_RED:
            case GL_GREEN:
            case GL_BLUE:
            case GL_ALPHA:
            case GL_LUMINANCE: return DXGI_FORMAT_R8_UNORM;
            case GL_LUMINANCE_ALPHA: return DXGI_FORMAT_R8G8_UNORM;
            case GL_RGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case GL_RGBA: return DXGI_FORMAT_R8G8B8A8_UNORM;
            default: return DXGI_FORMAT_UNKNOWN;
        }
    }
    return DXGI_FORMAT_UNKNOWN;
}

}  // namespace glRemix
