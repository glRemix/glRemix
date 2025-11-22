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
}  // namespace glRemix
