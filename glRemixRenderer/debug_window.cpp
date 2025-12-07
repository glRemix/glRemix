#include "debug_window.h"

#include <utility>
#include <imgui.h>
#include <imgui_internal.h>
#include "debug_log.h"
#include <cstdio>

using namespace glRemix;

void DebugWindow::render(const DebugInfo debug_info)
{
    if (ImGui::Begin("glRemix", nullptr, 0))
    {
        if (ImGui::BeginTabBar("DebugTabs"))
        {
            if (ImGui::BeginTabItem("Performance"))
            {
                render_performance_stats();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Render Settings"))
            {
                render_settings();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Asset List"))
            {
                const auto& mesh_records = debug_info.mesh_records;
                render_mesh_ids(mesh_records.records, mesh_records.count);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug Log"))
            {
                render_debug_log();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    render_mesh_options_window(debug_info.m_mesh_map);
    ImGui::End();
}

// get replace_mesh function from rt_app
void DebugWindow::set_replace_mesh_callback(
    std::function<void(UINT64 mesh_id, const char* asset_path)> callback)
{
    m_replace_mesh_callback = callback;
}

void DebugWindow::set_mesh_stats(const MeshStats& mesh_stats)
{
    m_mesh_stats = mesh_stats;
}

void DebugWindow::render_mesh_ids(const MeshRecord* records, const size_t count)
{
    ImGui::Text("List of Assets - Double Click for Asset Options");

    // render meshIDs and get selected mesh
    if (ImGui::BeginListBox("##assets"))
    {
        for (size_t i = 0; i < count; i++)
        {
            const auto mesh_id = records[i].mesh_id;

            const bool is_selected = (m_mesh_ID_to_replace == mesh_id);
            char buf[64];
            snprintf(buf, 64, "Asset ID: %llu", mesh_id);

            if (ImGui::Selectable(buf, is_selected))
            {
                m_mesh_ID_to_replace = mesh_id;
            }

            // for mesh options window
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                m_selected_mesh_for_window = mesh_id;
            }
        }
        ImGui::EndListBox();
    }
}

void DebugWindow::render_mesh_options_window(tsl::robin_map<UINT64, MeshRecord>* m_mesh_map)
{
    if (m_selected_mesh_for_window == ~0ull)
    {
        return;
    }

    // check if mesh still exists in map
    if (!m_mesh_map->contains(m_selected_mesh_for_window))
    {
        m_selected_mesh_for_window = ~0ull;
        return;
    }

    char title[64];
    snprintf(title, sizeof(title), "Asset Options: %llu", m_selected_mesh_for_window);

    // find position of main window
    ImGuiWindow* main_imgui = ImGui::FindWindowByName("glRemix");
    if (main_imgui)
    {
        ImVec2 pos = main_imgui->Pos;
        ImVec2 size = main_imgui->Size;
        ImVec2 new_pos(pos.x + size.x + 10.0f, pos.y);

        ImGui::SetNextWindowPos(new_pos, ImGuiCond_Always);
    }

    bool is_open = true;
    if (ImGui::Begin(title, &is_open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        auto& mesh = m_mesh_map->at(m_selected_mesh_for_window);

        // visibility toggle option
        ImGui::Checkbox("Visible", &mesh.visible);

        ImGui::Separator();

        ImGui::Text("Asset Replacement - Path to New GLTF File:");
        ImGui::InputText("##ReplacementPath", m_asset_path.data(), sizeof(m_asset_path));

        if (ImGui::Button("Replace Asset"))
        {
            if (m_replace_mesh_callback)
            {
                m_replace_mesh_callback(m_selected_mesh_for_window, m_asset_path.data());
            }
        }
    }

    ImGui::End();

    if (!is_open)
    {
        m_selected_mesh_for_window = ~0ull;
    }
}

void DebugWindow::render_performance_stats()
{
    const ImGuiIO& io = ImGui::GetIO();
    m_fps = io.Framerate;

    ImGui::Text("FPS: %.1f (%.3f ms/frame)", m_fps, 1000.0f / m_fps);
    ImGui::Separator();

    ImGui::Text("Meshes Rendered: %zu", m_mesh_stats.num_meshes_rendered);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted("This number is an upper bound; the actual number of rendered "
                               "meshes is less than or equal to this value.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    ImGui::Text("Mesh Count: %zu", m_mesh_stats.num_meshes);

    ImGui::Text("Texture Count: %zu", m_mesh_stats.num_textures);
    ImGui::Text("Material Count: %zu", m_mesh_stats.num_materials);
    // TODO: More stats like heap allocations, allocate descriptors, memory usage, etc
}

void DebugWindow::render_settings()
{
    ImGui::Checkbox("Unlock FPS", &m_parameters.unlocked);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(
            "Turn Vsync off and unlock the framerate (assuming your device supports tearing)");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    ImGui::Checkbox("Mirror Mode", &m_parameters.mirror_mode);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted("Transform random materials into perfectly specular materials");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    ImGui::BeginDisabled(!m_parameters.mirror_mode);
    ImGui::Indent();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderFloat("##p", &m_parameters.mirror_threshold, 0.0f, 1.0f, "%.2f");
    ImGui::Unindent();
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted("Proportion of materials to become mirrors");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::EndDisabled();

    ImGui::Checkbox("Lock Perspective", &m_parameters.perspective_locked);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(
            "Force game perspective matrix. NOTE: This option is subject for deprecation.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    // TODO: Reload environment map, shaders, etc
}

void DebugWindow::render_debug_log()
{
    static bool auto_scroll = true;
    ImGui::Checkbox("Auto-scroll", &auto_scroll);
    ImGui::Separator();

    ImGui::BeginChild("##DebugLogChild", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    const auto cur = dbglog_current_seq();
    const auto& log = get_debug_log();
    const auto start = cur > DebugLog::k_capacity ? cur - DebugLog::k_capacity : 0;

    for (auto seq = start; seq < cur; ++seq)
    {
        const auto idx = static_cast<UINT32>(seq % DebugLog::k_capacity);
        const auto& e = log.buffer[idx];
        // Only show if published and matches expected sequence
        if (e.seq == seq + 1)
        {
            if (std::strncmp(e.text, "ERROR:", 6) == 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", e.text);
            }
            else if (std::strncmp(e.text, "WARN:", 5) == 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", e.text);
            }
            else
            {
                ImGui::TextUnformatted(e.text);
            }
        }
    }

    if (auto_scroll)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}
