#pragma once

#include <tsl/robin_map.h>
#include <DirectXMath.h>
#include <span>

#include <shared/ipc_protocol.h>
#include <shared/gl_commands.h>

#include "application.h"
#include "debug_window.h"
#include "dx/d3d12_as.h"

#include "descriptor_pager.h"
#include "gl/gl_matrix_stack.h"

#include "structs.h"
#include <shared/containers/free_list_vector.h>

#include <filesystem>

#include "gl/gl_driver.h"

namespace glRemix
{
class glRemixRenderer : public Application
{
    static glDriver sm_driver;

    std::array<dx::D3D12CommandAllocator, m_frames_in_flight> m_cmd_pools{};

    // TODO: Delete this after proper multithreading setup is implemented
    std::array<dx::D3D12CommandAllocator, m_frames_in_flight> m_rt_cmd_pools{};

    ComPtr<ID3D12RootSignature> m_root_signature{};
    ComPtr<ID3D12PipelineState> m_raster_pipeline{};

    ComPtr<ID3D12RootSignature> m_rt_global_root_signature{};
    ComPtr<ID3D12StateObject> m_rt_pipeline{};

    dx::D3D12DescriptorHeap m_GPU_descriptor_heap{};
    dx::D3D12DescriptorHeap m_CPU_descriptor_heap{};

    dx::DescriptorPager m_descriptor_pager{};

    std::array<dx::D3D12Buffer, m_frames_in_flight> m_raygen_constant_buffers{};
    std::array<dx::D3D12Descriptor, m_frames_in_flight> m_raygen_cbv_descriptors{};

    dx::D3D12Buffer m_scratch_space{};

    dx::D3D12TLAS m_tlas{};
    dx::D3D12Descriptor m_tlas_descriptor{};

    // Shader table buffer for ray tracing pipeline (contains raygen, miss, and hit group)
    dx::D3D12Buffer m_shader_table{};
    UINT64 m_raygen_shader_table_offset{};
    UINT64 m_miss_shader_table_offset{};
    UINT64 m_hit_group_shader_table_offset{};

    dx::D3D12Texture m_uav_rt{};
    dx::D3D12Descriptor m_uav_rt_descriptor{};

    // This is written to by CPU potentially in two consecutive frames so we need to double buffer it
    FreeListVector<std::array<BufferAndDescriptor, m_frames_in_flight>> m_gpu_meshrecord_buffers;

    // BLAS, VB, IB per mesh
    // VB and IB have descriptors for SRV to be allocated from pager
    // TODO: Stop using GPU Upload memory for these because they are committed resources!!!
    // Since VB/IB/BLAS are destroyed and created so often we would like them to be placed resources
    FreeListVector<MeshResources> m_mesh_resources;

    // Textures
    std::array<std::vector<dx::D3D12Buffer>, m_frames_in_flight> m_texture_upload_buffers;
    FreeListVector<TextureAndDescriptor> m_textures;
    tsl::robin_map<UINT32, TextureAndDescriptor> m_texture_map;
    TextureAndDescriptor m_environment;

    // Materials per buffer
    // TODO: Make this a macro instead?
    static constexpr UINT MESHRECORDS_PER_BUFFER = 256;
    static constexpr UINT MATERIALS_PER_BUFFER = 256;

    // This is written to by CPU potentially in two consecutive frames so we need to double buffer it
    FreeListVector<std::array<BufferAndDescriptor, m_frames_in_flight>> m_material_buffers;
    std::array<BufferAndDescriptor, m_frames_in_flight> m_light_buffer;

    DebugWindow m_debug_window;

    void create_material_buffer();
    void create_mesh_record_buffer();

    // TODO: Expose this parameter in debug window?
    static constexpr UINT FRAME_LENIENCY = 10;
    UINT m_current_frame = 0;

    void create_swapchain_and_rts(HWND hwnd);
    void create_uav_rt();
    UINT64 create_hash(std::vector<Vertex> vertices, std::vector<UINT32> indices);

    // This should only be called from create_pending_buffers
    void build_mesh_blas_batch(std::vector<size_t> pending_indices, size_t count,
                               ID3D12GraphicsCommandList7* cmd_list);
    void create_pending_buffers(ID3D12GraphicsCommandList7* cmd_list);
    void create_pending_textures(ID3D12GraphicsCommandList7* cmd_list);
    void create_environment_map(ID3D12GraphicsCommandList7* cmd_list, const char* path);
    void build_tlas(ID3D12GraphicsCommandList7* cmd_list);

protected:
    void create() override;
    void render() override;
    void destroy() override;

private:
    // asset replacement
    void replace_mesh(UINT64 meshID, const char* new_asset_path);
    void transform_replacement_vertices(std::vector<Vertex>& gltf_vertices,
                                        std::array<float, 3> scale_val);
    void handle_per_frame_replacement();

public:
    glRemixRenderer() = default;
    ~glRemixRenderer() override;
};
}  // namespace glRemix
