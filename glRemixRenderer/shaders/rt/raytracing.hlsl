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

static const float EPSILON = 1e-8f;

bool solve_linear_system(float2x2 A, float2 B, out float x0, out float x1)
{
    float det = A[0][0] * A[1][1] - A[0][1] * A[1][0];
    if (abs(det) < EPSILON)
    {
        x0 = 0.0f;
        x1 = 0.0f;
        return false;
    }

    float invDet = 1.0f / det;
    x0 = (A[1][1] * B.x - A[0][1] * B.y) * invDet;
    x1 = (A[0][0] * B.y - A[1][0] * B.x) * invDet;

    if (isnan(x0) || isnan(x1))
        return false;

    return true;
}

void calculate_triangle_surface_differential(
    float3 p0, float3 p1, float3 p2,
    float2 uv0, float2 uv1, float2 uv2,
    out float3 dpdu, out float3 dpdv)
{
    float2 duv02 = uv0 - uv2;
    float2 duv12 = uv1 - uv2;
    float determinant = duv02.x * duv12.y - duv02.y * duv12.x;

    float3 dp02 = p0 - p2;
    float3 dp12 = p1 - p2;

    if (abs(determinant) < 1e-8f)
    {
        float3 ng = normalize(cross(p2 - p0, p1 - p0));

        if (abs(ng.x) > abs(ng.y))
        {
            dpdu = float3(-ng.z, 0.0f, ng.x);
            dpdu /= sqrt(ng.x * ng.x + ng.z * ng.z + 1e-8f);
        }
        else
        {
            dpdu = float3(0.0f, ng.z, -ng.y);
            dpdu /= sqrt(ng.y * ng.y + ng.z * ng.z + 1e-8f);
        }

        dpdv = cross(ng, dpdu);
    }
    else
    {
        float invdet = 1.0f / determinant;
        dpdu = (duv12.y * dp02 - duv02.y * dp12) * invdet;
        dpdv = (-duv12.x * dp02 + duv02.x * dp12) * invdet;
    }
}

// adapted from yining karl li and pbrt (https://blog.yiningkarlli.com/2018/10/bidirectional-mipmap.html#2018-10-25-mipmap-level-selection-and-ray-differentials)
float4 calculate_screen_space_differential(
    float3 p,
    float3 n,
    float3 origin,
    float3 rDirection,
    float3 xorigin,
    float3 rxDirection,
    float3 yorigin,
    float3 ryDirection,
    float3 dpdu,
    float3 dpdv
)
{
    float d = dot(n, p);


    float denomX = dot(n, rxDirection);
    float tx = -(dot(n, xorigin) - d) / denomX;
    float3 px = xorigin + tx * rxDirection;

    float denomY = dot(n, ryDirection);
    float ty = -(dot(n, yorigin) - d) / denomY;
    float3 py = yorigin + ty * ryDirection;

    float3 dpdx = px - p;
    float3 dpdy = py - p;

    int2 dim;
    float ax = abs(n.x);
    float ay = abs(n.y);
    float az = abs(n.z);

    if (ax > ay && ax > az)
        dim = int2(1, 2);
    else if (ay > az)
        dim = int2(0, 2);
    else
        dim = int2(0, 1);

    float2x2 A;
    A[0][0] = dpdu[dim.x];
    A[0][1] = dpdv[dim.x];
    A[1][0] = dpdu[dim.y];
    A[1][1] = dpdv[dim.y];

    float2 Bx = float2(px[dim.x] - p[dim.x], px[dim.y] - p[dim.y]);
    float2 By = float2(py[dim.x] - p[dim.x], py[dim.y] - p[dim.y]);

    float dudx, dvdx, dudy, dvdy;

    if (!solve_linear_system(A, Bx, dudx, dvdx))
    {
        dudx = 0.0f;
        dvdx = 0.0f;
    }

    if (!solve_linear_system(A, By, dudy, dvdy))
    {
        dudy = 0.0f;
        dvdy = 0.0f;
    }

    return float4(dudx, dvdx, dudy, dvdy);
}

float lod_from_surface_differential(float dudx, float dvdx, float dudy, float dvdy, int maxMip)
{
    float sx = length(float2(dudx, dvdx));
    float sy = length(float2(dudy, dvdy));
    float footprint = max(sx, sy);
    footprint = max(footprint, 1e-8f);

    float lod = log2(footprint);

    lod = clamp(lod, 0.0f, (float) (maxMip - 1u));
    return lod;
}

float lod_heuristic(float3 N, float3 viewDir, float dist)
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

void generate_camera_ray(float2 uv, out float3 origin, out float3 dir)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 near_point = float4(ndc, 0.0f, 1.0f);
    float4 far_point = float4(ndc, 1.0f, 1.0f);

    float4 near_world = mul(near_point, g_raygen_cb.inv_view_proj);
    float4 far_world = mul(far_point, g_raygen_cb.inv_view_proj);

    near_world /= near_world.w;
    far_world /= far_world.w;

    origin = near_world.xyz;
    dir = normalize(far_world.xyz - near_world.xyz);
}

[shader("raygeneration")]void RayGenMain()
{
    float2 base_uv = (float2) DispatchRaysIndex() / float2(g_raygen_cb.width, g_raygen_cb.height);
    uint seed = uint(DispatchRaysIndex().x * 1973 + DispatchRaysIndex().y * 9277 + 891);
    
    uint max_bounces = 3;
    int num_samples_per_pixel = 5;
    float3 final_color = float3(0, 0, 0);
    
    for (int sample = 0; sample < num_samples_per_pixel; ++sample)
    {
        // jitter for anti-aliasing
        float2 jitter = float2(rand(seed), rand(seed)) / float2(g_raygen_cb.width, g_raygen_cb.height);
        float2 jittered_uv = base_uv + jitter;

        float3 origin, ray_dir;
        generate_camera_ray(jittered_uv, origin, ray_dir);
        
        float2 pixelSize = float2(1.0f / g_raygen_cb.width, 1.0f / g_raygen_cb.height);
        
        float3 rx_origin, rx_dir;
        float3 ry_origin, ry_dir;
        generate_camera_ray(jittered_uv + float2(pixelSize.x, 0.0f), rx_origin, rx_dir);
        generate_camera_ray(jittered_uv + float2(0.0f, pixelSize.y), ry_origin, ry_dir);

        float3 sample_color = float3(0, 0, 0);
        float3 throughput = 1.0;

        for (uint bounce = 0; bounce < max_bounces; ++bounce)
        {
            RayPayload payload;
            payload.hit = false;

            payload.ray_origin = origin;
            payload.ray_dir = ray_dir;
            payload.rx_origin = rx_origin;
            payload.rx_dir = rx_dir;
            payload.ry_origin = ry_origin;
            payload.ry_dir = ry_dir;
            
            payload.depth = bounce;
            
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

        uint texWidth, texHeight, texMips;
        tex.GetDimensions(0, texWidth, texHeight, texMips);

        float3 p = hit_pos;
        float3 N = normalize(n_world);

        if (payload.depth == 0)
        {
            float3 origin = payload.ray_origin;
            float3 rDirection = payload.ray_dir;

            float3 xorigin = payload.rx_origin;
            float3 rxDirection = payload.rx_dir;

            float3 yorigin = payload.ry_origin;
            float3 ryDirection = payload.ry_dir;

            float3 p0 = v0.position;
            float3 p1 = v1.position;
            float3 p2 = v2.position;

            float2 uv0 = v0.uv;
            float2 uv1 = v1.uv;
            float2 uv2 = v2.uv;

            float3 dpdu_obj, dpdv_obj;
            calculate_triangle_surface_differential(p0, p1, p2, uv0, uv1, uv2, dpdu_obj, dpdv_obj);

            float3 dpdu = mul(dpdu_obj, o2w3x3);
            float3 dpdv = mul(dpdv_obj, o2w3x3);

            float4 dUV = calculate_screen_space_differential(
                p, N,
                origin, rDirection,
                xorigin, rxDirection,
                yorigin, ryDirection,
                dpdu, dpdv);

            float dudx = dUV.x;
            float dvdx = dUV.y;
            float dudy = dUV.z;
            float dvdy = dUV.w;

            float lod = lod_from_surface_differential(dudx, dvdx, dudy, dvdy, texMips);

            float4 tex_sample = tex.SampleLevel(g_sampler, uv, lod);
            tex_albedo = tex_sample.rgb;
        }
        else
        {
            float3 V = -normalize(WorldRayDirection());
            float dist = RayTCurrent();
            float lod = lod_heuristic(N, V, dist);

            lod = clamp(lod, 0.0f, (float) (texMips - 1u));

            float4 tex_sample = tex.SampleLevel(g_sampler, uv, lod);
            tex_albedo = tex_sample.rgb;
        }
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