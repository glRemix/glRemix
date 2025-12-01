#pragma once
#include <DirectXMath.h>
#include <vector>
#include "dx/d3d12_buffer.h"
#include "dx/d3d12_descriptor.h"
#include "dx/d3d12_texture.h"

using namespace DirectX;

namespace glRemix
{
#include "shared_structs.h"

struct MeshRecord
{
    UINT32 mesh_id;

    UINT32 blas_vb_ib_idx;
    UINT32 mv_idx;  // index into model view array
    UINT32 mat_idx;
    UINT32 tex_idx;

    // bounding box info
    XMFLOAT3 min_bb;
    XMFLOAT3 max_bb;

    // For garbage collection, last frame this mesh record was accessed
    UINT32 last_frame;

    bool visible = true;  // can be toggled in the imgui
};

struct BufferAndDescriptor
{
    dx::D3D12Buffer buffer;
    dx::D3D12Descriptor descriptor;
    UINT page_index = -1;
};

struct TextureAndDescriptor
{
    dx::D3D12Texture texture;
    dx::D3D12Descriptor descriptor;
    UINT page_index = -1;
};

struct MeshResources
{
    dx::D3D12Buffer blas;
    BufferAndDescriptor vertex_buffer;
    BufferAndDescriptor index_buffer;
};

struct PendingGeometry
{
    std::vector<Vertex> vertices;
    std::vector<UINT32> indices;
    UINT64 hash;
    UINT32 mat_idx;
    UINT32 mv_idx;
    UINT32 replace_idx = -1;
};

struct PendingTexture
{
    UINT32 index;
    dx::TextureDesc desc;
    const void* pixels;
};

}  // namespace glRemix
