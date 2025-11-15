#include "d3d12_context.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <DirectXMath.h>

#include <shared/math_utils.h>

#include "application.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

// Intercept messages and forward to ImGui
static LRESULT CALLBACK RendererWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
    {
        return true;
    }

    // Get the original window procedure from window user data
    auto original_wndproc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (original_wndproc)
    {
        return CallWindowProc(original_wndproc, hwnd, msg, wparam, lparam);
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

using namespace glRemix::dx;

void glRemix::dx::set_debug_name(ID3D12Object* obj, const char* name)
{
    if (name)
    {
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(name), name);
    }
}

// Includes typeless formats
static bool is_depth_stencil_format(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return true;
        default: return false;
    }
}

bool D3D12Context::create(const bool enable_debug_layer)
{
    UINT dxgi_factory_flags = 0;
    if (enable_debug_layer)
    {
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debug))))
        {
            m_debug->EnableDebugLayer();
#ifdef ENABLE_GPU_BASED_VALIDATION
            m_debug->SetEnableGPUBasedValidation(true);
#endif
            dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }

    // Create the DXGI factory
    if (FAILED(CreateDXGIFactory2(dxgi_factory_flags,
                                  IID_PPV_ARGS(m_dxgi_factory.ReleaseAndGetAddressOf()))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create DXGI factory\n");
        return false;
    }
    if (enable_debug_layer)
    {
        m_dxgi_factory->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("DXGI Factory") - 1,
                                       "DXGI Factory");
    }

    // Pick discrete GPU
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(m_dxgi_factory->EnumAdapterByGpuPreference(0,  // Adapter index
                                                          DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                          __uuidof(IDXGIAdapter1),
                                                          reinterpret_cast<void**>(
                                                              adapter.GetAddressOf()))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to find discrete GPU. Defaulting to 0th adapter\n");
        if (FAILED(m_dxgi_factory->EnumAdapters1(0, &adapter)))
        {
            return false;
        }
    }

    DXGI_ADAPTER_DESC1 desc;
    HRESULT hr = adapter->GetDesc1(&desc);
    if (FAILED(hr))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to get adapter description");
    }
    else
    {
        WCHAR buffer[256];
        swprintf_s(buffer, L"D3D12: Selected adapter: %s\n", desc.Description);
        OutputDebugStringW(buffer);
    }

    // DX12 Ultimate!
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_2,
                                 IID_PPV_ARGS(m_device.ReleaseAndGetAddressOf()))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create device");
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS12 m_options12{};  // For enhanced barriers
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &m_options12,
                                             sizeof(m_options12))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to query D3D12 options 12\n");
    }
    if (!m_options12.EnhancedBarriersSupported)
    {
        OutputDebugStringA("D3D12 ERROR: Enhanced barriers are not supported\n");
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 m_options5{};  // For raytracing
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &m_options5,
                                             sizeof(m_options5))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to query D3D12 options 5");
    }
    if (m_options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
    {
        OutputDebugStringA("D3D12 ERROR: Raytracing tier 1.1 is not supported\n");
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};  // For GPU upload heaps
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16,
                                             sizeof(options16))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to query D3D12 options 16");
        return false;
    }

    // Find the highest supported shader model
    constexpr std::array shader_models{
        D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_2,
        D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_5_1,
    };
    D3D12_FEATURE_DATA_SHADER_MODEL m_shader_model{};
    for (const auto& model : shader_models)
    {
        m_shader_model.HighestShaderModel = model;
        hr = m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &m_shader_model,
                                           sizeof(m_shader_model));
        if (SUCCEEDED(hr))
        {
            break;
        }
    }

    const D3D12MA::ALLOCATOR_DESC allocator_desc = {
        .Flags = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED
                 | D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED,  // Optional but recommended
        .pDevice = m_device.Get(),
        .PreferredBlockSize{},
        .pAllocationCallbacks{},
        .pAdapter = adapter.Get(),
    };

    if (FAILED(CreateAllocator(&allocator_desc, &m_allocator)))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create memory allocator");
        return false;
    }

    if (enable_debug_layer && SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgi_debug))))
    {
        m_dxgi_debug->EnableLeakTrackingForThread();
    }

    if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_dxc_utils))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create DXC utils\n");
        return false;
    }

    return create_fence(&m_fence_wait_all, 0, "wait all fence");
}

bool D3D12Context::get_window_dimensions(XMUINT2* dims)
{
    assert(m_window);
    RECT client_rect;
    if (!GetClientRect(m_window, &client_rect))
    {
        OutputDebugStringA("WinUser ERROR: Failed to get client rect\n");
        return false;
    }
    dims->x = client_rect.right - client_rect.left;
    dims->y = client_rect.bottom - client_rect.top;
    return true;
}

bool D3D12Context::create_swapchain(const HWND window, D3D12Queue* const queue,
                                    UINT* const frame_index)
{
    assert(frame_index);

    m_window = window;

    // Override to intercept messages for ImGui
    // Store the original window procedure in USERDATA
    m_original_wndproc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(window, GWLP_WNDPROC));
    SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(m_original_wndproc));
    SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RendererWndProc));

    // I hope the host app doesn't immediately resize the window after creation!
    XMUINT2 dims;
    if (!get_window_dimensions(&dims))
    {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapchain_descriptor{ .Width = dims.x,
                                                .Height = dims.y,
                                                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                                .SampleDesc = { .Count = 1, .Quality = 0 },
                                                .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                                .BufferCount = Application::m_frames_in_flight,
                                                .Scaling = DXGI_SCALING_STRETCH,
                                                .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                                .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
                                                .Flags = 0 };

    ComPtr<IDXGISwapChain1> swapchain1;
    if (FAILED(m_dxgi_factory->CreateSwapChainForHwnd(queue->queue.Get(),  // Force flush
                                                      window, &swapchain_descriptor, nullptr,
                                                      nullptr, swapchain1.ReleaseAndGetAddressOf())))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create swapchain\n");
        return false;
    }

    if (FAILED(swapchain1.As(&this->m_swapchain)))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to cast swapchain\n");
        return false;
    }

    for (UINT i = 0; i < swapchain_descriptor.BufferCount; ++i)
    {
        if (FAILED(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_swapchain_buffers[i]))))
        {
            OutputDebugStringA("D3D12 ERROR: Failed to get swapchain buffer\n");
            return false;
        }
    }

    m_window = window;
    m_swapchain_dims = dims;
    ;
    m_swapchain_queue = queue;
    *frame_index = m_swapchain->GetCurrentBackBufferIndex();

    return true;
}

bool D3D12Context::create_swapchain_descriptors(D3D12DescriptorTable* descriptors,
                                                D3D12DescriptorHeap* rtv_heap)
{
    assert(descriptors);
    assert(rtv_heap);
    if (rtv_heap->desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        OutputDebugStringA("D3D12 ERROR: Not RTV heap\n");
        return false;
    }
    if (rtv_heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
    {
        OutputDebugStringA("D3D12 ERROR: Not RTV heap\n");
        return false;
    }

    if (!rtv_heap->allocate(Application::m_frames_in_flight, descriptors))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to allocate descriptors\n");
        return false;
    }

    assert(m_swapchain_buffers.size() >= Application::m_frames_in_flight);
    for (UINT i = 0; i < Application::m_frames_in_flight; i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle{};
        rtv_heap->get_cpu_descriptor(&rtv_handle, descriptors->offset + i);
        m_device->CreateRenderTargetView(m_swapchain_buffers[i].Get(), nullptr, rtv_handle);
    }

    return true;
}

UINT D3D12Context::get_swapchain_index() const
{
    assert(m_swapchain);
    return m_swapchain->GetCurrentBackBufferIndex();
}

bool D3D12Context::present()
{
    assert(m_swapchain);
    if (FAILED(m_swapchain->Present(1, 0)))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to present swapchain\n");
        return false;
    }
    return true;
}

void D3D12Context::set_barrier_swapchain(D3D12_TEXTURE_BARRIER* barrier)
{
    assert(barrier);
    barrier->pResource = m_swapchain_buffers[get_swapchain_index()].Get();
    barrier->Subresources = {
        .IndexOrFirstMipLevel = 0,
        .NumMipLevels = 1,
        .FirstArraySlice = 0,
        .NumArraySlices = 1,
        // Most formats are single plane so ignore this
        .FirstPlane = 0,
        .NumPlanes = 1,
    };
}

bool D3D12Context::create_buffer(const BufferDesc& desc, D3D12Buffer* buffer,
                                 const char* debug_name) const
{
    assert(buffer);
    buffer->desc = desc;

    D3D12_RESOURCE_DESC1 resource_desc
	{
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Alignment = 0,
		.Width = desc.size,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_UNKNOWN,
		.SampleDesc =
		{
			.Count = 1,
			.Quality = 0,
		},
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
		// Don't care about SamplerFeedbackMipRegion
	};
    if (desc.uav)
    {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    if (desc.acceleration_structure)
    {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
    }
    D3D12MA::ALLOCATION_DESC allocation_desc{};
    allocation_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    if (desc.visibility & CPU)
    {
        if (desc.visibility & GPU)
        {
            allocation_desc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
        }
        else
        {
            allocation_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
        }
    }
    if (FAILED(m_allocator->CreateResource3(
            &allocation_desc, &resource_desc,
            D3D12_BARRIER_LAYOUT_UNDEFINED,  // You can't have UAV as initial layout, need to
                                             // transition after creation
            nullptr, 0, nullptr, buffer->allocation.ReleaseAndGetAddressOf(), IID_NULL, NULL)))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create buffer\n");
        return false;
    }

    set_debug_name(buffer->allocation.Get()->GetResource(), debug_name);

    initialize_tracked_resource(&buffer->barrier_state, buffer->allocation->GetResource(),
                                false,  // is_texture
                                D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, desc.size);

    return true;
}

bool D3D12Context::map_buffer(const D3D12Buffer* buffer, void** pointer)
{
    assert(buffer);
    assert(buffer->desc.visibility & CPU);

    D3D12_RANGE range(0, 0);
    void* mapped_ptr;
    if (FAILED(buffer->allocation.Get()->GetResource()->Map(0, &range, &mapped_ptr)))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to map buffer\n");
        return false;
    }
    *pointer = mapped_ptr;
    return true;
}

void D3D12Context::unmap_buffer(const D3D12Buffer* buffer)
{
    buffer->allocation.Get()->GetResource()->Unmap(0, nullptr);
}

bool D3D12Context::create_descriptor_heap(const D3D12_DESCRIPTOR_HEAP_DESC& desc,
                                          D3D12DescriptorHeap* heap, const char* debug_name) const
{
    assert(heap);
    if (heap->create(m_device.Get(), desc))
    {
        set_debug_name(heap->m_heap.Get(), debug_name);
        return true;
    }
    return false;
}

void D3D12Context::set_descriptor_heap(ID3D12GraphicsCommandList7* cmd_list,
                                       const D3D12DescriptorHeap& heap) const
{
    assert(cmd_list);
    ID3D12DescriptorHeap* heaps[] = { heap.m_heap.Get() };
    cmd_list->SetDescriptorHeaps(1, heaps);
}

void D3D12Context::set_descriptor_heaps(ID3D12GraphicsCommandList7* cmd_list,
                                        const D3D12DescriptorHeap& cbv_srv_uav_heap,
                                        const D3D12DescriptorHeap& sampler_heap) const
{
    assert(cmd_list);
    ID3D12DescriptorHeap* heaps[] = { cbv_srv_uav_heap.m_heap.Get(), sampler_heap.m_heap.Get() };
    cmd_list->SetDescriptorHeaps(2, heaps);
}

bool D3D12Context::create_texture(const TextureDesc& desc, const D3D12_BARRIER_LAYOUT init_layout,
                                  D3D12Texture* texture, D3D12_CLEAR_VALUE* clear_value,
                                  const char* debug_name) const
{
    assert(texture);
    const D3D12MA::ALLOCATION_DESC allocation_desc{
        .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    };

    D3D12_RESOURCE_DESC1 resource_desc
	{
		.Dimension = desc.dimension,
		.Alignment = 0,
		.Width = desc.width,
		.Height = desc.height,
		.DepthOrArraySize = desc.depth_or_array_size,
		.MipLevels = desc.mip_levels,
		.Format = desc.format,
		.SampleDesc =
		{
			.Count = 1,
			.Quality = 0,
		},
		.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};

    const bool is_ds_format = is_depth_stencil_format(desc.format);
    if (is_ds_format || desc.is_render_target)
    {
        clear_value->Format = desc.format;
        if (is_ds_format)
        {
            resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        if (desc.is_render_target)
        {
            resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
    }
    else
    {
        clear_value = nullptr;
    }
    if (init_layout == D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS)
    {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    if (FAILED(m_allocator->CreateResource3(&allocation_desc, &resource_desc, init_layout,
                                            clear_value, 0, nullptr,
                                            texture->allocation.ReleaseAndGetAddressOf(), IID_NULL,
                                            nullptr)))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create texture\n");
        return false;
    }
    texture->desc = desc;
    set_debug_name(texture->allocation->GetResource(), debug_name);

    // Initialize barrier tracking state
    D3D12_BARRIER_SUBRESOURCE_RANGE subresource_range{
        .IndexOrFirstMipLevel = 0,
        .NumMipLevels = desc.mip_levels,
        .FirstArraySlice = 0,
        .NumArraySlices = desc.depth_or_array_size,
        .FirstPlane = 0,
        .NumPlanes = 1,
    };

    initialize_tracked_resource(&texture->barrier_state, texture->allocation->GetResource(), true,
                                init_layout, &subresource_range, 0);

    return true;
}

bool D3D12Context::create_queue(const D3D12_COMMAND_LIST_TYPE type, D3D12Queue* queue,
                                const char* debug_name) const
{
    assert(queue);
    D3D12_COMMAND_QUEUE_DESC desc{ .Type = type,
                                   .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                                   .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
                                   .NodeMask = 0 };
    if (FAILED(m_device->CreateCommandQueue(&desc,
                                            IID_PPV_ARGS(queue->queue.ReleaseAndGetAddressOf()))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create command queue\n");
        return false;
    }
    queue->type = type;
    set_debug_name(queue->queue.Get(), debug_name);
    return true;
}

bool D3D12Context::create_command_allocator(D3D12CommandAllocator* cmd_allocator,
                                            D3D12Queue* const queue, const char* debug_name) const
{
    assert(cmd_allocator);
    assert(queue);
    if (FAILED(m_device->CreateCommandAllocator(queue->type,
                                                IID_PPV_ARGS(cmd_allocator->cmd_allocator
                                                                 .ReleaseAndGetAddressOf()))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create command allocator\n");
        return false;
    }
    cmd_allocator->queue = queue;
    set_debug_name(cmd_allocator->cmd_allocator.Get(), debug_name);
    return true;
}

bool D3D12Context::create_command_list(ID3D12GraphicsCommandList7** cmd_list,
                                       const D3D12CommandAllocator& cmd_allocator,
                                       const char* debug_name) const
{
    assert(cmd_list);
    if (FAILED(m_device->CreateCommandList(0,  // Single GPU
                                           cmd_allocator.queue->type,
                                           cmd_allocator.cmd_allocator.Get(),
                                           nullptr,  // Initial pipeline state
                                           IID_PPV_ARGS(cmd_list))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create command list\n");
        return false;
    }
    set_debug_name(*cmd_list, debug_name);
    return true;
}

bool D3D12Context::create_fence(D3D12Fence* fence, const UINT64 initial_value,
                                const char* debug_name) const
{
    assert(fence);
    if (FAILED(m_device->CreateFence(initial_value, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(fence->fence.ReleaseAndGetAddressOf()))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create fence\n");
        return false;
    }
    fence->event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    set_debug_name(fence->fence.Get(), debug_name);
    return true;
}

bool D3D12Context::wait_fences(const WaitInfo& info) const
{
    // Max 16 fences at a time
    if (info.count > 16)
    {
        return false;
    }
    std::array<HANDLE, 16> wait_handles{};
    for (UINT i = 0; i < info.count; i++)
    {
        const auto& d3d12_fence = info.fences[i];
        if (FAILED(d3d12_fence.fence->SetEventOnCompletion(info.values[i], d3d12_fence.event)))
        {
            OutputDebugStringA("D3D12 ERROR: Failed to set event on fence\n");
            return false;
        }
        wait_handles[i] = d3d12_fence.event;
    }
    WaitForMultipleObjectsEx(info.count, wait_handles.data(), info.wait_all, info.timeout, FALSE);
    return true;
}

void D3D12Context::set_barrier_resource(const D3D12Buffer* buffer, D3D12_BUFFER_BARRIER* barrier)
{
    assert(buffer);
    assert(barrier);
    barrier->pResource = buffer->allocation->GetResource();
}

void D3D12Context::set_barrier_resource(const D3D12Texture* texture, D3D12_TEXTURE_BARRIER* barrier)
{
    assert(texture);
    assert(barrier);
    barrier->pResource = texture->allocation->GetResource();
}

void D3D12Context::copy_texture_to_swapchain(ID3D12GraphicsCommandList7* cmd_list,
                                             const D3D12Texture& texture)
{
    assert(cmd_list);
    assert(&texture);

    const UINT copy_width = std::min(m_swapchain_dims.x, texture.desc.width);
    const UINT copy_height = std::min(m_swapchain_dims.y, texture.desc.height);

    D3D12_TEXTURE_COPY_LOCATION src{
        .pResource = texture.allocation->GetResource(),
        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        .SubresourceIndex = 0,
    };

    D3D12_TEXTURE_COPY_LOCATION dst{
        .pResource = m_swapchain_buffers[get_swapchain_index()].Get(),
        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        .SubresourceIndex = 0,
    };

    D3D12_BOX src_box{
        .left = 0,
        .top = 0,
        .front = 0,
        .right = copy_width,
        .bottom = copy_height,
        .back = 1,
    };

    cmd_list->CopyTextureRegion(&dst, 0, 0, 0, &src, &src_box);
}

bool D3D12Context::wait_idle(const D3D12Queue* queue)
{
    assert(queue);
    auto value = m_fence_wait_all.fence->GetCompletedValue() + 1;

    queue->queue->Signal(m_fence_wait_all.fence.Get(), value);

    const WaitInfo wait_info{ .wait_all = true,
                              .count = 1,
                              .fences = &m_fence_wait_all,
                              .values = &value,
                              .timeout = INFINITE };
    return wait_fences(wait_info);
}

DXGI_FORMAT D3D12Context::mask_to_format(const BYTE mask,
                                         const D3D_REGISTER_COMPONENT_TYPE component_type)
{
    // Determine number of components from mask
    // Mask bits: x=1, y=2, z=4, w=8
    BYTE component_count = 0;
    if (mask & 1)
    {
        component_count++;  // x
    }
    if (mask & 2)
    {
        component_count++;  // y
    }
    if (mask & 4)
    {
        component_count++;  // z
    }
    if (mask & 8)
    {
        component_count++;  // w
    }

    // Map component type and count to DXGI_FORMAT
    switch (component_type)
    {
        case D3D_REGISTER_COMPONENT_UINT32:
        {
            switch (component_count)
            {
                case 1: return DXGI_FORMAT_R32_UINT;
                case 2: return DXGI_FORMAT_R32G32_UINT;
                case 3: return DXGI_FORMAT_R32G32B32_UINT;
                case 4: return DXGI_FORMAT_R32G32B32A32_UINT;
                default: break;
            }
            break;
        }
        case D3D_REGISTER_COMPONENT_SINT32:
        {
            switch (component_count)
            {
                case 1: return DXGI_FORMAT_R32_SINT;
                case 2: return DXGI_FORMAT_R32G32_SINT;
                case 3: return DXGI_FORMAT_R32G32B32_SINT;
                case 4: return DXGI_FORMAT_R32G32B32A32_SINT;
                default: break;
            }
            break;
        }
        case D3D_REGISTER_COMPONENT_FLOAT32:
        {
            switch (component_count)
            {
                case 1: return DXGI_FORMAT_R32_FLOAT;
                case 2: return DXGI_FORMAT_R32G32_FLOAT;
                case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
                case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
                default: break;
            }
            break;
        }
        default: break;
    }

    // Default to unknown format
    return DXGI_FORMAT_UNKNOWN;
}

InputLayoutDesc D3D12Context::shader_reflection(ID3D12ShaderReflection* shader_reflection,
                                                const D3D12_SHADER_DESC& shader_desc,
                                                const bool increment_slot)
{
    assert(shader_reflection);

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_element_desc;
    input_element_desc.reserve(shader_desc.InputParameters);

    UINT slot = 0;
    for (UINT parameter_index = 0; parameter_index < shader_desc.InputParameters; parameter_index++)
    {
        D3D12_SIGNATURE_PARAMETER_DESC signature_parameter_desc{};
        const auto hr = shader_reflection->GetInputParameterDesc(parameter_index,
                                                                 &signature_parameter_desc);

        if (FAILED(hr))
        {
            continue;
        }

        input_element_desc.emplace_back(D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = signature_parameter_desc.SemanticName,
            .SemanticIndex = signature_parameter_desc.SemanticIndex,
            .Format = mask_to_format(signature_parameter_desc.Mask,
                                     signature_parameter_desc.ComponentType),
            .InputSlot = slot,
            .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0u,  // TODO: manual options for instancing
        });
        if (increment_slot)
        {
            slot++;
        }
    }

    return input_element_desc;
}

bool D3D12Context::reflect_input_layout(IDxcBlob* vertex_shader, InputLayoutDesc* input_layout,
                                        const bool increment_slot,
                                        ID3D12ShaderReflection** reflection) const
{
    assert(input_layout);
    assert(vertex_shader);

    const DxcBuffer reflection_buffer = { .Ptr = vertex_shader->GetBufferPointer(),
                                          .Size = vertex_shader->GetBufferSize(),
                                          .Encoding = 0 };

    if (FAILED(m_dxc_utils->CreateReflection(&reflection_buffer, IID_PPV_ARGS(reflection))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to reflect vertex shader\n");
        return false;
    }

    D3D12_SHADER_DESC shader_desc{};
    if (FAILED((*reflection)->GetDesc(&shader_desc)))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to get shader description\n");
        return false;
    }

    *input_layout = shader_reflection(*reflection, shader_desc, increment_slot);

    return true;
}

bool D3D12Context::load_blob_from_file(const wchar_t* path, IDxcBlobEncoding** blob) const
{
    return SUCCEEDED(m_dxc_utils->LoadFile(path, nullptr, blob));
}

static D3D12_BLEND_DESC make_default_blend_desc()
{
    D3D12_BLEND_DESC desc{
        .AlphaToCoverageEnable = FALSE,
        .IndependentBlendEnable = FALSE,
    };

    constexpr D3D12_RENDER_TARGET_BLEND_DESC default_render_target_blend_desc{
        .BlendEnable = FALSE,
        .LogicOpEnable = FALSE,
        .SrcBlend = D3D12_BLEND_ONE,
        .DestBlend = D3D12_BLEND_ZERO,
        .BlendOp = D3D12_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D12_BLEND_ONE,
        .DestBlendAlpha = D3D12_BLEND_ZERO,
        .BlendOpAlpha = D3D12_BLEND_OP_ADD,
        .LogicOp = D3D12_LOGIC_OP_NOOP,
        .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
    };

    for (auto& i : desc.RenderTarget)
    {
        i = default_render_target_blend_desc;
    }

    return desc;
}

static D3D12_DEPTH_STENCIL_DESC make_default_depth_stencil_desc()
{
    D3D12_DEPTH_STENCIL_DESC desc{
        .DepthEnable = FALSE,
        .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
        .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
        .StencilEnable = FALSE,
        .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
        .StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
        .FrontFace = {},
        .BackFace = {},
    };

    constexpr D3D12_DEPTH_STENCILOP_DESC default_stencil_op{
        .StencilFailOp = D3D12_STENCIL_OP_KEEP,
        .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
        .StencilPassOp = D3D12_STENCIL_OP_KEEP,
        .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
    };

    desc.FrontFace = default_stencil_op;
    desc.BackFace = default_stencil_op;

    return desc;
}

void D3D12Context::create_constant_buffer_view(const D3D12Buffer* buffer,
                                               const D3D12DescriptorTable* descriptor_table,
                                               const UINT descriptor_index) const
{
    assert(buffer);
    assert(descriptor_table);

    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle{};
    descriptor_table->heap->get_cpu_descriptor(&cpu_handle,
                                               descriptor_table->offset + descriptor_index);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{
        .BufferLocation = buffer->allocation->GetResource()->GetGPUVirtualAddress(),
        .SizeInBytes = static_cast<UINT>(buffer->desc.size),
    };

    m_device->CreateConstantBufferView(&cbv_desc, cpu_handle);
}

void D3D12Context::create_shader_resource_view_acceleration_structure(
    const D3D12Buffer& tlas, const D3D12DescriptorTable* descriptor_table,
    UINT descriptor_index) const
{
    assert(&tlas);
    assert(descriptor_table);

    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle{};
    descriptor_table->heap->get_cpu_descriptor(&cpu_handle,
                                               descriptor_table->offset + descriptor_index);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .RaytracingAccelerationStructure{
            .Location = tlas.get_gpu_address(),
        },
    };

    m_device->CreateShaderResourceView(nullptr, &srv_desc, cpu_handle);
}

void D3D12Context::create_unordered_access_view_texture(const D3D12Texture* texture,
                                                        const DXGI_FORMAT format,
                                                        const D3D12DescriptorTable* descriptor_table,
                                                        const UINT descriptor_index) const
{
    assert(texture);
    assert(descriptor_table);

    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle{};
    descriptor_table->heap->get_cpu_descriptor(&cpu_handle,
                                               descriptor_table->offset + descriptor_index);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{
        .Format = format,
        .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
        .Texture2D{
            .MipSlice = 0,
            .PlaneSlice = 0,
        },
    };

    m_device->CreateUnorderedAccessView(texture->allocation->GetResource(), nullptr, &uav_desc,
                                        cpu_handle);
}

bool D3D12Context::create_root_signature(const D3D12_ROOT_SIGNATURE_DESC& desc,
                                         ID3D12RootSignature** root_signature,
                                         const char* debug_name) const
{
    assert(root_signature);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           signature.ReleaseAndGetAddressOf(),
                                           error.ReleaseAndGetAddressOf())))
    {
        if (error)
        {
            OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
        }
        return false;
    }

    if (FAILED(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
                                             signature->GetBufferSize(),
                                             IID_PPV_ARGS(root_signature))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create root signature\n");
        return false;
    }

    if (debug_name)
    {
        set_debug_name(*root_signature, debug_name);
    }

    return true;
}

bool D3D12Context::create_graphics_pipeline(const GraphicsPipelineDesc& desc,
                                            IDxcBlob* vertex_shader, IDxcBlob* pixel_shader,
                                            ID3D12PipelineState** pipeline_state,
                                            const char* debug_name) const
{
    assert(pipeline_state);
    assert(desc.root_signature);
    assert(vertex_shader);
    assert(pixel_shader);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};

    pso_desc.pRootSignature = desc.root_signature;

    pso_desc.VS = { .pShaderBytecode = vertex_shader->GetBufferPointer(),
                    .BytecodeLength = vertex_shader->GetBufferSize() };
    pso_desc.PS = { .pShaderBytecode = pixel_shader->GetBufferPointer(),
                    .BytecodeLength = pixel_shader->GetBufferSize() };

    assert(!u64_overflows_u32(desc.input_layout.size()));

    pso_desc.InputLayout = { .pInputElementDescs = desc.input_layout.data(),
                             .NumElements = static_cast<UINT>(desc.input_layout.size()) };

    if (desc.rasterizer_state.has_value())
    {
        pso_desc.RasterizerState = desc.rasterizer_state.value();
    }
    else
    {
        pso_desc.RasterizerState = {
            .FillMode = D3D12_FILL_MODE_SOLID,
            .CullMode = D3D12_CULL_MODE_BACK,
            .FrontCounterClockwise = FALSE,
            .DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
            .DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
            .SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
            .DepthClipEnable = TRUE,
            .MultisampleEnable = FALSE,
            .AntialiasedLineEnable = FALSE,
            .ForcedSampleCount = 0,
            .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
        };
    }

    if (desc.blend_state.has_value())
    {
        pso_desc.BlendState = desc.blend_state.value();
    }
    else
    {
        pso_desc.BlendState = make_default_blend_desc();
    }

    if (desc.depth_stencil_state.has_value())
    {
        pso_desc.DepthStencilState = desc.depth_stencil_state.value();
    }
    else
    {
        pso_desc.DepthStencilState = make_default_depth_stencil_desc();
    }

    pso_desc.SampleMask = UINT_MAX;

    pso_desc.PrimitiveTopologyType = desc.topology_type;

    pso_desc.NumRenderTargets = desc.render_targets.num_render_targets;
    for (UINT i = 0; i < desc.render_targets.num_render_targets && i < 8; ++i)
    {
        pso_desc.RTVFormats[i] = desc.render_targets.rtv_formats[i];
    }

    pso_desc.DSVFormat = desc.render_targets.dsv_format;

    pso_desc.SampleDesc = { .Count = desc.sample_count, .Quality = desc.sample_quality };

    if (FAILED(m_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(pipeline_state))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create Graphics Pipeline State\n");
        return false;
    }

    set_debug_name(*pipeline_state, debug_name);

    return true;
}

bool D3D12Context::create_raytracing_pipeline(const RayTracingPipelineDesc& desc,
                                              IDxcBlob* raytracing_shaders,
                                              ID3D12StateObject** state_object,
                                              const char* debug_name) const
{
    assert(desc.global_root_signature);

    std::array<D3D12_EXPORT_DESC, 5> exports_array{};
    {
        UINT c = 0;
        for (UINT i = 0; i < desc.num_exports; i++)
        {
            if (desc.export_names[i])
            {
                exports_array[c++].Name = desc.export_names[i];
                // Don't rename exports (null)
            }
        }
    }

    D3D12_DXIL_LIBRARY_DESC dxil_lib{
        .DXILLibrary{
            .pShaderBytecode = raytracing_shaders->GetBufferPointer(),
            .BytecodeLength = raytracing_shaders->GetBufferSize(),
        },
        .NumExports = desc.num_exports,
        .pExports = exports_array.data(),
    };

    D3D12_STATE_SUBOBJECT lib_sub_obj{
        .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY,
        .pDesc = &dxil_lib,
    };

    // Define hit group subobject needed for pipeline
    D3D12_HIT_GROUP_DESC hg{
        .HitGroupExport = L"HG_Default",         // TODO: Option in struct for this?
        .Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,  // or PROCEDURAL TODO: Add option in struct for this
        .AnyHitShaderImport = desc.export_names[static_cast<UINT>(ExportType::ANY_HIT)],
        .ClosestHitShaderImport = desc.export_names[static_cast<UINT>(ExportType::CLOSEST_HIT)],
        .IntersectionShaderImport = desc.export_names[static_cast<UINT>(ExportType::INTERSECTION)],
    };

    // Define global, local root signatures
    D3D12_STATE_SUBOBJECT global_rs_subobj{
        .Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE,
        .pDesc = reinterpret_cast<const void*>(&desc.global_root_signature),
    };

    std::array<D3D12_STATE_SUBOBJECT, 5> local_rs_subobjs{};
    {
        UINT c = 0;
        for (auto& rs : desc.local_root_signatures)
        {
            if (rs)
            {
                local_rs_subobjs[c++] = D3D12_STATE_SUBOBJECT{
                    .Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE,
                    .pDesc = &rs,
                };
            }
        }
    }

    // Associate local root signatures with their exports
    std::array<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION, 5> local_rs_associations{};
    std::array<D3D12_STATE_SUBOBJECT, 5> local_rs_association_subobjs{};
    {
        UINT c = 0;
        for (UINT i = 0; i < 5; i++)
        {
            if (desc.local_root_signatures[i] && desc.export_names[i])
            {
                local_rs_associations[c].pSubobjectToAssociate = &local_rs_subobjs[i];
                local_rs_associations[c].NumExports = 1;
                local_rs_associations[c].pExports = &exports_array[i].Name, c++;
            }
        }
        for (UINT i = 0; i < c; i++)
        {
            local_rs_association_subobjs[i].Type
                = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
            local_rs_association_subobjs[i].pDesc = &local_rs_associations[i];
        }
    }

    // Configs
    D3D12_RAYTRACING_SHADER_CONFIG shader_config{
        .MaxPayloadSizeInBytes = desc.payload_size,
        .MaxAttributeSizeInBytes = desc.attribute_size,
    };
    D3D12_STATE_SUBOBJECT shader_config_subobj{
        .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG,
        .pDesc = &shader_config,
    };
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{
        .MaxTraceRecursionDepth = desc.max_recursion_depth,
    };

    D3D12_STATE_SUBOBJECT pipeline_config_subobj{
        .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG,
        .pDesc = &pipeline_config,
    };
    D3D12_STATE_SUBOBJECT hg_subobj{
        .Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP,
        .pDesc = &hg,
    };

    // Collect all subobjects
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(20);

    subobjects.push_back(lib_sub_obj);
    subobjects.push_back(hg_subobj);

    subobjects.push_back(shader_config_subobj);
    subobjects.push_back(pipeline_config_subobj);

    if (desc.global_root_signature)
    {
        subobjects.push_back(global_rs_subobj);
    }

    for (UINT i = 0; i < 5; i++)
    {
        if (desc.local_root_signatures[i])
        {
            subobjects.push_back(local_rs_subobjs[i]);
        }
    }

    UINT num_associations = 0;
    for (UINT i = 0; i < 5; i++)
    {
        if (desc.local_root_signatures[i] && desc.export_names[i])
        {
            subobjects.push_back(local_rs_association_subobjs[num_associations++]);
        }
    }

    assert(!u64_overflows_u32(subobjects.size()));

    D3D12_STATE_OBJECT_DESC state_object_desc{
        .Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
        .NumSubobjects = static_cast<UINT>(subobjects.size()),
        .pSubobjects = subobjects.data(),
    };

    if (FAILED(m_device->CreateStateObject(&state_object_desc, IID_PPV_ARGS(state_object))))
    {
        OutputDebugStringA("D3D12 ERROR: Failed to create ray tracing pipeline state object\n");
        return false;
    }

    set_debug_name(*state_object, debug_name);

    return true;
}

D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC D3D12Context::get_buffer_rt_description(
    const D3D12Buffer* vertex_buffer, const D3D12Buffer* index_buffer)
{
    assert(vertex_buffer);
    assert(vertex_buffer->desc.visibility & GPU);
    const auto count = vertex_buffer->desc.size / vertex_buffer->desc.stride;
    assert(!u64_overflows_u32(count));

    DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
    UINT index_count = 0;
    if (index_buffer)
    {
        assert(index_buffer->desc.visibility & GPU);
        assert(index_buffer->desc.stride == 2 || index_buffer->desc.stride == 4);
        if (index_buffer->desc.stride == 2)
        {
            index_format = DXGI_FORMAT_R16_UINT;
        }
        else if (index_buffer->desc.stride == 4)
        {
            index_format = DXGI_FORMAT_R32_UINT;
        }
        const auto idx_count = index_buffer->desc.size / index_buffer->desc.stride;
        assert(!u64_overflows_u32(idx_count) && "Index count must fit in 32 bits");
        index_count = static_cast<UINT>(idx_count);
    }

    return
    {
        .Transform3x4 = NULL,
        .IndexFormat = index_format,
        .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
        .IndexCount = index_count,
        .VertexCount = static_cast<UINT>(count),
        .IndexBuffer = index_buffer ? index_buffer->get_gpu_address() : 0,
        .VertexBuffer =
        {
            .StartAddress = vertex_buffer->get_gpu_address(),
            .StrideInBytes = vertex_buffer->desc.stride,
        },
    };
}

D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC D3D12Context::get_buffer_rt_description_subrange(
    const D3D12Buffer* vertex_buffer, const UINT32 vertex_count, const UINT32 vertex_offset,
    const D3D12Buffer* index_buffer, const UINT32 index_count, const UINT32 index_offset)
{
    assert(vertex_buffer);
    assert(vertex_buffer->desc.visibility & GPU);
    const auto count = vertex_buffer->desc.size / vertex_buffer->desc.stride;
    assert(!u64_overflows_u32(count));

    auto vb_base = vertex_buffer->allocation.Get()->GetResource()->GetGPUVirtualAddress();
    auto vb_start = vb_base + UINT64(vertex_offset) * vertex_buffer->desc.stride;

    DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
    UINT64 ib_start = 0;
    if (index_buffer)
    {
        assert(index_buffer->desc.visibility & GPU);
        assert(index_buffer->desc.stride == 2 || index_buffer->desc.stride == 4);
        if (index_buffer->desc.stride == 2)
        {
            index_format = DXGI_FORMAT_R16_UINT;
        }
        else if (index_buffer->desc.stride == 4)
        {
            index_format = DXGI_FORMAT_R32_UINT;
        }
        const auto idx_count = index_buffer->desc.size / index_buffer->desc.stride;
        assert(!u64_overflows_u32(idx_count) && "Index count must fit in 32 bits");

        const auto ib_base = index_buffer->allocation.Get()->GetResource()->GetGPUVirtualAddress();
        ib_start = ib_base + static_cast<UINT64>(index_offset) * index_buffer->desc.stride;
    }

    return
    {
        .Transform3x4 = NULL,
        .IndexFormat = index_format,
        .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
        .IndexCount = index_count,
        .VertexCount = static_cast<UINT>(vertex_count),
        .IndexBuffer = index_buffer ? ib_start : 0,
        .VertexBuffer =
        {
            .StartAddress = vb_start,
            .StrideInBytes = vertex_buffer->desc.stride,
        },
    };
}

D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO
D3D12Context::get_acceleration_structure_prebuild_info(
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& desc) const
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    m_device->GetRaytracingAccelerationStructurePrebuildInfo(&desc, &info);
    return info;
}

D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC D3D12Context::get_raytracing_acceleration_structure(
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& desc, D3D12Buffer* out,
    D3D12Buffer* in, D3D12Buffer* scratch) const
{
    assert(out);
    assert(scratch);
    return {
        .DestAccelerationStructureData = out->get_gpu_address(),
        .Inputs = desc,
        .SourceAccelerationStructureData = in ? in->get_gpu_address() : NULL,
        .ScratchAccelerationStructureData = scratch->get_gpu_address(),
    };
}

bool D3D12Context::mark_use(D3D12Buffer* buffer, const Usage use_kind)
{
    assert(buffer);
    return dx::mark_use(buffer->barrier_state, use_kind);
}

bool D3D12Context::mark_use(D3D12Texture* texture, const Usage use_kind)
{
    assert(texture);
    return dx::mark_use(texture->barrier_state, use_kind);
}

void D3D12Context::emit_barriers(ID3D12GraphicsCommandList7* cmd_list, D3D12Buffer* const* buffers,
                                 const size_t buffer_count, D3D12Texture* const* textures,
                                 const size_t texture_count)
{
    assert(cmd_list);
    assert(buffers || textures);
    const size_t total_count = buffer_count + texture_count;

    std::array<Resource*, 16> resources{};
    assert(total_count <= resources.size());

    auto b_ptr = buffers;
    size_t index = 0;
    for (auto ptr = buffers; ptr && *ptr && index < buffer_count; ++ptr)
    {
        resources[index++] = &(*ptr)->barrier_state;
    }
    for (auto ptr = textures; ptr && *ptr && index < total_count; ++ptr)
    {
        resources[index++] = &(*ptr)->barrier_state;
    }

    dx::emit_barriers(cmd_list, resources.data(), total_count, nullptr);
}

void D3D12Context::bind_vertex_buffers(ID3D12GraphicsCommandList7* cmd_list, const UINT start_slot,
                                       const UINT buffer_count, const D3D12Buffer* const* buffers,
                                       const UINT* sizes, const UINT* strides, const UINT* offsets)
{
    assert(cmd_list);
    assert(buffers);
    assert(buffer_count > 0);
    std::array<D3D12_VERTEX_BUFFER_VIEW, 8> views;
    for (UINT i = 0; i < buffer_count; i++)
    {
        assert(buffers[i]);
        views[i] = {
            .BufferLocation = buffers[i]->allocation->GetResource()->GetGPUVirtualAddress()
                              + (offsets ? offsets[i] : 0),
            .SizeInBytes = sizes ? sizes[i] : static_cast<UINT>(buffers[i]->desc.size),
            .StrideInBytes = strides ? strides[i] : static_cast<UINT>(buffers[i]->desc.stride),
        };
    }
    cmd_list->IASetVertexBuffers(start_slot, buffer_count, views.data());
}

void D3D12Context::bind_index_buffer(ID3D12GraphicsCommandList7* cmd_list,
                                     const D3D12Buffer& buffer, const UINT offset)
{
    assert(cmd_list);
    assert(buffer.desc.stride == 2 || buffer.desc.stride == 4);
    D3D12_INDEX_BUFFER_VIEW view{
        .BufferLocation = buffer.allocation->GetResource()->GetGPUVirtualAddress() + offset,
        .SizeInBytes = static_cast<UINT>(buffer.desc.size),
        .Format = buffer.desc.stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
    };
    cmd_list->IASetIndexBuffer(&view);
}

bool D3D12Context::init_imgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    assert(m_window);

    if (!ImGui_ImplWin32_Init(m_window))
    {
        OutputDebugStringA("ImGui ERROR: ImGui_ImplWin32_Init failed\n");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        .NumDescriptors
        = Application::m_frames_in_flight,  // This should match swapchain buffer count
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    };

    if (!create_descriptor_heap(heap_desc, &m_imgui_srv_heap, "imgui srv heap"))
    {
        return false;
    }

    if (!m_imgui_srv_heap.allocate(1, &m_imgui_font_desc))
    {
        OutputDebugStringA("ImGui ERROR: Failed to allocate descriptor for font texture\n");
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE font_cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE font_gpu{};
    m_imgui_srv_heap.get_cpu_descriptor(&font_cpu, m_imgui_font_desc.offset);
    if (!m_imgui_srv_heap.get_gpu_descriptor(&font_gpu, m_imgui_font_desc.offset))
    {
        return false;
    }

    m_imgui_font_cpu = font_cpu;
    m_imgui_font_gpu = font_gpu;
    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = m_device.Get();
    init_info.CommandQueue = m_swapchain_queue ? m_swapchain_queue->queue.Get() : nullptr;
    init_info.NumFramesInFlight = Application::m_frames_in_flight;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = m_imgui_srv_heap.m_heap.Get();

    init_info.UserData = this;
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                                        D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle,
                                        D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
    {
        auto* self = static_cast<D3D12Context*>(info->UserData);
        *out_cpu_desc_handle = self->m_imgui_font_cpu;
        *out_gpu_desc_handle = self->m_imgui_font_gpu;
    };
    init_info.SrvDescriptorFreeFn =
        [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
    {
        // Managed by entire heap
    };

    if (!ImGui_ImplDX12_Init(&init_info))
    {
        OutputDebugStringA("ImGui ERROR: ImGui_ImplDX12_Init failed\n");
        return false;
    }

    return true;
}

void D3D12Context::start_imgui_frame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void D3D12Context::render_imgui_draw_data(ID3D12GraphicsCommandList7* cmd_list)
{
    assert(cmd_list);
    assert(m_imgui_srv_heap.m_heap);
    ImGui::Render();
    ID3D12DescriptorHeap* heaps[] = { m_imgui_srv_heap.m_heap.Get() };
    cmd_list->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd_list);
}

void D3D12Context::destroy_imgui()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    assert(m_imgui_font_desc.heap);
    m_imgui_srv_heap.deallocate(&m_imgui_font_desc);
}

D3D12Context::~D3D12Context()
{
    m_allocator.Reset();
    m_swapchain.Reset();
    m_dxgi_factory.Reset();
    if (m_debug)
    {
        // As long as the only thing left is factory or device it's fine
        m_dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
        m_dxgi_debug.Reset();
        m_debug.Reset();
    }
    m_allocator.Reset();
    m_device.Reset();
}
