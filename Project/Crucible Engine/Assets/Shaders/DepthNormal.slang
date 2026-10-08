// Crucible Engine ambient occlusion, step 1 of 3 (steps 2 and 3 are aoFrag and aoBlurFrag in
// Post.slang): every mesh the main pass will draw, at half size, writing two things -
//   - depth, into a depth image;
//   - the surface's direction (its normal) in view space, into a colour image.
// The normal is the SMOOTH one stored at the vertices and blended across each triangle, the same
// one lighting uses. Working the direction out from the depth image instead would give each flat
// triangle its own direction, and a sphere or cylinder would show its facets in the occlusion.

// Only the prefix this shader reads; the full struct is in Common.slang.
struct FrameUniforms {
    float4x4 view;
    float4x4 proj;
};

struct InstanceData {
    float4x4 model;
    float4   params;   // unused here; keeps the 80-byte stride of Crucible::InstanceData
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms>  frame;
[[vk::binding(2, 0)]] StructuredBuffer<InstanceData> instances;

struct VSInput {
    float3 position;
    float3 normal;
};

struct VSOutput {
    float4 pos : SV_Position;
    float3 normalVS;
};

[shader("vertex")]
VSOutput depthNormalVert(VSInput input, uint instanceID : SV_VulkanInstanceID) {
    VSOutput output;
    float4x4 model = instances[instanceID].model;
    float4   world = mul(model, float4(input.position, 1.0));
    output.pos = mul(frame.proj, mul(frame.view, world));
    // Same simplification as the mesh shader: correct for uniform scale.
    float3 normalWS = mul((float3x3)model, input.normal);
    output.normalVS = mul((float3x3)frame.view, normalWS);
    return output;
}

[shader("fragment")]
float4 depthNormalFrag(VSOutput input) : SV_Target {
    // -1..1 stored as 0..1.
    return float4(normalize(input.normalVS) * 0.5 + 0.5, 1.0);
}
