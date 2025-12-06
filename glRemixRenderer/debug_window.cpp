#include "debug_window.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <cstdio>
#include <commdlg.h>

#include <shared/debug_utils.h>
#include <shared/math_utils.h>

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

    if (ImGui::GetCurrentContext() != nullptr)
    {
        // forward to ImGui
        static ImGuiIO& io = ImGui::GetIO();

        if (ImGui_ImplWin32_WndProcHandlerEx(hwnd, msg, wParam, lParam, io))
        {
            return 1;
        }
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

    if (m_dialog_thread.joinable())
    {
        // HANDLE_LOGIC_ERROR("glRemixRenderer - Dialog thread has not terminated properly.");
        // TODO: consistent shutdown of dialog thread.
        m_dialog_thread.join();
    }
}

void DebugWindow::render(const DebugInfo debug_info)
{
    ImGui::SetNextWindowPos(ImVec2(mk_initialPosY, mk_initialPosY), ImGuiCond_Once);

    bool asset_tab_active = false;
    if (ImGui::Begin(k_IMGUI_MAIN_WINDOW_ID, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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
                asset_tab_active = true;
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

    if (m_is_mesh_options_window_active &= asset_tab_active; m_is_mesh_options_window_active)
    {
        render_mesh_options_window(debug_info.m_mesh_map);
    }

    ImGui::End();
}

// get replace_mesh function from rt_app
void DebugWindow::set_replace_mesh_callback(
    std::function<bool(UINT64 mesh_id, const CHAR* asset_path)> callback)
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

            // for mesh options window
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                m_is_mesh_options_window_active = true;
                m_mesh_ID_to_replace = mesh_id;
                m_show_mesh_replacement_error_message = false;  // reset error status
            }
            else if (ImGui::Selectable(buf, is_selected))
            {
                m_mesh_ID_to_replace = mesh_id;
                m_show_mesh_replacement_error_message = false;  // reset error status
            }
        }
        ImGui::EndListBox();
    }
}

/**
 * @brief
 * @param local_hwnd
 * @param out_encoded_path: Win32 functions use UTF-16
 * @return
 */
static bool s_open_file_dialog_native_win32(const HWND& local_hwnd,
                                            std::array<WCHAR, 256>* out_encoded_path)
{
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrTitle = k_WIN32_FILE_DIALOG_WINDOW_TITLE;
    ofn.hwndOwner = local_hwnd;
    ofn.lpstrFilter = L"GLTF Files\0*.gltf;*.glb\0";
    ofn.lpstrFile = static_cast<LPTSTR>(out_encoded_path->data());
    ofn.nMaxFile = static_cast<DWORD>(out_encoded_path->size());
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    return GetOpenFileNameW(&ofn);
}

void DebugWindow::render_mesh_options_window(tsl::robin_map<UINT64, MeshRecord>* m_mesh_map)
{
    // check if mesh still exists in map
    if (!m_mesh_map->contains(m_mesh_ID_to_replace))
    {
        m_is_mesh_options_window_active = false;
        m_mesh_ID_to_replace = ~0ull;
        return;
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0));

    if (ImGui::BeginChild(k_IMGUI_ASSET_OPTIONS_WINDOW_ID, ImVec2(0, 0),
                          ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
    {
        static CHAR identifier[96];
        snprintf(identifier, sizeof(identifier), "Asset Options: %llu", m_mesh_ID_to_replace);

        if (ImGui::CollapsingHeader(identifier, &m_is_mesh_options_window_active,
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& mesh = m_mesh_map->at(m_mesh_ID_to_replace);

            // visibility toggle option
            ImGui::Checkbox("Toggle Visibility", &mesh.visible);

            ImGui::Separator();

            ImGui::TextUnformatted("Asset Replacement - Path to New GLTF File:");
            ImGui::InputText("##ReplacementPath", m_asset_path.data(), sizeof(m_asset_path));

            ImGui::SameLine();
            if (ImGui::Button("Browse"))
            {
                if (!m_dialog_open.exchange(true))
                {
                    static std::array<WCHAR, 256> encoded_path = {};
                    utf8_to_wide(m_asset_path.data(), encoded_path.data(), encoded_path.size());

                    auto handle_file_dialog_threading_fn = [this]()
                    {
                        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

                        if (SUCCEEDED(hr))
                        {
                            if (s_open_file_dialog_native_win32(this->m_hwnd, &encoded_path))
                            {
                                wide_to_utf8(encoded_path.data(), this->m_asset_path.data(),
                                             this->m_asset_path.size());
                            }
                            this->m_dialog_open.store(false);
                        }

                        CoUninitialize();
                    };

                    if (m_dialog_thread.joinable())
                    {
                        m_dialog_thread.join();
                    }
                    m_dialog_thread = std::thread(handle_file_dialog_threading_fn);
                }
            }

            if (ImGui::Button("Replace Asset"))
            {
                if (m_replace_mesh_callback)
                {
                    m_show_mesh_replacement_error_message
                        = !m_replace_mesh_callback(m_mesh_ID_to_replace, m_asset_path.data());
                }
            }

            static constexpr CHAR k_MESH_REPLACEMENT_FAILED_TEXT[]
                = "Entered path is not a valid GLTF or GLB file.\0";
            static constexpr ImVec4 k_MESH_REPLACEMENT_FAILED_TEXT_COLOR(1.0f, 0.0f, 0.0f, 1.0f);
            if (m_show_mesh_replacement_error_message)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, k_MESH_REPLACEMENT_FAILED_TEXT_COLOR);
                ImGui::TextUnformatted(k_MESH_REPLACEMENT_FAILED_TEXT);
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::EndChild();
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

    ImGui::BeginChild("##DebugLogChild", ImVec2(0, ImGui::GetTextLineHeight() * 20.0f),
                      ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_HorizontalScrollbar);

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
