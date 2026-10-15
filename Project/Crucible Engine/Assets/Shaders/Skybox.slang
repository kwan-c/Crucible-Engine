// Crucible Engine skybox: draws the current environment cubemap behind everything else.
// Rendered by Renderer::DrawSkybox() AFTER the opaque meshes, with the skybox pipeline
// (VulkanPipeline::CreateSkyboxPipeline): no vertex buffer (the cube comes from SV_VertexID), no
// culling, depth test LESS_OR_EQUAL, no depth write.

// Per-frame data (FrameUniforms, `frame`) and the fog functions.
#include "Common.slang"

// Set 1: the cubemap being drawn (one descriptor set per cubemap - Renderer::CreateCubemap*).
[[vk::binding(0, 1)]]
SamplerCube skyTex;

struct VSOutput {
    float4 pos : SV_Position;
    float3 dir;               // world-space view direction for this vertex
};

// Unit cube, 12 triangles. Winding doesn't matter (the pipeline doesn't cull).
static const float3 kCube[36] = {
    float3(-1,  1, -1), float3(-1, -1, -1), float3( 1, -1, -1),
    float3( 1, -1, -1), float3( 1,  1, -1), float3(-1,  1, -1),

    float3(-1, -1,  1), float3(-1, -1, -1), float3(-1,  1, -1),
    float3(-1,  1, -1), float3(-1,  1,  1), float3(-1, -1,  1),

    float3( 1, -1, -1), float3( 1, -1,  1), float3( 1,  1,  1),
    float3( 1,  1,  1), float3( 1,  1, -1), float3( 1, -1, -1),

    float3(-1, -1,  1), float3(-1,  1,  1), float3( 1,  1,  1),
    float3( 1,  1,  1), float3( 1, -1,  1), float3(-1, -1,  1),

    float3(-1,  1, -1), float3( 1,  1, -1), float3( 1,  1,  1),
    float3( 1,  1,  1), float3(-1,  1,  1), float3(-1,  1, -1),

    float3(-1, -1, -1), float3(-1, -1,  1), float3( 1, -1, -1),
    float3( 1, -1, -1), float3(-1, -1,  1), float3( 1, -1,  1)
};

[shader("vertex")]
// SV_VulkanVertexID (gl_VertexIndex as-is), not SV_VertexID: Slang lowers SV_VertexID to
// gl_VertexIndex - gl_BaseVertex for D3D parity, which needs the DrawParameters capability (the
// shaderDrawParameters device feature). This draw never uses a base vertex, so skip that.
VSOutput skyVert(uint vertexID : SV_VulkanVertexID) {
    VSOutput output;

    float3 p = kCube[vertexID];
    output.dir = p;

    // Rotation only: dropping the view matrix's translation keeps the camera permanently at the
    // cube's centre, so the sky never gets closer however far the camera moves.
    float3x3 viewRot = (float3x3)frame.view;
    float4 clip = mul(frame.proj, float4(mul(viewRot, p), 1.0));

    // z = w -> depth exactly 1.0 after the perspective divide: the far plane. Combined with the
    // pipeline's LESS_OR_EQUAL test against a 1.0 depth clear, the sky only fills pixels no mesh
    // covered.
    output.pos = clip.xyww;
    return output;
}

[shader("fragment")]
float4 skyFrag(VSOutput input) : SV_Target {
    // Linear HDR; exposure and tonemapping are applied to the whole image in Post.slang.
    float3 dir = normalize(input.dir);
    float3 sky = skyTex.Sample(dir).rgb * frame.renderParams.y;   // skybox intensity
    // Fog hides the horizon and thins toward the zenith, so the sky meets the fogged ground. With a
    // fog volume, the sun's glow in the fog and its shafts show against the sky too.
    float4 fog = FogForSky(dir, input.pos.xy / frame.clusterZ.zw);
    sky = sky * fog.a + fog.rgb;
    return float4(sky, 1.0);
}
