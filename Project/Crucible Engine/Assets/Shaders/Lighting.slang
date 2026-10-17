// Lights, shadows and the sky's light, as surfaces see them: shared by DefaultShader (meshes) and
// Water. Include it after Common.slang (it reads `frame`). Moved out of DefaultShader.slang
// unchanged when the water shader needed the same lighting; VolumetricFog.slang keeps its own
// copies of the few declarations it needs.
// (Any .slang that CompileShaders.cmake doesn't list is a shared include: editing it rebuilds
// every shader.)

// One light. Must match Crucible::GpuLight (RenderTypes.h) - four float4s, identical under std430.
struct GpuLight {
    float4 positionRange;     // xyz = world position, w = range
    float4 directionType;     // xyz = normalized travel direction, w = LightType as float
    float4 colourIntensity;   // rgb = linear colour, w = intensity
    float4 spotParams;        // x = cos(inner half-angle), y = cos(outer half-angle)
};

// Matches Crucible::LightType (LightTypes.h).
static const uint kLightDirectional = 0;
static const uint kLightPoint       = 1;
static const uint kLightSpot        = 2;

// This frame's lights (Renderer::UpdateLightBuffer). Only the first frame.lightParams.x are valid.
[[vk::binding(1, 0)]]
StructuredBuffer<GpuLight> lights;

// The sun's cascaded shadow map (one layer per cascade) and a comparison sampler
// (LESS_OR_EQUAL, linear filtering = hardware 2x2 PCF per tap).
[[vk::binding(3, 0)]] Texture2DArray<float>  shadowMap;
[[vk::binding(4, 0)]] SamplerComparisonState shadowSampler;

// Clustered lighting. The view volume is cut into clusterDims.x * y * z cells; each cell lists
// the point/spot lights that can reach it, so a pixel only loops over the lights near it instead
// of over every light in the scene. Built on the CPU per view (Renderer::BuildLightClusters).
struct ClusterCell {
    uint offset;   // first entry in clusterLightIndices
    uint count;    // how many entries
};
[[vk::binding(6, 0)]] StructuredBuffer<ClusterCell> clusterGrid;
[[vk::binding(7, 0)]] StructuredBuffer<uint>        clusterLightIndices;

// The sky as a light source: the current skybox, blurred more and more in each smaller mip level
// (Renderer::BuildEnvironmentLighting). A shiny surface reads a sharp level, a rough one a blurry
// level, so reflections soften with roughness.
[[vk::binding(8, 0)]] SamplerCube envMap;

static const float kPi = 3.14159265358979323846;

// Smoothly windowed inverse-square falloff (the UE4/Frostbite form): physically-based 1/d^2 near
// the light, forced to exactly zero at `range` so a light's influence has a hard bound.
float DistanceAttenuation(float distSq, float range) {
    float ratio  = distSq / max(range * range, 1e-4);           // (d/r)^2
    float window = saturate(1.0 - ratio * ratio);                // 1 - (d/r)^4
    return (window * window) / max(distSq, 1e-4);
}

// One cascade's answer for worldPos: 1 = fully lit, 0 = fully shadowed, -1 = this cascade's map
// does not cover the point.
float SampleCascade(float3 worldPos, float3 geomN, float3 L, uint c) {
    // Move the lookup point off the surface before comparing depths, by an amount measured in
    // THIS cascade's texels (far cascades have big texels and need a bigger push, or surfaces
    // shadow themselves in stripes): along the normal - growing as the light gets more grazing,
    // where a texel covers the most surface depth - and a little toward the light.
    // The push is kept as small as it can be: a point pushed off a wall picks up the shadow of
    // anything hovering that close to the wall.
    float  NdotL     = saturate(dot(geomN, L));
    float  texelWS   = frame.cascadeTexelSize[c];
    float  grazing   = sqrt(saturate(1.0 - NdotL * NdotL));
    float3 lookup    = worldPos + geomN * (texelWS * frame.shadowParams.z * grazing) + L * texelWS;
    float4 lightClip = mul(frame.shadowViewProj[c], float4(lookup, 1.0));
    float3 ndc       = lightClip.xyz / lightClip.w;

    // The shadow pass uses the same flipped viewport as every other pass, so +Y NDC is the
    // top row: v = 0.5 - 0.5 * y.
    float2 uv    = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    float  texel = frame.shadowParams.x;
    // 1.5 texels in from the edge, so the filter below never reads outside the map.
    if (any(uv < 1.5 * texel) || any(uv > 1.0 - 1.5 * texel) || ndc.z > 1.0) { return -1.0; }

    float  ref = ndc.z - frame.shadowParams.y;
    float  lit = 0.0;
    // 3x3 taps, each a hardware-filtered 2x2 compare: a soft 4x4-texel kernel for 9 fetches.
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            lit += shadowMap.SampleCmpLevelZero(shadowSampler, float3(uv + float2(x, y) * texel, float(c)), ref);
        }
    }
    return lit / 9.0;
}

// Fraction of the shadow-casting light that reaches worldPos (1 = fully lit). geomN is the
// interpolated vertex normal (not the normal-mapped one).
float SampleShadow(float3 worldPos, float3 geomN, float3 L) {
    uint cascadeCount = frame.lightParams.z;
    if (cascadeCount == 0) { return 1.0; }

    // glm::lookAt is right-handed: the camera looks down -Z, so view depth is -z.
    float viewDepth = -mul(frame.view, float4(worldPos, 1.0)).z;
    float lastSplit = frame.cascadeSplits[cascadeCount - 1];
    if (viewDepth >= lastSplit) { return 1.0; }

    uint c = 0;
    while (c + 1 < cascadeCount && viewDepth >= frame.cascadeSplits[c]) { ++c; }

    float lit = SampleCascade(worldPos, geomN, L, c);
    if (lit < 0.0) { lit = 1.0; }

    // Over the last part of a cascade, cross-fade into the next (coarser) one, so the change of
    // sharpness is a gradual blend instead of a visible line across the ground.
    if (c + 1 < cascadeCount) {
        float nearEdge  = c == 0 ? 0.0 : frame.cascadeSplits[c - 1];
        float farEdge   = frame.cascadeSplits[c];
        float bandStart = farEdge - (farEdge - nearEdge) * 0.2;
        if (viewDepth > bandStart) {
            float next = SampleCascade(worldPos, geomN, L, c + 1);
            if (next >= 0.0) { lit = lerp(lit, next, (viewDepth - bandStart) / (farEdge - bandStart)); }
        }
    }

    // Fade out over the last part of the shadow distance instead of a hard edge.
    float fadeStart = lastSplit * (1.0 - frame.shadowParams.w);
    float fade = saturate((viewDepth - fadeStart) / max(lastSplit - fadeStart, 1e-3));
    return lerp(lit, 1.0, fade);
}

// Debug view (ShadowSettings::debugCascades): which shadow cascade each pixel uses. Red, green,
// blue, yellow = cascade 1 to 4; the paler stripe at the far end of each is where it cross-fades
// into the next; untinted = beyond the shadow distance.
float3 CascadeDebugTint(float3 worldPos) {
    uint cascadeCount = frame.lightParams.z;
    float viewDepth = -mul(frame.view, float4(worldPos, 1.0)).z;
    if (cascadeCount == 0 || viewDepth >= frame.cascadeSplits[cascadeCount - 1]) { return float3(1.0, 1.0, 1.0); }

    uint c = 0;
    while (c + 1 < cascadeCount && viewDepth >= frame.cascadeSplits[c]) { ++c; }

    float3 tint = c == 0 ? float3(1.0, 0.35, 0.35) : c == 1 ? float3(0.35, 1.0, 0.35) : c == 2 ? float3(0.35, 0.45, 1.0) : float3(1.0, 1.0, 0.35);
    if (c + 1 < cascadeCount) {
        float nearEdge = c == 0 ? 0.0 : frame.cascadeSplits[c - 1];
        float farEdge  = frame.cascadeSplits[c];
        if (viewDepth > farEdge - (farEdge - nearEdge) * 0.2) { tint = lerp(tint, float3(1.0, 1.0, 1.0), 0.5); }
    }
    return tint;
}

// The direction towards one GpuLight from this surface point (L) and the light arriving from it
// (distance falloff and spot cone applied). false = the point is out of its range.
bool LightIncoming(GpuLight light, float3 worldPos, out float3 L, out float3 radiance) {
    uint type = (uint)(light.directionType.w + 0.5);
    radiance  = light.colourIntensity.rgb * light.colourIntensity.w;

    if (type == kLightDirectional) {
        L = -light.directionType.xyz;
        return true;
    }

    float3 toLight = light.positionRange.xyz - worldPos;
    float  distSq  = dot(toLight, toLight);
    L = float3(0.0, 0.0, 1.0);
    if (distSq >= light.positionRange.w * light.positionRange.w) { return false; }
    L = toLight * rsqrt(max(distSq, 1e-8));
    radiance *= DistanceAttenuation(distSq, light.positionRange.w);

    if (type == kLightSpot) {
        // Angle between the light's axis and the direction from the light to this point.
        float cosAngle = dot(-L, light.directionType.xyz);
        radiance *= smoothstep(light.spotParams.y, light.spotParams.x, cosAngle);
    }
    return true;
}

// The cluster of point/spot lights for this pixel (svPos = SV_Position, at worldPos).
ClusterCell ClusterFor(float4 svPos, float3 worldPos) {
    uint3 dims      = frame.clusterDims.xyz;
    float viewDepth = -mul(frame.view, float4(worldPos, 1.0)).z;
    // SV_Position.xy is the pixel position, y down; the cluster grid's y runs up.
    uint cx = min(uint(svPos.x / frame.clusterZ.z * float(dims.x)), dims.x - 1);
    uint cy = min(uint((1.0 - svPos.y / frame.clusterZ.w) * float(dims.y)), dims.y - 1);
    float slice = log(max(viewDepth, frame.clusterZ.x) / frame.clusterZ.x) * frame.clusterZ.y;
    uint cz = min(uint(max(slice, 0.0)), dims.z - 1);
    return clusterGrid[frame.clusterDims.w + cx + cy * dims.x + cz * dims.x * dims.y];
}

// Hemisphere ambient: blend ground -> sky by how much `dir` points up.
float3 HemisphereAmbient(float3 dir) {
    float t = saturate(dir.y * 0.5 + 0.5);
    return lerp(frame.ambientGround.rgb, frame.ambientSky.rgb, t);
}

// Light arriving from the whole sky around direction n, for diffuse surfaces. The sky is stored as
// nine numbers per colour channel (spherical harmonics, frame.skySH): enough to capture "bluer
// from above, warmer from the sun's side, darker from below" - all that diffuse light can show.
float3 SkyIrradiance(float3 n) {
    float3 e = frame.skySH[0].rgb
             + frame.skySH[1].rgb * n.y + frame.skySH[2].rgb * n.z + frame.skySH[3].rgb * n.x
             + frame.skySH[4].rgb * (n.x * n.y) + frame.skySH[5].rgb * (n.y * n.z)
             + frame.skySH[6].rgb * (3.0 * n.z * n.z - 1.0)
             + frame.skySH[7].rgb * (n.x * n.z) + frame.skySH[8].rgb * (n.x * n.x - n.y * n.y);
    return max(e, float3(0.0, 0.0, 0.0));
}
