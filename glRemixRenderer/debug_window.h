#pragma once

#include <imgui.h>
#include <vector>
#include <functional>
#include <string>

#include "application.h"
#include "dx/d3d12_as.h"
#include "gl/gl_matrix_stack.h"
#include <DirectXMath.h>

#include "structs.h"
#include "tsl/robin_map.h"

namespace glRemix
{
class DebugWindow
{
    float m_fps = 0.0f;

    const std::vector<MeshRecord>* m_meshes = nullptr;
    uint64_t m_mesh_ID_to_replace = -1;
    char m_asset_path_buffer[256] = "";
    std::function<void(uint64_t meshID, const char* asset_path)>
        m_replace_mesh_callback;  // replace_mesh function from rt_app

    void render_performance_stats();
    void render_settings();
    void render_debug_log();
    void render_mesh_ids();

public:
    struct
    {
        bool unlocked = false;
    } m_parameters;

    void render();

    void set_mesh_buffer(std::vector<MeshRecord>& meshes);
    void set_replace_mesh_callback(
        std::function<void(uint64_t meshID, const char* asset_path)> callback);
};
}  // namespace glRemix
