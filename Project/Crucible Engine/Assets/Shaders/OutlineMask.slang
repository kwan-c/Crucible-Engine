// Crucible Engine selection outline, pass 1 of 2 (pass 2 is OutlineComposite.slang).
// Editor Scene view only. The two passes:
//   1. maskVert/maskFrag: the selected object's instances, drawn with no depth test into a
//      per-viewport R8 mask (1 = covered) - so the outline also shows through walls, like Unity.
//   2. compositeVert/compositeFrag: a fullscreen triangle over the finished viewport image. Pixels
//      outside the mask but within `thickness` pixels of it get the outline colour, alpha-blended.

// Only the prefix the mask pass reads; the full struct is in DefaultShader2.slang.
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

[shader("vertex")]
float4 maskVert(float3 position : POSITION, uint instanceID : SV_VulkanInstanceID) : SV_Position {
    float4 world = mul(instances[instanceID].model, float4(position, 1.0));
    return mul(frame.proj, mul(frame.view, world));
}

[shader("fragment")]
float4 maskFrag() : SV_Target {
    return float4(1.0, 0.0, 0.0, 0.0);
}

