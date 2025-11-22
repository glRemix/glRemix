#pragma once

#include "structs.h"
#include <tsl/robin_map.h>
#include "gl/gl_matrix_stack.h"
#include <array>
#include <vector>

namespace glRemix
{
class glState
{
public:
    UINT32 m_current_frame;
    size_t m_offset;  // tracked by state for display list purposes

    HWND hwnd;
    bool m_create_context;

    XMFLOAT4 m_color = { 1.0f, 1.0f, 1.0f, 1.0f };
    XMFLOAT3 m_normal = { 0.0f, 0.0f, 1.0f };  // Default according to spec
    XMFLOAT2 m_uv = { 0.0f, 0.0f };
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

    // geometry
    UINT32 m_topology = GL_QUADS;
    std::vector<Vertex> t_vertices;
    std::vector<UINT32> t_indices;
    std::vector<MeshRecord> m_meshes;

    tsl::robin_map<UINT64, MeshRecord> m_mesh_map;
    UINT32 m_num_mesh_resources;
    std::vector<PendingGeometry> m_pending_geometries;
};
}  // namespace glRemix
