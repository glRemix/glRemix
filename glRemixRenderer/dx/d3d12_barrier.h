#pragma once

#include <cstdint>
#include <vector>
#include <d3d12.h>

namespace glRemix::dx
{
struct Resource
{
    ID3D12Resource* resource = nullptr;

    union
    {
        D3D12_BARRIER_SUBRESOURCE_RANGE subresource_range;  // Textures
        UINT64 size_in_bytes;                               // Buffers
    };

    D3D12_BARRIER_ACCESS tracked_access = D3D12_BARRIER_ACCESS_NO_ACCESS;
    D3D12_BARRIER_ACCESS next_access = D3D12_BARRIER_ACCESS_NO_ACCESS;
    D3D12_BARRIER_SYNC tracked_sync = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_SYNC next_sync = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_LAYOUT tracked_layout = D3D12_BARRIER_LAYOUT_COMMON;
    D3D12_BARRIER_LAYOUT next_layout = D3D12_BARRIER_LAYOUT_UNDEFINED;
    bool is_texture = false;
    bool tracked_valid = false;
    bool touched = false;
};

enum class Usage : UINT32
{
    SRV_PIXEL,
    SRV_NON_PIXEL,
    SRV_COMPUTE,
    SRV_RT,
    CBV,
    UAV_PIXEL,
    UAV_COMPUTE,
    UAV_RT,
    RTV,
    DSV_READ,
    DSV_WRITE,
    COPY_SRC,
    COPY_DST,
    RESOLVE_SRC,
    RESOLVE_DST,
    PRESENT,
    SHADER_TABLE,
    AS_READ,
    AS_WRITE,
};

struct BarrierLogEvent
{
    ID3D12Resource* resource = nullptr;
    D3D12_BARRIER_TYPE barrier_type = D3D12_BARRIER_TYPE_TEXTURE;
    D3D12_BARRIER_ACCESS access_before = D3D12_BARRIER_ACCESS_NO_ACCESS;
    D3D12_BARRIER_ACCESS access_after = D3D12_BARRIER_ACCESS_NO_ACCESS;
    D3D12_BARRIER_SYNC sync_before = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_SYNC sync_after = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_LAYOUT layout_before = D3D12_BARRIER_LAYOUT_COMMON;
    D3D12_BARRIER_LAYOUT layout_after = D3D12_BARRIER_LAYOUT_COMMON;
};

// Primitive barrier batching/tracking system.
// You basically pick from some presets to build a barrier in place, then when you call
// emit_barriers the buffer/texture barriers are batched together in one dispatch.

void initialize_tracked_resource(Resource* resource, ID3D12Resource* d3d_resource, bool is_texture,
                                 D3D12_BARRIER_LAYOUT initial_layout = D3D12_BARRIER_LAYOUT_COMMON,
                                 const D3D12_BARRIER_SUBRESOURCE_RANGE* subresources = nullptr,
                                 UINT64 explicit_size_in_bytes = 0);

bool mark_use(Resource& resource, Usage usage);

void emit_barriers(ID3D12GraphicsCommandList7* command_list, Resource* const* resources,
                   size_t resource_count);
}  // namespace glRemix::dx
