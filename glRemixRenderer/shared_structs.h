// Structs shared between C++ and HLSL shaders

#pragma once

#include "hlsl_compat.h"

struct RayGenConstantBuffer
{
    XMFLOAT4X4 view_proj;
    XMFLOAT4X4 inv_view_proj;
    float width;
    float height;
};

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT4 color;
    XMFLOAT3 normal;
    XMFLOAT2 uv;
};

struct Light
{
    XMFLOAT4 ambient;
    XMFLOAT4 diffuse;
    XMFLOAT4 specular;

    XMFLOAT4 position;

    XMFLOAT3 spot_direction;
    float spot_exponent;
    float spot_cutoff;

    float constant_attenuation;
    float linear_attenuation;
    float quadratic_attenuation;

    BOOL enabled;
#if __cplusplus

private:
#endif
    XMUINT3 padding;

#if __cplusplus

public:
    Light()
        : ambient(0, 0, 0, 1)
        , diffuse(1, 1, 1, 1)
        , specular(0, 0, 0, 1)
        , position(0, 0, 1, 1)  // w position = 0 means directional light, otherwise point light
        , spot_direction(0, 0, -1)
        , spot_exponent(0)
        , spot_cutoff(180.0f)
        , constant_attenuation(1.0f)
        , linear_attenuation(0.0f)
        , quadratic_attenuation(0.0f)
        , enabled(FALSE)
    {
    }
#endif
};

struct Material
{
    XMFLOAT4 ambient;
    XMFLOAT4 diffuse;
    XMFLOAT4 specular;
    XMFLOAT4 emission;

    float shininess;
};

struct GPUMeshRecord
{
    UINT32 vb_idx;
    UINT32 ib_idx;
    UINT32 mat_buffer_idx;
    UINT32 mat_idx;
    UINT32 tex_idx;
};

struct RayPayload
{
    XMFLOAT4 color;
    XMFLOAT3 normal;
    BOOL hit;
    XMFLOAT3 hit_pos;

    XMFLOAT3 ray_origin;
    XMFLOAT3 ray_dir;
    XMFLOAT3 rx_origin;
    XMFLOAT3 rx_dir;
    XMFLOAT3 ry_origin;
    XMFLOAT3 ry_dir;

    UINT32 depth;
};
