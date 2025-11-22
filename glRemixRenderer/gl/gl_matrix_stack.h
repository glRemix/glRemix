#pragma once

#include <basetsd.h>
#include <DirectXMath.h>
#include <stack>

#include <Windows.h>
#include <GL/gl.h>

using namespace DirectX;

namespace glRemix::gl
{

class glMatrixStack
{
public:
    glMatrixStack();

    std::stack<XMFLOAT4X4> model_view;
    std::stack<XMFLOAT4X4> projection;
    std::stack<XMFLOAT4X4> texture;

    void push(UINT32 mode);
    void pop(UINT32 mode);
    XMFLOAT4X4& top(UINT32 mode);
    void mul_set(UINT32 mode, const XMMATRIX& r);  // multiplies and sets top of stack
    void mul_set(UINT32 mode, const float* m);     // multiplies and sets top of stack

    // operations
    void identity(UINT32 mode);
    void rotate(UINT32 mode, float angle, float x, float y, float z);
    void translate(UINT32 mode, float x, float y, float z);
    void scale(UINT32 mode, float x, float y, float z);
    void ortho(UINT32 mode, double l, double r, double b, double t, double n, double f);
    void frustum(UINT32 mode, double l, double r, double b, double t, double n, double f);
    void perspective(UINT32 mode, double fov_y, double aspect, double n, double f);
    void load(UINT32 mode, const float* m);

    // debug
    void print_stacks() const;
};

}  // namespace glRemix::gl
