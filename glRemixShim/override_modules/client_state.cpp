#include "override_modules/common_includes.h"

#include <shared/utils/gl_utils.h>

#include "gl_loader.h"

namespace glRemix::hooks
{

/* CLIENT STATE */
void APIENTRY gl_enable_client_state_ovr(GLenum array)
{
    GLRemixClientArrayInterface* target = nullptr;

    GLRemixClientArrayType array_type = utils::MapTo(array, g_client_active_texcoord_unit);
    if (array_type == GLRemixClientArrayType::_INVALID)
    {
        return;
    }

    target = &g_client_arrays[static_cast<UINT32>(array_type)];

    if (target && !target->enabled)
    {
        target->enabled = true;
        g_enabled_client_arrays_count++;
    }

    return;  // do NOT send to IPC
}

void APIENTRY gl_disable_client_state_ovr(GLenum array)
{
    GLRemixClientArrayInterface* target = nullptr;

    GLRemixClientArrayType array_type = utils::MapTo(array, g_client_active_texcoord_unit);
    if (array_type == GLRemixClientArrayType::_INVALID)
    {
        return;
    }

    target = &g_client_arrays[static_cast<UINT32>(array_type)];

    if (target && target->enabled)
    {
        target->enabled = false;
        g_enabled_client_arrays_count--;
    }

    return;
}

void APIENTRY gl_vertex_pointer_ovr(GLint size, GLenum type, GLsizei stride, const void* pointer)
{
    GLRemixClientArrayType array_type = GLRemixClientArrayType::VERTEX;

    GLRemixClientArrayInterface& a = g_client_arrays[static_cast<UINT32>(array_type)];
    a.ipc_payload.size = static_cast<UINT32>(size);
    a.ipc_payload.type = static_cast<UINT32>(type);
    a.ipc_payload.stride = utils::InterpretStride(size, type, stride);
    a.ipc_payload.array_type = array_type;
    a.ptr = pointer;
    return;
}

void APIENTRY gl_normal_pointer_ovr(GLenum type, GLsizei stride, const void* pointer)
{
    GLRemixClientArrayType array_type = GLRemixClientArrayType::NORMAL;

    GLRemixClientArrayInterface& a = g_client_arrays[static_cast<UINT32>(array_type)];
    a.ipc_payload.size = static_cast<UINT32>(3);
    a.ipc_payload.type = static_cast<UINT32>(type);
    a.ipc_payload.stride = utils::InterpretStride(3, type, stride);
    a.ipc_payload.array_type = array_type;
    a.ptr = pointer;
    return;
}

void APIENTRY gl_color_pointer_ovr(GLint size, GLenum type, GLsizei stride, const void* pointer)
{
    GLRemixClientArrayType array_type = GLRemixClientArrayType::COLOR;

    GLRemixClientArrayInterface& a = g_client_arrays[static_cast<UINT32>(array_type)];
    a.ipc_payload.size = static_cast<UINT32>(size);
    a.ipc_payload.type = static_cast<UINT32>(type);
    a.ipc_payload.stride = utils::InterpretStride(size, type, stride);
    a.ipc_payload.array_type = array_type;
    a.ptr = pointer;
    return;
}

void APIENTRY gl_tex_coord_pointer_ovr(GLint size, GLenum type, GLsizei stride, const void* pointer)
{
    GLRemixClientArrayType array_type = static_cast<GLRemixClientArrayType>(
        static_cast<UINT32>(GLRemixClientArrayType::TEXCOORD0) + g_client_active_texcoord_unit);

    GLRemixClientArrayInterface& a = g_client_arrays[static_cast<UINT32>(array_type)];

    a.ipc_payload.size = static_cast<UINT32>(size);
    a.ipc_payload.type = static_cast<UINT32>(type);
    a.ipc_payload.stride = utils::InterpretStride(size, type, stride);
    a.ipc_payload.array_type = array_type;
    a.ptr = pointer;
    return;
}

void APIENTRY gl_index_pointer_ovr(GLenum type, GLsizei stride, const void* pointer)
{
    GLRemixClientArrayType array_type = GLRemixClientArrayType::COLORIDX;

    GLRemixClientArrayInterface& a = g_client_arrays[static_cast<UINT32>(array_type)];
    a.ipc_payload.size = 1;
    a.ipc_payload.type = static_cast<UINT32>(type);
    a.ipc_payload.stride = utils::InterpretStride(1, type, stride);
    a.ipc_payload.array_type = array_type;
    a.ptr = pointer;
    return;
}

void APIENTRY gl_edge_flag_pointer_ovr(GLsizei stride, const void* pointer)
{
    GLRemixClientArrayType array_type = GLRemixClientArrayType::EDGEFLAG;

    GLRemixClientArrayInterface& a = g_client_arrays[static_cast<UINT32>(array_type)];
    a.ipc_payload.size = 1;
    a.ipc_payload.type = static_cast<UINT32>(GL_UNSIGNED_BYTE);
    a.ipc_payload.stride = utils::InterpretStride(1, GL_UNSIGNED_BYTE, stride);
    a.ipc_payload.array_type = array_type;
    a.ptr = pointer;
    return;
}

static UINT32 s_precompute_client_payload_bytes(GLsizei count)
{
    UINT32 total_bytes = 0;
    for (GLRemixClientArrayInterface& a : g_client_arrays)
    {
        if (!a.enabled)
        {
            continue;
        }

        const UINT32 a_bytes = utils::ComputeClientArraySize(count, a.ipc_payload.size,
                                                             a.ipc_payload.type,
                                                             a.ipc_payload.stride);

        a.ipc_payload.array_bytes = a_bytes;
        total_bytes += a_bytes;
    }

    return total_bytes;
}

static void s_fill_client_array_headers(GLRemixClientArrayHeader (&out)[NUM_CLIENT_ARRAYS])
{
    SIZE_T curr = 0;
    for (const GLRemixClientArrayInterface& i : g_client_arrays)
    {
        if (i.enabled)
        {
            out[curr] = i.ipc_payload;
            curr++;
        }
    }
}

void APIENTRY gl_draw_arrays_ovr(GLenum mode, GLint first, GLsizei count)
{
    // precompute size of all currently enabled client arrays
    const UINT32 extra_data_bytes = s_precompute_client_payload_bytes(count);

    GLRemixDrawArraysCommand payload{
        .mode = static_cast<UINT32>(mode),                             // mode
        .first = static_cast<UINT32>(first),                           // first
        .count = static_cast<UINT32>(count),                           // count
        .enabled = static_cast<UINT32>(g_enabled_client_arrays_count)  // enabled
    };

    s_fill_client_array_headers(payload.headers);

    // pass in `extra_data_bytes` but pass in the actual extra data pointers later
    g_ipc.write_command(GLCommandType::GLREMIXCMD_DRAW_ARRAYS, payload, extra_data_bytes, false,
                        nullptr);

    for (const GLRemixClientArrayInterface& a : g_client_arrays)
    {
        if (!a.enabled)
        {
            continue;
        }

        // factor in desired offset
        const UINT8* a_ptr = reinterpret_cast<const UINT8*>(a.ptr) + (first * a.ipc_payload.stride);

        // write pointer to this extra data directly
        g_ipc.write_simple(a_ptr, a.ipc_payload.array_bytes);
    }
}

static UINT32 s_read_index(SIZE_T i, GLenum type, const void* indices)
{
    switch (type)
    {
        case GL_UNSIGNED_BYTE:
            return static_cast<UINT32>(reinterpret_cast<const UINT8*>(indices)[i]);
        case GL_UNSIGNED_SHORT:
            return static_cast<UINT32>(reinterpret_cast<const UINT16*>(indices)[i]);
        case GL_UNSIGNED_INT:
            return static_cast<UINT32>(reinterpret_cast<const UINT32*>(indices)[i]);
    }
    return 0;
};

static void s_draw_elements_base(GLsizei count, GLenum type, const void* indices)
{
    thread_local std::vector<UINT8> scratch_buffer;

    for (const GLRemixClientArrayInterface& a : g_client_arrays)
    {
        if (!a.enabled)
        {
            continue;
        }

        if (a.ipc_payload.array_type == GLRemixClientArrayType::INDICES)
        {  // No need to scatter indices by themselves
            g_ipc.write_simple(a.ptr, a.ipc_payload.array_bytes);
            continue;
        }

        const UINT8* a_ptr = reinterpret_cast<const UINT8*>(a.ptr);

        const UINT32& a_bytes = a.ipc_payload.array_bytes;
        const UINT32& a_stride = a.ipc_payload.stride;

        scratch_buffer.resize(a_bytes);
        UINT8* dst_ptr = scratch_buffer.data();

        /* SCATTER STEP */
        for (UINT32 i = 0; i < static_cast<UINT32>(count); i++)
        {
            UINT32 idx = s_read_index(i, type, indices);

            const UINT8* src = a_ptr + (idx * a_stride);
            UINT8* dst = dst_ptr + (i * a_stride);

            memcpy(dst, src, a_stride);
        }

        g_ipc.write_simple(dst_ptr, a.ipc_payload.array_bytes);  // write pointer directly
    }
}

/**
 * @brief OpenGL has indices in a separate category from normal client arrays but we will send
 * it in our payload as just another client array.
 */
static void s_fake_gl_indices_pointer(GLenum type, const void* indices)
{
    GLRemixClientArrayType array_type = GLRemixClientArrayType::INDICES;

    GLRemixClientArrayInterface& a = g_client_arrays[static_cast<UINT32>(array_type)];
    a.ipc_payload.size = 1;
    a.ipc_payload.type = static_cast<UINT32>(type);
    a.ipc_payload.stride = utils::InterpretStride(1, type, 0);
    a.ipc_payload.array_type = array_type;
    a.enabled = true;
    a.ptr = indices;
}

void APIENTRY gl_draw_elements_ovr(GLenum mode, GLsizei count, GLenum type, const void* indices)
{
    s_fake_gl_indices_pointer(type, indices);

    const UINT32 extra_data_bytes = s_precompute_client_payload_bytes(count);

    GLRemixDrawElementsCommand payload{ .mode = static_cast<UINT32>(mode),
                                        .count = static_cast<UINT32>(count),
                                        .type = static_cast<UINT32>(type),
                                        .enabled = g_enabled_client_arrays_count };

    s_fill_client_array_headers(payload.headers);

    g_ipc.write_command(GLCommandType::GLREMIXCMD_DRAW_ELEMENTS, payload, extra_data_bytes, false,
                        nullptr);

    s_draw_elements_base(count, type, indices);
}

void APIENTRY gl_draw_range_elements_ovr(GLenum mode, GLuint start, GLuint end, GLsizei count,
                                         GLenum type, const void* indices)
{
    s_fake_gl_indices_pointer(type, indices);

    const UINT32 extra_data_bytes = s_precompute_client_payload_bytes(count);

    GLRemixDrawRangeElementsCommand payload{ .mode = static_cast<UINT32>(mode),
                                             .start = static_cast<UINT32>(start),
                                             .count = static_cast<UINT32>(count),
                                             .type = static_cast<UINT32>(type),
                                             .enabled = g_enabled_client_arrays_count };

    s_fill_client_array_headers(payload.headers);

    g_ipc.write_command(GLCommandType::GLREMIXCMD_DRAW_RANGE_ELEMENTS, payload, extra_data_bytes,
                        false, nullptr);

    s_draw_elements_base(count, type, indices);
}

}  // namespace glRemix::hooks
