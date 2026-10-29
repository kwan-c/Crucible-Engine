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

// World position -> clip space, pulled a hair towards the camera (0.2% of its distance; its place
// on screen is unchanged), so a depth-tested line lying exactly on a surface - a collider on its
// own mesh - wins the depth test instead of flickering in and out of it.
float4 ToClip(float3 worldPos) {
    float4 viewPos = mul(frame.view, float4(worldPos, 1.0));
    viewPos.xyz *= 0.998;
    return mul(frame.proj, viewPos);
}

[shader("vertex")]
VSOutput lineVert(VSInput input) {
    VSOutput output;
    output.pos = ToClip(input.position);
    output.color = input.color;
    return output;
}

// ---- Sphere outlines (DebugDraw::Sphere): the circle where the sphere meets what is behind it,
// as THIS view's camera sees it - Unity's "horizon disc". One instance per sphere
// (Crucible::DebugSphere), drawn as kOutlineSegments line segments with no vertex data: each
// vertex works out its own point on the circle from its index.
//
// Seen from a camera at distance d from the centre, a sphere of radius r shows the rim where the
// sight lines just graze it. Those touching points form a circle facing the camera:
//   - its centre is on the line to the camera, r*r/d from the sphere's centre;
//   - its radius is r * sqrt(d*d - r*r) / d.
// (A sight line touching the sphere is at right angles to the radius it touches, so camera,
// centre and touching point make a right-angled triangle; those two lengths follow from it.)
// With the camera inside the sphere nothing grazes it, so there is no outline.

static const uint kOutlineSegments = 64; // DebugLinePass::kOutlineVertices = 2 x this

// Matches Crucible::DebugSphere, read per instance: location 0 = centre (xyz) + radius (w),
// location 1 = colour (R8G8B8A8_UNORM).
struct SphereInput {
    float4 centerRadius;
    float4 color;
}

[shader("vertex")]
VSOutput sphereOutlineVert(SphereInput input, uint vertexID : SV_VulkanVertexID) {
    VSOutput output;
    output.color = input.color;

    float3  centre  = input.centerRadius.xyz;
    float   r       = input.centerRadius.w;
    float3  toEye   = frame.cameraPosWS.xyz - centre;
    float   d2      = dot(toEye, toEye);
    if (d2 <= r * r * 1.0001) {
        // Camera inside (or on) the sphere: put the vertex outside the clip volume, so every
        // segment is clipped away and nothing is drawn.
        output.pos = float4(2.0, 2.0, 2.0, 1.0);
        return output;
    }
    float   d = sqrt(d2);
    float3  n = toEye / d;
    float3  circleCentre = centre + n * (r * r / d);
    float   circleRadius = r * sqrt(d2 - r * r) / d;

    // Two directions across the circle (any pair at right angles to n will do)
    float3 seed = abs(n.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(n, seed));
    float3 b = cross(n, t);

    // Line list: vertices 2s and 2s+1 are segmnet s, from point s to point s+1 on the circle.
    uint pointIndex = vertexID / 2 + (vertexID & 1);
    float angle = 6.28318530718 * float(pointIndex) / float(kOutlineSegments);
    output.pos = ToClip(circleCentre + (t * cos(angle) + b * sin(angle)) * circleRadius);
    return output;
}

[shader("fragment")]
float4 lineFrag(VSOutput input) : SV_Target {
    // Colours are authored in sRGB (DebugDraw.h). The render targets are *_SRGB, which encode
    // whatever we write as if it were linear - so decode first, or every colour comes out washed out.
    float3 linearColor = pow(saturate(input.color.rgb), float3(2.2, 2.2, 2.2));
    return float4(linearColor, 1.0);
}
