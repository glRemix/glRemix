#include "debug_window.h"

#include <utility>
#include "imgui.h"

using namespace glRemix;

void DebugWindow::render()
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
                render_mesh_ids();
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
    render_mesh_options_window();
    ImGui::End();
}

// get mesh buffer from rt_app
void DebugWindow::set_mesh_buffer(std::vector<MeshRecord>& meshes,
                                  tsl::robin_map<UINT64, MeshRecord>& mesh_map)
{
    m_meshes = &meshes;
    m_mesh_map = &mesh_map;
}

// get replace_mesh function from rt_app
void DebugWindow::set_replace_mesh_callback(
    std::function<void(uint64_t meshID, const char* asset_path)> callback)
{
    m_replace_mesh_callback = callback;
}

void DebugWindow::render_mesh_ids()
{
    ImGui::Text("List of Assets - Double Click for Asset Options");

    // render meshIDs and get selected mesh
    if (ImGui::BeginListBox("##assets"))
    {
        for (auto it = m_mesh_map->begin(); it != m_mesh_map->end();)
        {
            auto mesh_id = it->first;
            auto& mesh = it->second;

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

            ++it;
        }
        ImGui::EndListBox();
    }

}

void DebugWindow::render_mesh_options_window()
{
    if (m_selected_mesh_for_window == static_cast<uint64_t>(-1))
    {
        return;
    }

    // check if mesh still exists in map
    if (!m_mesh_map->contains(m_selected_mesh_for_window))
    {
        m_selected_mesh_for_window = static_cast<uint64_t>(-1);
        return;
    }

    char title[64];
    snprintf(title, sizeof(title), "Asset Options: %llu", m_selected_mesh_for_window);

    ImGui::Begin(title, nullptr, 0);

    auto& mesh = m_mesh_map->at(m_selected_mesh_for_window);

    // visibility toggle option
    ImGui::Checkbox("Visible", &mesh.visible);

    ImGui::Separator();

    ImGui::Text("Asset Replacement - Path to New GLTF File:");
    ImGui::InputText("##ReplacementPath", m_asset_path_buffer, sizeof(m_asset_path_buffer));

    if (ImGui::Button("Replace Asset"))
    {
        if (m_replace_mesh_callback)
        {
            m_replace_mesh_callback(m_selected_mesh_for_window, m_asset_path_buffer);
        }
    }

    ImGui::Separator();

    // close window button
    if (ImGui::Button("Close"))
    {
        m_selected_mesh_for_window = static_cast<uint64_t>(-1);
    }

    ImGui::End();
}

void DebugWindow::render_performance_stats()
{
    const ImGuiIO& io = ImGui::GetIO();
    m_fps = io.Framerate;

    ImGui::Text("FPS: %.1f (%.3f ms/frame)", m_fps, 1000.0f / m_fps);
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
    // TODO: Debug messages
}
