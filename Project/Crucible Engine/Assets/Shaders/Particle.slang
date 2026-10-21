// Crucible Engine particles: every live particle of every visible emitter, drawn as camera-facing
// quads after the meshes and the sky (Renderer::DrawParticles). No vertex buffer: six vertices per
// particle are generated from the vertex index, and the particle itself is read from the per-frame
// particle buffer. Unlit - a particle's colour IS its brightness (values above 1 glow through
// bloom); the light that sparks or fire cast on the scene comes from ordinary point lights that
// ParticleSystem submits for the emitter.

// Per-frame data (FrameUniforms, `frame`) and the fog functions.
#include "Common.slang"

// One particle. Must match Crucible::GpuParticle (RenderTypes.h), 64 bytes.
struct GpuParticle {
    float4 posSize;    // xyz = world position, w = width in metres
    float4 color;      // rgb = linear colour (may exceed 1), a = opacity
    float4 axisRot;    // xyz = stretch vector (velocity * stretch; zero = plain billboard), w = rotation (radians)
    float4 misc;       // x = flipbook frame index
};

// Per draw (one emitter's batch). Must match Crucible::ParticlePush.
struct ParticlePush {
    float4 params;     // x = flipbook columns, y = rows, z = 1 additive / 0 alpha blend, w = 1 textured / 0 soft disc
};

[[vk::binding(5, 0)]] StructuredBuffer<GpuParticle>  particles;
[[vk::binding(0, 1)]] Sampler2D                      particleTex;
[[vk::push_constant]] ConstantBuffer<ParticlePush>   pc;

struct VSOutput {
    float4 pos : SV_Position;
    float4 color;
    float2 uv;       // into the texture (flipbook cell already applied)
    float2 local;    // -1..1 across the quad, for the procedural soft disc
    float4 fog;      // fog in front of the particle's centre: rgb = light it adds, a = how much of the particle shows (FogForPoint)
};

static const float2 kCorners[6] = {
    float2(-1, -1), float2( 1, -1), float2( 1,  1),
    float2(-1, -1), float2( 1,  1), float2(-1,  1)
};

[shader("vertex")]
VSOutput particleVert(uint vertexID : SV_VulkanVertexID) {
    VSOutput output;

    GpuParticle p = particles[vertexID / 6];
    float2 c      = kCorners[vertexID % 6];
    float  half   = p.posSize.w * 0.5;

    float3 world;
    float  stretchSq = dot(p.axisRot.xyz, p.axisRot.xyz);
    if (stretchSq > 1e-8) {
        // Stretched along its motion (sparks, rain): long axis = the stretch vector, short axis =
        // perpendicular to it and to the line of sight, so the streak always faces the camera.
        float  len   = sqrt(stretchSq);
        float3 along = p.axisRot.xyz / len;
        float3 toCam = normalize(frame.cameraPosWS.xyz - p.posSize.xyz);
        float3 side  = cross(along, toCam);
        float  sideSq = dot(side, side);
        side = sideSq > 1e-8 ? side * rsqrt(sideSq) : float3(1.0, 0.0, 0.0);
        world = p.posSize.xyz + side * (c.x * half) + along * (c.y * (half + len * 0.5));
    } else {
        // Plain billboard. The view matrix's first two rows are the camera's right and up axes in
        // world space.
        float3 right = frame.view[0].xyz;
        float3 up    = frame.view[1].xyz;
        float  s = sin(p.axisRot.w), co = cos(p.axisRot.w);
        float2 r = float2(c.x * co - c.y * s, c.x * s + c.y * co);
        world = p.posSize.xyz + (right * r.x + up * r.y) * half;
    }

    output.pos   = mul(frame.proj, mul(frame.view, float4(world, 1.0)));
    output.color = p.color;
    // Fog is looked up once, at the particle's centre (its screen position comes from the centre's
    // own projection, not this corner's).
    float4 centreClip = mul(frame.proj, mul(frame.view, float4(p.posSize.xyz, 1.0)));
    float2 centreNdc  = centreClip.xy / max(centreClip.w, 1e-4);
    output.fog   = FogForPoint(p.posSize.xyz, float2(centreNdc.x * 0.5 + 0.5, 0.5 - centreNdc.y * 0.5));
    output.local = c;

    // Flipbook: frame f of a cols x rows atlas, read left to right, top to bottom.
    float2 uv   = float2(c.x * 0.5 + 0.5, 0.5 - c.y * 0.5);
    float  cols = max(pc.params.x, 1.0), rows = max(pc.params.y, 1.0);
    float  f    = clamp(floor(p.misc.x), 0.0, cols * rows - 1.0);
    float  row  = floor(f / cols);
    float  col  = f - row * cols;
    output.uv   = (float2(col, row) + uv) / float2(cols, rows);

    return output;
}

// The scene pass has a second output, the toon outline pass's line data. Particles leave it as
// it is: they write zero there with zero alpha, which the shared blend state (ONE,
// ONE_MINUS_SRC_ALPHA) turns into "keep what is there" - one blend state for both outputs, so no
// independentBlend device feature is needed.
struct ParticleOutput {
    float4 colour : SV_Target0;
    float4 lines  : SV_Target1;
};

[shader("fragment")]
ParticleOutput particleFrag(VSOutput input) {
    float4 texel;
    if (pc.params.w > 0.5) {
        texel = particleTex.Sample(input.uv);
    } else {
        // No texture: a soft round dot, bright in the middle.
        float a = saturate(1.0 - length(input.local));
        texel = float4(1.0, 1.0, 1.0, a * a);
    }

    float4 color = texel * input.color;
    // The pipeline blends (ONE, ONE_MINUS_SRC_ALPHA). Writing alpha 0 makes that purely additive;
    // writing the real alpha makes it ordinary (premultiplied) alpha blending.
    // Fog: a glowing (additive) particle just dims; an ordinary one is covered like a surface.
    bool   additive = pc.params.z > 0.5;
    float3 rgb      = additive ? color.rgb * input.fog.a : color.rgb * input.fog.a + input.fog.rgb;
    ParticleOutput output;
    output.colour = float4(rgb * color.a, additive ? 0.0 : color.a);
    output.lines  = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}
