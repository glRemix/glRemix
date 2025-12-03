#include "debug_window.h"

#include <utility>
#include <imgui.h>

using namespace glRemix;

void DebugWindow::render(DebugInfo debug_info)
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
            if (ImGui::BeginTabItem("Asset Replacement"))
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
    ImGui::End();
}

// get replace_mesh function from rt_app
void DebugWindow::set_replace_mesh_callback(
    std::function<void(uint64_t mesh_id, const char* asset_path)> callback)
{
    m_replace_mesh_callback = callback;
}

void DebugWindow::set_mesh_stats(const MeshStats& mesh_stats)
{
    m_mesh_stats = mesh_stats;
}

void DebugWindow::render_mesh_ids(const MeshRecord* records, const size_t count)
{
    ImGui::Text("List of Meshes ");

    // render meshIDs and get selected mesh
    if (ImGui::BeginListBox("##assets"))
    {
        for (size_t i = 0; i < count; i++)
        {
            const auto mesh_id = records[i].mesh_id;

            const bool is_selected = (m_mesh_ID_to_replace == mesh_id);
            char buf[64];
            snprintf(buf, 64, "Mesh ID: %llu", mesh_id);
            if (ImGui::Selectable(buf, is_selected))
            {
                m_mesh_ID_to_replace = mesh_id;
            }
        }
        ImGui::EndListBox();
    }

    // handle asset replacement with selected mesh
    if (std::cmp_not_equal(m_mesh_ID_to_replace, -1))
    {
        ImGui::Separator();

        // get new asset path from user input
        ImGui::InputText("Replacement Asset Path", m_asset_path.data(), m_asset_path.size());

        // if button is pressed to replace asset, call replace_mesh from rt_app
        if (ImGui::Button("Replace Asset"))
        {
            ImGui::Text("%s", m_asset_path);
            if (m_replace_mesh_callback)
            {
                m_replace_mesh_callback(m_mesh_ID_to_replace, m_asset_path.data());
            }
        }
    }
}

void DebugWindow::render_performance_stats()
{
    const ImGuiIO& io = ImGui::GetIO();
    m_fps = io.Framerate;

    ImGui::Text("FPS: %.1f (%.3f ms/frame)", m_fps, 1000.0f / m_fps);
    ImGui::Separator();
    ImGui::Text("Meshes Rendered: %zu", m_mesh_stats.num_meshes);
    ImGui::Text("Textures: %zu", m_mesh_stats.num_textures);
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
    // TODO: Reload environment map, shaders, etc
}

void DebugWindow::render_debug_log()
{
    // TODO: Debug messages
}
