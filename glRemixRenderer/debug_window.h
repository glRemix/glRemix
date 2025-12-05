#pragma once

#include <functional>

#include "structs.h"
#include <array>
#include <tsl/robin_map.h>

namespace glRemix
{
constexpr wchar_t k_LOCAL_WINDOW_CLASS_DEFAULT_NAME[] = L"glRemix Debug Window Class";
constexpr wchar_t k_LOCAL_WINDOW_DEFAULT_TEXT[] = L"glRemix Debug Window";

class DebugWindow
{
public:
    struct MeshStats
    {
        size_t num_meshes_rendered;
        size_t num_meshes;
        size_t num_textures;
    };

    struct DebugInfo
    {
        struct
        {
            MeshRecord* records;
            size_t count;
        } mesh_records;  // Array for replacement
    };

private:
    MeshStats m_mesh_stats{};

    float m_fps = 0.0f;

    tsl::robin_map<UINT64, MeshRecord>* m_mesh_map = nullptr;
    UINT64 m_mesh_ID_to_replace = -1;
    std::array<char, 256> m_asset_path{};
    std::function<void(UINT64 mesh_id, const char* asset_path)>
        m_replace_mesh_callback;  // replace_mesh function from rt_app

    const uint32_t m_initialSizeX = 600;
    const uint32_t m_initialSizeY = 400;

    void render_performance_stats();
    void render_settings();
    void render_debug_log();
    void render_mesh_ids(const MeshRecord* records, size_t count);
    void render_mesh_visibility();

public:
    HWND m_hwnd;

    struct
    {
        bool unlocked = false;
        bool mirror_mode = false;
        float mirror_threshold = 0.5f;
        bool perspective_locked = false;
    } m_parameters;

    void init_imgui_frontends();

    void render(DebugInfo debug_info);

    void set_replace_mesh_callback(
        std::function<void(UINT64 mesh_id, const char* asset_path)> callback);
    void set_mesh_stats(const MeshStats& mesh_stats);
};
}  // namespace glRemix
