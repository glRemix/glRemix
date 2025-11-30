#include "shared_structs.h"

struct VSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

RayGenConstantBuffer g_rayGenCB : register(b0);

PSInput VSMain(VSInput input)
{
    PSInput output;

    float4 p = float4(input.Position, 1.0f);
    output.Position = mul(p, g_rayGenCB.current.view_proj);
    output.Color = input.Color;

    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.Color;
}
