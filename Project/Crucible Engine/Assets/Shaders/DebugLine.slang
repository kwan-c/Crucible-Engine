// Crucible Engine debug lines (DebugDraw.h): plain coloured line segments, drawn after the meshes
// and sky by Renderer::DrawDebugLines with one of two pipelines (depth-tested, or always on top -
// see VulkanPipeline::CreateDebugLinePipelines). No lighting, no tonemapping: a debug colour
// should come out exactly as written.

// Per-frame data (descriptor set 0, binding 0). Must match Crucible::FrameUniforms (RenderTypes.h)
// AND the copies in DefaultShader.slang / Skybox.slang exactly - all bind the same set 0.
struct FrameUniforms {
    float4x4 view;
    float4x4 proj;
    float4   cameraPosWS;
    float4   ambientSky;
    float4   ambientGround;
    float4   renderParams;
    uint4    lightParams;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms> frame;

// Matches Crucible::DebugVertex: location 0 = position (R32G32B32_SFLOAT), location 1 = colour
// (R8G8B8A8_UNORM, so it arrives here already as 0..1 floats).
struct VSInput {
    float3 position;
    float4 color;
};

struct VSOutput {
    float4 pos : SV_Position;
    float4 color;
};

[shader("vertex")]
VSOutput lineVert(VSInput input) {
    VSOutput output;
    // Pulled a hair towards the camera (0.2% of its distance; its place on screen is unchanged),
    // so a depth-tested line lying exactly on a surface - a collider on its own mesh - wins the
    // depth test instead of flickering in and out of it.
    float4 viewPos = mul(frame.view, float4(input.position, 1.0));
    viewPos.xyz   *= 0.998;
    output.pos     = mul(frame.proj, viewPos);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 lineFrag(VSOutput input) : SV_Target {
    // Colours are authored in sRGB (DebugDraw.h). The render targets are *_SRGB, which encode
    // whatever we write as if it were linear - so decode first, or every colour comes out washed out.
    float3 linearColor = pow(saturate(input.color.rgb), float3(2.2, 2.2, 2.2));
    return float4(linearColor, 1.0);
}
