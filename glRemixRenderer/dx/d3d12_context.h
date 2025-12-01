#pragma once

#include <array>
#include <vector>
#include <optional>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <dxgidebug.h>
#include <D3D12MemAlloc.h>
#include <DirectXMath.h>
#include <d3d12shader.h>
#include <dxcapi.h>

#include "d3d12_buffer.h"
#include "d3d12_command_allocator.h"
#include "d3d12_descriptor_heap.h"
#include "d3d12_fence.h"
#include "d3d12_queue.h"
#include "d3d12_pipeline_types.h"
#include "d3d12_texture.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace glRemix::dx
{
class D3D12Context
{
    D3D12DescriptorHeap m_imgui_srv_heap{};
    D3D12Descriptor m_imgui_font_desc{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_imgui_font_cpu{};  // Persistent CPU handle for allocator callback
    D3D12_GPU_DESCRIPTOR_HANDLE m_imgui_font_gpu{};  // Persistent GPU handle for allocator callback

    ComPtr<IDXGIFactory6> m_dxgi_factory = nullptr;

    ComPtr<ID3D12Debug3> m_debug;
    ComPtr<IDXGIDebug1> m_dxgi_debug;

    ComPtr<ID3D12Device5> m_device;
    ComPtr<D3D12MA::Allocator> m_allocator;

    ComPtr<IDxcUtils> m_dxc_utils = nullptr;

    HWND m_window = nullptr;

    XMUINT2 m_swapchain_dims{};
    ComPtr<IDXGISwapChain3> m_swapchain;
    std::array<ComPtr<ID3D12Resource>, 2> m_swapchain_buffers;  // 2 is upper bound
    D3D12Queue* m_swapchain_queue = nullptr;

    D3D12Fence m_fence_wait_all{};

    BOOL m_allow_tearing = FALSE;

    static DXGI_FORMAT mask_to_format(BYTE mask, D3D_REGISTER_COMPONENT_TYPE component_type);
    static std::vector<D3D12_INPUT_ELEMENT_DESC> shader_reflection(
        ID3D12ShaderReflection* shader_reflection, const D3D12_SHADER_DESC& shader_desc,
        bool increment_slot = false);

public:
    bool create(bool enable_debug_layer);

    static bool s_get_window_dimensions(XMUINT2* dims, const HWND window);

    bool create_swapchain(HWND window, D3D12Queue* queue, UINT* frame_index);
    bool create_swapchain_descriptors(D3D12Descriptor* descriptors, D3D12DescriptorHeap* rtv_heap);
    UINT get_swapchain_index() const;
    bool present(bool unlocked);

    void set_barrier_swapchain(D3D12_TEXTURE_BARRIER* barrier);

    bool create_buffer(const BufferDesc& desc, D3D12Buffer* buffer,
                       const char* debug_name = nullptr) const;
    // Convenience functions
    bool map_buffer(const D3D12Buffer* buffer, void** pointer);
    void unmap_buffer(const D3D12Buffer* buffer);

    bool create_descriptor_heap(const D3D12_DESCRIPTOR_HEAP_DESC& desc, D3D12DescriptorHeap* heap,
                                const char* debug_name = nullptr) const;
    void set_descriptor_heap(ID3D12GraphicsCommandList7* cmd_list,
                             const D3D12DescriptorHeap& heap) const;
    void set_descriptor_heaps(ID3D12GraphicsCommandList7* cmd_list,
                              const D3D12DescriptorHeap& cbv_srv_uav_heap,
                              const D3D12DescriptorHeap& sampler_heap) const;

    void create_constant_buffer_view(const D3D12Buffer* buffer,
                                     const D3D12Descriptor& descriptor) const;
    // Structured buffers only
    void create_shader_resource_view(const D3D12Buffer& buffer,
                                     const D3D12Descriptor& descriptor) const;
    void create_shader_resource_view_raw(const D3D12Buffer& buffer,
                                         const D3D12Descriptor& descriptor) const;
    void create_shader_resource_view_typed(const D3D12Buffer& buffer, DXGI_FORMAT format,
                                           const D3D12Descriptor& descriptor) const;
    void create_shader_resource_view_acceleration_structure(const D3D12Buffer& tlas,
                                                            const D3D12Descriptor& descriptor) const;
    void create_unordered_access_view_texture(const D3D12Texture& texture, DXGI_FORMAT format,
                                              const D3D12Descriptor& descriptor) const;
    void create_shader_resource_view_texture(const D3D12Texture& texture, DXGI_FORMAT format,
                                             const D3D12Descriptor& descriptor) const;

    void copy_descriptors(const D3D12Descriptor& dest_start, const D3D12Descriptor& src_start,
                          UINT count) const;

    bool create_texture(const TextureDesc& desc, D3D12_BARRIER_LAYOUT init_layout,
                        D3D12Texture* texture, D3D12_CLEAR_VALUE* clear_value,
                        const char* debug_name = nullptr) const;
    // Creates staging buffer with data and records copy to texture
    bool copy_to_texture(ID3D12GraphicsCommandList7* cmd_list, const void* data,
                         D3D12Buffer* staging, D3D12Texture* texture);

    bool create_queue(D3D12_COMMAND_LIST_TYPE type, D3D12Queue* queue,
                      const char* debug_name = nullptr) const;
    bool create_command_allocator(D3D12CommandAllocator* cmd_allocator, D3D12Queue* queue,
                                  const char* debug_name = nullptr) const;
    bool create_command_list(ID3D12GraphicsCommandList7** cmd_list,
                             const D3D12CommandAllocator& cmd_allocator,
                             const char* debug_name = nullptr) const;

    bool create_fence(D3D12Fence* fence, UINT64 initial_value,
                      const char* debug_name = nullptr) const;
    bool wait_fences(const WaitInfo& info) const;

    void set_barrier_resource(const D3D12Buffer* buffer, D3D12_BUFFER_BARRIER* barrier);
    void set_barrier_resource(const D3D12Texture* texture, D3D12_TEXTURE_BARRIER* barrier);

    // TODO: Copy parameters
    void copy_texture_to_swapchain(ID3D12GraphicsCommandList7* cmd_list,
                                   const D3D12Texture& texture);
    // void copy_texture_to_texture()

    bool reflect_input_layout(IDxcBlob* vertex_shader, InputLayoutDesc* input_layout,
                              bool increment_slot, ID3D12ShaderReflection** reflection) const;

    bool create_root_signature(const D3D12_ROOT_SIGNATURE_DESC& desc,
                               ID3D12RootSignature** root_signature,
                               const char* debug_name = nullptr) const;

    // TODO: Custom file loading, no wide char
    bool load_blob_from_file(const wchar_t* path, IDxcBlobEncoding** blob) const;

    bool create_graphics_pipeline(const GraphicsPipelineDesc& desc, IDxcBlob* vertex_shader,
                                  IDxcBlob* pixel_shader, ID3D12PipelineState** pipeline_state,
                                  const char* debug_name) const;

    bool create_raytracing_pipeline(const RayTracingPipelineDesc& desc,
                                    IDxcBlob* raytracing_shaders, ID3D12StateObject** state_object,
                                    const char* debug_name) const;

    // TODO: Similar function for AABB buffers? Need it for area lights.
    static D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC get_buffer_rt_description(
        const D3D12Buffer* vertex_buffer, const D3D12Buffer* index_buffer);
    static D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC get_buffer_rt_description_subrange(
        const D3D12Buffer* vertex_buffer, UINT32 vertex_count, UINT32 vertex_offset,
        const D3D12Buffer* index_buffer, UINT32 index_count, UINT32 index_offset);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO get_acceleration_structure_prebuild_info(
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& desc) const;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC get_raytracing_acceleration_structure(
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& desc, D3D12Buffer* out,
        D3D12Buffer* in, D3D12Buffer* scratch) const;

    bool mark_use(D3D12Buffer* buffer, Usage use_kind);
    bool mark_use(D3D12Texture* texture, Usage use_kind);

    void emit_barriers(ID3D12GraphicsCommandList7* cmd_list, D3D12Buffer* const* buffers,
                       size_t buffer_count, D3D12Texture* const* textures, size_t texture_count);

    void bind_vertex_buffers(ID3D12GraphicsCommandList7* cmd_list, UINT start_slot,
                             UINT buffer_count, const D3D12Buffer* const* buffers,
                             const UINT* sizes, const UINT* strides, const UINT* offsets);
    void bind_index_buffer(ID3D12GraphicsCommandList7* cmd_list, const D3D12Buffer& buffer,
                           UINT offset);

    // Note: ImGui using win32 is blurry, even the sample is like this, so I assume it's expected
    bool init_imgui();
    void start_imgui_frame();
    void render_imgui_draw_data(ID3D12GraphicsCommandList7* cmd_list);
    void destroy_imgui();

    bool wait_idle(const D3D12Queue* queue);

    HWND get_window() const
    {
        return m_window;
    }

    ~D3D12Context();
};

void set_debug_name(ID3D12Object* obj, const char* name);
}  // namespace glRemix::dx
