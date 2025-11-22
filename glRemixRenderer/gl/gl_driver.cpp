#include "gl_driver.h"
#include "gl_command_utils.h"

#include <chrono>

glRemix::glDriver::glDriver()
{
    init();
}

namespace glRemix
{
static void handle_wgl_create_context(const GLCommandContext& ctx, const void* data)
{
    const auto cmd = static_cast<const WGLCreateContextCommand*>(data);
    ctx.state.hwnd = cmd->hwnd;
    ctx.state.m_create_context = true;
}

// -----------------------------------------------------------------------------
// GEOMETRY COMMANDS
// -----------------------------------------------------------------------------

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
    switch (state.m_topology)
    {
        case GL_QUAD_STRIP:
        {
            const size_t quad_count = state.t_vertices.size() >= 4
                                          ? (state.t_vertices.size() - 2) / 2
                                          : 0;
            state.t_indices.reserve(quad_count * 6);

            for (UINT32 k = 0; k + 3 < state.t_vertices.size(); k += 2)
            {
                UINT32 a = k + 0;
                UINT32 b = k + 1;
                UINT32 c = k + 2;
                UINT32 d = k + 3;

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

            for (UINT32 k = 0; k + 3 < state.t_vertices.size(); k += 4)
            {
                UINT32 a = k + 0;
                UINT32 b = k + 1;
                UINT32 c = k + 2;
                UINT32 d = k + 3;

                state.t_indices.push_back(a);
                state.t_indices.push_back(b);
                state.t_indices.push_back(c);
                state.t_indices.push_back(a);
                state.t_indices.push_back(c);
                state.t_indices.push_back(d);
            }
            break;
        }
        default: break;
    }

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
    }

    // get index data to hash
    for (int i = 0; i < state.t_indices.size(); ++i)
    {
        const UINT32& index = state.t_indices[i];
        hash_combine(index);
    }

    // check if hash exists
    UINT64 hash = seed;

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
        pending.mat_idx = static_cast<UINT32>(state.m_materials.size());
        pending.mv_idx = static_cast<UINT32>(state.m_matrix_pool.size());

        new_mesh.blas_vb_ib_idx = state.m_num_mesh_resources + state.m_pending_geometries.size();

        state.m_mesh_map.emplace(hash, new_mesh);

        state.m_pending_geometries.push_back(std::move(pending));

        mesh = &state.m_mesh_map[hash];
    }

    // Assign per-instance data (not cached)
    mesh->mat_idx = static_cast<UINT32>(state.m_materials.size());
    // Store the current state of the material in the materials buffer
    // TODO: Modifying materials?
    state.m_materials.push_back(state.m_material);

    mesh->mv_idx = static_cast<UINT32>(state.m_matrix_pool.size());

    state.m_matrix_pool.push_back(state.m_matrix_stack.top(GL_MODELVIEW));

    mesh->last_frame = state.m_current_frame;

    state.m_meshes.push_back(*mesh);
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

static void handle_vertex2f(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLVertex2fCommand*>(data);

    const Vertex vertex{ .position = fv_to_xmf3(GLVec3f{ cmd->x, cmd->y, 0.0f }),
                         .color = ctx.state.m_color,
                         .normal = ctx.state.m_normal,
                         .uv = ctx.state.m_uv };
    ctx.state.t_vertices.push_back(vertex);
}

static void handle_normal3f(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLNormal3fCommand*>(data);
    ctx.state.m_normal = fv_to_xmf3(*cmd);
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

static void handle_texcoord2f(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLTexCoord2fCommand*>(data);
    ctx.state.m_uv = fv_to_xmf2(*cmd);
}

// -----------------------------------------------------------------------------
// MATERIAL TEXTURE COLOR
// -----------------------------------------------------------------------------

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

static void handle_clear_color(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLClearColorCommand*>(data);

    ctx.state.m_clear_color = fv_to_xmf4(cmd->color);
}

// -----------------------------------------------------------------------------
// MATRIX COMMANDS
// -----------------------------------------------------------------------------

static void handle_matrix_mode(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLMatrixModeCommand*>(data);

    ctx.state.m_matrix_mode = cmd->mode;
}

static void handle_push_matrix(const GLCommandContext& ctx, const void* data)
{
    ctx.state.m_matrix_stack.push(ctx.state.m_matrix_mode);
}

static void handle_pop_matrix(const GLCommandContext& ctx, const void* data)
{
    ctx.state.m_matrix_stack.pop(ctx.state.m_matrix_mode);
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

//////////////// MATRIX OPERATIONS ////////////////////////////////

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

static void handle_translate(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLTranslateCommand*>(data);

    const float x = cmd->t.x;
    const float y = cmd->t.y;
    const float z = cmd->t.z;

    ctx.state.m_matrix_stack.translate(ctx.state.m_matrix_mode, x, y, z);
}

static void handle_ortho(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLOrthoCommand*>(data);

    ctx.state.m_matrix_stack.ortho(ctx.state.m_matrix_mode, cmd->left, cmd->right, cmd->bottom,
                                   cmd->top, cmd->zNear, cmd->zFar);
}

static void handle_frustum(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLFrustumCommand*>(data);

    ctx.state.m_matrix_stack.frustum(ctx.state.m_matrix_mode, cmd->left, cmd->right, cmd->bottom,
                                     cmd->top, cmd->zNear, cmd->zFar);
}

static void handle_perspective(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLPerspectiveCommand*>(data);

    ctx.state.m_matrix_stack.perspective(ctx.state.m_matrix_mode, cmd->fovY, cmd->aspect,
                                         cmd->zNear, cmd->zFar);
}

// -----------------------------------------------------------------------------
// DISPLAY LISTS
// -----------------------------------------------------------------------------

static void handle_new_list(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLNewListCommand*>(data);

    ctx.state.m_list_index = cmd->list;
    ctx.state.m_execution_mode = cmd->mode;

    ctx.state.m_display_list_begin = ctx.state.m_offset;
}

static void handle_call_list(const GLCommandContext& ctx, const void* data)
{
    const auto* cmd = static_cast<const GLCallListCommand*>(data);

    if (ctx.state.m_display_lists.contains(cmd->list))
    {
        auto& list_buf = ctx.state.m_display_lists[cmd->list];

        size_t offset = 0;
        const UINT32 list_size = static_cast<UINT32>(list_buf.size());

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
// STATE COMMANDS
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// DRAW CALLS
// -----------------------------------------------------------------------------

// TODO (implementations coming soon)
static void handle_draw_arrays(const GLCommandContext& ctx, const void* data) {}

static void handle_draw_elements(const GLCommandContext& ctx, const void* data) {}

}  // namespace glRemix

void glRemix::glDriver::init_handlers()
{
    using enum GLCommandType;

    gl_command_handlers.fill(nullptr);

    gl_command_handlers[static_cast<size_t>(WGLCMD_CREATE_CONTEXT)] = &handle_wgl_create_context;

    // GEOMETRY COMMANDS
    gl_command_handlers[static_cast<size_t>(GLCMD_BEGIN)] = &handle_begin;
    gl_command_handlers[static_cast<size_t>(GLCMD_END)] = &handle_end;

    gl_command_handlers[static_cast<size_t>(GLCMD_VERTEX3F)] = &handle_vertex3f;
    gl_command_handlers[static_cast<size_t>(GLCMD_VERTEX2F)] = &handle_vertex2f;
    gl_command_handlers[static_cast<size_t>(GLCMD_NORMAL3F)] = &handle_normal3f;
    gl_command_handlers[static_cast<size_t>(GLCMD_COLOR3F)] = &handle_color3f;
    gl_command_handlers[static_cast<size_t>(GLCMD_COLOR4F)] = &handle_color4f;
    gl_command_handlers[static_cast<size_t>(GLCMD_TEXCOORD2F)] = &handle_texcoord2f;

    // MATERIALS LIGHTS TEXTURES
    gl_command_handlers[static_cast<size_t>(GLCMD_LIGHTF)] = &handle_lightf;
    gl_command_handlers[static_cast<size_t>(GLCMD_LIGHTFV)] = &handle_lightfv;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALI)] = &handle_materiali;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALF)] = &handle_materialf;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALIV)] = &handle_materialfv;
    gl_command_handlers[static_cast<size_t>(GLCMD_MATERIALFV)] = &handle_materialfv;

    gl_command_handlers[static_cast<size_t>(GLCMD_CLEAR_COLOR)] = &handle_clear_color;

    // MATRIX COMMANDS
    gl_command_handlers[static_cast<size_t>(GLCMD_MATRIX_MODE)] = &handle_matrix_mode;
    gl_command_handlers[static_cast<size_t>(GLCMD_PUSH_MATRIX)] = &handle_push_matrix;
    gl_command_handlers[static_cast<size_t>(GLCMD_POP_MATRIX)] = &handle_pop_matrix;
    gl_command_handlers[static_cast<size_t>(GLCMD_LOAD_IDENTITY)] = &handle_load_identity;
    gl_command_handlers[static_cast<size_t>(GLCMD_LOAD_MATRIX)] = &handle_load_matrix;
    gl_command_handlers[static_cast<size_t>(GLCMD_MULT_MATRIX)] = &handle_mult_matrix;

    gl_command_handlers[static_cast<size_t>(GLCMD_ROTATE)] = &handle_rotate;
    gl_command_handlers[static_cast<size_t>(GLCMD_SCALE)] = &handle_scale;
    gl_command_handlers[static_cast<size_t>(GLCMD_TRANSLATE)] = &handle_translate;
    gl_command_handlers[static_cast<size_t>(GLCMD_ORTHO)] = &handle_ortho;
    gl_command_handlers[static_cast<size_t>(GLCMD_FRUSTUM)] = &handle_frustum;
    gl_command_handlers[static_cast<size_t>(GLCMD_PERSPECTIVE)] = &handle_perspective;

    // DISPLAY LISTS
    gl_command_handlers[static_cast<size_t>(GLCMD_NEW_LIST)] = &handle_new_list;
    gl_command_handlers[static_cast<size_t>(GLCMD_CALL_LIST)] = &handle_call_list;
    gl_command_handlers[static_cast<size_t>(GLCMD_END_LIST)] = &handle_end_list;

    // STATE COMMANDS
    gl_command_handlers[static_cast<size_t>(GLCMD_ENABLE)] = &handle_enable;
    gl_command_handlers[static_cast<size_t>(GLCMD_DISABLE)] = &handle_disable;

    // DRAW CALLS
    gl_command_handlers[static_cast<size_t>(GLCMD_DRAW_ARRAYS)] = &handle_draw_arrays;
    gl_command_handlers[static_cast<size_t>(GLCMD_DRAW_ELEMENTS)] = &handle_draw_elements;
}

void glRemix::glDriver::init()
{
    m_ipc.init_reader();
    m_command_buffer.resize(m_ipc.get_max_payload_size());

    init_handlers();
}

void glRemix::glDriver::process_stream()
{
    UINT32 buffer_size = 0;
    UINT32 frame_index = 0;
    m_ipc.consume_frame_or_wait(m_command_buffer.data(), &buffer_size, &frame_index);

    if (buffer_size == 0)
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

    m_state.m_offset = 0;
    GLCommandContext ctx{ m_state, *this };
    read_buffer(ctx, m_command_buffer.data(), buffer_size, m_state.m_offset);
}

void glRemix::glDriver::read_buffer(const GLCommandContext& ctx, const UINT8* buffer,
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
                      view.data_size);
            OutputDebugStringA(buffer);
        }
    }
}

bool glRemix::glDriver::read_next_command(const UINT8* buffer, size_t buffer_size, size_t& offset,
                                          GLCommandView& out)
{
    // ensure that we are not reading out of bounds
    if (offset + sizeof(GLCommandUnifs) > buffer_size)
    {
        return false;
    }

    const auto* header = reinterpret_cast<const GLCommandUnifs*>(buffer + offset);
    offset += sizeof(GLCommandUnifs);

    // ensure that we are not reading out of bounds
    if (offset + header->dataSize > buffer_size)
    {
        return false;
    }

    out.type = header->type;
    out.data_size = header->dataSize;
    out.data = buffer + offset;

    offset += header->dataSize;  // move header after extracting latest command
    return true;
}
