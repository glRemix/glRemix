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

TextureCube<float4> environment_map : register(t2);
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

float3 direct_lighting(float3 hit_pos, float3 N, float3 albedo)
{
    float3 direct = 0.0f;

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        Light curr_light = light_cb.lights[i];
        if (!curr_light.enabled)
            continue;

        float3 light_pos = curr_light.position.xyz;
        float3 L = normalize(light_pos - hit_pos);
        float dist = length(light_pos - hit_pos);

        // attenuation
        float attenuation = 1.0 /
            (curr_light.constant_attenuation +
             curr_light.linear_attenuation * dist +
             curr_light.quadratic_attenuation * dist * dist);

        float NdotL = max(dot(N, L), 0.0);

        float3 ambient = albedo * curr_light.ambient.rgb;
        float3 diffuse = albedo * curr_light.diffuse.rgb * NdotL;

        direct += (ambient + diffuse) * attenuation;
    }

    return direct;
}

float compute_lod(float3 N, float3 viewDir, float dist)
{
    const float mipStartDist = 8.0f;
    const float mipScale = 0.02f; // how fast LOD grows with distance

    float lod = 0.0f;
    if (dist > mipStartDist)
    {
        float d = (dist - mipStartDist) * mipScale;
        lod = log2(max(d, 1e-4f));
    }

    float NdotV = abs(dot(N, viewDir));
    float facing = saturate(NdotV);

    float angleFactor = lerp(1.5f, 0.4f, facing);
    lod *= angleFactor;

    return lod;
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

        for (uint bounce = 0; bounce < max_bounces; ++bounce)
        {
            RayPayload payload;
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
                // env hit (treat payload.color as radiance)
                sample_color += throughput * payload.color.rgb;
                break;
            }

            float3 N = normalize(payload.normal);
            float3 albedo = payload.color.rgb;
            float3 hit_pos = payload.hit_pos;
            
            // direct lighting contribution on first bounce
            if (bounce == 0)
            {
                float3 direct = direct_lighting(hit_pos, N, albedo);
                sample_color += throughput * direct;
            }
            
            float2 xi = float2(rand(seed), rand(seed));
            float3 local_dir = cosine_sample_hemisphere(xi);
            float3 new_dir = transform_to_world(local_dir, N);

            throughput *= albedo;
            
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
    float3 vertex_color = v0.color.rgb * bary.x + v1.color.rgb * bary.y + v2.color.rgb * bary.z;
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
        
        float3 V = -normalize(WorldRayDirection());
        float3 N = normalize(n_world);
        float dist = RayTCurrent();
        
        float lod = compute_lod(N, V, dist);

        float4 tex_sample = tex.SampleLevel(g_sampler, uv, lod);
        tex_albedo = tex_sample.rgb;
    }
    
    bool has_vertex = dot(vertex_color, vertex_color) > 1e-4;
    bool has_tex = mesh.tex_idx != 0xFFFFFFFFu && dot(tex_albedo, tex_albedo) > 1e-4;
    bool has_mat_diff = dot(mat.diffuse.rgb, mat.diffuse.rgb) > 1e-4;
    
    float3 base = float3(1, 1, 1);
    
    if (has_tex)
    {
        base = tex_albedo;
        
        if (has_vertex)
        {
            base *= vertex_color;
        }
    }
    else
    {
        if (has_vertex)
        {
            base *= vertex_color;
        }
        if (has_mat_diff)
        {
            base *= mat.diffuse.rgb;
        }
    }
    
    
    if (dot(base, base) < 1e-6)
    {
        if (has_tex)
        {
            base = tex_albedo;
        }
        else if (has_mat_diff)
        {
            base = mat.diffuse.rgb;
        }
        else if (has_vertex)
        {
            base = vertex_color;
        }
        else
        {
            base = float3(1.0, 1.0, 1.0);

        }
    }
    
    payload.hit_pos = hit_pos;
    payload.normal = n_world;
    payload.hit = true;
    payload.color = float4(base, 1.0);
}


[shader("miss")]void MissMain(inout RayPayload payload)
{
    float3 dir = normalize(WorldRayDirection());
    float3 env_color = environment_map.SampleLevel(g_sampler, dir, 0.0f).rgb;

    float intensity = 1.0f;
    payload.hit = false;
    payload.color = float4(env_color * intensity, 1.0f);
}
