#include "mesh_loader.h"
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <DirectXMath.h>
#include "structs.h"
#include "shared/arena.h"

#include <filesystem>
#include <vector>

bool glRemix::load_mesh_from_path(std::filesystem::path asset_path,
                                  std::vector<Vertex>& out_vertices,
                                  std::vector<UINT32>& out_indices,
                                  std::vector<Material>& out_materials, XMFLOAT3& out_min_bb,
                                  XMFLOAT3& out_max_bb)
{
    fastgltf::Parser parser;

    auto data = fastgltf::GltfDataBuffer::FromPath(asset_path);
    if (data.error() != fastgltf::Error::None)
    {
        return false;
    }

    auto asset = parser.loadGltf(data.get(), asset_path.parent_path(),
                                 fastgltf::Options::LoadExternalBuffers);
    if (auto error = asset.error(); error != fastgltf::Error::None)
    {
        return false;
    }

    // fastgltf::validate(asset.get());

    // get first mesh only for now
    auto& mesh = asset->meshes[0];

    // loop through primitives and put all data into a single MeshRecord mesh
    for (auto& primitive : mesh.primitives)
    {
        // get indices
        const auto& index_acc = asset->accessors[primitive.indicesAccessor.value()];
        if (!index_acc.bufferViewIndex.has_value())
        {
            return false;
        }

        size_t index_offset = out_indices.size();
        out_indices.resize(index_offset + index_acc.count);
        if (index_acc.componentType == fastgltf::ComponentType::UnsignedByte
            || index_acc.componentType == fastgltf::ComponentType::UnsignedShort)
        {
            // handle u16 values
            auto temp = get_arena().alloc_array<uint16_t>(index_acc.count);
            if (!temp)
            {
                return false;
            }
            fastgltf::copyFromAccessor<uint16_t>(asset.get(), index_acc, temp);

            for (size_t i = 0; i < index_acc.count; i++)
            {
                out_indices[index_offset + i] = static_cast<uint32_t>(temp[i]);
            }
        }
        else
        {
            // u32 values
            fastgltf::copyFromAccessor<uint32_t>(asset.get(), index_acc, &out_indices[index_offset]);
        }

        // get vertices
        const auto& position_acc
            = asset->accessors[primitive.findAttribute("POSITION")->accessorIndex];
        size_t vertex_offset = out_vertices.size();
        out_vertices.resize(vertex_offset + position_acc.count);
        for (int i = 0; i < position_acc.count; ++i)
        {
            // initialize vertex data
            Vertex& vertex = out_vertices[vertex_offset + i];
            out_vertices[vertex_offset + i].position = { 0, 0, 0 };
            out_vertices[vertex_offset + i].color = { 1, 1, 1, 1 };
            out_vertices[vertex_offset + i].normal = { 0, 1, 0 };
        }
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset.get(), position_acc, [&](fastgltf::math::fvec3 p, size_t idx)
            { out_vertices[vertex_offset + idx].position = { p.x(), p.y(), p.z() }; });

        // get normals if they exist
        const auto* normal_it = primitive.findAttribute("NORMAL");
        if (normal_it != primitive.attributes.end())
        {
            const auto& normal_acc = asset->accessors[normal_it->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                asset.get(), normal_acc, [&](fastgltf::math::fvec3 n, size_t idx)
                { out_vertices[vertex_offset + idx].normal = { n.x(), n.y(), n.z() }; });
        }
    }

    // get info for bounding box
    for (const auto& vertex : out_vertices)
    {
        XMVECTOR p = XMLoadFloat3(&vertex.position);
        XMVECTOR minv = XMLoadFloat3(&out_min_bb);
        XMVECTOR maxv = XMLoadFloat3(&out_max_bb);

        minv = XMVectorMin(minv, p);
        maxv = XMVectorMax(maxv, p);

        XMStoreFloat3(&out_min_bb, minv);
        XMStoreFloat3(&out_max_bb, maxv);

        /*out_min_bb = XMMin(out_min_bb, vertex.position);
        out_max_bb = XMMax(out_max_bb, vertex.position);*/
    }

    return true;
}
