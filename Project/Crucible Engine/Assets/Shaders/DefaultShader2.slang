struct VSInput {
    float3 inPosition;
    float3 inNormal;
    float2 inUV;
    float3 inColor;
};

// Per-frame data (descriptor set 0, binding 0). Must match Crucible::FrameUniforms.
struct FrameUniforms {
    float4x4 view;
    float4x4 proj;
};

// Per-draw data. Must match the model push constant written in RenderSystem.cpp (sizeof(glm::mat4)).
struct PushConstants {
    float4x4 model;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms> frame;

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float3 normalWS;
    float2 uv;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(pc.model, float4(input.inPosition, 1.0));
    output.pos = mul(frame.proj, mul(frame.view, worldPos));
    output.color = input.inColor;
    output.uv = input.inUV;
    // NOTE: correct for uniform scale only. Non-uniform scale needs the inverse-transpose
    // of the model's upper-left 3x3 instead of the 3x3 itself.
    output.normalWS = normalize(mul((float3x3)pc.model, input.inNormal));
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput inVert) : SV_Target {
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
    float  nDotL     = max(dot(normalize(inVert.normalWS), lightDir), 0.0);
    float3 ambient   = inVert.color * 0.25;
    float3 diffuse   = inVert.color * nDotL * 0.85;
    return float4(ambient + diffuse, 1.0);
}
