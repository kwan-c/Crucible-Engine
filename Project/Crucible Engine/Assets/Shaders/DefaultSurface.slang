// The built-in surface: glTF metallic-roughness, five textures times the material's factors
// (Crucible::MaterialFactors, the first fields of the push constant). What every material used
// before shader graphs, and what a material without a graph still uses.
// See Surface.slang for how a surface function and SurfaceTemplate.slang fit together.
//
// Every material (Renderer::CreateMaterial) binds set 1's five texture slots below; any slot the
// caller left Invalid was already substituted with a neutral placeholder texture on the C++ side
// (Renderer::WriteMaterialDescriptorSet), so this shader never has to branch on "is this map
// present".

#include "Surface.slang"

// Set 1: the current draw's material. Binding order must match
// VulkanPipeline::CreateMaterialDescriptorSetLayout exactly.
[[vk::binding(0, 1)]] Sampler2D albedoTex;
[[vk::binding(1, 1)]] Sampler2D normalTex;
[[vk::binding(2, 1)]] Sampler2D metallicRoughnessTex;   // glTF convention: G = roughness, B = metallic
[[vk::binding(3, 1)]] Sampler2D emissiveTex;
[[vk::binding(4, 1)]] Sampler2D occlusionTex;           // R channel only, per glTF

SurfaceData EvaluateSurface(SurfaceInput s) {
    SurfaceData o;

    float4 albedoSample = albedoTex.Sample(s.uv);
    o.albedo = albedoSample.rgb * pc.albedoTint.rgb * s.vertexColour;
    // Per-object colour tint: MaterialParams::custom (InstanceData::params.yzw). All zero (the
    // default, and objects without MaterialParams) = no tint, so existing objects are unchanged.
    if (any(s.custom != float3(0.0, 0.0, 0.0))) { o.albedo *= s.custom; }
    o.alpha = albedoSample.a * pc.albedoTint.a;

    float3 mrSample = metallicRoughnessTex.Sample(s.uv).rgb;
    o.roughness = mrSample.g * pc.roughnessFactor;
    o.metallic  = mrSample.b * pc.metallicFactor;

    o.occlusion = occlusionTex.Sample(s.uv).r;
    o.emissive  = emissiveTex.Sample(s.uv).rgb * pc.emissiveFactor;
    
    // Unpack x and y [0,1] -> [-1,1] and rebuild z from them (a unit vector pointing out of the
    // surface): BC5 normal maps store only x and y, and for every other normal map this gives the
    // z it already had.
    o.normalTS.xy = normalTex.Sample(s.uv).rg * 2.0 - 1.0;
    o.normalTS.z  = sqrt(saturate(1.0 - dot(o.normalTS.xy, o.normalTS.xy)));

    return o;
}
