#include "debug_window.h"
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
            if (ImGui::BeginTabItem("Debug Log"))
            {
                render_debug_log();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (ImGui::BeginTabBar("Asset Replacement"))
        {
            if (ImGui::BeginTabItem("Mesh IDs"))
            {
                render_mesh_ids();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
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
    ImGui::Text("Asset Replacement");
    ImGui::Text("List of Assets");

    // render meshIDs and get selected mesh
    if (ImGui::BeginListBox("##assets"))
    {
        for (auto it = m_mesh_map->begin(); it != m_mesh_map->end();)
        {
            auto meshID = it->first;
            auto& mesh = it->second;

            ImGui::PushID(meshID);

            ImGui::BeginGroup();

            const bool is_selected = (m_meshID_to_replace == meshID);
            char buf[64];
            snprintf(buf, 64, "Mesh ID: %llu", meshID);
            if (ImGui::Selectable(buf, is_selected))
            {
                m_meshID_to_replace = meshID;
            }

            ImGui::SameLine();

            bool visible = mesh.visible;
            if (ImGui::Checkbox("##visible", &visible))
            {
                // toggle visibility
                m_mesh_map->at(meshID).visible = visible;
            }

            ImGui::EndGroup();

            ImGui::PopID();

            ++it;
        }

        ImGui::EndListBox();
    }

    // handle asset replacement with selected mesh
    if (m_meshID_to_replace != -1)
    {
        ImGui::Separator();

        // get new asset path from user input
        ImGui::InputText("Replacement Asset Path", m_asset_path_buffer, sizeof(m_asset_path_buffer));

        // if button is pressed to replace asset, call replace_mesh from rt_app
        if (ImGui::Button("Replace Asset"))
        {
            ImGui::Text("%s", m_asset_path_buffer);
            if (m_replace_mesh_callback)
            {
                m_replace_mesh_callback(m_meshID_to_replace, m_asset_path_buffer);
            }
        }
    }
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
    // TODO: Reload environment map, shaders, etc
}

void DebugWindow::render_debug_log()
{
    // TODO: Debug messages
}
