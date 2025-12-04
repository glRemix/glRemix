#pragma once

#include "structs.h"
#include <tsl/robin_map.h>
#include "gl/gl_matrix_stack.h"
#include <array>
#include <vector>

namespace glRemix
{
// TODO link gl driver side to glext_enums.inl
#define GL_TEXTURE0_ARB 0x84C0
#define GL_TEXTURE1_ARB 0x84C1
#define MAX_TEXTURES 2

class glState
{
public:
    UINT64 m_current_frame;
    size_t m_offset;  // tracked by state for display list purposes

    HWND hwnd;
    bool m_create_context;
    bool m_swapchain_creation_deferred = false;

    XMFLOAT4 m_color = { 1.0f, 1.0f, 1.0f, 1.0f };
    XMFLOAT3 m_normal = { 0.0f, 0.0f, 1.0f };  // Default according to spec
    XMFLOAT2 m_uv = { 0.0f, 0.0f };
    XMFLOAT2 m_uv2 = { 0.0f, 0.0f };
    XMFLOAT4 m_clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
    Material m_material;  // global material

    // display lists
    bool m_in_call = false;
    UINT32 m_execution_mode = GL_COMPILE_AND_EXECUTE;
    UINT32 m_list_index = 0;
    size_t m_display_list_begin = 0;
    void* m_buffer_begin;

    tsl::robin_map<int, std::vector<UINT8>> m_display_lists;

    // cached structs
    std::vector<Material> m_materials;
    std::vector<XMFLOAT4X4> m_matrix_pool;

    // lighting
    std::array<Light, 8> m_lights{};
    bool m_lighting;  // TODO use this to somehow enable or disable lighting
                      // potential ideas could involve passing a root constant to the shader

    // matrix
    gl::glMatrixStack m_matrix_stack;
    UINT32 m_matrix_mode = GL_MODELVIEW;
    bool m_perspective = true;

    // geometry
    UINT32 m_topology = GL_QUADS;
    std::vector<Vertex> t_vertices;
    std::vector<UINT32> t_indices;
    std::vector<MeshRecord> m_meshes;

    tsl::robin_map<UINT64, MeshRecord> m_mesh_map;
    UINT32 m_num_mesh_resources;
    std::vector<PendingGeometry> m_pending_geometries;

#ifdef GLREMIX_DYNAMIC_MESH_CAP
    size_t m_last_rendered_mesh_count = 0;
    static constexpr float MESH_CAP_RATIO = 2.0f;
#endif

    // textures
    bool m_texture_2d;
    UINT32 m_num_textures;
    tsl::robin_map<UINT32, UINT32> m_texture_indices;
    tsl::robin_map<UINT32, PendingTexture> m_pending_textures;
    tsl::robin_map<UINT32, MeshRecord>
        m_mesh_replacement_tracker;  // maps index in m_meshes of mesh to be replaced, with
                                     // replacement mesh

    // multitextures
    UINT32 m_active_texture = GL_TEXTURE0_ARB;
    tsl::robin_map<UINT32, UINT32> m_texture_binds;
    tsl::robin_map<UINT32, UINT32> m_enabled_textures;  // misleading name but stores whether or not
                                                        // each active texture arb slot is enabled
};
}  // namespace glRemix
