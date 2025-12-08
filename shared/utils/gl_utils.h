#pragma once

#include <GL/gl.h>

namespace glRemix
{
namespace utils
{
/* Handled all cases specified here:
 * https://learn.microsoft.com/en-us/windows/win32/opengl/glteximage2d */
static inline UINT32 _BytesPerComponentType(GLenum type)
{
    switch (type)
    {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:
        case GL_BITMAP: return 1;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT: return 2;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT: return 4;
        case GL_DOUBLE: return 8;
        default: return 0;
    }
}

/* Handled all cases specified here:
 * https://learn.microsoft.com/en-us/windows/win32/opengl/glteximage2d */
static inline UINT32 _ComponentsPerPixelFormat(GLenum format)
{
    switch (format)
    {
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_ALPHA:
        case GL_LUMINANCE: return 1;
        case GL_LUMINANCE_ALPHA: return 2;
        case GL_RGB: return 3;
        case GL_RGBA: return 4;
        default: return 0;
    }
}

static inline UINT32 ComputePixelDataSize(GLsizei width, GLsizei height, GLenum format, GLenum type)
{
    return static_cast<UINT32>(width) * static_cast<UINT32>(height)
           * _ComponentsPerPixelFormat(format) * _BytesPerComponentType(type);
}

static inline SIZE_T InterpretStride(GLint size, GLenum type, GLint stride)
{
    if (stride == 0)
    {
        UINT32 bytes_per_component = _BytesPerComponentType(type);

        stride = size * bytes_per_component;
    }
    return stride;
}

/**
 * @param stride: Assumes stride has already been passed through `InterpretStride`
 */
static inline UINT32 ComputeClientArraySize(GLint count, GLint size, GLenum type, GLsizei stride)
{
    return count * stride;
}

static inline GLRemixClientArrayType MapTo(GLenum cap, const UINT32 texcoord_unit)
{
    switch (cap)
    {
        case GL_VERTEX_ARRAY: return GLRemixClientArrayType::VERTEX;
        case GL_NORMAL_ARRAY: return GLRemixClientArrayType::NORMAL;
        case GL_COLOR_ARRAY: return GLRemixClientArrayType::COLOR;
        case GL_TEXTURE_COORD_ARRAY:
            return static_cast<GLRemixClientArrayType>(
                static_cast<UINT32>(GLRemixClientArrayType::TEXCOORD0) + texcoord_unit);
        case GL_INDEX_ARRAY: return GLRemixClientArrayType::COLORIDX;
        case GL_EDGE_FLAG_ARRAY: return GLRemixClientArrayType::EDGEFLAG;
        default: return GLRemixClientArrayType::_INVALID;
    }
}
}  // namespace utils
}  // namespace glRemix
