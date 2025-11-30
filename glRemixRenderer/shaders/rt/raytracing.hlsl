#include "shared_structs.h"

#define GL_ObjectToWorld3x4() WorldToObject3x4()
#define GL_WorldToObject3x4() ObjectToWorld3x4()

RaytracingAccelerationStructure scene : register(t0);

RWTexture2D<float4> color_output : register(u0);
RWTexture2D<float4> normal_roughness : register(u1);
RWTexture2D<float2> motion_vectors : register(u2);
RWTexture2D<float> z_view : register(u3);

ConstantBuffer<RayGenConstantBuffer> g_raygen_cb : register(b0);

struct LightCB
{
    Light lights[8];
};
ConstantBuffer<LightCB> light_cb : register(b1);

StructuredBuffer<GPUMeshRecord> meshes : register(t1);

SamplerState g_sampler : register(s0);

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/intersection-attributes
typedef BuiltInTriangleIntersectionAttributes TriAttributes;

// helper functions
float rand(inout uint seed)
{
    seed = 1664525u * seed + 1013904223u;
    return float(seed & 0x00FFFFFFu) / 16777216.0;
}

float3 cosine_sample_hemisphere(float2 xi)
{
    float r = sqrt(xi.x);
    float theta = 2.0f * 3.14159265 * xi.y;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - xi.x));
    return float3(x, y, z);
}

float3 transform_to_world(float3 local_dir, float3 N)
{
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(local_dir.x * tangent + local_dir.y * bitangent + local_dir.z * N);
}

[shader("raygeneration")]void RayGenMain()
{
    float2 uv = (float2) DispatchRaysIndex() / float2(g_raygen_cb.dimensions);
    uint seed = uint(DispatchRaysIndex().x * 1973 + DispatchRaysIndex().y * 9277 + 891);
    
    uint max_bounces = 3;
    int num_samples_per_pixel = 5;
    float3 final_color = float3(0, 0, 0);
    
    float3 primary_normal = float3(0, 0, 0);
    float primary_roughness = 0.0f;
    float primary_z = 0.0f;
    float3 primary_hit_pos = float3(0, 0, 0);
    float3 primary_obj_pos = float3(0, 0, 0);  // Object-space position for motion vectors
    uint primary_instance_id = 0;
    bool primary_hit = false;
    
    for (int sample = 0; sample < num_samples_per_pixel; ++sample)
    {
        // jitter for anti-aliasing
        float2 jitter = float2(rand(seed), rand(seed)) / float2(g_raygen_cb.dimensions);
        float2 jittered_uv = uv + jitter;

        float2 ndc = uv * 2.0f - 1.0f;
        ndc.y = -ndc.y; // Flip Y for correct screen space orientation

        // Transform from NDC to world space using inverse view-projection
        // Near plane point in clip space
        float4 near_point = float4(ndc, 0.0, 1.0);
        float4 far_point = float4(ndc, 1.0, 1.0); // look down -Z match gl convention

        // Transform to world space
        float4 near_world = mul(near_point, g_raygen_cb.current.inv_view_proj);
        float4 far_world = mul(far_point, g_raygen_cb.current.inv_view_proj);

        near_world /= near_world.w;
        far_world /= far_world.w;

        float3 origin = near_world.xyz;
        float3 ray_dir = normalize(far_world.xyz - near_world.xyz);

        float3 sample_color = float3(0, 0, 0);
        float3 throughput = 1.0;
        
        RayPayload payload;

        for (uint bounce = 0; bounce < max_bounces; ++bounce)
        {
            payload.color = float4(0, 0, 0, 0);
            payload.normal = float3(0, 0, 0);
            payload.roughness = 1.0; // TODO: Use actual roughness
            payload.hit = false;
            payload.obj_pos = float3(0, 0, 0);
            payload.instance_id = 0;

            RayDesc ray;
            ray.Origin = origin;
            ray.Direction = ray_dir;
            ray.TMin = 0.001;
            ray.TMax = 10000.0;

            // Note winding order if you don't see anything
            TraceRay(scene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);

            if (sample == 0 && bounce == 0)
            {
                primary_hit = payload.hit;
                if (payload.hit)
                {
                    primary_normal = payload.normal;
                    primary_roughness = payload.roughness;
                    primary_hit_pos = payload.hit_pos;
                    primary_obj_pos = payload.obj_pos;
                    primary_instance_id = payload.instance_id;
                    primary_z = length(payload.hit_pos - origin);
                }
            }

            if (!payload.hit)
            {
                // same as miss shader
                sample_color += throughput * payload.color.rgb;
                break;
            }

            sample_color += throughput * payload.color.rgb;

            float2 xi = float2(rand(seed), rand(seed));
            float3 local_dir = cosine_sample_hemisphere(xi);
            float3 N = normalize(payload.normal);
            float3 new_dir = transform_to_world(local_dir, N);

            ray_dir = new_dir;
            origin = payload.hit_pos + ray_dir * 0.001;
        }

        final_color += sample_color;
    }
    
    final_color /= num_samples_per_pixel; // average samples
    
    color_output[DispatchRaysIndex().xy] = float4(final_color, 1.0);

    normal_roughness[DispatchRaysIndex().xy] = float4(primary_normal, primary_roughness);
    
    float2 motion = float2(0, 0);
    float3 prev_world_pos = float3(0, 0, 0);
    
    if (primary_hit)
    {
        GPUMeshRecord hit_mesh = meshes[primary_instance_id];
        
        float3 curr_world_pos = mul(hit_mesh.curr_obj_to_world, float4(primary_obj_pos, 1.0));
        prev_world_pos = mul(hit_mesh.prev_obj_to_world, float4(primary_obj_pos, 1.0));
        
        // Project current and previous world positions to clip space
        float4 curr_clip = mul(float4(curr_world_pos, 1.0), g_raygen_cb.current.view_proj);
        float4 prev_clip = mul(float4(prev_world_pos, 1.0), g_raygen_cb.previous.view_proj);

        float2 curr_ndc = curr_clip.xy / curr_clip.w;
        float2 prev_ndc = prev_clip.xy / prev_clip.w;

        // Flip Y
        curr_ndc.y = -curr_ndc.y;
        prev_ndc.y = -prev_ndc.y;

        float2 curr_pixel = (curr_ndc * 0.5 + 0.5) * float2(g_raygen_cb.dimensions);
        float2 prev_pixel = (prev_ndc * 0.5 + 0.5) * float2(g_raygen_cb.dimensions);
        
        // Motion vector in pixel space
        motion = curr_pixel - prev_pixel;
    }
    motion_vectors[DispatchRaysIndex().xy] = motion;

    z_view[DispatchRaysIndex().xy] = primary_z;
}

[shader("closesthit")]void ClosestHitMain(inout RayPayload payload, in TriAttributes attr)
{
    // Fetch mesh record for this instance; indices are into the global SRV heap
    const GPUMeshRecord mesh = meshes[InstanceID()];

    // Bindless fetch of vertex and index buffers from the global SRV heap
    StructuredBuffer<Vertex> vb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.vb_idx)];
    StructuredBuffer<uint> ib = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.ib_idx)];

    // Triangle indices for this primitive
    const uint tri = PrimitiveIndex();
    const uint i0 = ib[tri * 3 + 0];
    const uint i1 = ib[tri * 3 + 1];
    const uint i2 = ib[tri * 3 + 2];

    // Vertex fetch
    const Vertex v0 = vb[i0];
    const Vertex v1 = vb[i1];
    const Vertex v2 = vb[i2];

    float3 bary;
    bary.yz = attr.barycentrics;
    bary.x = 1.0f - bary.y - bary.z;

    // Interpolate attributes
    float2 uv = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;
    float3 albedo = v0.color.rgb * bary.x + v1.color.rgb * bary.y + v2.color.rgb * bary.z;
    float3 n_obj = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);

    // Transform normal to world space (assumes uniform scale)
    float3x3 o2w3x3 = (float3x3) GL_ObjectToWorld3x4();
    float3 n_world = normalize(mul(n_obj, o2w3x3));

    const float3 hit_pos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    Material mat;
    mat.ambient = float4(1.0, 1.0, 1.0, 1.0);
    mat.diffuse = float4(1.0, 1.0, 1.0, 1.0);
    mat.specular = float4(0.0, 0.0, 0.0, 1.0);
    mat.emission = float4(0.0, 0.0, 0.0, 1.0);
    mat.shininess = 32.0;

    // Fetch material
    if (mesh.mat_buffer_idx != 0xFFFFFFFFu)  // -1
    {
        StructuredBuffer<Material> mat_buf
            = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.mat_buffer_idx)];
        mat = mat_buf[mesh.mat_idx];
    }
    
    float3 tex_albedo = float3(1.0, 1.0, 1.0);
    
    // Fetch texture
    if (mesh.tex_idx != 0xFFFFFFFFu)
    {
        Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.tex_idx)];
        float4 tex_sample = tex.SampleLevel(g_sampler, uv, 0.0f);
        tex_albedo = tex_sample.rgb;
    }
        
    float3 final_color = float3(0, 0, 0);
    
    // iterate through lights
    for (int i = 0; i < 8; ++i)
    {
        Light curr_light = light_cb.lights[i];
        if (!curr_light.enabled)
        {
            continue;
        }
        
        float3 light_pos = curr_light.position.xyz;
        float3 light_dir = normalize(light_pos - hit_pos);
        float light_dist = length(light_pos - hit_pos);
        
        // attenuation
        float attenuation = 1.0 /
            ((curr_light.constant_attenuation) +
             (curr_light.linear_attenuation * light_dist) +
             (curr_light.quadratic_attenuation * light_dist * light_dist));

        // diffuse
        float n_dot_l = max(dot(n_world, light_dir), 0.0);
        float3 diffuse = mat.diffuse.rgb * tex_albedo * curr_light.diffuse.rgb * albedo * n_dot_l;
        
        // ambient
        float3 ambient = mat.ambient.rgb * tex_albedo * curr_light.ambient.rgb * albedo;

        float3 color = (diffuse + ambient) * attenuation;
        final_color += color;
    }

    payload.hit_pos = hit_pos;
    payload.normal = n_world;
    payload.roughness = 0.0; // TODO: Use actual roughness
    payload.hit = true;
    payload.color = float4(final_color, 1.0);

    // Interpolate object-space position directly from vertices
    payload.obj_pos = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
    payload.instance_id = InstanceID();
}


[shader("miss")]void MissMain(inout RayPayload payload)
{
    payload.color = float4(0.0, 0.0, 0.0, 1.0);
    payload.roughness = 1.0f;
    payload.hit = false;
}
