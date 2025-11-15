#include "rt_app.h"

#include <cstdio>
#include <cmath>

#include <thread>
#include <chrono>
#include <vector>
#include <filesystem>

#include <imgui.h>

#include <shared/math_utils.h>
#include <shared/gl_commands.h>

#include "dx/d3d12_barrier.h"

void glRemix::glRemixRenderer::create()
{
    for (UINT i = 0; i < m_frames_in_flight; i++)
    {
        THROW_IF_FALSE(m_context.create_command_allocator(&m_cmd_pools[i], &m_gfx_queue,
                                                          "frame command allocator"));
    }

    // Create raster root signature
    {
        D3D12_ROOT_PARAMETER root_params[1]{};
        root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        root_params[0].Descriptor.ShaderRegister = 0;
        root_params[0].Descriptor.RegisterSpace = 0;

        // TODO: Make a singular very large root signature that is used for all raster pipelines
        // TODO: Probably want a texture and sampler as this will be for UI probably
        D3D12_ROOT_SIGNATURE_DESC root_sig_desc;
        root_sig_desc.NumParameters = 1;
        root_sig_desc.pParameters = root_params;
        root_sig_desc.NumStaticSamplers = 0;
        root_sig_desc.pStaticSamplers = nullptr;
        root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        THROW_IF_FALSE(m_context.create_root_signature(root_sig_desc,
                                                       m_root_signature.ReleaseAndGetAddressOf(),
                                                       "triangle root signature"));
    }

    // Get executable directory for shader paths
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    std::filesystem::path shader_dir = exe_dir / "shaders";

    // Compile dummy raster pipeline for testing
    ComPtr<IDxcBlobEncoding> vertex_shader;
    std::wstring vs_path = (shader_dir / "triangle_vs_6_6_VSMain.dxil").wstring();
    THROW_IF_FALSE(
        m_context.load_blob_from_file(vs_path.c_str(), vertex_shader.ReleaseAndGetAddressOf()));
    ComPtr<IDxcBlobEncoding> pixel_shader;
    std::wstring ps_path = (shader_dir / "triangle_ps_6_6_PSMain.dxil").wstring();
    THROW_IF_FALSE(
        m_context.load_blob_from_file(ps_path.c_str(), pixel_shader.ReleaseAndGetAddressOf()));

    dx::GraphicsPipelineDesc pipeline_desc{
        .render_targets{
            .num_render_targets = 1,
            .rtv_formats = { DXGI_FORMAT_R8G8B8A8_UNORM },
            .dsv_format = DXGI_FORMAT_UNKNOWN,
        },
    };

    pipeline_desc.root_signature = m_root_signature.Get();

    ComPtr<ID3D12ShaderReflection>
        shader_reflection_interface;  // Needs to stay in scope until pipeline is created
    THROW_IF_FALSE(
        m_context.reflect_input_layout(vertex_shader.Get(), &pipeline_desc.input_layout, false,
                                       shader_reflection_interface.ReleaseAndGetAddressOf()));

    THROW_IF_FALSE(m_context.create_graphics_pipeline(pipeline_desc, vertex_shader.Get(),
                                                      pixel_shader.Get(),
                                                      m_raster_pipeline.ReleaseAndGetAddressOf(),
                                                      "raster pipeline"));

    // Create raytracing global root signature
    // TODO: Make a singular very large root signature that is used for all ray tracing pipelines
    {
        std::array<D3D12_DESCRIPTOR_RANGE, 3> descriptor_ranges{};

        // Acceleration structure (SRV) at t0
        // t0: TLAS; t1: meshes; t2: materials; t3: lights
        descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptor_ranges[0].NumDescriptors = 1;  // 4 in the future
        descriptor_ranges[0].BaseShaderRegister = 0;
        descriptor_ranges[0].RegisterSpace = 0;
        descriptor_ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // Output UAV at u0
        descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptor_ranges[1].NumDescriptors = 1;
        descriptor_ranges[1].BaseShaderRegister = 0;
        descriptor_ranges[1].RegisterSpace = 0;
        descriptor_ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // Constant buffer at b0
        descriptor_ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descriptor_ranges[2].NumDescriptors = 1;
        descriptor_ranges[2].BaseShaderRegister = 0;
        descriptor_ranges[2].RegisterSpace = 0;
        descriptor_ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        std::array<D3D12_ROOT_PARAMETER, 1> root_parameters{};

        // Single table for everything
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[0].DescriptorTable.NumDescriptorRanges = 3;
        root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges.data();

        D3D12_ROOT_SIGNATURE_DESC root_sig_desc{};
        root_sig_desc.NumParameters = static_cast<UINT>(root_parameters.size());
        root_sig_desc.pParameters = root_parameters.data();
        root_sig_desc.NumStaticSamplers = 0;
        root_sig_desc.pStaticSamplers = nullptr;
        root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        THROW_IF_FALSE(
            m_context.create_root_signature(root_sig_desc,
                                            m_rt_global_root_signature.ReleaseAndGetAddressOf(),
                                            "rt global root signature"));
    }

    // Create ray tracing descriptor heap for TLAS SRV, Output UAV, and constant buffer
    // Added SRV for meshes, materials, and lights
    {
        D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc{
            .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            .NumDescriptors
            = 6,  // TLAS SRV, meshes SRV, materials SRV, lights SRV, Output UAV, Raygen CB
            .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
        };
        THROW_IF_FALSE(m_context.create_descriptor_heap(descriptor_heap_desc, &m_rt_descriptor_heap,
                                                        "ray tracing descriptor heap"));

        // Allocate descriptor table for all ray tracing descriptors (views will be created after
        // resources are ready)
        THROW_IF_FALSE(m_rt_descriptor_heap.allocate(6, &m_rt_descriptors));
    }

    // Compile ray tracing pipeline
    ComPtr<IDxcBlobEncoding> raytracing_shaders;
    std::wstring rt_path = (shader_dir / "raytracing_lib_6_6.dxil").wstring();
    THROW_IF_FALSE(m_context.load_blob_from_file(rt_path.c_str(),
                                                 raytracing_shaders.ReleaseAndGetAddressOf()));
    dx::RayTracingPipelineDesc rt_desc = dx::make_ray_tracing_pipeline_desc(
        L"RayGenMain", L"MissMain", L"ClosestHitMain"
        // TODO: Add Any Hit shader
        // TODO: Add Intersection shader if doing non-triangle geometry
    );
    rt_desc.global_root_signature = m_rt_global_root_signature.Get();
    rt_desc.max_recursion_depth = 1;
    // Make sure these match in the shader
    rt_desc.payload_size = sizeof(float) * (4 + 3 + 1 + 3);
    rt_desc.attribute_size = sizeof(float) * 2;
    THROW_IF_FALSE(m_context.create_raytracing_pipeline(rt_desc, raytracing_shaders.Get(),
                                                        m_rt_pipeline.ReleaseAndGetAddressOf(),
                                                        "rt pipeline"));

    // Create shader table buffer for ray tracing pipeline
    {
        ComPtr<ID3D12StateObjectProperties> rt_pipeline_properties;
        THROW_IF_FALSE(SUCCEEDED(m_rt_pipeline->QueryInterface(
            IID_PPV_ARGS(rt_pipeline_properties.ReleaseAndGetAddressOf()))));

        constexpr UINT64 shader_identifier_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        constexpr UINT64 shader_table_alignment = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

        // Calculate aligned offsets for each table
        m_raygen_shader_table_offset = 0;
        m_miss_shader_table_offset = align_u64(shader_identifier_size, shader_table_alignment);
        m_hit_group_shader_table_offset = align_u64(m_miss_shader_table_offset
                                                        + shader_identifier_size,
                                                    shader_table_alignment);

        UINT64 total_shader_table_size = align_u64(m_hit_group_shader_table_offset
                                                       + shader_identifier_size,
                                                   shader_table_alignment);

        // Create single buffer for all shader tables
        dx::BufferDesc shader_table_desc{
            .size = total_shader_table_size,
            .stride = 0,
            .visibility = static_cast<dx::BufferVisibility>(dx::CPU | dx::GPU),
        };
        THROW_IF_FALSE(m_context.create_buffer(shader_table_desc, &m_shader_table, "shader tables"));

        // Fill out shader table
        void* cpu_ptr;
        THROW_IF_FALSE(m_context.map_buffer(&m_shader_table, &cpu_ptr));

        void* raygen_identifier = rt_pipeline_properties->GetShaderIdentifier(L"RayGenMain");
        assert(raygen_identifier);
        memcpy(static_cast<UINT8*>(cpu_ptr) + m_raygen_shader_table_offset, raygen_identifier,
               shader_identifier_size);

        void* miss_identifier = rt_pipeline_properties->GetShaderIdentifier(L"MissMain");
        assert(miss_identifier);
        memcpy(static_cast<UINT8*>(cpu_ptr) + m_miss_shader_table_offset, miss_identifier,
               shader_identifier_size);

        void* hit_group_identifier = rt_pipeline_properties->GetShaderIdentifier(L"HG_Default");
        assert(hit_group_identifier);
        memcpy(static_cast<UINT8*>(cpu_ptr) + m_hit_group_shader_table_offset, hit_group_identifier,
               shader_identifier_size);

        m_context.unmap_buffer(&m_shader_table);
    }

    dx::BufferDesc raygen_cb_desc{
        .size = align_u32(sizeof(RayGenConstantBuffer), CB_ALIGNMENT),
        .stride = align_u32(sizeof(RayGenConstantBuffer), CB_ALIGNMENT),
        .visibility = static_cast<dx::BufferVisibility>(dx::CPU | dx::GPU),
    };
    for (UINT i = 0; i < m_frames_in_flight; i++)
    {
        THROW_IF_FALSE(m_context.create_buffer(raygen_cb_desc, &m_raygen_constant_buffers[i],
                                               "raygen constant buffer"));
    }

    constexpr UINT64 scratch_size = 16u * MEGABYTE;
    dx::BufferDesc scratch_buffer_desc{
        .size = scratch_size,
        .stride = 0,
        .visibility = dx::GPU,
        .uav = true,  // Scratch space must be in UAV layout
    };
    THROW_IF_FALSE(
        m_context.create_buffer(scratch_buffer_desc, &m_scratch_space, "BLAS scratch space"));

    // Start out with 64 instances/meshes
    dx::BufferDesc instance_buffer_desc{
        .size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * 64,
        .stride = sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
        .visibility = static_cast<dx::BufferVisibility>(dx::CPU | dx::GPU),
    };
    THROW_IF_FALSE(
        m_context.create_buffer(instance_buffer_desc, &m_tlas.instance, "TLAS instance buffer"));

    // TODO: Use CPU descriptors and copy to GPU visible heap
    // Will be updated each frame
    m_context.create_constant_buffer_view(&m_raygen_constant_buffers[0], &m_rt_descriptors, 2);
}

void glRemix::glRemixRenderer::read_gl_command_stream()
{
    // stall until initialized
    while (!m_ipc.init_reader())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }

    const UINT32 buf_capacity = m_ipc.get_capacity();  // ask shared mem for capacity
    std::vector<UINT8> ipc_buf(buf_capacity);          // decoupled local buffer here

    // stall until frame data is grabbed
    UINT32 bytes_read = 0;
    while (
        !m_ipc.try_consume_frame(ipc_buf.data(), static_cast<UINT32>(ipc_buf.size()), &bytes_read))
    {
        OutputDebugStringA("No frame data available.\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(60));  // rest before next poll
    }

    const auto* frameHeader = reinterpret_cast<const GLFrameUnifs*>(ipc_buf.data());

    // if no data was captured return
    if (frameHeader->payload_size == 0)
    {
        char buffer[256];
        sprintf(buffer, "Frame %u: no new commands.\n", frameHeader->frame_index);
        OutputDebugStringA(buffer);
        return;
    }

    m_meshes.clear();       // per frame meshes
    m_matrix_pool.clear();  // reset matrix pool each frame
    m_materials.clear();

    // for acceleration structure building
    THROW_IF_FALSE(SUCCEEDED(m_cmd_pools[get_frame_index()].cmd_allocator->Reset()));
    ComPtr<ID3D12GraphicsCommandList7> cmd_list;
    THROW_IF_FALSE(m_context.create_command_list(cmd_list.ReleaseAndGetAddressOf(),
                                                 m_cmd_pools[get_frame_index()]));

    // loop through data from frame
    read_ipc_buffer(ipc_buf, sizeof(GLFrameUnifs), bytes_read, cmd_list.Get());

    THROW_IF_FALSE(SUCCEEDED(cmd_list->Close()));
    const std::array<ID3D12CommandList*, 1> lists = { cmd_list.Get() };
    m_gfx_queue.queue->ExecuteCommandLists(1, lists.data());

    // Signal fence and wait for GPU to finish
    auto current_fence_value = ++m_fence_frame_ready_val[get_frame_index()];
    m_gfx_queue.queue->Signal(m_fence_frame_ready.fence.Get(), current_fence_value);

    dx::WaitInfo wait_info{
        .wait_all = true,
        .count = 1,
        .fences = &m_fence_frame_ready,
        .values = &current_fence_value,
        .timeout = INFINITE,
    };

    THROW_IF_FALSE(m_context.wait_fences(wait_info));  // Block CPU until done
}

void glRemix::glRemixRenderer::read_ipc_buffer(std::vector<UINT8>& ipc_buf, size_t start_offset,
                                               UINT32 bytes_read,
                                               ID3D12GraphicsCommandList7* cmd_list, bool call_list)
{
    // display list logic
    UINT32 list_index = 0;
    size_t display_list_begin = 0;

    size_t offset = start_offset;  // add additional start index

    while (offset + sizeof(GLCommandUnifs) <= bytes_read)
    {
        const auto* header = reinterpret_cast<const GLCommandUnifs*>(ipc_buf.data() + offset);
        offset += sizeof(GLCommandUnifs);

        bool advance = true;

        switch (header->type)
        {
            case GLCommandType::GLCMD_CREATE:
            {
                HWND hwnd;
                memcpy(&hwnd, ipc_buf.data() + offset, sizeof(HWND));
                THROW_IF_FALSE(m_context.create_swapchain(hwnd, &m_gfx_queue, &m_frame_index));
                THROW_IF_FALSE(
                    m_context.create_swapchain_descriptors(&m_swapchain_descriptors, &m_rtv_heap));
                THROW_IF_FALSE(m_context.init_imgui());
                create_uav_rt();
                // TODO: Descriptor should be on CPU heap
                m_context.create_unordered_access_view_texture(&m_uav_render_target,
                                                               m_uav_render_target.desc.format,
                                                               &m_rt_descriptors, 1);
            }
            case GLCommandType::GLCMD_NEW_LIST:
            {
                const auto* list = reinterpret_cast<const GLNewListCommand*>(
                    ipc_buf.data() + offset);  // reach into data payload

                list_index = list->list;
                list_mode_ = static_cast<gl::GLListMode>(list->mode);  // set global execution state

                display_list_begin = offset + header->dataSize;

                break;
            }
            case GLCommandType::GLCMD_CALL_LIST:
            {
                const auto* list = reinterpret_cast<const GLCallListCommand*>(
                    ipc_buf.data() + offset);  // reach into data payload

                if (m_display_lists.contains(list->list))
                {
                    auto& list_buf = m_display_lists[list->list];
                    const UINT32 listEnd = static_cast<UINT32>(list_buf.size());

                    read_ipc_buffer(list_buf, 0, listEnd, cmd_list, true);
                }
                else
                {
                    char buffer[256];
                    sprintf_s(buffer, "CALL_LIST missing id %u\n", list->list);
                    OutputDebugStringA(buffer);
                }
                break;
            }
            case GLCommandType::GLCMD_END_LIST:
            {
                if (call_list)
                {
                    return;  // return immediately if we are within a calllist (we should return to
                             // the invocation of read_ipc_buffer)
                }
                const auto display_list_end
                    = offset;  // record GL_END_LIST to mark end of display list

                // record new list in respective index
                std::vector new_list(ipc_buf.begin() + display_list_begin,
                                     ipc_buf.begin() + display_list_end);
                m_display_lists[list_index] = std::move(new_list);

                list_mode_ = gl::GLListMode::COMPILE_AND_EXECUTE;  // reset execution state

                break;
            }
            // if we encounter GL_BEGIN we know that a new geometry is to be created
            case GLCommandType::GLCMD_BEGIN:
            {
                const auto* type = reinterpret_cast<const GLBeginCommand*>(
                    ipc_buf.data() + offset);  // reach into data payload
                offset += header->dataSize;    // we enter read geometry assuming first command
                                               // inbetween glbegin and end
                advance = false;
                read_geometry(
                    ipc_buf, &offset, static_cast<GLTopology>(type->mode), bytes_read,
                    cmd_list);  // store geometry data in vertex buffers depending on topology type
                break;
            }
            case GLCommandType::GLCMD_NORMAL3F:
            {
                const auto* n = reinterpret_cast<const GLNormal3fCommand*>(ipc_buf.data() + offset);
                m_normal[0] = n->x;
                m_normal[1] = n->y;
                m_normal[2] = n->z;
                break;
            }
            case GLCommandType::GLCMD_MATERIALF:
            {
                const auto* mat = reinterpret_cast<const GLMaterialCommand*>(
                    ipc_buf.data() + offset);  // reach into data payload

                // TODO: when material f is encountered, edit the current m_material based on the
                // param and value

                break;
            }
            case GLCommandType::GLCMD_MATERIALFV:
            {
                const auto* mat = reinterpret_cast<const GLMaterialfvCommand*>(
                    ipc_buf.data() + offset);  // reach into data payload

                // TODO: when material fv is encountered, edit the current m_material based on the
                // param and value

                break;
            }
            case GLCommandType::GLCMD_LIGHTF:
            {
                const auto* light = reinterpret_cast<const GLLightCommand*>(
                    ipc_buf.data() + offset);  // reach into data payload

                // TODO: when light f is encountered, edit the corresponding index in m_lights based
                // on the param and value

                break;
            }
            case GLCommandType::GLCMD_LIGHTFV:
            {
                const auto* light = reinterpret_cast<const GLLightfvCommand*>(
                    ipc_buf.data() + offset);  // reach into data payload

                // TODO: when light fv is encountered, edit the corresponding index in m_lights
                // based on the param and value

                break;
            }
            case GLCommandType::GLCMD_MATRIX_MODE:
            {
                const auto* type = reinterpret_cast<const GLMatrixModeCommand*>(
                    ipc_buf.data() + offset);  // reach into data payload
                matrix_mode = static_cast<gl::GLMatrixMode>(type->mode);
                break;
            }
            case GLCommandType::GLCMD_PUSH_MATRIX:
            {
                m_matrix_stack.push(matrix_mode);
                break;
            }
            case GLCommandType::GLCMD_POP_MATRIX:
            {
                m_matrix_stack.pop(matrix_mode);
                break;
            }
            case GLCommandType::GLCMD_LOAD_IDENTITY:
            {
                m_matrix_stack.identity(matrix_mode);
                break;
            }
            case GLCommandType::GLCMD_ROTATE:
            {
                const auto* angle_axis = reinterpret_cast<const GLRotateCommand*>(ipc_buf.data()
                                                                                  + offset);
                const float angle = angle_axis->angle;
                const float x = angle_axis->axis.x;
                const float y = angle_axis->axis.y;
                const float z = angle_axis->axis.z;

                m_matrix_stack.rotate(matrix_mode, angle, x, y, z);
                break;
            }
            case GLCommandType::GLCMD_TRANSLATE:
            {
                const auto* vec = reinterpret_cast<const GLTranslateCommand*>(ipc_buf.data()
                                                                              + offset);
                m_matrix_stack.translate(matrix_mode, vec->t.x, vec->t.y, vec->t.z);
                break;
            }
            case GLCommandType::GLCMD_FRUSTUM:
            {
                const auto* frust = reinterpret_cast<const GLFrustumCommand*>(ipc_buf.data()
                                                                              + offset);

                // get current window dimensions
                XMUINT2 win_dims{};
                m_context.get_window_dimensions(&win_dims);

                // recompute frustum based on dx12 window but using intercepted gl stream params
                const double n = frust->zNear;
                const double f = frust->zFar;

                const float aspect = static_cast<float>(win_dims.x)
                                     / static_cast<float>(win_dims.y);

                const double fov_y = 2.0 * std::atan(frust->top / n);

                const double t = n * std::tan(fov_y * 0.5);
                const double b = -t;
                const double r = t * aspect;
                const double l = -r;

                m_matrix_stack.frustum(matrix_mode, l, r, b, t, n, f);

                break;
            }

            default:
            {
                char buffer[256];
                sprintf_s(buffer, "Unhandled Command: %d (size: %u)\n", header->type,
                          header->dataSize);
                OutputDebugStringA(buffer);
                break;
            }
        }

        if (advance)
        {
            offset += header->dataSize;
        }
    }
}

// adds to vertex and index buffers depending on topology type
void glRemix::glRemixRenderer::read_geometry(std::vector<UINT8>& ipc_buf, size_t* const offset,
                                             GLTopology topology, UINT32 bytes_read,
                                             ID3D12GraphicsCommandList7* cmd_list)
{
    bool end_primitive = false;

    // we will first assess if this mesh has been encountered before
    std::vector<Vertex> t_vertices;
    std::vector<UINT32> t_indices;

    while (*offset + sizeof(GLCommandUnifs) <= bytes_read)
    {
        const auto* header = reinterpret_cast<const GLCommandUnifs*>(
            ipc_buf.data()  // get most recent header
            + *offset);

        *offset += sizeof(GLCommandUnifs);  // move into data payload

        switch (header->type)
        {
            case GLCommandType::GLCMD_VERTEX3F:
            {
                const auto* v = reinterpret_cast<const GLVertex3fCommand*>(ipc_buf.data() + *offset);
                Vertex vertex{ { v->x, v->y, v->z },
                               { m_color[0], m_color[1], m_color[2] },
                               { m_normal[0], m_normal[1], m_normal[2] } };
                t_vertices.push_back(vertex);
                break;
            }
            case GLCommandType::GLCMD_NORMAL3F:
            {
                const auto* n = reinterpret_cast<const GLNormal3fCommand*>(ipc_buf.data() + *offset);
                m_normal[0] = n->x;
                m_normal[1] = n->y;
                m_normal[2] = n->z;
                break;
            }
            case GLCommandType::GLCMD_COLOR3F:
            {
                const auto* c = reinterpret_cast<const GLColor3fCommand*>(ipc_buf.data() + *offset);
                m_color[0] = c->x;
                m_color[1] = c->y;
                m_color[2] = c->z;
                break;
            }
            case GLCommandType::GLCMD_COLOR4F:
            {
                const auto* c = reinterpret_cast<const GLColor4fCommand*>(ipc_buf.data() + *offset);
                m_color[0] = c->x;
                m_color[1] = c->y;
                m_color[2] = c->z;
                m_color[3] = c->w;
                break;
            }
            case GLCommandType::GLCMD_END:  // read vertices until GL_END is encountered at
                                            // which point we will have reached end of geometry
            {
                end_primitive = true;
                break;
            }
            default: printf("    (Unhandled primitive command)\n"); break;
        }

        *offset += header->dataSize;  // move past data to next command

        if (end_primitive)
        {
            break;
        }
    }

    // determine indices based on specified topology
    UINT32 num_indices = 0;
    if (topology == GLTopology::GL_QUAD_STRIP)
    {
        for (UINT32 k = 0; k + 3 < t_vertices.size(); k += 2)
        {
            UINT32 a = k + 0;
            UINT32 b = k + 1;
            UINT32 c = k + 2;
            UINT32 d = k + 3;

            t_indices.push_back(a);
            t_indices.push_back(b);
            t_indices.push_back(d);
            t_indices.push_back(a);
            t_indices.push_back(d);
            t_indices.push_back(c);

            num_indices += 6;
        }
    }
    else if (topology == GLTopology::GL_QUADS)
    {
        for (UINT32 k = 0; k + 3 < t_vertices.size(); k += 4)
        {
            UINT32 a = k + 0;
            UINT32 b = k + 1;
            UINT32 c = k + 2;
            UINT32 d = k + 3;

            t_indices.push_back(a);
            t_indices.push_back(b);
            t_indices.push_back(c);
            t_indices.push_back(a);
            t_indices.push_back(c);
            t_indices.push_back(d);

            num_indices += 6;
        }
    }

    // hashing - logic from boost::hash_combine
    size_t seed = 0;
    auto hash_combine = [&seed](auto const& v)
    {
        seed ^= std::hash<std::decay_t<decltype(v)>>{}(v) + 0x9e3779b97f4a7c15ULL + (seed << 6)
                + (seed >> 2);
    };

    // reduces floating point instability
    auto quantize = [](const float v, const float precision = 1e-5f) -> float
    { return std::round(v / precision) * precision; };

    // get vertex data to hash
    for (int i = 0; i < t_vertices.size(); ++i)
    {
        const Vertex& vertex = t_vertices[i];
        hash_combine(quantize(vertex.position[0]));
        hash_combine(quantize(vertex.position[1]));
        hash_combine(quantize(vertex.position[2]));
        hash_combine(quantize(vertex.color[0]));
        hash_combine(quantize(vertex.color[1]));
        hash_combine(quantize(vertex.color[2]));
    }

    // get index data to hash
    for (int i = 0; i < t_indices.size(); ++i)
    {
        const UINT32& index = t_indices[i];
        hash_combine(index);
    }

    // check if hash exists
    UINT64 hash = seed;

    MeshRecord mesh;
    m_mesh_map.find(hash);
    if (m_mesh_map.contains(hash))
    {
        mesh = m_mesh_map[hash];
    }
    else
    {
        mesh.vertex_count = t_vertices.size();
        mesh.index_count = t_indices.size();
        mesh.mesh_id = hash;  // set hash to meshID

        // create vertex buffer
        dx::D3D12Buffer t_vertex_buffer;
        dx::D3D12Buffer t_index_buffer;

        mesh.vertex_id = m_vertex_buffers.size();
        mesh.index_id = m_index_buffers.size();

        dx::BufferDesc vertex_buffer_desc{
            .size = sizeof(Vertex) * t_vertices.size(),
            .stride = sizeof(Vertex),
            .visibility = static_cast<dx::BufferVisibility>(dx::CPU | dx::GPU),
        };
        void* cpu_ptr;
        THROW_IF_FALSE(
            m_context.create_buffer(vertex_buffer_desc, &t_vertex_buffer, "vertex buffer"));

        THROW_IF_FALSE(m_context.map_buffer(&t_vertex_buffer, &cpu_ptr));
        memcpy(cpu_ptr, t_vertices.data(), vertex_buffer_desc.size);
        m_context.unmap_buffer(&t_vertex_buffer);
        m_vertex_buffers.push_back(std::move(t_vertex_buffer));

        // create index buffer
        dx::BufferDesc index_buffer_desc{
            .size = sizeof(UINT) * t_indices.size(),
            .stride = sizeof(UINT),
            .visibility = static_cast<dx::BufferVisibility>(dx::CPU | dx::GPU),
        };
        THROW_IF_FALSE(m_context.create_buffer(index_buffer_desc, &t_index_buffer, "index buffer"));

        THROW_IF_FALSE(m_context.map_buffer(&t_index_buffer, &cpu_ptr));
        memcpy(cpu_ptr, t_indices.data(), index_buffer_desc.size);
        m_context.unmap_buffer(&t_index_buffer);
        m_index_buffers.push_back(std::move(t_index_buffer));

        // create blas buffer
        mesh.blas_id = build_mesh_blas(m_vertex_buffers[mesh.vertex_id],
                                       m_index_buffers[mesh.index_id], cmd_list);

        m_mesh_map[hash] = mesh;
    }

    // we assign materials here
    mesh.mat_id = static_cast<UINT32>(m_materials.size());
    m_materials.push_back(
        m_material);  // store the current state of the material in the materials buffer

    mesh.mv_id = static_cast<UINT32>(m_matrix_pool.size());
    m_matrix_pool.push_back(m_matrix_stack.top(gl::GLMatrixMode::MODELVIEW));

    m_meshes.push_back(std::move(mesh));
}

int glRemix::glRemixRenderer::build_mesh_blas(const dx::D3D12Buffer& vertex_buffer,
                                              const dx::D3D12Buffer& index_buffer,
                                              ID3D12GraphicsCommandList7* cmd_list)
{
    dx::D3D12Buffer t_blas_buffer;

    // Build BLAS here for now, but renderer will construct them dynamically for new geometry in
    // render loop
    D3D12_RAYTRACING_GEOMETRY_DESC tri_desc{
        .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
        .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,  // Try to use this liberally as its faster
        .Triangles = m_context.get_buffer_rt_description(&vertex_buffer, &index_buffer),
    };
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_desc{
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
        // Lots of options here, probably want to use different ones, especially the update one
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE,
        .NumDescs = 1,
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
        .pGeometryDescs = &tri_desc,
    };

    const auto blas_prebuild_info = m_context.get_acceleration_structure_prebuild_info(blas_desc);
    // TODO: Reuse scratch space for all BLAS
    // The same could be done for TLAS(s) as well

    assert(blas_prebuild_info.ScratchDataSizeInBytes < m_scratch_space.desc.size);

    dx::BufferDesc blas_buffer_desc{
        .size = blas_prebuild_info.ResultDataMaxSizeInBytes,
        .stride = 0,
        .visibility = dx::GPU,
        .acceleration_structure = true,
    };
    THROW_IF_FALSE(m_context.create_buffer(blas_buffer_desc, &t_blas_buffer, "BLAS buffer"));

    const auto blas_build_desc = m_context.get_raytracing_acceleration_structure(blas_desc,
                                                                                 &t_blas_buffer,
                                                                                 nullptr,
                                                                                 &m_scratch_space);

    // Mark resources for BLAS build
    m_context.mark_use(&m_scratch_space, dx::Usage::UAV_COMPUTE);
    m_context.mark_use(&t_blas_buffer, dx::Usage::AS_WRITE);
    std::array build_resources = { &m_scratch_space, &t_blas_buffer };
    m_context.emit_barriers(cmd_list, build_resources.data(), build_resources.size(), nullptr, 0);

    cmd_list->BuildRaytracingAccelerationStructure(&blas_build_desc, 0, nullptr);

    // Transition BLAS to read state
    m_context.mark_use(&t_blas_buffer, dx::Usage::AS_READ);
    std::array read_resources = { &t_blas_buffer };
    m_context.emit_barriers(cmd_list, read_resources.data(), read_resources.size(), nullptr, 0);

    m_blas_buffers.push_back(t_blas_buffer);
    return m_blas_buffers.size() - 1;
}

static D3D12_RAYTRACING_INSTANCE_DESC mv_to_instance_desc(const XMFLOAT4X4& mv)
{
    D3D12_RAYTRACING_INSTANCE_DESC desc = {};

    // Rotation / upper 3x3
    desc.Transform[0][0] = mv._11;
    desc.Transform[0][1] = mv._21;
    desc.Transform[0][2] = mv._31;
    desc.Transform[0][3] = mv._41;

    desc.Transform[1][0] = mv._12;
    desc.Transform[1][1] = mv._22;
    desc.Transform[1][2] = mv._32;
    desc.Transform[1][3] = mv._42;

    desc.Transform[2][0] = mv._13;
    desc.Transform[2][1] = mv._23;
    desc.Transform[2][2] = mv._33;
    desc.Transform[2][3] = mv._43;

    return desc;
}

// builds top level acceleration structure with blas buffer (can be called each frame likely)
void glRemix::glRemixRenderer::build_tlas(ID3D12GraphicsCommandList7* cmd_list)
{
    // create an instance descriptor for all geometry
    // TODO: Check if this truncates size_t -> UINT
    const UINT instance_count = static_cast<UINT>(m_meshes.size());  // this frame's meshes

    if (instance_count == 0)
    {
        return;
    }

    // TODO: Use some sort of static allocator or reuse previous buffer
    // Use static vector as hack for now
    static std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instance_descs(instance_count);
    if (instance_count > instance_descs.size())
    {
        instance_descs.resize(instance_count);
    }
    for (UINT i = 0; i < instance_count; i++)
    {
        MeshRecord mesh = m_meshes[i];

        assert(mesh.blas_id < m_blas_buffers.size());
        assert(mesh.mv_id < m_matrix_pool.size());

        const auto blas_addr = m_blas_buffers[mesh.blas_id].get_gpu_address();
        assert(blas_addr != 0);

        D3D12_RAYTRACING_INSTANCE_DESC desc = mv_to_instance_desc(m_matrix_pool[mesh.mv_id]);

        desc.InstanceID = i;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = blas_addr;

        instance_descs[i] = desc;
    }

    assert(m_tlas.instance.desc.size >= sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instance_count);
    // TODO: Recreate instance buffer if too small

    void* cpu_ptr;
    THROW_IF_FALSE(m_context.map_buffer(&m_tlas.instance, &cpu_ptr));
    memcpy(cpu_ptr, instance_descs.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instance_count);
    m_context.unmap_buffer(&m_tlas.instance);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_desc{
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
        .NumDescs = instance_count,
        .InstanceDescs = m_tlas.instance.get_gpu_address(),
    };

    bool should_update = m_tlas.buffer.desc.size > 0
                         && m_tlas.last_instance_count == instance_count;

    const auto tlas_prebuild_info = m_context.get_acceleration_structure_prebuild_info(tlas_desc);

    assert(tlas_prebuild_info.ScratchDataSizeInBytes < m_scratch_space.desc.size);

    // Only recreate buffer on first time or if too small
    // TODO: A warning should be issued when this happens
    if (tlas_prebuild_info.ResultDataMaxSizeInBytes > m_tlas.buffer.desc.size)
    {
        const dx::BufferDesc tlas_buffer_desc{
            .size = tlas_prebuild_info.ResultDataMaxSizeInBytes,
            .stride = 0,
            .visibility = dx::GPU,
            .acceleration_structure = true,
        };
        THROW_IF_FALSE(m_context.create_buffer(tlas_buffer_desc, &m_tlas.buffer, "TLAS buffer"));
        should_update = false;  // Can't update a newly created buffer
    }

    if (should_update)
    {
        tlas_desc.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    }

    m_tlas.last_instance_count = instance_count;  // Track for next frame

    // Mark TLAS for build
    m_context.mark_use(&m_scratch_space, dx::Usage::UAV_COMPUTE);
    m_context.mark_use(&m_tlas.buffer, dx::Usage::AS_WRITE);
    std::array write_resources = { &m_scratch_space, &m_tlas.buffer };
    m_context.emit_barriers(cmd_list, write_resources.data(), write_resources.size(), nullptr, 0);

    const auto tlas_build_desc
        = m_context.get_raytracing_acceleration_structure(tlas_desc, &m_tlas.buffer,
                                                          should_update
                                                              ? &m_tlas.buffer
                                                              : nullptr,  // For in place update
                                                          &m_scratch_space);

    cmd_list->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);

    // Transition TLAS to read state
    m_context.mark_use(&m_tlas.buffer, dx::Usage::AS_READ);
    const std::array read_resources = { &m_tlas.buffer };
    m_context.emit_barriers(cmd_list, read_resources.data(), read_resources.size(), nullptr, 0);

    // TODO: This should not be here and the descriptor should live on a CPU heap and be copied
    m_context.create_shader_resource_view_acceleration_structure(m_tlas.buffer, &m_rt_descriptors,
                                                                 0);
}

void glRemix::glRemixRenderer::render()
{
    // Read GL stream and set resources accordingly
    read_gl_command_stream();

    m_context.start_imgui_frame();

    m_debug_window.render();

    // Be careful not to call the ID3D12Interface reset instead
    THROW_IF_FALSE(SUCCEEDED(m_cmd_pools[get_frame_index()].cmd_allocator->Reset()));

    // Create a command list in the open state
    ComPtr<ID3D12GraphicsCommandList7> cmd_list;
    THROW_IF_FALSE(m_context.create_command_list(cmd_list.ReleaseAndGetAddressOf(),
                                                 m_cmd_pools[get_frame_index()]));

    const auto swapchain_idx = m_context.get_swapchain_index();

    XMUINT2 win_dims{};
    m_context.get_window_dimensions(&win_dims);

    // Set viewport, scissor
    const D3D12_VIEWPORT viewport{
        .TopLeftX = 0,
        .TopLeftY = 0,
        .Width = static_cast<float>(win_dims.x),
        .Height = static_cast<float>(win_dims.y),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };
    const D3D12_RECT scissor_rect{
        .left = 0,
        .top = 0,
        .right = static_cast<LONG>(win_dims.x),
        .bottom = static_cast<LONG>(win_dims.y),
    };
    cmd_list->RSSetViewports(1, &viewport);
    cmd_list->RSSetScissorRects(1, &scissor_rect);

    // Build TLAS
    build_tlas(cmd_list.Get());
    m_context.create_shader_resource_view_acceleration_structure(m_tlas.buffer, &m_rt_descriptors,
                                                                 0);
    // Dispatch rays to UAV render target
    {
        XMMATRIX view = XMMatrixIdentity();

        XMMATRIX proj = XMLoadFloat4x4(&m_matrix_stack.top(gl::GLMatrixMode::PROJECTION));

        XMMATRIX view_proj = XMMatrixMultiply(view, proj);

        XMMATRIX inverse_view_projection = XMMatrixInverse(nullptr, view_proj);

        RayGenConstantBuffer raygen_cb{
            .width = static_cast<float>(win_dims.x),
            .height = static_cast<float>(win_dims.y),
        };
        XMStoreFloat4x4(&raygen_cb.projection_matrix, XMMatrixTranspose(view_proj));
        XMStoreFloat4x4(&raygen_cb.inv_projection_matrix,
                        XMMatrixTranspose(inverse_view_projection));

        // Copy constant buffer to GPU
        auto raygen_cb_ptr = &m_raygen_constant_buffers[get_frame_index()];
        void* cb_ptr;
        THROW_IF_FALSE(m_context.map_buffer(raygen_cb_ptr, &cb_ptr));
        memcpy(cb_ptr, &raygen_cb, sizeof(RayGenConstantBuffer));
        m_context.unmap_buffer(raygen_cb_ptr);

        // Mark UAV texture for raytracing use and emit barrier
        THROW_IF_FALSE(m_context.mark_use(&m_uav_render_target, dx::Usage::UAV_RT));
        std::array rt_textures = { &m_uav_render_target };
        m_context.emit_barriers(cmd_list.Get(), nullptr, 0, rt_textures.data(), rt_textures.size());

        cmd_list->SetPipelineState1(m_rt_pipeline.Get());
        cmd_list->SetComputeRootSignature(m_rt_global_root_signature.Get());

        // TODO: Copy from a CPU descriptor heap instead of recreating each frame
        m_context.create_constant_buffer_view(&m_raygen_constant_buffers[get_frame_index()],
                                              &m_rt_descriptors, 2);

        // Set descriptor heap and table our descriptors
        m_context.set_descriptor_heap(cmd_list.Get(), m_rt_descriptor_heap);
        D3D12_GPU_DESCRIPTOR_HANDLE descriptor_table_handle{};
        m_rt_descriptor_heap.get_gpu_descriptor(&descriptor_table_handle, m_rt_descriptors.offset);
        cmd_list->SetComputeRootDescriptorTable(0, descriptor_table_handle);

        D3D12_GPU_VIRTUAL_ADDRESS shader_table_base_address = m_shader_table.get_gpu_address();

        D3D12_DISPATCH_RAYS_DESC dispatch_desc{
            .RayGenerationShaderRecord{
                .StartAddress = shader_table_base_address + m_raygen_shader_table_offset,
                .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            },
            .MissShaderTable{
                .StartAddress = shader_table_base_address + m_miss_shader_table_offset,
                .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                .StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            },
            .HitGroupTable{
                .StartAddress = shader_table_base_address + m_hit_group_shader_table_offset,
                .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                .StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            },
            .Width = win_dims.x,
            .Height = win_dims.y,
            .Depth = 1,
        };

        cmd_list->DispatchRays(&dispatch_desc);
    }

    // Copy the ray traced UAV texture into the current swapchain backbuffer, then render ImGui on top
    {
        constexpr D3D12_BARRIER_SUBRESOURCE_RANGE subresource_range{
            .IndexOrFirstMipLevel = 0,
            .NumMipLevels = 1,
            .FirstArraySlice = 0,
            .NumArraySlices = 1,
            .FirstPlane = 0,
            .NumPlanes = 1,
        };

        // Transition UAV texture from UAV to copy source
        THROW_IF_FALSE(m_context.mark_use(&m_uav_render_target, dx::Usage::COPY_SRC));
        std::array copy_textures = { &m_uav_render_target };
        m_context.emit_barriers(cmd_list.Get(), nullptr, 0, copy_textures.data(),
                                copy_textures.size());

        // Transition swapchain from PRESENT to COPY_DEST manually
        D3D12_TEXTURE_BARRIER swap_to_copy_dest{
            .SyncBefore = D3D12_BARRIER_SYNC_NONE,
            .SyncAfter = D3D12_BARRIER_SYNC_COPY,
            .AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS,
            .AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST,
            .LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT,
            .LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_DEST,
            .Subresources = subresource_range,
        };
        m_context.set_barrier_swapchain(&swap_to_copy_dest);
        D3D12_BARRIER_GROUP swap_barrier_group{
            .Type = D3D12_BARRIER_TYPE_TEXTURE,
            .NumBarriers = 1,
            .pTextureBarriers = &swap_to_copy_dest,
        };
        cmd_list->Barrier(1, &swap_barrier_group);

        m_context.copy_texture_to_swapchain(cmd_list.Get(), m_uav_render_target);

        // Transition swapchain from COPY_DEST to RENDER_TARGET
        D3D12_TEXTURE_BARRIER swap_to_rtv{
            .SyncBefore = D3D12_BARRIER_SYNC_COPY,
            .SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET,
            .AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
            .AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET,
            .LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST,
            .LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            .Subresources = subresource_range,
        };
        m_context.set_barrier_swapchain(&swap_to_rtv);
        D3D12_BARRIER_GROUP swap_rtv_barrier_group{
            .Type = D3D12_BARRIER_TYPE_TEXTURE,
            .NumBarriers = 1,
            .pTextureBarriers = &swap_to_rtv,
        };
        cmd_list->Barrier(1, &swap_rtv_barrier_group);

        // Transition UAV back to UAV layout for next frame
        THROW_IF_FALSE(m_context.mark_use(&m_uav_render_target, dx::Usage::UAV_RT));
        std::array post_copy_textures = { &m_uav_render_target };
        m_context.emit_barriers(cmd_list.Get(), nullptr, 0, post_copy_textures.data(),
                                post_copy_textures.size());
    }

    // Draw everything (ImGui over the copied ray traced image)
    D3D12_CPU_DESCRIPTOR_HANDLE swapchain_rtv{};
    m_swapchain_descriptors.heap->get_cpu_descriptor(&swapchain_rtv, m_swapchain_descriptors.offset
                                                                         + swapchain_idx);
    cmd_list->OMSetRenderTargets(1, &swapchain_rtv, FALSE, nullptr);

    // This is where rasterization could go
    // ex m_context.bind_vertex_buffers m_context.bind_index_buffer cmd_list->DrawIndexedInstanced
    // After setting the root signature and pipeline state and binding any resources

    // Render ImGui
    m_context.render_imgui_draw_data(cmd_list.Get());

    // Transition swapchain image to present
    {
        constexpr D3D12_BARRIER_SUBRESOURCE_RANGE subresource_range{
            .IndexOrFirstMipLevel = 0,
            .NumMipLevels = 1,
            .FirstArraySlice = 0,
            .NumArraySlices = 1,
            .FirstPlane = 0,
            .NumPlanes = 1,
        };

        D3D12_TEXTURE_BARRIER swapchain_present{
            .SyncBefore = D3D12_BARRIER_SYNC_DRAW,
            .SyncAfter = D3D12_BARRIER_SYNC_NONE,
            .AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET,
            .AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS,
            .LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            .LayoutAfter = D3D12_BARRIER_LAYOUT_PRESENT,
            .Subresources = subresource_range,
        };
        m_context.set_barrier_swapchain(&swapchain_present);

        D3D12_BARRIER_GROUP barrier_group{
            .Type = D3D12_BARRIER_TYPE_TEXTURE,
            .NumBarriers = 1,
            .pTextureBarriers = &swapchain_present,
        };
        cmd_list->Barrier(1, &barrier_group);
    }

    // Submit the command list
    auto current_fence_value = ++m_fence_frame_ready_val[get_frame_index()];  // Increment wait value
    THROW_IF_FALSE(SUCCEEDED(cmd_list->Close()));
    const std::array<ID3D12CommandList*, 1> lists = { cmd_list.Get() };
    m_gfx_queue.queue->ExecuteCommandLists(1, lists.data());

    // End of all work for queue, signal fence
    THROW_IF_FALSE(
        SUCCEEDED(m_gfx_queue.queue->Signal(m_fence_frame_ready.fence.Get(), current_fence_value)));

    // This must be called after EndFrame/Render but can be called after command list submission
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    m_context.present();

    increment_frame_index();

    // If next frame is ready to be used, otherwise wait
    if (m_fence_frame_ready.fence->GetCompletedValue() < m_fence_frame_ready_val[get_frame_index()])
    {
        dx::WaitInfo wait_info{ .wait_all = true,
                                .count = 1,
                                .fences = &m_fence_frame_ready,
                                .values = &current_fence_value,
                                .timeout = INFINITE };
        THROW_IF_FALSE(m_context.wait_fences(wait_info));
    }
    m_fence_frame_ready_val[get_frame_index()] = current_fence_value + 1;
}

void glRemix::glRemixRenderer::destroy()
{
    m_context.destroy_imgui();
    m_rt_descriptor_heap.deallocate(&m_rt_descriptors);
}

void glRemix::glRemixRenderer::create_uav_rt()
{
    XMUINT2 win_dims{};
    THROW_IF_FALSE(m_context.get_window_dimensions(&win_dims));

    const dx::TextureDesc uav_rt_desc{
        .width = win_dims.x,
        .height = win_dims.y,
        .depth_or_array_size = 1,
        .mip_levels = 1,
        .format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .is_render_target = false,
    };

    THROW_IF_FALSE(m_context.create_texture(uav_rt_desc, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
                                            &m_uav_render_target, nullptr, "UAV and RT texture"));
}

glRemix::glRemixRenderer::~glRemixRenderer() {}
