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
// A surface of its own (MagicOrb.slang, a shader graph) can have PARAMETERS of its own, set per
// material: numbers and colours read with MaterialParam(slot), textures declared with
// MATERIAL_TEXTURE(slot, name). Which slot is what is listed in <ShaderName>.params.json beside
// the shader - see the end of this file.
//
// A surface can also MOVE the mesh's vertices - grass that sways, a flag, a wobbling blob - by
// defining SURFACE_MOVES_VERTICES and a second function before it includes the template:
//
//     #define SURFACE_MOVES_VERTICES
//     float3 EvaluateVertexOffset(SurfaceInput s)     // metres, in the world; added to the vertex
//
// It is given the vertex the way EvaluateSurface is given a pixel (uv, worldPos, normalWS ...),
// and the template runs it in EVERY pass that draws the mesh - the scene, the sun's shadow, the
// ambient-occlusion pre-pass and the editor's selection outline - so all of them move together.
// Such a shader has eleven more entry points (the end of SurfaceTemplate.slang); the engine
// sees them in the compiled module and builds the extra pipelines (MaterialShaders). What the
// function may not do, because those passes do not have it: read `pc` (the material's factors),
// or sample a texture with Sample() - use SampleLevel(uv, 0.0) in a vertex stage.
// A hand-written shader that does this needs those entry points on its line in
// CompileShaders.cmake as well. The engine still decides what is in view by the mesh's own
// box, so an offset that is large against the object can make it vanish at the screen's edge.
// Lighting uses the mesh's own normals: the offset moves the surface, it does not turn it.
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
    uint   paramOffset;   // where this material's own parameters start in materialParams (below)
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

// ---- A shader's own parameters -------------------------------------------------------------------
// Every material's values for its shader's parameters, one float4 per parameter, all materials one
// after another (set 0, binding 11; one buffer per frame in flight, filled by RenderResources).
// The built-in surface has none: its settings are the factors in the push constant above.
[[vk::binding(11, 0)]] StructuredBuffer<float4> materialParams;

// Where the material being drawn has its parameters in materialParams. Every entry point of the
// template sets this first, from its own pass's push constant (the scene's is pc.paramOffset;
// the depth-only passes have one of their own), so MaterialParam reads the same in every pass.
static uint gMaterialParamOffset = 0;

// This material's value for parameter `slot` - its position in the shader's parameter list
// (<ShaderName>.params.json, first = 0). A float is in .x, a float2 in .xy, a colour in .rgb.
float4 MaterialParam(uint slot) {
    return materialParams[gMaterialParamOffset + slot];
}

// Declares this shader's texture `slot` (0-7: its position in the list's "textures") under the
// name `name`, to sample as usual: name.Sample(s.uv). Set 1 holds eight texture slots for every
// material; a slot the material leaves empty holds the default the list names (white, black or a
// flat normal).
//     MATERIAL_TEXTURE(0, patternTex);
#define MATERIAL_TEXTURE(slot, name) [[vk::binding(slot, 1)]] Sampler2D name

#endif // CRUCIBLE_SURFACE_SLANG
