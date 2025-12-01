#pragma once

#include <filesystem>
#include <vector>
#include <memory>

#include <DirectXMath.h>
#include "dx/d3d12_as.h"
#include "structs.h"

namespace glRemix
{
class MeshLoader
{
    std::vector<std::unique_ptr<uint8_t[]>> m_owned_texture_buffers;

public:
    bool load_mesh_from_path(std::filesystem::path asset_path, std::vector<Vertex>& out_vertices,
                             std::vector<UINT32>& out_indices, PendingTexture& out_texture,
                             XMFLOAT3& out_min_bb, XMFLOAT3& out_max_bb);
};
}

// namespace glRemix
