// Structures for representing OpenGL commands to be sent to the renderer process
// Not the same as the OpenGL API calls coming from gl.xml

#pragma once

#include <cstdint>

namespace glRemix
{

/* ENUMS */
enum class GLCommandType : UINT32
{
    // Core Immediate Mode
    GLCMD_BEGIN = 1,
    GLCMD_END,
    GLCMD_VERTEX2F,
    GLCMD_VERTEX3F,
    GLCMD_COLOR3F,
    GLCMD_COLOR4F,
    GLCMD_NORMAL3F,
    GLCMD_TEXCOORD2F,

    // Display Lists
    GLCMD_CALL_LIST,
    GLCMD_NEW_LIST,
    GLCMD_END_LIST,

    // Client State
    GLREMIXCMD_DRAW_ARRAYS,
    GLREMIXCMD_DRAW_ELEMENTS,
    GLREMIXCMD_DRAW_RANGE_ELEMENTS,

    // Matrix Operations
    GLCMD_MATRIX_MODE,
    GLCMD_LOAD_IDENTITY,
    GLCMD_LOAD_MATRIX,
    GLCMD_MULT_MATRIX,
    GLCMD_PUSH_MATRIX,
    GLCMD_POP_MATRIX,
    GLCMD_TRANSLATE,
    GLCMD_ROTATE,
    GLCMD_SCALE,
    GLCMD_VIEWPORT,
    GLCMD_ORTHO,
    GLCMD_FRUSTUM,
    GLCMD_PERSPECTIVE,

    // Rendering
    GLCMD_CLEAR,
    GLCMD_CLEAR_COLOR,
    GLCMD_FLUSH,
    GLCMD_FINISH,
    GLCMD_BIND_TEXTURE,
    GLCMD_GEN_TEXTURES,
    GLCMD_DELETE_TEXTURES,
    GLCMD_TEX_IMAGE_2D,
    GLCMD_TEX_PARAMETER,
    GLCMD_TEX_ENV_I,
    GLCMD_TEX_ENV_F,

    // Fixed Function
    GLCMD_LIGHTF,
    GLCMD_LIGHTFV,
    GLCMD_MATERIALI,
    GLCMD_MATERIALF,
    GLCMD_MATERIALIV,
    GLCMD_MATERIALFV,
    GLCMD_ALPHA_FUNC,

    // State Management
    GLCMD_ENABLE,
    GLCMD_DISABLE,
    GLCMD_COLOR_MASK,
    GLCMD_DEPTH_MASK,
    GLCMD_BLEND_FUNC,
    GLCMD_POINT_SIZE,
    GLCMD_POLYGON_OFFSET,
    GLCMD_CULL_FACE,
    GLCMD_STENCIL_MASK,
    GLCMD_STENCIL_FUNC,
    GLCMD_STENCIL_OP,
    GLCMD_STENCIL_OP_SEPARATE_ATI,

    // Other
    WGLCMD_INPUT_EVENT,
    GLREMIXCMD_SEND_HWND,

    _COUNT,  // sentinel value (keep this as the last element in enum to always have a count
             // of enum elements)
};

enum class GLRemixClientArrayType : UINT32
{
    VERTEX,    // GL_VERTEX_ARRAY
    NORMAL,    // GL_NORMAL_ARRAY
    COLOR,     // GL_COLOR_ARRAY
    TEXCOORD,  // GL_TEXCOORD_ARRAY
    COLORIDX,  // GL_INDEX_ARRAY
    EDGEFLAG,  // GL_EDGEFLAG_ARRAY
    INDICES,   // `indices` passed in `glDrawElements`
    _COUNT,
    _INVALID
};

/* HEADER STRUCTS */
struct GLCommandHeader
{
    GLCommandType type;
    UINT32 cmd_bytes;
};

// per-frame uniforms for OpenGL commands sent via IPC
struct GLFrameHeader
{
    UINT32 frame_index;  // incremental frame counter
    UINT32 frame_bytes;  // bytes following this ipc_payload
};

struct GLRemixClientArrayHeader
{
    UINT32 size;
    UINT32 type;
    UINT32 stride;
    UINT32 array_bytes;
    GLRemixClientArrayType array_type;
};

/* COMPONENT STRUCTS */
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
    UINT32 reserved = 0;  // to maintain alignment. think of as padding GPUBuffers
};

/* CORE IMMEDIATE MODE */
struct GLBeginCommand
{
    UINT32 mode;  // GL_TRIANGLES, GL_QUADS, etc.
};

using GLEndCommand = GLEmptyCommand;
using GLVertex2fCommand = GLVec2f;
using GLVertex3fCommand = GLVec3f;
using GLColor3fCommand = GLVec3f;
using GLColor4fCommand = GLVec4f;
using GLNormal3fCommand = GLVec3f;
using GLTexCoord2fCommand = GLVec2f;

/* DISPLAY LISTS */
struct GLCallListCommand
{
    UINT32 list;
};

struct GLNewListCommand
{
    UINT32 list;
    UINT32 mode;  // enum GL_COMPILE or GL_COMPILE_AND_EXECUTE
};

using GLEndListCommand = GLEmptyCommand;

/* CLIENT STATE */
struct GLRemixDrawArraysCommand
{
    UINT32 mode;
    UINT32 first;
    UINT32 count;
    UINT32 enabled;  // amount of currently enabled client array kinds (<= 6)
    GLRemixClientArrayHeader headers[static_cast<UINT32>(GLRemixClientArrayType::_COUNT)];
};

struct GLRemixDrawElementsCommand
{
    UINT32 mode;
    UINT32 count;
    UINT32 type;
    UINT32 enabled;
    GLRemixClientArrayHeader headers[static_cast<UINT32>(GLRemixClientArrayType::_COUNT)];
};

struct GLRemixDrawRangeElementsCommand
{
    UINT32 mode;
    UINT32 start;
    UINT32 end;
    UINT32 count;
    UINT32 type;
    UINT32 enabled;
    GLRemixClientArrayHeader headers[static_cast<UINT32>(GLRemixClientArrayType::_COUNT)];
};

/* MATRIX OPERATIONS */
struct GLMatrixModeCommand
{
    UINT32 mode;
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

/* RENDERING */
struct GLClearCommand
{
    UINT32 mask;
};

struct GLClearColorCommand
{
    GLVec4f color;
};

using GLFlushCommand = GLEmptyCommand;
using GLFinishCommand = GLEmptyCommand;

struct GLBindTextureCommand
{
    UINT32 target;
    UINT32 texture;
};

constexpr UINT32 k_MAX_TEXTURE_IDS_PER_COMMAND = 64;

struct GLGenTexturesCommand
{
    UINT32 n;
    UINT32 ids[k_MAX_TEXTURE_IDS_PER_COMMAND];  // should usually be 1-4
};

struct GLDeleteTexturesCommand
{
    UINT32 n;
    UINT32 ids[k_MAX_TEXTURE_IDS_PER_COMMAND];
};

struct GLTexImage2DCommand
{
    UINT32 target;
    UINT32 level;
    UINT32 internalFormat;  // must be same as format.
    UINT32 width;
    UINT32 height;
    UINT32 border;  // the width of the border. must be either 0 or 1.
    UINT32 format;
    UINT32 type;
};

struct GLTexParameterCommand
{
    UINT32 target;
    UINT32 pname;
    float param;
};

struct GLTexEnviCommand
{
    UINT32 target;
    UINT32 pname;
    UINT32 param;
};

struct GLTexEnvfCommand
{
    UINT32 target;
    UINT32 pname;
    float param;
};

/* FIXED FUNCTION */
struct GLLightCommand
{
    UINT32 light;
    UINT32 pname;
    float param;
};

struct GLLightfvCommand
{
    UINT32 light;
    UINT32 pname;
    GLVec4f params;
};

struct GLMaterialiCommand
{
    UINT32 face;
    UINT32 pname;
    int param;
};

struct GLMaterialfCommand
{
    UINT32 face;
    UINT32 pname;
    float param;
};

struct GLMaterialivCommand
{
    UINT32 face;
    UINT32 pname;
    GLVec4f params;
};

struct GLMaterialfvCommand
{
    UINT32 face;
    UINT32 pname;
    GLVec4f params;
};

struct GLAlphaFuncCommand
{
    UINT32 func;
    float ref;
};

/* STATE MANAGEMENT */
struct GLEnableCommand
{
    UINT32 cap;
};

struct GLDisableCommand
{
    UINT32 cap;
};

struct GLColorMaskCommand
{
    UINT8 r, g, b, a;
};

struct GLDepthMaskCommand
{
    UINT8 flag;
};

struct GLBlendFuncCommand
{
    UINT32 sfactor;
    UINT32 dfactor;
};

struct GLPointSizeCommand
{
    float size;
};

struct GLPolygonOffsetCommand
{
    float factor;
    float units;
};

struct GLCullFaceCommand
{
    UINT32 mode;
};

struct GLStencilMaskCommand
{
    UINT32 mask;
};

struct GLStencilFuncCommand
{
    UINT32 func;
    INT32 ref;
    UINT32 mask;
};

struct GLStencilOpCommand
{
    UINT32 sfail;
    UINT32 dpfail;
    UINT32 dppass;
};

struct GLStencilOpSeparateATICommand
{
    UINT32 face;
    UINT32 sfail;
    UINT32 dpfail;
    UINT32 dppass;
};

struct WGLInputEventCommand
{
    UINT32 msg;
    UINT64 wparam;
    UINT64 lparam;
};

struct GLRemixSendHWNDCommand
{
    HWND hwnd;  // NOTE: 32-bit in x86 shim, 64-bit in x64 shim
};
}  // namespace glRemix
