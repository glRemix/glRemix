#include "d3d12_barrier.h"

#include <array>
#include <cassert>
#include <iomanip>
#include <stdexcept>

#include "shared/arena.h"

namespace glRemix::dx
{
namespace
{
struct UseDefinition
{
    D3D12_BARRIER_ACCESS access;
    D3D12_BARRIER_SYNC sync;
    D3D12_BARRIER_LAYOUT layout;
};

// Collapse barriers into a smaller subset of possible combinations
// Kind of like https://github.com/Tobski/simple_vulkan_synchronization
// These need to be in the correct order as the enum
constexpr std::array use_definitions = {
    // SRV uses
    UseDefinition{ .access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                   .sync = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                   .layout = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE },
    UseDefinition{ .access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                   .sync = D3D12_BARRIER_SYNC_NON_PIXEL_SHADING,
                   .layout = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE },
    UseDefinition{ .access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                   .sync = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                   .layout = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE },
    UseDefinition{ .access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                   .sync = D3D12_BARRIER_SYNC_RAYTRACING,
                   .layout = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE },

    UseDefinition{ .access = D3D12_BARRIER_ACCESS_CONSTANT_BUFFER,
                   .sync = D3D12_BARRIER_SYNC_ALL_SHADING,
                   .layout = D3D12_BARRIER_LAYOUT_GENERIC_READ },

    // UAV uses
    UseDefinition{ .access = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                   .sync = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                   .layout = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS },
    UseDefinition{ .access = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                   .sync = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                   .layout = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS },
    UseDefinition{ .access = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                   .sync = D3D12_BARRIER_SYNC_RAYTRACING,
                   .layout = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS },

    // Render target
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .sync = D3D12_BARRIER_SYNC_RENDER_TARGET,
        .layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
    },

    // Depth stencil
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ,
        .sync = D3D12_BARRIER_SYNC_DEPTH_STENCIL,
        .layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
    },
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
        .sync = D3D12_BARRIER_SYNC_DEPTH_STENCIL,
        .layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
    },

    // Copy
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_COPY_SOURCE,
        .sync = D3D12_BARRIER_SYNC_COPY,
        .layout = D3D12_BARRIER_LAYOUT_COPY_SOURCE,
    },
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_COPY_DEST,
        .sync = D3D12_BARRIER_SYNC_COPY,
        .layout = D3D12_BARRIER_LAYOUT_COPY_DEST,
    },

    // Resolve
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_RESOLVE_SOURCE,
        .sync = D3D12_BARRIER_SYNC_RESOLVE,
        .layout = D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE,
    },
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_RESOLVE_DEST,
        .sync = D3D12_BARRIER_SYNC_RESOLVE,
        .layout = D3D12_BARRIER_LAYOUT_RESOLVE_DEST,
    },

    // PRESENT
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_COMMON,
        .sync = D3D12_BARRIER_SYNC_NONE,
        .layout = D3D12_BARRIER_LAYOUT_PRESENT,
    },

    // Raytracing shader table (buffer read by raytracing pipeline)
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
        .sync = D3D12_BARRIER_SYNC_RAYTRACING,
        .layout = D3D12_BARRIER_LAYOUT_COMMON,
    },

    // Acceleration structure
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ,
        .sync = D3D12_BARRIER_SYNC_RAYTRACING,
        .layout = D3D12_BARRIER_LAYOUT_COMMON,
    },
    UseDefinition{
        .access = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE,
        .sync = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
        .layout = D3D12_BARRIER_LAYOUT_COMMON,
    },
};

const UseDefinition& lookup_use_definition(Usage use_kind)
{
    const size_t index = static_cast<size_t>(use_kind);
    assert(index < use_definitions.size());
    return use_definitions[index];
}

constexpr const UseDefinition* lookup_definition_by_layout(D3D12_BARRIER_LAYOUT layout)
{
    // This is a small loop so should be ok, and constexpr allows for optimizations
    for (const auto& def : use_definitions)
    {
        if (def.layout == layout)
        {
            return &def;
        }
    }
    return nullptr;
}

bool are_layouts_compatible(D3D12_BARRIER_LAYOUT layout_a, D3D12_BARRIER_LAYOUT layout_b)
{
    if (layout_a == layout_b)
    {
        return true;
    }

    // You can transition anything to or from UNDEFINED
    if (layout_a == D3D12_BARRIER_LAYOUT_UNDEFINED || layout_b == D3D12_BARRIER_LAYOUT_UNDEFINED)
    {
        return true;
    }

    // COMMON and GENERIC_READ are compatible with each other
    const bool a_is_generic = layout_a == D3D12_BARRIER_LAYOUT_COMMON
                              || layout_a == D3D12_BARRIER_LAYOUT_GENERIC_READ;
    const bool b_is_generic = layout_b == D3D12_BARRIER_LAYOUT_COMMON
                              || layout_b == D3D12_BARRIER_LAYOUT_GENERIC_READ;
    if (a_is_generic && b_is_generic)
    {
        return true;
    }

    const bool a_is_read = layout_a == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE
                           || layout_a == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
    const bool b_is_read = layout_b == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE
                           || layout_b == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;

    // SHADER_RESOURCE and DEPTH_STENCIL_READ is ok
    if (a_is_read && b_is_read)
    {
        return true;
    }

    // Everything else is incompatible, you can't have READ/WRITE, SOURCE/DEST together at the same
    // time for example
    return false;
}

bool choose_layout(D3D12_BARRIER_LAYOUT current_layout, D3D12_BARRIER_LAYOUT candidate,
                   D3D12_BARRIER_LAYOUT* out)
{
    assert(out);
    if (candidate == D3D12_BARRIER_LAYOUT_UNDEFINED)
    {
        *out = current_layout;
        return true;
    }

    if (!are_layouts_compatible(current_layout, candidate))
    {
        return false;
    }
    *out = candidate;
    return true;
}

}  // namespace

void initialize_tracked_resource(Resource* resource, ID3D12Resource* d3d_resource,
                                 const bool is_texture, const D3D12_BARRIER_LAYOUT initial_layout,
                                 const D3D12_BARRIER_SUBRESOURCE_RANGE* subresources,
                                 const UINT64 explicit_size_in_bytes)
{
    assert(resource);
    resource->resource = d3d_resource;
    resource->is_texture = is_texture;
    resource->tracked_layout = initial_layout;
    resource->next_layout = initial_layout;

    if (const auto* def = lookup_definition_by_layout(initial_layout))
    {
        resource->tracked_access = def->access;
        resource->tracked_sync = def->sync;
        resource->tracked_valid = true;
    }

    if (is_texture)
    {
        // Initialize texture-specific data in the union
        if (subresources)
        {
            resource->subresource_range = *subresources;
        }
        else
        {
            D3D12_BARRIER_SUBRESOURCE_RANGE range{
                .IndexOrFirstMipLevel = 0,
                .NumMipLevels = 1,
                .FirstArraySlice = 0,
                .NumArraySlices = 1,
                .FirstPlane = 0,
                .NumPlanes = 1,
            };

            if (resource->resource)
            {
                const D3D12_RESOURCE_DESC desc = resource->resource->GetDesc();
                range.NumMipLevels = desc.MipLevels;
                range.NumArraySlices = desc.DepthOrArraySize;
            }

            resource->subresource_range = range;
        }
    }
    else
    {
        // Initialize buffer-specific data in the union
        resource->size_in_bytes = explicit_size_in_bytes;

        if (resource->resource && !resource->size_in_bytes)
        {
            const D3D12_RESOURCE_DESC desc = resource->resource->GetDesc();
            if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                resource->size_in_bytes = desc.Width;
            }
        }
    }
}

bool mark_use(Resource& resource, const Usage usage)
{
    const UseDefinition& definition = lookup_use_definition(usage);

    if (!resource.touched)
    {
        resource.next_access = definition.access;
        resource.next_sync = definition.sync;
        if (resource.is_texture)
        {
            resource.next_layout = definition.layout;
        }
        resource.touched = true;
    }
    else
    {
        resource.next_access |= definition.access;
        resource.next_sync |= definition.sync;
        if (resource.is_texture)
        {
            D3D12_BARRIER_LAYOUT layout;
            if (!choose_layout(resource.next_layout, definition.layout, &layout))
            {
                return false;
            }
            resource.next_layout = layout;
        }
    }
    return true;
}

void emit_barriers(ID3D12GraphicsCommandList7* command_list, Resource* const* resources,
                   const size_t resource_count)
{
    assert(command_list);
    if (resource_count == 0)
    {
        return;
    }

    auto& arena = get_arena();
    const auto texture_barriers = arena.alloc_array<D3D12_TEXTURE_BARRIER>(resource_count);
    const auto buffer_barriers = arena.alloc_array<D3D12_BUFFER_BARRIER>(resource_count);
    if (!texture_barriers || !buffer_barriers)
    {
        throw std::runtime_error("Arena exhausted");
    }
    size_t texture_barrier_count = 0;
    size_t buffer_barrier_count = 0;

    std::array<D3D12_BARRIER_GROUP, 2> barrier_groups;
    size_t barrier_group_count = 0;

    for (size_t i = 0; i < resource_count; ++i)
    {
        Resource* resource_ptr = resources[i];
        if (!resource_ptr || !resource_ptr->touched)
        {
            continue;
        }

        Resource& resource = *resource_ptr;

        const D3D12_BARRIER_ACCESS access_before = resource.tracked_valid
                                                       ? resource.tracked_access
                                                       : D3D12_BARRIER_ACCESS_NO_ACCESS;
        const D3D12_BARRIER_SYNC sync_before = resource.tracked_valid ? resource.tracked_sync
                                                                      : D3D12_BARRIER_SYNC_NONE;
        const D3D12_BARRIER_LAYOUT layout_before = resource.tracked_valid ? resource.tracked_layout
                                                   : resource.is_texture
                                                       ? D3D12_BARRIER_LAYOUT_UNDEFINED
                                                       : D3D12_BARRIER_LAYOUT_COMMON;

        if (resource.is_texture)
        {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = sync_before,
                .SyncAfter = resource.next_sync,
                .AccessBefore = access_before,
                .AccessAfter = resource.next_access,
                .LayoutBefore = layout_before,
                .LayoutAfter = resource.next_layout,
                .pResource = resource.resource,
                .Subresources = resource.subresource_range,
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
            };

            const bool requires_barrier = !resource.tracked_valid
                                          || barrier.SyncBefore != barrier.SyncAfter
                                          || barrier.AccessBefore != barrier.AccessAfter
                                          || barrier.LayoutBefore != barrier.LayoutAfter;

            if (requires_barrier)
            {
                texture_barriers[texture_barrier_count++] = barrier;
            }
        }
        else
        {
            D3D12_BUFFER_BARRIER barrier{
                .SyncBefore = sync_before,
                .SyncAfter = resource.next_sync,
                .AccessBefore = access_before,
                .AccessAfter = resource.next_access,
                .pResource = resource.resource,
                .Offset = 0,
                .Size = resource.size_in_bytes,
            };

            if (!barrier.Size && barrier.pResource)
            {
                barrier.Size = barrier.pResource->GetDesc().Width;
            }

            const bool requires_barrier = !resource.tracked_valid
                                          || barrier.SyncBefore != barrier.SyncAfter
                                          || barrier.AccessBefore != barrier.AccessAfter;

            if (requires_barrier)
            {
                buffer_barriers[buffer_barrier_count++] = barrier;
            }
        }

        // Update tracked state
        resource.tracked_access = resource.next_access;
        resource.tracked_sync = resource.next_sync;
        if (resource.is_texture)
        {
            resource.tracked_layout = resource.next_layout;
        }
        resource.tracked_valid = true;

        // Reset for next usage
        resource.touched = false;
        resource.next_access = D3D12_BARRIER_ACCESS_NO_ACCESS;
        resource.next_sync = D3D12_BARRIER_SYNC_NONE;
        if (resource.is_texture)
        {
            resource.next_layout = resource.tracked_layout;
        }
    }

    if (texture_barrier_count > 0)
    {
        barrier_groups[barrier_group_count++] = {
            .Type = D3D12_BARRIER_TYPE_TEXTURE,
            .NumBarriers = static_cast<UINT>(texture_barrier_count),
            .pTextureBarriers = texture_barriers,
        };
    }

    if (buffer_barrier_count > 0)
    {
        barrier_groups[barrier_group_count++] = {
            .Type = D3D12_BARRIER_TYPE_BUFFER,
            .NumBarriers = static_cast<UINT>(buffer_barrier_count),
            .pBufferBarriers = buffer_barriers,
        };
    }

    if (barrier_group_count > 0)
    {
        command_list->Barrier(static_cast<UINT>(barrier_group_count), barrier_groups.data());
    }
}
}  // namespace glRemix::dx
