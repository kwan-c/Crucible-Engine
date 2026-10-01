// Crucible Engine shadow caster pass: depth only, one cascade of the sun's shadow map per draw
// list (Renderer::DrawShadowCascades). No fragment shader - the rasterizer writes depth alone.
// The pipeline (VulkanPipeline::CreateShadowPipeline) adds slope-scaled depth bias and doesn't cull,
// so open meshes (planes, single-sided cards) still cast.

// Only the prefix this shader reads; the full struct is in DefaultShader2.slang.
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

struct InstanceData {
    float4x4 model;
};

struct ShadowPush {
    uint cascade;   // which shadowViewProj to use (also the array layer being rendered)
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms>  frame;
[[vk::binding(2, 0)]] StructuredBuffer<InstanceData> instances;
[[vk::push_constant]] ConstantBuffer<ShadowPush>     pc;

[shader("vertex")]
float4 shadowVert(float3 position : POSITION, uint instanceID : SV_VulkanInstanceID) : SV_Position {
    float4 world = mul(instances[instanceID].model, float4(position, 1.0));
    return mul(frame.shadowViewProj[pc.cascade], world);
}
