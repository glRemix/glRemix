// Structures for representing OpenGL commands to be sent to the renderer process
// Not the same as the OpenGL API calls coming from gl.xml

#pragma once

#include <cstdint>

namespace glRemix
{
enum class GLCommandType : uint32_t {
    // Basic OpenGL 1.x commands
    GLCMD_BEGIN = 1,
    GLCMD_END,
    GLCMD_VERTEX2F,
    GLCMD_VERTEX3F,
    GLCMD_COLOR3F,
    GLCMD_COLOR4F,
    GLCMD_NORMAL3F,
    GLCMD_TEXCOORD2F,

    // Matrix operations
    GLCMD_MATRIX_MODE,
    GLCMD_LOAD_IDENTITY,
    GLCMD_LOAD_MATRIX,
    GLCMD_MULT_MATRIX,
    GLCMD_PUSH_MATRIX,
    GLCMD_POP_MATRIX,
    GLCMD_TRANSLATE,
    GLCMD_ROTATE,
    GLCMD_SCALE,

    // Texture operations
    GLCMD_BIND_TEXTURE,
    GLCMD_GEN_TEXTURES,
    GLCMD_DELETE_TEXTURES,
    GLCMD_TEX_IMAGE_2D,
    GLCMD_TEX_PARAMETER,

    // Lighting
    GLCMD_ENABLE,
    GLCMD_DISABLE,
    GLCMD_LIGHT,
    GLCMD_LIGHTF,
    GLCMD_LIGHTFV,
    GLCMD_MATERIAL,
    GLCMD_MATERIALF,
    GLCMD_MATERIALFV,

    // Buffer operations
    GLCMD_CLEAR,
    GLCMD_CLEAR_COLOR,
    GLCMD_FLUSH,
    GLCMD_FINISH,

    // Viewport and projection
    GLCMD_VIEWPORT,
    GLCMD_ORTHO,
    GLCMD_FRUSTUM,
    GLCMD_PERSPECTIVE,

    // Other
    GLCMD_SHUTDOWN,

    // Display Lists
    GLCMD_CALL_LIST,
    GLCMD_NEW_LIST,
    GLCMD_END_LIST,
};

struct GLVec2f
{
    float x, y;
};

struct GLVec3f
{
    float x, y, z;
};

struct GLVec4f
{
    float x, y, z, w;
};

struct GLVec3d
{
    double x, y, z;
};

struct GLVec4d
{
    double x, y, z, w;
};

struct GLEmptyCommand
{
    uint32_t reserved = 0;  // to maintain alignment. think of as padding GPUBuffers
};

// Name *Unifs for clear association
// Header for all commands
struct GLCommandUnifs
{
    GLCommandType type;
    uint32_t dataSize;
};

// Specific command structures
struct GLBeginCommand
{
    uint32_t mode;  // GL_TRIANGLES, GL_QUADS, etc.
};

using GLEndCommand = GLEmptyCommand;

using GLVertex2fCommand = GLVec2f;
using GLVertex3fCommand = GLVec3f;
using GLColor3fCommand = GLVec3f;
using GLColor4fCommand = GLVec4f;
using GLNormal3fCommand = GLVec3f;
using GLTexCoord2fCommand = GLVec2f;

// Matrix operations
struct GLMatrixModeCommand
{
    uint32_t mode;
};

struct GLLoadMatrixCommand
{
    float m[16];
};

struct GLMultMatrixCommand
{
    float m[16];
};

using GLLoadIdentityCommand = GLEmptyCommand;
using GLPushMatrixCommand = GLEmptyCommand;
using GLPopMatrixCommand = GLEmptyCommand;

struct GLTranslateCommand
{
    GLVec3f t;
};

struct GLRotateCommand
{
    float angle;
    GLVec3f axis;
};

struct GLScaleCommand
{
    GLVec3f s;
};

// Texture operations
struct GLBindTextureCommand
{
    uint32_t target;
    uint32_t texture;
};

struct GLGenTexturesCommand
{
    uint32_t n;
};

struct GLDeleteTexturesCommand
{
    uint32_t n;
};

struct GLTexImage2DCommand
{
    uint32_t target;
    uint32_t level;
    uint32_t internalFormat; // must be same as format.
    uint32_t width;
    uint32_t height;
    uint32_t border; // the width of the border. must be either 0 or 1.
    uint32_t format;
    uint32_t type;

    uint32_t dataSize;    // number of bytes of pixel data following this struct
    uint32_t dataOffset;  // byte offset from start of command (sizeof(GLTexImage2DCommand))
};

struct GLTexParameterCommand
{
    uint32_t target;
    uint32_t pname;
    float param;
};

// Lighting
struct GLEnableCommand
{
    uint32_t cap;
};

struct GLDisableCommand
{
    uint32_t cap;
};

struct GLLightCommand
{
    uint32_t light;
    uint32_t pname;
    float param;
};

struct GLLightfvCommand
{
    uint32_t light;
    uint32_t pname;
    GLVec4f params;
};

struct GLMaterialCommand
{
    uint32_t face;
    uint32_t pname;
    float param;
};

struct GLMaterialfvCommand
{
    uint32_t face;
    uint32_t pname;
    GLVec4f params;
};

// Buffer ops
struct GLClearCommand
{
    uint32_t mask;
};

struct GLClearColorCommand
{
    GLVec4f color;
};

using GLFlushCommand = GLEmptyCommand;
using GLFinishCommand = GLEmptyCommand;

// Viewport & projection
struct GLViewportCommand
{
    int32_t x, y, width, height;
};

struct GLOrthoCommand
{
    double left, right, bottom, top, zNear, zFar;
};

struct GLFrustumCommand
{
    double left, right, bottom, top, zNear, zFar;
};

struct GLPerspectiveCommand
{
    double fovY, aspect, zNear, zFar;
};

// Other
using GLShutdownCommand = GLEmptyCommand;

// Display Lists
struct GLCallListCommand
{
    uint32_t list;
};

struct GLNewListCommand
{
    uint32_t list;
    uint32_t mode;  // enum GL_COMPILE or GL_COMPILE_AND_EXECUTE
};

using GLEndListCommand = GLEmptyCommand;

}  // namespace glRemix