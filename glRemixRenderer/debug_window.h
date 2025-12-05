#pragma once

#include <functional>

#include "structs.h"
#include <array>
#include <thread>
#include <tsl/robin_map.h>

namespace glRemix
{
constexpr WCHAR k_LOCAL_WINDOW_CLASS_DEFAULT_NAME[] = L"glRemix Window Class";
constexpr WCHAR k_LOCAL_WINDOW_DEFAULT_TEXT[] = L"glRemix Window";

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

        tsl::robin_map<UINT64, MeshRecord>* m_mesh_map = nullptr;
    };

private:
    MeshStats m_mesh_stats{};

    float m_fps = 0.0f;

    UINT64 m_mesh_ID_to_replace = UINT_MAX;
    std::array<CHAR, 256> m_asset_path{};
    std::function<void(UINT64 mesh_id, const CHAR* asset_path)>
        m_replace_mesh_callback;  // replace_mesh function from rt_app

    UINT64 m_selected_mesh_for_window = ~0ull;

    std::atomic<bool> m_dialog_open{ false };  // prevents multiple dialogs
    std::thread m_dialog_thread;

    const UINT32 mk_initialPoxX = 0;
    const UINT32 mk_initialPosY = 0;

    void render_performance_stats();
    void render_settings();
    void render_debug_log();
    void render_mesh_ids(const MeshRecord* records, size_t count);
    void render_mesh_options_window(tsl::robin_map<UINT64, MeshRecord>* m_mesh_map);

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
        std::function<void(UINT64 mesh_id, const CHAR* asset_path)> callback);
    void set_mesh_stats(const MeshStats& mesh_stats);

    void destroy();
};
}  // namespace glRemix
