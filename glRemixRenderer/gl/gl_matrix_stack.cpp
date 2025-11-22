#include "gl_matrix_stack.h"

#include <cmath>
#include <vector>

using namespace DirectX;
using namespace glRemix::gl;

static XMFLOAT4X4 load_identity()
{
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, XMMatrixIdentity());
    return m;
}

// "Initially there is only one matrix on each stack and all matrices are set to the identity"
// -gl 1.x spec pg 29
glMatrixStack::glMatrixStack()
{
    const auto i = load_identity();

    model_view.push(i);
    projection.push(i);
    texture.push(i);
}

void glMatrixStack::identity(const UINT32 mode)
{
    std::stack<XMFLOAT4X4>* stack;
    switch (mode)
    {
        case GL_MODELVIEW: stack = &model_view; break;
        case GL_PROJECTION: stack = &projection; break;
        case GL_TEXTURE: stack = &texture; break;
        default: return;
    }

    if (stack->empty())
    {
        return;
    }

    XMStoreFloat4x4(&stack->top(), XMMatrixIdentity());
}

void glMatrixStack::push(const UINT32 mode)
{
    switch (mode)
    {
        case GL_MODELVIEW:
            if (!model_view.empty())
            {
                model_view.push(model_view.top());
            }
            break;

        case GL_PROJECTION:
            if (!projection.empty())
            {
                projection.push(projection.top());
            }
            break;

        case GL_TEXTURE:
            if (!texture.empty())
            {
                texture.push(texture.top());
            }
            break;
    }
}

void glMatrixStack::pop(const UINT32 mode)
{
    switch (mode)
    {
        case GL_MODELVIEW:
            if (model_view.size() > 1)
            {
                model_view.pop();
            }
            break;

        case GL_PROJECTION:
            if (projection.size() > 1)
            {
                projection.pop();
            }
            break;

        case GL_TEXTURE:
            if (texture.size() > 1)
            {
                texture.pop();
            }
            break;

        default: break;
    }
}

XMFLOAT4X4& glMatrixStack::top(const UINT32 mode)
{
    switch (mode)
    {
        case GL_MODELVIEW: return model_view.top();

        case GL_PROJECTION: return projection.top();

        case GL_TEXTURE: return texture.top();

        default:
        {
            static XMFLOAT4X4 identity;
            XMStoreFloat4x4(&identity, XMMatrixIdentity());
            return identity;
        }
    }
}

void glMatrixStack::mul_set(const UINT32 mode, const XMMATRIX& r)
{
    std::stack<XMFLOAT4X4>* stack;
    switch (mode)
    {
        case GL_MODELVIEW: stack = &model_view; break;
        case GL_PROJECTION: stack = &projection; break;
        case GL_TEXTURE: stack = &texture; break;
        default: return;
    }

    if (!stack || stack->empty())
    {
        return;
    }

    const auto M = XMLoadFloat4x4(&stack->top());

    const auto out = XMMatrixMultiply(r, M);

    XMStoreFloat4x4(&stack->top(), out);
}

void glMatrixStack::mul_set(const UINT32 mode, const float* m)
{
    std::stack<XMFLOAT4X4>* stack;
    switch (mode)
    {
        case GL_MODELVIEW: stack = &model_view; break;
        case GL_PROJECTION: stack = &projection; break;
        case GL_TEXTURE: stack = &texture; break;
        default: return;
    }

    if (!stack || stack->empty())
    {
        return;
    }

    const auto M = XMLoadFloat4x4(&stack->top());

    XMFLOAT4X4 glMat;
    memcpy(&glMat, m, sizeof(float) * 16);
    XMMATRIX dxMat = XMLoadFloat4x4(&glMat);

    const auto out = XMMatrixMultiply(dxMat, M);

    XMStoreFloat4x4(&stack->top(), out);
}

// operations

void glMatrixStack::rotate(const UINT32 mode, const float angle, const float x, const float y,
                           const float z)
{
    const float len = std::sqrt(x * x + y * y + z * z);
    const float inv_len = 1.0f / len;

    const auto axis = XMVectorSet(x * inv_len, y * inv_len, z * inv_len, 0.0f);
    const float radians = XMConvertToRadians(angle);

    const auto r = XMMatrixRotationAxis(axis, radians);

    mul_set(mode, r);
}

void glMatrixStack::translate(const UINT32 mode, const float x, const float y, const float z)
{
    const auto t = XMMatrixTranslation(x, y, z);

    mul_set(mode, t);
}

void glMatrixStack::scale(const UINT32 mode, const float x, const float y, const float z)
{
    const auto s = XMMatrixScaling(x, y, z);
    mul_set(mode, s);
}

void glMatrixStack::ortho(const UINT32 mode, const double l, const double r, const double b,
                          const double t, const double n, const double f)
{
    const auto L = static_cast<float>(l);
    const auto R = static_cast<float>(r);
    const auto B = static_cast<float>(b);
    const auto T = static_cast<float>(t);
    const auto N = static_cast<float>(n);
    const auto F = static_cast<float>(f);

    XMFLOAT4X4 m = { 2.0f / (R - L),
                     0.0f,
                     0.0f,
                     0.0f,
                     0.0f,
                     2.0f / (T - B),
                     0.0f,
                     0.0f,
                     0.0f,
                     0.0f,
                     -2.0f / (F - N),
                     0.0f,
                     -(R + L) / (R - L),
                     -(T + B) / (T - B),
                     -(F + N) / (F - N),
                     1.0f };

    XMMATRIX mat = XMLoadFloat4x4(&m);
    mat = XMMatrixTranspose(mat);

    mul_set(mode, mat);
}

void glMatrixStack::frustum(const UINT32 mode, const double l, const double r, const double b,
                            const double t, const double n, const double f)
{
    const auto L = static_cast<float>(l);
    const auto R = static_cast<float>(r);
    const auto B = static_cast<float>(b);
    const auto T = static_cast<float>(t);
    const auto N = static_cast<float>(n);
    const auto F = static_cast<float>(f);

    const auto p = XMMatrixPerspectiveOffCenterRH(L, R, B, T, N, F);

    mul_set(mode, p);
}

void glMatrixStack::perspective(const UINT32 mode, const double fov_y, const double aspect,
                                const double n, const double f)
{
    const auto fov = static_cast<float>(fov_y);
    const auto asp = static_cast<float>(aspect);
    const auto N = static_cast<float>(n);
    const auto F = static_cast<float>(f);

    const auto p = XMMatrixPerspectiveFovRH(fov, asp, N, F);

    mul_set(mode, p);
}

void glMatrixStack::load(const UINT32 mode, const float* m)
{
    std::stack<XMFLOAT4X4>* stack;
    switch (mode)
    {
        case GL_MODELVIEW: stack = &model_view; break;
        case GL_PROJECTION: stack = &projection; break;
        case GL_TEXTURE: stack = &texture; break;
        default: return;
    }

    if (!stack || stack->empty())
    {
        return;
    }

    stack->top() = { m[0], m[4], m[8],  m[12], m[1], m[5], m[9],  m[13],
                     m[2], m[6], m[10], m[14], m[3], m[7], m[11], m[15] };
}

void glMatrixStack::print_stacks() const
{
    auto print_matrix = [](const XMFLOAT4X4& m, const char* label, const int level)
    {
        std::printf("[%s stack level %d]\n", label, level);
        std::printf("  %.6f  %.6f  %.6f  %.6f\n"
                    "  %.6f  %.6f  %.6f  %.6f\n"
                    "  %.6f  %.6f  %.6f  %.6f\n"
                    "  %.6f  %.6f  %.6f  %.6f\n",
                    m._11, m._12, m._13, m._14, m._21, m._22, m._23, m._24, m._31, m._32, m._33,
                    m._34, m._41, m._42, m._43, m._44);
    };

    auto dump_stack = [&](const std::stack<XMFLOAT4X4>& s, const char* name)
    {
        if (s.empty())
        {
            std::printf("[%s stack] (empty)\n", name);
            return;
        }

        // Copy to preserve original
        std::stack<XMFLOAT4X4> tmp = s;

        // Collect to vector to print bottom → top
        std::vector<XMFLOAT4X4> mats;
        mats.reserve(tmp.size());
        while (!tmp.empty())
        {
            mats.push_back(tmp.top());
            tmp.pop();
        }

        // mats[0] is original top; reverse for bottom-first
        std::reverse(mats.begin(), mats.end());

        std::printf("=== %s stack (%zu matrices, bottom to top) ===\n", name, mats.size());
        for (size_t i = 0; i < mats.size(); ++i)
        {
            print_matrix(mats[i], name, static_cast<int>(i));
        }
    };

    std::printf("\n===== glMatrixStack dump =====\n");
    dump_stack(model_view, "MODELVIEW");
    dump_stack(projection, "PROJECTION");
    dump_stack(texture, "TEXTURE");
    std::printf("===== end dump =====\n\n");
}
