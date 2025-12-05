#include "debug_window.h"

#include <utility>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <cstdio>

#include <shared/debug_utils.h>

#include "debug_log.h"

using namespace glRemix;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(HWND hWnd, UINT msg, WPARAM wParam,
                                                               LPARAM lParam, ImGuiIO& io);

LRESULT CALLBACK s_local_window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        default: break;  // TODO: case on msg for custom input handling
    }

    // forward to ImGui
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui_ImplWin32_WndProcHandlerEx(hwnd, msg, wParam, lParam, io))
    {
        return 1;
    }

    // it is required to default handle all callbacks that have reached this point
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void DebugWindow::init_imgui_frontends()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASSEXW wc = {};

    wc.lpfnWndProc = s_local_window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = k_LOCAL_WINDOW_CLASS_DEFAULT_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.cbSize = sizeof(WNDCLASSEXW);

    THROW_IF_FALSE(RegisterClassExW(&wc));  // register the class

    m_hwnd = CreateWindowEx(0, k_LOCAL_WINDOW_CLASS_DEFAULT_NAME, k_LOCAL_WINDOW_DEFAULT_TEXT,
                            WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                            nullptr, nullptr, hInstance, nullptr);

    THROW_IF_FALSE(m_hwnd);
    SetFocus(m_hwnd);

    THROW_IF_FALSE(ImGui_ImplWin32_Init(m_hwnd));

    ImGuiViewport* main_vp = ImGui::GetMainViewport();
    main_vp->PlatformHandle = (void*)m_hwnd;
    main_vp->PlatformHandleRaw = (void*)m_hwnd;
}

void glRemix::DebugWindow::destroy()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    UnregisterClassW(k_LOCAL_WINDOW_CLASS_DEFAULT_NAME, GetModuleHandle(nullptr));
}

void DebugWindow::render(const DebugInfo debug_info)
{
    ImGui::SetNextWindowPos(ImVec2(mk_initialPosY, mk_initialPosY), ImGuiCond_Once);
    if (ImGui::Begin("glRemix", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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
            if (ImGui::BeginTabItem("Toggle Mesh Visibility"))
            {
                render_mesh_visibility();
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

void DebugWindow::render_mesh_visibility()
{
    ImGui::Text("Toggle Mesh Visibility ");
    if (ImGui::BeginListBox("##mesh_visibility"))
    {
        if (m_mesh_map && !m_mesh_map->empty())
        {
            for (auto it = m_mesh_map->begin(); it != m_mesh_map->end();)
            {
                auto mesh_id = it->first;
                ImGui::PushID(reinterpret_cast<void*>(mesh_id));
                bool& visible = m_mesh_map->at(mesh_id).visible;
                char buf[64];
                snprintf(buf, 64, "Mesh ID: %llu", mesh_id);
                ImGui::Checkbox(buf, &visible);
                ImGui::PopID();
                ++it;
            }
        }
        else
        {
            ImGui::Text("Zero meshes available to toggle.");
        }
        ImGui::EndListBox();
    }
}

void DebugWindow::render_performance_stats()
{
    const ImGuiIO& io = ImGui::GetIO();
    m_fps = io.Framerate;

    ImGui::Text("FPS: %.1f (%.3f ms/frame)", m_fps, 1000.0f / m_fps);
    ImGui::Separator();
    ImGui::Text("Meshes Rendered: %zu", m_mesh_stats.num_meshes_rendered);
    ImGui::Text("Mesh Count: %zu", m_mesh_stats.num_meshes);
    ImGui::Text("Texture Count: %zu", m_mesh_stats.num_textures);
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
