#pragma once

#include <tsl/robin_map.h>
#include <array>
#include <vector>

#include "generated/glext_enums.inl"

#include "gl/gl_matrix_stack.h"

#include "structs.h"

namespace glRemix
{

struct glState
{
    UINT64 m_current_frame = ~0u;

    XMFLOAT4 m_color = { 1.0f, 1.0f, 1.0f, 1.0f };
    XMFLOAT3 m_normal = { 0.0f, 0.0f, 1.0f };  // Default according to spec
    XMFLOAT2 m_uv = { 0.0f, 0.0f };
    XMFLOAT2 m_uv2 = { 0.0f, 0.0f };
    XMFLOAT4 m_clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
    Material m_material{};  // global material
    size_t m_offset = ~0u;  // tracked by state for display list purposes

    HWND m_host_hwnd = nullptr;
    const HWND m_local_hwnd;  // hwnd created for this process

    bool m_create_context = false;
    bool m_swapchain_creation_deferred = false;

    // display lists
    bool m_in_call = false;
    UINT32 m_execution_mode = GL_COMPILE_AND_EXECUTE;
    UINT32 m_list_index = ~0u;
    size_t m_display_list_begin = ~0u;
    void* m_buffer_begin = nullptr;

    tsl::robin_map<int, std::vector<UINT8>> m_display_lists{};

    // cached structs
    std::vector<Material> m_materials{};
    std::vector<XMFLOAT4X4> m_matrix_pool{};

    // lighting
    std::array<Light, 8> m_lights{};
    bool m_lighting = false;  // TODO use this to somehow enable or disable lighting
                              // potential ideas could involve passing a root constant to the shader

    // matrix
    gl::glMatrixStack m_matrix_stack;
    UINT32 m_matrix_mode = GL_MODELVIEW;
    bool m_perspective = true;

    // geometry
    UINT32 m_topology = GL_QUADS;
    std::vector<Vertex> t_vertices{};
    std::vector<UINT32> t_indices{};
    std::vector<MeshRecord> m_meshes{};

    tsl::robin_map<UINT64, MeshRecord> m_mesh_map{};
    UINT32 m_num_mesh_resources = ~0u;
    std::vector<PendingGeometry> m_pending_geometries{};

#ifdef GLREMIX_DYNAMIC_MESH_CAP
    size_t m_last_rendered_mesh_count = ~0u;
    static constexpr float MESH_CAP_RATIO = 2.0f;
#endif

    // textures
    bool m_texture_2d = false;
    UINT32 m_num_textures = 0;
    tsl::robin_map<UINT32, UINT32> m_texture_indices{};
    tsl::robin_map<UINT32, PendingTexture> m_pending_textures{};

    // multitextures
    UINT32 m_active_texture = GL_TEXTURE0_ARB;
    tsl::robin_map<UINT32, UINT32> m_texture_binds{};
    tsl::robin_map<UINT32, UINT32> m_enabled_textures{};  // misleading name but stores whether or not
                                                          // each active texture arb slot is enabled

    explicit glState(const HWND local_hwnd) : m_local_hwnd(local_hwnd) {}
};
}  // namespace glRemix
