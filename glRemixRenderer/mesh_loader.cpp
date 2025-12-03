#include "mesh_loader.h"
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <DirectXMath.h>
#include <DirectXTex.h>
#include "dx/d3d12_as.h"
#include "structs.h"

#include <filesystem>
#include <vector>
#include <cstring>
#include <memory>

bool glRemix::MeshLoader::load_mesh_from_path(std::filesystem::path asset_path,
                                              std::vector<Vertex>& out_vertices,
                                              std::vector<UINT32>& out_indices,
                                              PendingTexture& out_texture, XMFLOAT3& out_min_bb,
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

    // get textures
    const auto& img = asset->images;
    for (size_t i = 0; i < 1; ++i)  // just get base color texture for now
    {
        const auto& img = asset->images[i];

        // load image using DirectXTex
        ScratchImage scratch_image;
        TexMetadata metadata{};

        if (auto* uri = std::get_if<fastgltf::sources::URI>(&img.data))
        {
            std::filesystem::path uri_path = uri->uri.fspath();
            std::filesystem::path path = asset_path.parent_path() / uri_path;

            HRESULT hr = LoadFromWICFile(path.c_str(), WIC_FLAGS_FORCE_RGB, &metadata,
                                         scratch_image);

            if (FAILED(hr))
            {
                printf("Failed to load texture file: %s\n", path.string().c_str());
                return false;
            }
        }

        // convert to correct format
        ScratchImage converted;
        const DXGI_FORMAT target_format = DXGI_FORMAT_R8G8B8A8_UNORM;

        if (metadata.format != target_format)
        {
            HRESULT hr = Convert(*scratch_image.GetImage(0, 0, 0), target_format,
                                 TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);

            if (FAILED(hr))
            {
                printf("Failed to convert GLTF texture to RGBA8\n");
                return false;
            }

            scratch_image = std::move(converted);
        }

        const Image* img_data = scratch_image.GetImage(0, 0, 0);
        const size_t byte_size = img_data->slicePitch;
        auto pixel_buffer = std::make_unique<uint8_t[]>(byte_size);
        memcpy(pixel_buffer.get(), img_data->pixels, byte_size);

        out_texture.index = static_cast<UINT32>(i);
        out_texture.desc = {
            static_cast<UINT32>(img_data->width), static_cast<UINT32>(img_data->height), 1,    1,
            DXGI_FORMAT_R8G8B8A8_UNORM,           D3D12_RESOURCE_DIMENSION_TEXTURE2D,    false
        };
        out_texture.pixels.assign(pixel_buffer.get(), pixel_buffer.get() + byte_size);

        m_owned_texture_buffers.emplace_back(std::move(pixel_buffer));
    }

    // get first mesh only for now
    auto& mesh = asset->meshes[0];

    // loop through primitives and put all data into a single MeshRecord mesh
    for (auto& primitive : mesh.primitives)
    {
        size_t vertex_offset = out_vertices.size();
        const auto& position_acc
            = asset->accessors[primitive.findAttribute("POSITION")->accessorIndex];
        const size_t primitive_vertex_count = position_acc.count;
        

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
            std::vector<uint16_t> temp(index_acc.count);
            fastgltf::copyFromAccessor<uint16_t>(asset.get(), index_acc, temp.data());

            for (size_t i = 0; i < index_acc.count; i++)
            {
                const uint32_t original_idx = static_cast<uint32_t>(temp[i]);
                // check if index is in bounds for this primitive
                if (original_idx >= primitive_vertex_count)
                {
                    printf("index %u out of bounds (max: %zu) in primitive\n", original_idx, primitive_vertex_count);
                    return false;
                }
                out_indices[index_offset + i] = original_idx + static_cast<uint32_t>(vertex_offset);
            }
        }
        else
        {
            // handle u32 values
            std::vector<uint32_t> temp(index_acc.count);
            fastgltf::copyFromAccessor<uint32_t>(asset.get(), index_acc, temp.data());
            
            for (size_t i = 0; i < index_acc.count; i++)
            {
                const uint32_t original_idx = temp[i];
                // check if index is in bounds for this primitive
                if (original_idx >= primitive_vertex_count)
                {
                    printf("index %u out of bounds (max: %zu) in primitive\n", original_idx, primitive_vertex_count);
                    return false;
                }
                out_indices[index_offset + i] = original_idx + static_cast<uint32_t>(vertex_offset);
            }
        }


        // get vertices
        out_vertices.resize(vertex_offset + position_acc.count);
        for (int i = 0; i < position_acc.count; ++i)
        {
            // initialize vertex data
            Vertex& vertex = out_vertices[vertex_offset + i];
            out_vertices[vertex_offset + i].position = { 0, 0, 0 };
            out_vertices[vertex_offset + i].color = { 1, 1, 1, 1 };
            out_vertices[vertex_offset + i].normal = { 0, 1, 0 };
            out_vertices[vertex_offset + i].uv = { 0, 0 };
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


        // get uvs if they exist
        const auto* texcoord_it = primitive.findAttribute("TEXCOORD_0");
        if (texcoord_it == primitive.attributes.end())
        {
            texcoord_it = primitive.findAttribute("TEXCOORD");
        }
        if (texcoord_it != primitive.attributes.end())
        {
            const auto& texcoord_acc = asset->accessors[texcoord_it->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                asset.get(), texcoord_acc, [&](fastgltf::math::fvec2 uv, size_t idx)
                { out_vertices[vertex_offset + idx].uv = { uv.x(), uv.y() }; });
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
    }

    return true;
}
