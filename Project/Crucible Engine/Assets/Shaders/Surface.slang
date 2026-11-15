// The contract between a material's surface function and the engine's mesh shader.
//
// A mesh shader is two parts. The SURFACE part answers one question per pixel - "what is this
// surface like here?" (albedo, normal, metallic, roughness, emissive, occlusion, alpha) - in one
// function:
//
//     SurfaceData EvaluateSurface(SurfaceInput s)
//
// The TEMPLATE part (SurfaceTemplate.slang) is everything else, written once by hand: the entry
// points, skinning, the tangent frame, cut-out discard, toon and PBR lighting, shadows, fog and
// the outline line data. It calls EvaluateSurface and nothing else material-specific.
//
// DefaultSurface.slang is the built-in surface (five textures times the material's factors).
// A shader graph generates another one. Either way a complete shader is two includes, the surface
// first:
//
//     #include "DefaultSurface.slang"
//     #include "SurfaceTemplate.slang"
//
// (Any .slang that CompileShaders.cmake doesn't list is a shared include: editing it rebuilds
// every shader.)

#ifndef CRUCIBLE_SURFACE_SLANG
#define CRUCIBLE_SURFACE_SLANG

// What the template hands a surface function about the pixel being shaded.
struct SurfaceInput {
    float2 uv;
    float3 worldPos;
    float3 normalWS;       // the mesh's own normal (unit; already flipped on a double-sided back face)
    float3 tangentWS;      // unit, perpendicular to normalWS: the direction u grows in
    float3 bitangentWS;    // unit, perpendicular to both: the direction v grows in
    float3 vertexColour;
    float3 viewDir;        // unit, from the surface towards the camera
    float  time;           // seconds since the Renderer started (frame.time.x)
    float3 custom;         // MaterialParams::custom (InstanceData::params.yzw): per object, all zero by default
};

// What a surface function answers. The template clamps metallic and roughness to what the
// lighting can take and normalizes the normal, so a surface cannot break the lighting.
struct SurfaceData {
    float3 albedo;         // linear
    float3 normalTS;       // tangent space: x along tangentWS, y along bitangentWS, z out of the
                           // surface. (0, 0, 1) = the mesh's own normal.
    float  metallic;       // 0..1
    float  roughness;      // 0..1
    float3 emissive;       // linear radiance, before the object's own glow multiplier
    float  occlusion;      // 1 = open, lower = ambient light blocked
    float  alpha;          // only compared against the material's alpha cutoff (cut-out materials)
};

// Per-draw data: the current material's scalar settings (constant across a batch, since batches
// never cross materials). Must match Crucible::PushConstants (RenderTypes.h) byte for byte - the
// first five fields are Crucible::MaterialFactors. Written out flat (not as a nested struct) so
// the offsets are the C++ ones under every packing rule.
struct PushConstants {
    float4 albedoTint;
    float3 emissiveFactor;
    float  metallicFactor;
    float  roughnessFactor;
    float  alphaCutoff;   // 0 = solid
    uint   flags;         // bits 0-1: shading (0 = the scene's, 1 = toon, 2 = realistic),
                          // bit 2: toonEdge applies, bit 3: toonShade applies
    float  outline;       // toon outlines on this material (0..1)
    float4 toonEdge;      // x = edge position, y = softness (the material's own, flag bit 2)
    float4 toonShade;     // rgb = shade tint, linear (the material's own, flag bit 3)
    float4 toonLit;       // rgb = tint of the lit side, linear (white = none)
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

#endif // CRUCIBLE_SURFACE_SLANG
