#pragma once

#include <d3d12.h>
#include <vector>
#include <array>
#include <optional>

namespace glRemix::dx
{
using InputLayoutDesc = std::vector<D3D12_INPUT_ELEMENT_DESC>;

struct RenderTargetDesc
{
    UINT num_render_targets = 1;
    std::array<DXGI_FORMAT, 8> rtv_formats;
    DXGI_FORMAT dsv_format = DXGI_FORMAT_UNKNOWN;
};

struct GraphicsPipelineDesc
{
    ID3D12RootSignature* root_signature = nullptr;
    InputLayoutDesc input_layout{};
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    RenderTargetDesc render_targets{};

    std::optional<D3D12_RASTERIZER_DESC> rasterizer_state;
    std::optional<D3D12_BLEND_DESC> blend_state;
    std::optional<D3D12_DEPTH_STENCIL_DESC> depth_stencil_state;

    UINT sample_count = 1;
    UINT sample_quality = 0;
    bool increment_input_slot
        = false;  // For shader reflection: if true, increment slot for each input element
};

enum ExportType : UINT
{
    RAY_GEN = 0,
    MISS = 1,
    CLOSEST_HIT = 2,
    ANY_HIT = 3,
    INTERSECTION = 4,
    SHADOW_MISS = 5,
    SHADOW_CLOSEST_HIT = 6,
};

struct RayTracingPipelineDesc
{
    ID3D12RootSignature* global_root_signature = nullptr;
    ID3D12RootSignature* local_root_signature = nullptr;

    static constexpr UINT MAX_EXPORTS = 7;

    // Order matters, it follows the above enum
    std::array<const wchar_t*, MAX_EXPORTS> export_names{};
    UINT num_exports = 0;

    // Also follows the order of the enum
    std::array<ID3D12RootSignature*, MAX_EXPORTS> local_root_signatures{};

    UINT max_recursion_depth = 1;
    UINT payload_size = 0;
    UINT attribute_size = 0;
};

// Helper function to create a RayTracingPipelineDesc with proper ordering
inline RayTracingPipelineDesc make_ray_tracing_pipeline_desc(
    const wchar_t* ray_gen_shader, const wchar_t* miss_shader, const wchar_t* closest_hit_shader,
    const wchar_t* any_hit_shader = nullptr, const wchar_t* intersection_shader = nullptr,
    const wchar_t* shadow_miss_shader = nullptr, const wchar_t* shadow_closest_hit_shader = nullptr)
{
    RayTracingPipelineDesc desc{};
    desc.export_names[static_cast<UINT>(ExportType::RAY_GEN)] = ray_gen_shader;
    desc.export_names[static_cast<UINT>(ExportType::MISS)] = miss_shader;
    desc.export_names[static_cast<UINT>(ExportType::CLOSEST_HIT)] = closest_hit_shader;
    desc.export_names[static_cast<UINT>(ExportType::ANY_HIT)] = any_hit_shader;
    desc.export_names[static_cast<UINT>(ExportType::INTERSECTION)] = intersection_shader;
    desc.export_names[static_cast<UINT>(ExportType::SHADOW_MISS)] = shadow_miss_shader;
    desc.export_names[static_cast<UINT>(ExportType::SHADOW_CLOSEST_HIT)] = shadow_closest_hit_shader;

    // Count non-null exports
    UINT count = 0;
    if (ray_gen_shader)
    {
        count++;
    }
    if (miss_shader)
    {
        count++;
    }
    if (closest_hit_shader)
    {
        count++;
    }
    if (any_hit_shader)
    {
        count++;
    }
    if (intersection_shader)
    {
        count++;
    }
    if (shadow_miss_shader)
    {
        count++;
    }
    if (shadow_closest_hit_shader)
    {
        count++;
    }
    desc.num_exports = count;

    return desc;
}
}  // namespace glRemix::dx
