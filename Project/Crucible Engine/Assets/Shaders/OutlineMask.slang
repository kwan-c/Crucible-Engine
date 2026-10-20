// Crucible Engine selection outline, pass 1 of 2 (pass 2 is OutlineComposite.slang).
// Editor Scene view only. The two passes:
//   1. maskVert/maskFrag: the selected object's instances, drawn with no depth test into a
//      per-viewport R8 mask (1 = covered) - so the outline also shows through walls, like Unity.
//   2. compositeVert/compositeFrag: a fullscreen triangle over the finished viewport image. Pixels
//      outside the mask but within `thickness` pixels of it get the outline colour, alpha-blended.

// Only the prefix the mask pass reads; the full struct is in DefaultShader.slang.
struct FrameUniforms {
    float4x4 view;
    float4x4 proj;
};

struct InstanceData {
    float4x4 model;
    float4   params;   // unused here; keeps the 80-byte stride of Crucible::InstanceData
};

// Cut-out materials (leaves): the mask follows their shape, not their cards (Crucible::CutoutPush,
// and the material's albedo texture at set 1).
struct CutoutPush {
    float alphaCutoff;
    float alphaTint;   // the material's albedoTint.a
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms>  frame;
[[vk::binding(2, 0)]] StructuredBuffer<InstanceData> instances;
[[vk::binding(0, 1)]] Sampler2D                      albedoTex;
[[vk::push_constant]] ConstantBuffer<CutoutPush>     pc;

[shader("vertex")]
float4 maskVert(float3 position : POSITION, uint instanceID : SV_VulkanInstanceID) : SV_Position {
    float4 world = mul(instances[instanceID].model, float4(position, 1.0));
    return mul(frame.proj, mul(frame.view, world));
}

[shader("fragment")]
float4 maskFrag() : SV_Target {
    return float4(1.0, 0.0, 0.0, 0.0);
}

struct CutoutVSOutput {
    float4 pos : SV_Position;
    float2 uv;
};

[shader("vertex")]
// uv is Vertex::uv, vertex input location 2 (OutlinePass binds locations 0 and 2 only).
CutoutVSOutput maskCutoutVert([[vk::location(0)]] float3 position : POSITION, [[vk::location(2)]] float2 uv : TEXCOORD0, uint instanceID : SV_VulkanInstanceID) {
    CutoutVSOutput output;
    float4 world = mul(instances[instanceID].model, float4(position, 1.0));
    output.pos = mul(frame.proj, mul(frame.view, world));
    output.uv  = uv;
    return output;
}

[shader("fragment")]
float4 maskCutoutFrag(CutoutVSOutput input) : SV_Target {
    if (albedoTex.Sample(input.uv).a * pc.alphaTint < pc.alphaCutoff) { discard; }
    return float4(1.0, 0.0, 0.0, 0.0);
}

