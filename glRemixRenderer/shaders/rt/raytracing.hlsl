#include "shared_structs.h"

#define GL_ObjectToWorld3x4() WorldToObject3x4()
#define GL_WorldToObject3x4() ObjectToWorld3x4()

RaytracingAccelerationStructure scene : register(t0);

RWTexture2D<float4> render_target : register(u0);
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
    return float(seed & 0x00FFFFFFu) / 16777216.0f;
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
    float3 up = abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(local_dir.x * tangent + local_dir.y * bitangent + local_dir.z * N);
}

[shader("raygeneration")]void RayGenMain()
{
    float2 uv = (float2) DispatchRaysIndex() / float2(g_raygen_cb.width, g_raygen_cb.height);
    uint seed = uint(DispatchRaysIndex().x * 1973 + DispatchRaysIndex().y * 9277 + 891);
    
    uint max_bounces = 3;
    int num_samples_per_pixel = 5;
    float3 final_color = float3(0, 0, 0);
    
    for (int sample = 0; sample < num_samples_per_pixel; ++sample)
    {
        // jitter for anti-aliasing
        float2 jitter = float2(rand(seed), rand(seed)) / float2(g_raygen_cb.width, g_raygen_cb.height);
        float2 jittered_uv = uv + jitter;

        float2 ndc = uv * 2.0f - 1.0f;
        ndc.y = -ndc.y; // Flip Y for correct screen space orientation

        // Transform from NDC to world space using inverse view-projection
        // Near plane point in clip space
        float4 near_point = float4(ndc, 0.0f, 1.0f);
        float4 far_point = float4(ndc, 1.0f, 1.0f); // look down -Z match gl convention

        // Transform to world space
        float4 near_world = mul(near_point, g_raygen_cb.inv_view_proj);
        float4 far_world = mul(far_point, g_raygen_cb.inv_view_proj);

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
            payload.hit = false;

            RayDesc ray;
            ray.Origin = origin;
            ray.Direction = ray_dir;
            ray.TMin = 0.001;
            ray.TMax = 10000.0;

        // Note winding order if you don't see anything
        TraceRay(scene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);

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
            origin = payload.hit_pos + ray_dir * 0.001f;
        }

        final_color += sample_color;
    }
    
    final_color /= num_samples_per_pixel; // average samples
    
    render_target[DispatchRaysIndex().xy] = float4(final_color, 1.0);
    // RenderTarget[DispatchRaysIndex().xy] = float4(uv, 0.0, 1.0);
    // RenderTarget[DispatchRaysIndex().xy] = float4(ray_dir * 0.5 + 0.5, 1.0);
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

    //// DEBUG
    // payload.color = float4(v0.position, 1.0); payload.hit = true; return;
    // payload.color = float4(v0.color, 1.0); payload.hit = true; return;
    // payload.color = float4(v0.normal * 0.5 + 0.5, 1.0); payload.hit = true; return;

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
        float3 diffuse = mat.diffuse.rgb * curr_light.diffuse.rgb * albedo * n_dot_l;
        
        // ambient
        float3 ambient = mat.ambient.rgb * curr_light.ambient.rgb * albedo;

        float3 color = (diffuse + ambient) * attenuation;
        final_color += color;
    }

    payload.hit_pos = hit_pos;
    payload.normal = n_world;
    payload.hit = true;
    payload.color = float4(final_color, 1.0);
}


[shader("miss")]void MissMain(inout RayPayload payload)
{
    payload.color = float4(0.0, 0.0, 0.0, 1.0);
    payload.hit = false;
}
