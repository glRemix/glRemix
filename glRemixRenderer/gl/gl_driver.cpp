#include "gl_driver.h"
#include "gl_command_utils.h"
#include <shared/gl_utils.h>

#include <Windows.h>
LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace glRemix
{
static void hash_and_commit_geometry(glState& state, const size_t* client_indices = nullptr)
{
    if (!state.m_perspective || state.t_indices.empty())
    {
        state.t_vertices.clear();
        state.t_indices.clear();
        return;
    }

    // bounding box variables
    XMFLOAT3 min_bb = { 100.0, 100.0, 100.0 };
    XMFLOAT3 max_bb = { -100.0, -100.0, -100.0 };

    // hashing - logic from boost::hash_combine
    size_t seed = 0;
    auto hash_combine = [&seed](auto const& v)
    {
        seed ^= std::hash<std::decay_t<decltype(v)>>{}(v) + 0x9e3779b97f4a7c15ULL + (seed << 6)
                + (seed >> 2);
    };

    // reduces floating point instability
    auto quantize = [](const float v, const float precision = 1e-5f) -> float
    { return std::round(v / precision) * precision; };

    // get vertex data to hash
    for (int i = 0; i < state.t_vertices.size(); ++i)
    {
        const Vertex& vertex = state.t_vertices[i];
        hash_combine(quantize(vertex.position.x));
        hash_combine(quantize(vertex.position.y));
        hash_combine(quantize(vertex.position.z));
        hash_combine(quantize(vertex.color.x));
        hash_combine(quantize(vertex.color.y));
        hash_combine(quantize(vertex.color.z));

        // add bounding box info
        XMVECTOR p = XMLoadFloat3(&vertex.position);
        XMVECTOR minv = XMLoadFloat3(&min_bb);
        XMVECTOR maxv = XMLoadFloat3(&max_bb);

        minv = XMVectorMin(minv, p);
        maxv = XMVectorMax(maxv, p);

        XMStoreFloat3(&min_bb, minv);
        XMStoreFloat3(&max_bb, maxv);
    }

    bool use_existing = client_indices != nullptr;
    // get index data to hash
    for (int i = 0; i < state.t_indices.size(); ++i)
    {
        const uint32_t& index = use_existing ? client_indices[state.t_indices[i]]
                                             : state.t_indices[i];
        hash_combine(index);
    }

    // check if hash exists
    uint64_t hash = seed;

    MeshRecord* mesh;
    if (state.m_mesh_map.contains(hash))
    {
        mesh = &state.m_mesh_map[hash];
    }
    else
    {
        MeshRecord new_mesh;

        // Store pending geometry for deferred BLAS building
        PendingGeometry pending;
        pending.vertices = std::move(state.t_vertices);
        pending.indices = std::move(state.t_indices);
        pending.hash = hash;
        pending.mat_idx = static_cast<uint32_t>(state.m_materials.size());
        pending.mv_idx = static_cast<uint32_t>(state.m_matrix_pool.size());

        new_mesh.blas_vb_ib_idx = state.m_num_mesh_resources + state.m_pending_geometries.size();

        new_mesh.tex_idx = 0xFFFFFFFFu;  // default global texture index

        state.m_mesh_map.emplace(hash, new_mesh);

        state.m_pending_geometries.push_back(std::move(pending));

        mesh = &state.m_mesh_map[hash];
    }

    // Assign per-instance data (not cached)
    mesh->mat_idx = static_cast<uint32_t>(state.m_materials.size());
    // Store the current state of the material in the materials buffer
    // TODO: Modifying materials?
    state.m_materials.push_back(state.m_material);

    mesh->mv_idx = static_cast<uint32_t>(state.m_matrix_pool.size());

    state.m_matrix_pool.push_back(state.m_matrix_stack.top(GL_MODELVIEW));

    if (state.m_texture_2d)
    {
        auto& map = state.m_texture_indices;
        auto it = map.find(state.m_texture_index);
        if (it != map.end())
        {
            mesh->tex_idx = it->second;
        }
    }

    mesh->last_frame = state.m_current_frame;

    mesh->min_bb = min_bb;
    mesh->max_bb = max_bb;

    state.m_meshes.push_back(*mesh);
}

static void triangulate(glState& state)
{
    switch (state.m_topology)
    {
        case GL_POINTS:
        {
            break;
        }
        case GL_LINES:
        {
            const size_t vert_count = state.t_vertices.size();
            if (vert_count < 2)
            {
                break;
            }

            const size_t seg_count = vert_count / 2;
            state.t_indices.reserve(seg_count * 3);

            for (uint32_t k = 0; k + 1 < vert_count; k += 2)
            {
                uint32_t a = k + 0;
                uint32_t b = k + 1;

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(b);
            }
            break;
        }
        case GL_LINE_STRIP:
        {
            const size_t vert_count = state.t_vertices.size();
            if (vert_count < 2)
            {
                break;
            }

            const size_t seg_count = vert_count - 1;
            state.t_indices.reserve(seg_count * 3);

            for (uint32_t k = 0; k + 1 < vert_count; ++k)
            {
                uint32_t a = k;
                uint32_t b = k + 1;

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(b);
            }
            break;
        }

        case GL_LINE_LOOP:
        {
            const size_t vert_count = state.t_vertices.size();
            if (vert_count < 2)
            {
                break;
            }
            const size_t seg_count = vert_count;
            state.t_indices.reserve(seg_count * 3);

            for (uint32_t k = 0; k + 1 < vert_count; ++k)
            {
                uint32_t a = k;
                uint32_t b = k + 1;

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(b);
            }
            {
                uint32_t a = static_cast<uint32_t>(vert_count - 1);
                uint32_t b = 0;

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(b);
            }
            break;
        }

        case GL_QUAD_STRIP:
        {
            const size_t quad_count = state.t_vertices.size() >= 4
                                          ? (state.t_vertices.size() - 2) / 2
                                          : 0;
            state.t_indices.reserve(quad_count * 6);

            for (uint32_t k = 0; k + 3 < state.t_vertices.size(); k += 2)
            {
                uint32_t a = k + 0;
                uint32_t b = k + 1;
                uint32_t c = k + 2;
                uint32_t d = k + 3;

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(d);
                state.t_indices.push_back(a);
                state.t_indices.push_back(d);
                state.t_indices.push_back(c);
            }
            break;
        }

        case GL_QUADS:
        {
            const size_t quad_count = state.t_vertices.size() / 4;
            state.t_indices.reserve(quad_count * 6);

            for (uint32_t k = 0; k + 3 < state.t_vertices.size(); k += 4)
            {
                uint32_t a = k + 0;
                uint32_t b = k + 1;
                uint32_t c = k + 2;
                uint32_t d = k + 3;

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(c);
                state.t_indices.push_back(a);
                state.t_indices.push_back(c);
                state.t_indices.push_back(d);
            }
            break;
        }

        case GL_TRIANGLES:
        {
            const size_t vert_count = state.t_vertices.size();
            if (vert_count < 3)
            {
                break;
            }

            const size_t tri_count = vert_count / 3;
            state.t_indices.reserve(tri_count * 3);

            for (uint32_t k = 0; k + 2 < vert_count; k += 3)
            {
                state.t_indices.push_back(k + 0);
                state.t_indices.push_back(k + 1);
                state.t_indices.push_back(k + 2);
            }
            break;
        }

        case GL_TRIANGLE_STRIP:
        {
            const size_t vert_count = state.t_vertices.size();
            if (vert_count < 3)
            {
                break;
            }

            const size_t tri_count = vert_count - 2;
            state.t_indices.reserve(tri_count * 3);

            for (uint32_t k = 0; k + 2 < vert_count; ++k)
            {
                uint32_t a, b, c;

                if ((k & 1) == 0)
                {
                    a = k;
                    b = k + 1;
                    c = k + 2;
                }
                else
                {
                    a = k + 1;
                    b = k;
                    c = k + 2;
                }

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(c);
            }
            break;
        }

        case GL_TRIANGLE_FAN:
        {
            const size_t vert_count = state.t_vertices.size();
            if (vert_count < 3)
            {
                break;
            }

            const size_t tri_count = vert_count - 2;
            state.t_indices.reserve(tri_count * 3);

            uint32_t center = 0;
            for (uint32_t k = 1; k + 1 < vert_count; ++k)
            {
                uint32_t a = center;
                uint32_t b = k;
                uint32_t c = k + 1;

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(c);
            }
            break;
        }

        case GL_POLYGON:
        {
            const size_t vert_count = state.t_vertices.size();
            if (vert_count < 3)
            {
                break;
            }

            const size_t tri_count = vert_count - 2;
            state.t_indices.reserve(tri_count * 3);

            uint32_t center = 0;
            for (uint32_t k = 1; k + 1 < vert_count; ++k)
            {
                uint32_t a = center;
                uint32_t b = k;
                uint32_t c = k + 1;

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(c);
            }
            break;
        }

        default: break;
    }
}

// CORE IMMEDIATE MODE
static void handle_begin(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLBeginCommand*>(data);
    ctx.state.m_topology = cmd->mode;

    ctx.state.t_vertices.clear();
    ctx.state.t_indices.clear();
}

static void handle_end(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLEndCommand*>(data);
    glState& state = ctx.state;

    triangulate(state);

    hash_and_commit_geometry(state);
}

static void handle_vertex2f(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLVertex2fCommand*>(data);

    const Vertex vertex{ .position = fv_to_xmf3(GLVec3f{ cmd->x, cmd->y, 0.0f }),
                         .color = ctx.state.m_color,
                         .normal = ctx.state.m_normal,
                         .uv = ctx.state.m_uv };
    ctx.state.t_vertices.push_back(vertex);
}

static void handle_vertex3f(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLVertex3fCommand*>(data);

    const Vertex vertex{ .position = fv_to_xmf3(*cmd),
                         .color = ctx.state.m_color,
                         .normal = ctx.state.m_normal,
                         .uv = ctx.state.m_uv };
    ctx.state.t_vertices.push_back(vertex);
}

static void handle_color3f(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLColor3fCommand*>(data);
    ctx.state.m_color = { cmd->x, cmd->y, cmd->z, ctx.state.m_color.w };
}

static void handle_color4f(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLColor4fCommand*>(data);
    ctx.state.m_color = fv_to_xmf4(*cmd);
}

static void handle_normal3f(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLNormal3fCommand*>(data);
    ctx.state.m_normal = fv_to_xmf3(*cmd);
}

static void handle_texcoord2f(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLTexCoord2fCommand*>(data);
    ctx.state.m_uv = fv_to_xmf2(*cmd);
}

// -----------------------------------------------------------------------------
// MATERIAL COLOR
// -----------------------------------------------------------------------------

static void handle_call_list(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLCallListCommand*>(data);

    if (ctx.state.m_display_lists.contains(cmd->list))
    {
        auto& list_buf = ctx.state.m_display_lists[cmd->list];

        size_t offset = 0;
        const uint32_t list_size = static_cast<uint32_t>(list_buf.size());

        ctx.state.m_in_call = true;
        ctx.driver.read_buffer(ctx, list_buf.data(), list_size, offset);
    }
    else
    {
        char buffer[256];
        sprintf_s(buffer, "CALL_LIST missing id %u\n", cmd->list);
        OutputDebugStringA(buffer);
    }
}

static void handle_new_list(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLNewListCommand*>(data);

    ctx.state.m_list_index = cmd->list;
    ctx.state.m_execution_mode = cmd->mode;

    ctx.state.m_display_list_begin = ctx.state.m_offset;
}

static void handle_end_list(const GLCommandContext& ctx, const void* data)
{
    if (ctx.state.m_in_call)
    {
        ctx.state.m_in_call = false;
        return;
    }

    const auto* cmd = static_cast<const GLEndListCommand*>(data);

    const auto display_list_end = ctx.state
                                      .m_offset;  // record GL_END_LIST to mark end of display list

    // record new list in respective index
    std::vector new_list(ctx.driver.get_command_buffer_data() + ctx.state.m_display_list_begin,
                         ctx.driver.get_command_buffer_data() + display_list_end);
    ctx.state.m_display_lists[ctx.state.m_list_index] = std::move(new_list);

    ctx.state.m_execution_mode = GL_COMPILE_AND_EXECUTE;  // reset execution state
}

// -----------------------------------------------------------------------------
// TEXTURE
// -----------------------------------------------------------------------------

static void handle_bind_texture(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLBindTextureCommand*>(data);
    ctx.state.m_texture_index = cmd->texture;
}

static void handle_delete_textures(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLDeleteTexturesCommand*>(data);

    UINT32 index = ctx.state.m_texture_indices[cmd->n];

    // TODO (delete textures)
}

static void handle_tex_image_2d(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLTexImage2DCommand*>(data);

    const auto* bytes = static_cast<const UINT8*>(data);
    const void* data_ptr = bytes + sizeof(GLTexImage2DCommand);

    PendingTexture tex;
    tex.desc = { cmd->width,
                 cmd->height,
                 1,  // depth of array size is always 1 (gl does not support non 2d)
                 1,
                 gl_format_to_dxgi(cmd->internalFormat, cmd->format, cmd->type),
                 D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                 false };
    tex.pixels = data_ptr;

    glState& state = ctx.state;
    const UINT32 global_tex_index = state.m_num_textures
                                    + static_cast<UINT32>(state.m_pending_textures.size());
    state.m_texture_indices[state.m_texture_index] = global_tex_index;

    state.m_pending_textures.push_back(std::move(tex));
}

static void handle_tex_param(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLTexParameterCommand*>(data);
}

static void handle_tex_envi(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLTexEnviCommand*>(data);
}

static void handle_tex_envf(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLTexEnvfCommand*>(data);
}

// -----------------------------------------------------------------------------
// MATRIX COMMANDS
// -----------------------------------------------------------------------------
// CLIENT STATE
template<typename I, typename O>
void convert_ptr_template(size_t count, const void* ptr, O* out, bool normalize = false)
{
    const I* src_ptr = static_cast<const I*>(ptr);
    for (size_t i = 0; i < count; ++i)
    {
        out[i] = normalize ? static_cast<O>(src_ptr[i]) / 255.f : static_cast<O>(src_ptr[i]);
    }
}

template<typename O>
void convert_ptr(uint32_t type, size_t count, const void* ptr, O* out, bool normalize = false)
{
    switch (type)
    {
        case GL_UNSIGNED_BYTE: convert_ptr_template<uint8_t>(count, ptr, out, normalize); break;
        case GL_BYTE: convert_ptr_template<int8_t>(count, ptr, out); break;
        case GL_UNSIGNED_SHORT: convert_ptr_template<uint16_t>(count, ptr, out); break;
        case GL_SHORT: convert_ptr_template<int16_t>(count, ptr, out); break;
        case GL_UNSIGNED_INT: convert_ptr_template<uint32_t>(count, ptr, out); break;
        case GL_INT: convert_ptr_template<int32_t>(count, ptr, out); break;
        case GL_FLOAT: convert_ptr_template<float>(count, ptr, out); break;
        case GL_DOUBLE: convert_ptr_template<double>(count, ptr, out); break;
        default: throw std::runtime_error("Unsupported type");
    }
}

static void handle_draw_arrays(const GLCommandContext& ctx, const void* data)
{
    const GLRemixDrawArraysCommand* cmd = static_cast<const GLRemixDrawArraysCommand*>(data);
    glState& state = ctx.state;

    state.t_vertices.clear();
    state.t_indices.clear();

    state.m_topology = cmd->mode;
    state.t_vertices.resize(cmd->count);

    const uint8_t* client_data = reinterpret_cast<const uint8_t*>(cmd + 1);

    thread_local std::vector<float> scratch_buffer;

    // Loop over enabled arrays
    for (uint32_t arr = 0; arr < cmd->enabled; arr++)
    {
        const GLRemixClientArrayHeader& h = cmd->headers[arr];

        const size_t component_count = cmd->count * h.size;
        const bool normalize = h.array_type == GLRemixClientArrayType::COLOR;

        scratch_buffer.resize(component_count);
        float* f_ptr = scratch_buffer.data();

        for (size_t v_idx = 0; v_idx < cmd->count; v_idx++)
        {
            const void* src = client_data + (v_idx * h.stride);

            convert_ptr<float>(h.type, h.size, src, f_ptr, normalize);

            switch (h.array_type)
            {
                case GLRemixClientArrayType::VERTEX:
                    state.t_vertices[v_idx].position = XMFLOAT3{ f_ptr[0], f_ptr[1],
                                                                 h.size > 2 ? f_ptr[2] : 0.0f };
                    break;

                case GLRemixClientArrayType::NORMAL:
                    state.t_vertices[v_idx].normal = XMFLOAT3{ f_ptr[0], f_ptr[1], f_ptr[2] };
                    break;

                case GLRemixClientArrayType::COLOR:
                    state.t_vertices[v_idx].color = XMFLOAT4{ f_ptr[0], f_ptr[1], f_ptr[2],
                                                              h.size > 3 ? f_ptr[3] : 1.0f };
                    break;

                case GLRemixClientArrayType::TEXCOORD:
                    state.t_vertices[v_idx].uv = XMFLOAT2{ f_ptr[0], h.size > 1 ? f_ptr[1] : 0.0f };
                    break;

                default: break;
            }

            f_ptr += h.size;
        }
        client_data += h.array_bytes;
    }
    triangulate(state);

    hash_and_commit_geometry(state);
}

static void handle_draw_elements(const GLCommandContext& ctx, const void* data)
{
    const GLRemixDrawElementsCommand* cmd = static_cast<const GLRemixDrawElementsCommand*>(data);
    glState& state = ctx.state;

    state.t_vertices.clear();
    state.t_indices.clear();

    state.m_topology = cmd->mode;
    state.t_vertices.resize(cmd->count);

    const uint8_t* client_data = reinterpret_cast<const uint8_t*>(cmd + 1);

    thread_local std::vector<float> scratch_buffer;
    thread_local std::vector<size_t> client_indices;

    // Loop over enabled arrays
    for (uint32_t arr = 0; arr < cmd->enabled; arr++)
    {
        const GLRemixClientArrayHeader& h = cmd->headers[arr];

        if (h.array_type == GLRemixClientArrayType::INDICES)
        {
            client_indices.resize(cmd->count);
            convert_ptr<size_t>(h.type, cmd->count, client_data, client_indices.data());
            client_data += h.array_bytes;
            continue;
        }

        const size_t component_count = cmd->count * h.size;
        const bool normalize = h.array_type == GLRemixClientArrayType::COLOR;

        scratch_buffer.resize(component_count);
        float* f_ptr = scratch_buffer.data();

        for (size_t v_idx = 0; v_idx < cmd->count; v_idx++)
        {
            const void* src = client_data + (v_idx * h.stride);

            convert_ptr<float>(h.type, h.size, src, f_ptr, normalize);

            switch (h.array_type)
            {
                case GLRemixClientArrayType::VERTEX:
                    state.t_vertices[v_idx].position = XMFLOAT3{ f_ptr[0], f_ptr[1],
                                                                 h.size > 2 ? f_ptr[2] : 0.0f };
                    break;

                case GLRemixClientArrayType::NORMAL:
                    state.t_vertices[v_idx].normal = XMFLOAT3{ f_ptr[0], f_ptr[1], f_ptr[2] };
                    break;

                case GLRemixClientArrayType::COLOR:
                    state.t_vertices[v_idx].color = XMFLOAT4{ f_ptr[0], f_ptr[1], f_ptr[2],
                                                              h.size > 3 ? f_ptr[3] : 1.0f };
                    break;

                case GLRemixClientArrayType::TEXCOORD:
                    state.t_vertices[v_idx].uv = XMFLOAT2{ f_ptr[0], h.size > 1 ? f_ptr[1] : 0.0f };
                    break;

                default: break;
            }

            f_ptr += h.size;
        }
        client_data += h.array_bytes;
    }

    triangulate(state);

    hash_and_commit_geometry(state, client_indices.data());
}

// MATRIX OPERATIONS
static void handle_matrix_mode(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLMatrixModeCommand*>(data);

    ctx.state.m_matrix_mode = cmd->mode;
}

static void handle_load_identity(const GLCommandContext& ctx, const void* data)
{
    ctx.state.m_matrix_stack.identity(ctx.state.m_matrix_mode);
}

static void handle_load_matrix(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLLoadMatrixCommand*>(data);
    ctx.state.m_matrix_stack.load(ctx.state.m_matrix_mode, cmd->m);
}

static void handle_mult_matrix(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLMultMatrixCommand*>(data);
    ctx.state.m_matrix_stack.mul_set(ctx.state.m_matrix_mode, cmd->m);
}

static void handle_push_matrix(const GLCommandContext& ctx, const void* data)
{
    ctx.state.m_matrix_stack.push(ctx.state.m_matrix_mode);
}

static void handle_pop_matrix(const GLCommandContext& ctx, const void* data)
{
    ctx.state.m_matrix_stack.pop(ctx.state.m_matrix_mode);
}

static void handle_translate(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLTranslateCommand*>(data);

    const float x = cmd->t.x;
    const float y = cmd->t.y;
    const float z = cmd->t.z;

    ctx.state.m_matrix_stack.translate(ctx.state.m_matrix_mode, x, y, z);
}

static void handle_rotate(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLRotateCommand*>(data);

    const float angle = cmd->angle;
    const float x = cmd->axis.x;
    const float y = cmd->axis.y;
    const float z = cmd->axis.z;

    ctx.state.m_matrix_stack.rotate(ctx.state.m_matrix_mode, angle, x, y, z);
}

static void handle_scale(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLScaleCommand*>(data);

    const float x = cmd->s.x;
    const float y = cmd->s.y;
    const float z = cmd->s.z;

    ctx.state.m_matrix_stack.scale(ctx.state.m_matrix_mode, x, y, z);
}

static void handle_ortho(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLOrthoCommand*>(data);

    ctx.state.m_matrix_stack.ortho(ctx.state.m_matrix_mode, cmd->left, cmd->right, cmd->bottom,
                                   cmd->top, cmd->zNear, cmd->zFar);
    ctx.state.m_perspective = false;
}

static void handle_frustum(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLFrustumCommand*>(data);

    ctx.state.m_matrix_stack.frustum(ctx.state.m_matrix_mode, cmd->left, cmd->right, cmd->bottom,
                                     cmd->top, cmd->zNear, cmd->zFar);
    ctx.state.m_perspective = true;
}

static void handle_perspective(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLPerspectiveCommand*>(data);

    ctx.state.m_matrix_stack.perspective(ctx.state.m_matrix_mode, cmd->fovY, cmd->aspect,
                                         cmd->zNear, cmd->zFar);
    ctx.state.m_perspective = true;
}

// RENDERING
static void handle_clear_color(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLClearColorCommand*>(data);

    ctx.state.m_clear_color = fv_to_xmf4(cmd->color);
}

// FIXED FUNCTION
static void handle_lightf(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLLightCommand*>(data);

    uint32_t light_index = cmd->light - GL_LIGHT0;
    Light& m_light = ctx.state.m_lights[light_index];

    switch (cmd->pname)
    {
        case GL_SPOT_EXPONENT: m_light.spot_exponent = cmd->param; break;
        case GL_SPOT_CUTOFF: m_light.spot_cutoff = cmd->param; break;
        case GL_CONSTANT_ATTENUATION: m_light.constant_attenuation = cmd->param; break;
        case GL_LINEAR_ATTENUATION: m_light.linear_attenuation = cmd->param; break;
        case GL_QUADRATIC_ATTENUATION: m_light.quadratic_attenuation = cmd->param; break;
        default: break;
    }
}

static void handle_lightfv(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLLightfvCommand*>(data);

    uint32_t light_index = cmd->light - GL_LIGHT0;
    Light& m_light = ctx.state.m_lights[light_index];

    switch (cmd->pname)
    {
        case GL_POSITION: m_light.position = fv_to_xmf4(cmd->params); break;
        case GL_AMBIENT: m_light.ambient = fv_to_xmf4(cmd->params); break;
        case GL_DIFFUSE: m_light.diffuse = fv_to_xmf4(cmd->params); break;
        case GL_SPECULAR: m_light.specular = fv_to_xmf4(cmd->params); break;
        case GL_SPOT_DIRECTION:
            m_light.spot_direction = fv_to_xmf3(
                GLVec3f{ cmd->params.x, cmd->params.y, cmd->params.z });
            break;
        default: break;
    }
}

static void handle_materiali(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLMaterialiCommand*>(data);

    float param = static_cast<float>(cmd->param);

    switch (cmd->pname)
    {
        case GL_AMBIENT: ctx.state.m_material.ambient = f_to_xmf4(param); break;
        case GL_DIFFUSE: ctx.state.m_material.diffuse = f_to_xmf4(param); break;
        case GL_SPECULAR: ctx.state.m_material.specular = f_to_xmf4(param); break;
        default:
            switch (cmd->pname)
            {
                case GL_EMISSION: ctx.state.m_material.emission = f_to_xmf4(param); break;
                case GL_SHININESS: ctx.state.m_material.shininess = param; break;
                default: break;
            }
            break;
    }
}

static void handle_materialf(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLMaterialfCommand*>(data);

    switch (cmd->pname)
    {
        case GL_AMBIENT: ctx.state.m_material.ambient = f_to_xmf4(cmd->param); break;
        case GL_DIFFUSE: ctx.state.m_material.diffuse = f_to_xmf4(cmd->param); break;
        case GL_SPECULAR: ctx.state.m_material.specular = f_to_xmf4(cmd->param); break;
        default:
            switch (cmd->pname)
            {
                case GL_EMISSION: ctx.state.m_material.emission = f_to_xmf4(cmd->param); break;
                case GL_SHININESS: ctx.state.m_material.shininess = cmd->param; break;
                default: break;
            }
            break;
    }
}

static void handle_materialfv(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLMaterialfvCommand*>(data);

    switch (cmd->pname)
    {
        case GL_AMBIENT: ctx.state.m_material.ambient = fv_to_xmf4(cmd->params); break;
        case GL_DIFFUSE: ctx.state.m_material.diffuse = fv_to_xmf4(cmd->params); break;
        case GL_SPECULAR: ctx.state.m_material.specular = fv_to_xmf4(cmd->params); break;
        default:
            switch (cmd->pname)
            {
                case GL_EMISSION: ctx.state.m_material.emission = fv_to_xmf4(cmd->params); break;
                case GL_SHININESS: ctx.state.m_material.shininess = cmd->params.x; break;
                case GL_AMBIENT_AND_DIFFUSE:
                    ctx.state.m_material.ambient = fv_to_xmf4(cmd->params);
                    ctx.state.m_material.diffuse = fv_to_xmf4(cmd->params);
                    break;
                default: break;
            }
            break;
    }
}

// STATE MANAGEMENT
static void set_state(const GLCommandContext& ctx, unsigned int cap, bool value)
{
    // light handling
    if (cap >= GL_LIGHT0 && cap <= GL_LIGHT7)
    {
        uint32_t light_index = cap - GL_LIGHT0;
        ctx.state.m_lights[light_index].enabled = value;
        return;
    }

    switch (cap)
    {
        case GL_LIGHTING:
        {
            ctx.state.m_lighting = value;
            break;
        }
        case GL_TEXTURE_2D:
        {
            ctx.state.m_texture_2d = value;
            break;
        }
        // TODO add support for more params when encountered (but large majority will be ignored likely)
        break;
    }
}

static void handle_enable(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLEnableCommand*>(data);
    set_state(ctx, cmd->cap, true);
}

static void handle_disable(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLDisableCommand*>(data);
    set_state(ctx, cmd->cap, false);
}

// OTHER
static void handle_wgl_create_context(const GLCommandContext& ctx, const void* data)
{
    const auto cmd = static_cast<const WGLCreateContextCommand*>(data);
    ctx.state.hwnd = cmd->hwnd;
    ctx.state.m_create_context = true;
}

static void handle_wgl_input_event(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const WGLInputEventCommand*>(data);
    if (ctx.state.hwnd)
    {
        ImGui_ImplWin32_WndProcHandler(ctx.state.hwnd, cmd->msg, cmd->wparam,
                                       static_cast<LPARAM>(cmd->lparam));
    }
}
}  // namespace glRemix

void glRemix::glDriver::init_handlers()
{
    using enum GLCommandType;

    gl_command_handlers.fill(nullptr);

    // CORE IMMEDIATE MODE
    gl_command_handlers[static_cast<size_t>(GLCMD_BEGIN)] = &handle_begin;
    gl_command_handlers[static_cast<size_t>(GLCMD_END)] = &handle_end;
    gl_command_handlers[static_cast<size_t>(GLCMD_VERTEX2F)] = &handle_vertex2f;
    gl_command_handlers[static_cast<size_t>(GLCMD_VERTEX3F)] = &handle_vertex3f;
    gl_command_handlers[static_cast<size_t>(GLCMD_COLOR3F)] = &handle_color3f;
    gl_command_handlers[static_cast<size_t>(GLCMD_COLOR4F)] = &handle_color4f;
    gl_command_handlers[static_cast<size_t>(GLCMD_NORMAL3F)] = &handle_normal3f;
    gl_command_handlers[static_cast<size_t>(GLCMD_TEXCOORD2F)] = &handle_texcoord2f;

    // MATERIALS LIGHTS
    gl_command_handlers[static_cast<size_t>(GLCMD_LIGHTF)] = &handle_lightf;
    gl_command_handlers[static_cast<size_t>(GLCMD_LIGHTFV)] = &handle_lightfv;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALI)] = &handle_materiali;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALF)] = &handle_materialf;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALIV)] = &handle_materialfv;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALFV)] = &handle_materialfv;
    gl_command_handlers[static_cast<size_t>(GLCMD_CLEAR_COLOR)] = &handle_clear_color;

    // TEXTURES
    gl_command_handlers[static_cast<size_t>(GLCMD_BIND_TEXTURE)] = &handle_bind_texture;
    gl_command_handlers[static_cast<size_t>(GLCMD_DELETE_TEXTURES)] = &handle_delete_textures;
    gl_command_handlers[static_cast<size_t>(GLCMD_TEX_IMAGE_2D)] = &handle_tex_image_2d;
    gl_command_handlers[static_cast<size_t>(GLCMD_TEX_PARAMETER)] = &handle_tex_param;
    gl_command_handlers[static_cast<size_t>(GLCMD_TEX_ENV_I)] = &handle_tex_envi;
    gl_command_handlers[static_cast<size_t>(GLCMD_TEX_ENV_F)] = &handle_tex_envf;
    // CLIENT STATE
    gl_command_handlers[static_cast<size_t>(GLREMIXCMD_DRAW_ARRAYS)] = &handle_draw_arrays;
    gl_command_handlers[static_cast<size_t>(GLREMIXCMD_DRAW_ELEMENTS)] = &handle_draw_elements;
    gl_command_handlers[static_cast<size_t>(GLREMIXCMD_DRAW_RANGE_ELEMENTS)] = &handle_draw_elements;

    // MATRIX OPERATIONS
    gl_command_handlers[static_cast<size_t>(GLCMD_MATRIX_MODE)] = &handle_matrix_mode;
    gl_command_handlers[static_cast<size_t>(GLCMD_LOAD_IDENTITY)] = &handle_load_identity;
    gl_command_handlers[static_cast<size_t>(GLCMD_LOAD_MATRIX)] = &handle_load_matrix;
    gl_command_handlers[static_cast<size_t>(GLCMD_MULT_MATRIX)] = &handle_mult_matrix;
    gl_command_handlers[static_cast<size_t>(GLCMD_PUSH_MATRIX)] = &handle_push_matrix;
    gl_command_handlers[static_cast<size_t>(GLCMD_POP_MATRIX)] = &handle_pop_matrix;
    gl_command_handlers[static_cast<size_t>(GLCMD_TRANSLATE)] = &handle_translate;
    gl_command_handlers[static_cast<size_t>(GLCMD_ROTATE)] = &handle_rotate;
    gl_command_handlers[static_cast<size_t>(GLCMD_SCALE)] = &handle_scale;
    gl_command_handlers[static_cast<size_t>(GLCMD_ORTHO)] = &handle_ortho;
    gl_command_handlers[static_cast<size_t>(GLCMD_FRUSTUM)] = &handle_frustum;
    gl_command_handlers[static_cast<size_t>(GLCMD_PERSPECTIVE)] = &handle_perspective;

    // RENDERING
    gl_command_handlers[static_cast<size_t>(GLCMD_CLEAR_COLOR)] = &handle_clear_color;

    // FIXED FUNCTION
    gl_command_handlers[static_cast<size_t>(GLCMD_LIGHTF)] = &handle_lightf;
    gl_command_handlers[static_cast<size_t>(GLCMD_LIGHTFV)] = &handle_lightfv;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALI)] = &handle_materiali;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALF)] = &handle_materialf;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALIV)] = &handle_materialfv;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALFV)] = &handle_materialfv;

    // STATE MANAGEMENT
    gl_command_handlers[static_cast<size_t>(GLCMD_ENABLE)] = &handle_enable;
    gl_command_handlers[static_cast<size_t>(GLCMD_DISABLE)] = &handle_disable;

    // DISPLAY LISTS
    gl_command_handlers[static_cast<size_t>(GLCMD_NEW_LIST)] = &handle_new_list;
    gl_command_handlers[static_cast<size_t>(GLCMD_CALL_LIST)] = &handle_call_list;
    gl_command_handlers[static_cast<size_t>(GLCMD_END_LIST)] = &handle_end_list;

    // OTHER
    gl_command_handlers[static_cast<size_t>(WGLCMD_CREATE_CONTEXT)] = &handle_wgl_create_context;
    gl_command_handlers[static_cast<size_t>(WGLCMD_INPUT_EVENT)] = &handle_wgl_input_event;
}

glRemix::glDriver::glDriver()
{
    init();
}

void glRemix::glDriver::init()
{
    m_ipc.init_reader();

    init_handlers();
}

void glRemix::glDriver::process_stream()
{
    uint32_t frame_index = 0;
    uint32_t frame_bytes = 0;
    m_ipc.consume_frame_or_wait(m_command_buffer.data(), &frame_index, &frame_bytes);

    if (frame_bytes == 0)
    {
        return;
    }

    m_state.m_current_frame = frame_index;

    // reset per frames
    m_state.m_create_context = false;
    m_state.m_meshes.clear();              // per frame meshes
    m_state.m_matrix_pool.clear();         // reset matrix pool each frame
    m_state.m_materials.clear();
    m_state.m_pending_geometries.clear();  // clear pending geometry data
    m_state.m_pending_textures.clear();

    m_state.m_offset = 0;
    GLCommandContext ctx{ m_state, *this };
    read_buffer(ctx, m_command_buffer.data(), frame_bytes, m_state.m_offset);
}

void glRemix::glDriver::read_buffer(const GLCommandContext& ctx, const uint8_t* buffer,
                                    size_t buffer_size, size_t& offset)
{
    GLCommandView view{};
    while (read_next_command(buffer, buffer_size, offset, view))
    {
        // execution mode hack
        if (ctx.state.m_execution_mode == GL_COMPILE)
        {
            if (view.type != GLCommandType::GLCMD_END_LIST)
            {
                continue;
            }
        }

        const auto idx = static_cast<size_t>(view.type);

        GLCommandHandler handler = gl_command_handlers[idx];

        if (handler)
        {
            handler(ctx, view.data);
        }
        else
        {
            char buffer[256];
            sprintf_s(buffer, "glxRemixRenderer - Unhandled Command: %d (size: %u)\n", view.type,
                      view.cmd_bytes);
            OutputDebugStringA(buffer);
        }
    }
}

bool glRemix::glDriver::read_next_command(const uint8_t* buffer, size_t buffer_size, size_t& offset,
                                          GLCommandView& out)
{
    // ensure that we are not reading out of bounds
    if (offset + sizeof(GLCommandHeader) > buffer_size)
    {
        return false;
    }

    const auto* header = reinterpret_cast<const GLCommandHeader*>(buffer + offset);
    offset += sizeof(GLCommandHeader);

    // ensure that we are not reading out of bounds
    if (offset + header->cmd_bytes > buffer_size)
    {
        return false;
    }

    out.type = header->type;
    out.cmd_bytes = header->cmd_bytes;
    out.data = buffer + offset;

    offset += header->cmd_bytes;  // move header after extracting latest command
    return true;
}
