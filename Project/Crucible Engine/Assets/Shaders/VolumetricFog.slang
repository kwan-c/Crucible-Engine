// Crucible Engine volumetric fog (VolumetricFogPass): the scene's height fog as a froxel volume,
// lit by the sun (with its shadows, which is what makes light shafts) and by point and spot lights.
// Two compute passes per view, before the scene is drawn:
//
//   fogInject     one thread per froxel (a cell of a grid that follows the camera: 160 x 90 across
//                 the screen, 128 slices in depth, spaced further apart the further out they are).
//                 Works out the fog's density there and the light it scatters toward the camera.
//   fogIntegrate  one thread per column of froxels, walking away from the camera and adding up the
//                 light the fog has added so far and how much of what lies behind still shows.
//                 The light (not the density) is softened across neighbouring columns on the way,
//                 so shaft edges blend over a few froxels instead of showing the grid.
//                 Also stores, past the last slice, the light fog along that column's line of sight
//                 gets with nothing shadowed (see FogColumnSource in Common.slang): the fog beyond
//                 the volume, and the sky's fog, are lit with it.
//
// Meshes, particles and the sky then read the second volume (Common.slang's FogForPoint and
// FogForSky). Everything here is linear HDR radiance, like the scene.
//
// The light a bit of fog sends toward the camera, per unit of density, is
//     ambient                                  light from all around (the fog colour, or the sky)
//   + intensity * (sun and other directional lights, the shadow-casting one tested against the
//                  shadow map, + point and spot lights)
// each weighted by the phase function: how much of the light arriving from one direction is
// scattered toward the camera.

#include "Common.slang"

// One light. Must match Crucible::GpuLight (RenderTypes.h) and DefaultShader.slang.
struct GpuLight {
    float4 positionRange;     // xyz = world position, w = range
    float4 directionType;     // xyz = normalized travel direction, w = LightType as float
    float4 colourIntensity;   // rgb = linear colour, w = intensity
    float4 spotParams;        // x = cos(inner half-angle), y = cos(outer half-angle)
};
static const uint kLightDirectional = 0;
static const uint kLightSpot        = 2;

// Must match DefaultShader.slang's ClusterCell.
struct ClusterCell {
    uint offset;
    uint count;
};

[[vk::binding(1, 0)]] StructuredBuffer<GpuLight>     lights;
[[vk::binding(3, 0)]] Texture2DArray<float>          shadowMap;
[[vk::binding(4, 0)]] SamplerComparisonState         shadowSampler;
[[vk::binding(6, 0)]] StructuredBuffer<ClusterCell>  clusterGrid;
[[vk::binding(7, 0)]] StructuredBuffer<uint>         clusterLightIndices;

// Set 1: this view's two volumes (VolumetricFogPass's FogVolumeTargets).
// lightVolume:      rgb = light scattered toward the camera per metre, a = extinction per metre.
// integratedVolume: rgb = light added from the camera to the slice's far edge, a = transmittance;
//                   one slice more than lightVolume: the last holds each column's unshadowed light.
[[vk::binding(0, 1)]] [format("rgba16f")] RWTexture3D<float4> lightVolume;
[[vk::binding(1, 1)]] [format("rgba16f")] RWTexture3D<float4> integratedVolume;

static const float kPi = 3.14159265;

// Henyey-Greenstein phase function: how much light arriving along one direction leaves along
// another, cosTheta being the angle between them. g > 0 scatters mostly forward, which is why fog
// glows around the sun when you look toward it. Integrates to 1 over the sphere.
float PhaseHG(float cosTheta, float g) {
    float g2    = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * kPi * denom * sqrt(denom));
}

// A cheap fixed "random" value per cell (interleaved gradient noise, Jimenez 2014). Each froxel
// samples at this fraction of the way through its slice instead of at its centre, so the steps
// between slices become fine noise, smoothed when the volume is filtered, instead of bands.
// (The sun's shadow is sampled at several points per froxel instead; see FogShadowCoverage.)
float CellNoise(float2 p) {
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

// View-space point at view depth 1 through screen position uv (0..1, top-left origin). Scaling it
// by a depth gives the point at that depth. (Clip-space +y is up; the scene's viewport is flipped.)
float3 FroxelViewRay(float2 uv) {
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return float3(ndc.x / frame.proj[0][0], ndc.y / frame.proj[1][1], -1.0);
}

// View space -> world space for a direction: the view matrix's rows are the camera's axes.
float3 ViewToWorld(float3 v) {
    return frame.view[0].xyz * v.x + frame.view[1].xyz * v.y + frame.view[2].xyz * v.z;
}

// How much sunlight reaches a point in the air (1 = all): one hardware-filtered tap of the cascade
// covering it. Simpler than surface shadowing (no surface, so no bias to push off one), and the
// slice noise plus filtering soften it. Fades out toward the end of the shadow distance the same
// way surfaces do.
float FogShadow(float3 worldPos, float viewDepth) {
    uint cascadeCount = frame.lightParams.z;
    if (cascadeCount == 0) { return 1.0; }
    float lastSplit = frame.cascadeSplits[cascadeCount - 1];
    if (viewDepth >= lastSplit) { return 1.0; }

    uint c = 0;
    while (c + 1 < cascadeCount && viewDepth >= frame.cascadeSplits[c]) { ++c; }

    float4 lightClip = mul(frame.shadowViewProj[c], float4(worldPos, 1.0));
    float3 ndc = lightClip.xyz / lightClip.w;
    float2 uv  = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0) { return 1.0; }
    float lit = shadowMap.SampleCmpLevelZero(shadowSampler, float3(uv, float(c)), ndc.z - frame.shadowParams.y);

    float fadeStart = lastSplit * (1.0 - frame.shadowParams.w);
    float fade = saturate((viewDepth - fadeStart) / max(lastSplit - fadeStart, 1e-3));
    return lerp(lit, 1.0, fade);
}

// Light from all around that the fog scatters toward the camera (per unit of scattering): the
// fog's light in shade. The fog colour itself (the plain fog's look), or with the Skybox's
// volumetricSkyAmbient the sky's average light - the same spherical harmonics and strength that
// light surfaces in shade - tinted by the fog colour. Either way scaled by volumetricAmbient:
// lower = darker shade, so the sun's shafts stand out.
//
// The sky's light is taken as even from all directions (its average): fog scatters light many
// times over, which evens it out, and the sky's harmonics weighted toward one direction swing
// strongly and can go below zero with a high anisotropy.
float3 AmbientScatter() {
    float3 light = frame.fogColour.rgb;
    if (frame.volumetricAmbient.y > 0.5) {
        float3 sky = frame.ambientParams.x > 0.5
                   ? max(frame.skySH[0].rgb, float3(0.0, 0.0, 0.0)) * frame.ambientParams.z    // average of the sky (band 0)
                   : (frame.ambientSky.rgb + frame.ambientGround.rgb) * 0.5;                    // average of the two flat colours
        light *= sky;
    }
    return light * frame.volumetricAmbient.x;
}

// How much of the sun reaches a froxel (0..1), as the average of several shadow-map taps spread
// through it: across its footprint (a rotated grid) and through its depth (one tap per quarter of
// the slice, each nudged by the cell's noise). One tap per froxel turns a shadow edge into a
// pattern of whole lit and unlit froxels - visible as pixelated shafts; the average gives the
// share of the froxel that is lit, which shades smoothly.
static const uint   kShadowTaps = 4;
static const float2 kShadowTapOffsets[kShadowTaps] = { float2(-0.375, -0.125), float2(0.125, -0.375),
                                                       float2(0.375, 0.125), float2(-0.125, 0.375) };
float FogShadowCoverage(uint3 id, uint3 size, float noise) {
    float3 camera = frame.cameraPosWS.xyz;
    float  lit    = 0.0;
    for (uint k = 0; k < kShadowTaps; ++k) {
        float2 tapUV    = (float2(id.xy) + 0.5 + kShadowTapOffsets[k]) / float2(size.xy);
        float  tapDepth = FogSliceDepth(float(id.z) + (float(k) + noise) / float(kShadowTaps));
        lit += FogShadow(camera + ViewToWorld(FroxelViewRay(tapUV) * tapDepth), tapDepth);
    }
    return lit / float(kShadowTaps);
}

// The directional lights' light scattered toward the camera, looking along rd (they come first
// in the light buffer). The shadow-casting one is scaled by sunVisible (FogShadowCoverage, or 1
// for unshadowed fog): that is where the shafts come from.
float3 DirectionalScatter(float3 rd, float sunVisible) {
    float  g      = frame.volumetricLight.y;
    float3 result = float3(0.0, 0.0, 0.0);
    uint dirCount = frame.lightParams.w;
    uint shadower = frame.lightParams.y;   // index + 1, 0 = none
    for (uint i = 0; i < dirCount; ++i) {
        GpuLight light = lights[i];
        float3 radiance = light.colourIntensity.rgb * light.colourIntensity.w;
        float  phase    = PhaseHG(dot(light.directionType.xyz, -rd), g);
        float  visible  = (i + 1 == shadower) ? sunVisible : 1.0;
        result += radiance * phase * visible;
    }
    return result;
}

// Point and spot lights: the ones listed for the light cluster worldPos lies in (the same grid the
// mesh shader uses; a light is listed in every cell its range touches).
float3 LocalScatter(float3 worldPos, float3 rd, float viewDepth, float2 uv) {
    float  g      = frame.volumetricLight.y;
    float3 result = float3(0.0, 0.0, 0.0);
    uint3 dims  = frame.clusterDims.xyz;
    uint  cx    = min(uint(uv.x * float(dims.x)), dims.x - 1);
    uint  cy    = min(uint((1.0 - uv.y) * float(dims.y)), dims.y - 1);   // the cluster grid's y runs up
    float slice = log(max(viewDepth, frame.clusterZ.x) / frame.clusterZ.x) * frame.clusterZ.y;
    uint  cz    = min(uint(max(slice, 0.0)), dims.z - 1);
    ClusterCell cell = clusterGrid[frame.clusterDims.w + cx + cy * dims.x + cz * dims.x * dims.y];
    for (uint k = 0; k < cell.count; ++k) {
        GpuLight light   = lights[clusterLightIndices[cell.offset + k]];
        float3   toLight = light.positionRange.xyz - worldPos;
        float    distSq  = dot(toLight, toLight);
        float    range   = light.positionRange.w;
        if (distSq >= range * range) { continue; }

        // The mesh shader's falloff (inverse square, windowed to 0 at the range), except that the
        // inverse square stops growing within half a metre: air right at a light would otherwise
        // glow without limit.
        float ratio  = distSq / max(range * range, 1e-4);
        float window = saturate(1.0 - ratio * ratio);
        float3 radiance = light.colourIntensity.rgb * light.colourIntensity.w * (window * window) / max(distSq, 0.25);

        float3 travel = -toLight * rsqrt(max(distSq, 1e-8));   // from the light to this point
        if (uint(light.directionType.w + 0.5) == kLightSpot) {
            radiance *= smoothstep(light.spotParams.y, light.spotParams.x, dot(travel, light.directionType.xyz));
        }
        result += radiance * PhaseHG(dot(travel, -rd), g);
    }
    return result;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void fogInject(uint3 id : SV_DispatchThreadID) {
    uint3 size;
    lightVolume.GetDimensions(size.x, size.y, size.z);
    if (any(id >= size)) { return; }

    float2 uv        = (float2(id.xy) + 0.5) / float2(size.xy);
    float3 ray       = FroxelViewRay(uv);
    float3 camera    = frame.cameraPosWS.xyz;

    // Density: the analytic fog's optical depth across this slice (its far edge minus its near
    // edge, the first slice starting at the camera), per metre of line of sight. Adding the slices
    // up in fogIntegrate then gives back exactly the analytic fog at every slice edge.
    float nearDepth = id.z == 0 ? 0.0 : FogSliceDepth(float(id.z));
    float farDepth  = FogSliceDepth(float(id.z + 1));
    float stepLen   = (farDepth - nearDepth) * length(ray);
    float across    = FogOpticalDepth(camera + ViewToWorld(ray * farDepth)) - (id.z == 0 ? 0.0 : FogOpticalDepth(camera + ViewToWorld(ray * nearDepth)));
    float sigma     = max(across, 0.0) / max(stepLen, 1e-5);

    // Point and spot lights are sampled at a fixed point inside the slice (see CellNoise); the
    // sun's shadow at several (FogShadowCoverage).
    float  noise     = CellNoise(float2(id.xy) + float(id.z) * 5.588238);
    float  depth     = FogSliceDepth(float(id.z) + noise);
    float3 worldPos  = camera + ViewToWorld(ray * depth);
    float3 rd        = normalize(ViewToWorld(ray));   // the same for the whole column

    // Light this bit of fog sends toward the camera, per unit of density: its light in shade,
    // plus the lights' scattered light (with the sun's shadows), scaled by the Skybox's
    // volumetric strength.
    float3 source = AmbientScatter();
    if (sigma > 0.0 && frame.volumetricLight.x > 0.0) {
        float sunVisible = frame.lightParams.y > 0 ? FogShadowCoverage(id, size, noise) : 1.0;
        source += (DirectionalScatter(rd, sunVisible) + LocalScatter(worldPos, rd, depth, uv)) * frame.volumetricLight.x;
    }
    lightVolume[id] = float4(source * sigma, sigma);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void fogIntegrate(uint3 id : SV_DispatchThreadID) {
    uint3 size;   // the light volume's size; the integrated volume has one slice more
    lightVolume.GetDimensions(size.x, size.y, size.z);
    if (any(id.xy >= size.xy)) { return; }

    // Metres along this column's line of sight per metre of view depth.
    float2 uv       = (float2(id.xy) + 0.5) / float2(size.xy);
    float3 ray      = FroxelViewRay(uv);
    float  rayScale = length(ray);

    float3 added         = float3(0.0, 0.0, 0.0);
    float  transmittance = 1.0;
    float  nearDepth     = 0.0;   // the first slice starts at the camera
    for (uint s = 0; s < size.z; ++s) {
        float farDepth = FogSliceDepth(float(s + 1));
        float stepLen  = (farDepth - nearDepth) * rayScale;
        nearDepth      = farDepth;

        // The light per unit of density, softened over this froxel and its 8 neighbours (weights
        // 1-2-1 each way, by density: fog that is not there adds nothing). Only the light: the
        // density stays this column's own, so the fog hides exactly as much as before.
        float4 sum = float4(0.0, 0.0, 0.0, 0.0);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int2 c = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0), int2(size.xy) - 1);
                sum += lightVolume[uint3(uint2(c), s)] * float((2 - abs(dx)) * (2 - abs(dy)));
            }
        }
        float  sigma  = lightVolume[uint3(id.xy, s)].a;
        float3 source = sum.a > 1e-8 ? sum.rgb / sum.a : float3(0.0, 0.0, 0.0);

        // Over one slice of constant density sigma and source light L, the light reaching the
        // camera is L * (1 - exp(-sigma * length)), dimmed by the fog already in front of it. This
        // closed form stays right however thick the slice is (no "too much fog in one step" error).
        float  t      = exp(-sigma * stepLen);
        added         += transmittance * source * (1.0 - t);
        transmittance *= t;

        integratedVolume[uint3(id.xy, s)] = float4(added, transmittance);
    }

    // Past the last slice: the light fog along this line of sight gets with nothing shadowed and
    // no point or spot lights - what the fog beyond the volume, and the sky's fog, are lit by. It is
    // the same for every depth (directional light and the sky depend only on the direction).
    float3 rd = normalize(ViewToWorld(ray));
    float3 unshadowed = AmbientScatter() + DirectionalScatter(rd, 1.0) * frame.volumetricLight.x;
    integratedVolume[uint3(id.xy, size.z)] = float4(unshadowed, 1.0);
}
