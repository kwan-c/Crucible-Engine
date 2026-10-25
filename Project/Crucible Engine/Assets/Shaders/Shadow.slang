// Crucible Engine shadow caster pass: depth only, one cascade of the sun's shadow map per draw
// list (Renderer::DrawShadowCascades). No fragment shader - the rasterizer writes depth alone.
// The pipeline (VulkanPipeline::CreateShadowPipeline) adds slope-scaled depth bias and doesn't cull,
// so open meshes (planes, single-sided cards) still cast.
// Cut-out materials (leaves) use the second pair of entry points: the albedo texture's alpha
// decides which pixels cast, so the shadow has the leaves' shape.

// Only the prefix this shader reads; the full struct is in DefaultShader.slang.
struct FrameUniforms {
    float4x4 view;
    float4x4 proj;
    float4   cameraPosWS;
    float4   ambientSky;
    float4   ambientGround;
    float4   renderParams;
    uint4    lightParams;
    float4x4 shadowViewProj[4];
};

#include "ModelVertex.slang"

// Must match ShadowPass's push constant: the cascade, then Crucible::CutoutPush.
struct ShadowPush {
    uint  cascade;       // which shadowViewProj to use (also the array layer being rendered)
    float alphaCutoff;   // cut-out pipeline only
    float alphaTint;     // the material's albedoTint.a
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms>  frame;

[[vk::binding(0, 1)]] Sampler2D                      albedoTex;   // the material's (set 1), cut-out only
[[vk::push_constant]] ConstantBuffer<ShadowPush>     pc;

[shader("vertex")]
float4 shadowVert([[vk::location(0)]] float3 position : POSITION, [[vk::location(5)]] uint4 joints : BLENDINDICES, [[vk::location(6)]] float4 weights : BLENDWEIGHT, uint instanceID : SV_VulkanInstanceID) : SV_Position {
    float4 world = mul(ModelVertexTransform(instanceID, joints, weights), float4(position, 1.0));
    return mul(frame.shadowViewProj[pc.cascade], world);
}

struct CutoutVSOutput {
    float4 pos : SV_Position;
    float2 uv;
};

[shader("vertex")]
// uv is Vertex::uv, vertex input location 2 (ShadowPass binds locations 0 and 2 only).
CutoutVSOutput shadowCutoutVert([[vk::location(0)]] float3 position : POSITION, [[vk::location(2)]] float2 uv : TEXCOORD0, [[vk::location(5)]] uint4 joints : BLENDINDICES, [[vk::location(6)]] float4 weights : BLENDWEIGHT, uint instanceID : SV_VulkanInstanceID) {
    CutoutVSOutput output;
    float4 world = mul(ModelVertexTransform(instanceID, joints, weights), float4(position, 1.0));
    output.pos = mul(frame.shadowViewProj[pc.cascade], world);
    output.uv  = uv;
    return output;
}

[shader("fragment")]
void shadowCutoutFrag(CutoutVSOutput input) {
    if (albedoTex.Sample(input.uv).a * pc.alphaTint < pc.alphaCutoff) { discard; }
}
