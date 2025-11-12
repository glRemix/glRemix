#pragma once

#include <GL/gl.h>

namespace glRemix
{

/* Handled all cases specified here: https://learn.microsoft.com/en-us/windows/win32/opengl/glteximage2d */
static inline size_t _BytesPerComponent(GLenum type)
{
    switch (type)
    {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:
        case GL_BITMAP: return 1;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT: return 4;
        default: return 0;
    }
}

/* Handled all cases specified here: https://learn.microsoft.com/en-us/windows/win32/opengl/glteximage2d */
static inline size_t _ComponentsPerPixel(GLenum format)
{
    switch (format)
    {
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_ALPHA:
        case GL_LUMINANCE: return 1;
        case GL_LUMINANCE_ALPHA:
        case GL_RGB: return 3;
        case GL_RGBA: return 4;
        default: return 0;
    }
}

inline size_t ComputePixelDataSize(GLsizei width, GLsizei height, GLenum format, GLenum type)
{
    return static_cast<size_t>(width) * static_cast<size_t>(height) * _ComponentsPerPixel(format)
           * _BytesPerComponent(type);
}

}  // namespace glRemix
