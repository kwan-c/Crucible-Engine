// Shared by the shaders that need the whole per-frame block: DefaultShader, Skybox, Particle.
// (#include is resolved by slangc relative to this folder; Engine/CompileShaders.cmake treats it as a shared include.)

// Per-frame data (descriptor set 0, binding 0). Must match Crucible::FrameUniforms (RenderTypes.h)
// exactly. Shaders that only need the first few members declare just that prefix themselves.
struct FrameUniforms {
    float4x4 view;
    float4x4 proj;
    float4   cameraPosWS;     // xyz used, w unused (matches the C++ struct's std140 padding)
    float4   ambientSky;      // rgb = ambient radiance for up-facing surfaces, w unused
    float4   ambientGround;   // rgb = ambient radiance for down-facing surfaces, w unused
    float4   renderParams;    // x = exposure, y = skybox intensity
    uint4    lightParams;     // x = number of valid lights in `lights`,
                              // w = how many of them are directional (they come first),
                              // y = index+1 of the shadow-casting light (0 = no shadows),
                              // z = number of shadow cascades in use (0..kMaxCascades)
    // ---- Cascaded shadow maps (Renderer::ComputeShadowCascades) ----
    float4x4 shadowViewProj[4];   // world -> light clip space, one per cascade
    float4   cascadeSplits;       // view-space far distance of each cascade
    float4   cascadeTexelSize;    // world-space size of one shadow-map texel, per cascade
    float4   shadowParams;        // x = 1/shadow-map resolution, y = depth bias (NDC),
                                  // z = normal-offset bias (in texels), w = fade-out fraction
    // ---- Clustered lights (Renderer::BuildLightClusters) ----
    uint4    clusterDims;         // xyz = clusters across / down / in depth, w = this view's first cell
    float4   clusterZ;            // x = near plane, y = depth clusters / log(far / near),
                                  // zw = render target size in pixels
    float4   time;                // x = seconds since the Renderer started, y = length of the last frame,
                                  // z = 1 to tint surfaces by shadow cascade (debug view)
    // ---- Fog (Skybox component) ----
    float4   fogColour;           // rgb = fog colour, a = density at the base height (0 = no fog)
    float4   fogParams;           // x = how fast it thins with height, y = base height,
                                  // z = fog-free distance in front of the camera, w = most it can hide (0..1)
    // ---- Light from the sky (image-based lighting) ----
    float4   ambientParams;       // x = 1: ambient comes from the sky, 0: from ambientSky/ambientGround,
                                  // y = blurriest mip level of envMap, z = strength
    float4   skySH[9];            // the sky's diffuse light as spherical harmonics (see SkyIrradiance)
    // ---- Volumetric fog (VolumetricFogPass, VolumetricFog.slang) ----
    float4   volumetricParams;    // x = 1: fog comes from this view's fog volume (0 = the analytic fog below),
                                  // y = the volume's near depth (m), z = its far depth (m), w = log(far / near)
    float4   volumetricLight;     // x = how strongly lights scatter in the fog, y = anisotropy g (-1..1),
                                  // z = depth slices in the volume, w unused
    float4   volumetricAmbient;   // x = strength of the fog's light in shade (1 = the fog colour's look),
                                  // y = 1: that light is the sky's average, tinted by the fog colour
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms> frame;

// ---- Fog ---------------------------------------------------------------------------------------
// Height fog: thick near fogParams.y and thinning exponentially above it, so distant things fade
// into the fog colour and low ground is hazier than hilltops. The amount of fog between the
// camera and a point is worked out exactly (not stepped through), so it costs a few instructions.

// Fog density at world height y, relative to the density at the base height. Capped below the
// base (the exponential would otherwise overflow), which only matters deep underneath it.
float FogDensityAt(float y) {
    return exp(min(-frame.fogParams.x * (y - frame.fogParams.y), 20.0));
}

// How much fog lies between the camera and a surface point (optical depth: the fog hides
// 1 - exp(-this) of what is behind it).
float FogOpticalDepth(float3 worldPos) {
    float3 toPoint = worldPos - frame.cameraPosWS.xyz;
    float  dist    = length(toPoint);

    // Average density along the line of sight: with an exponential falloff this has an exact
    // answer in terms of the densities at its two ends (and is simply the density at the camera
    // when the line is level).
    float atCamera = FogDensityAt(frame.cameraPosWS.y);
    float climb    = frame.fogParams.x * toPoint.y;
    float average  = abs(climb) > 1e-3 ? (atCamera - FogDensityAt(worldPos.y)) / climb : atCamera;

    // The fog-free distance in front of the camera eases in (no fog, then gradually the full
    // rate) rather than switching on, which would draw a visible ring on the ground.
    float start  = frame.fogParams.z;
    float beyond = max(dist - start, 0.0);
    float fogged = beyond * beyond / (beyond + max(start * 0.5, 1e-3));

    return frame.fogColour.a * average * fogged;
}

// 0 = clear, 1 = fully fog-coloured, for a surface point.
float FogAmount(float3 worldPos) {
    if (frame.fogColour.a <= 0.0) { return 0.0; }
    return min(1.0 - exp(-FogOpticalDepth(worldPos)), frame.fogParams.w);
}

// The same for the sky. Above the horizon the line of sight climbs out of the fog for ever, so
// the fog is thickest at the horizon and thins upward. Below the horizon (seen where there is no
// ground) it runs down to the fog's base height, as if the ground were there - so both sides
// approach full fog at the horizon itself and the two meet without a line.
float FogOpticalDepthSky(float3 dir) {
    float falloff  = frame.fogParams.x;
    float atCamera = FogDensityAt(frame.cameraPosWS.y);
    float steep    = max(abs(dir.y), 1e-4);
    float amount;
    if (falloff <= 1e-5) {
        amount = 1e6;                                             // no height falloff: the fog never ends
    } else if (dir.y >= 0.0) {
        amount = atCamera / (falloff * steep);
    } else {
        amount = max(1.0 - atCamera, atCamera * 0.05) / (falloff * steep);   // (the max keeps a camera below the base sensible)
    }
    return frame.fogColour.a * amount;
}

float FogAmountSky(float3 dir) {
    if (frame.fogColour.a <= 0.0) { return 0.0; }
    return min(1.0 - exp(-FogOpticalDepthSky(dir)), frame.fogParams.w);
}

// ---- Volumetric fog ----------------------------------------------------------------------------
// The same fog, lit: VolumetricFog.slang fills a froxel volume per view (a grid that follows the
// camera: 160 x 90 cells across the screen, slices in depth spaced further apart the further
// out they are) with the fog's density and the light scattered toward the camera at each cell -
// the fog colour, plus sunlight where the sun is not shadowed (light shafts) and point and spot
// lights - and adds it up from the camera outward. Shading then reads, for its pixel and depth,
// how much light the fog adds and how much of what is behind it still shows.
//
// Each slice's density is the analytic fog's own optical depth across it (FogOpticalDepth at the
// slice's far edge minus at its near edge), so the volume hides exactly what the analytic fog
// hides along every line of sight; with the lights' scattering at 0 and the fog colour as its
// light in shade (volumetricAmbient 1, sky ambient off) it looks the same.
//
// Past the volume's far depth (and for the sky's fog) the fog is still lit, by the sky and the
// directional lights, just not shadowed: the volume's last slice holds that light per column.

// View depth (m) at slice coordinate c (0 = the volume's near depth, slices = its far depth).
float FogSliceDepth(float c) {
    return frame.volumetricParams.y * exp(c / frame.volumetricLight.z * frame.volumetricParams.w);
}

// The reverse: slice coordinate of a view depth (0 for anything nearer than the near depth).
float FogSliceCoord(float viewDepth) {
    return log(max(viewDepth, frame.volumetricParams.y) / frame.volumetricParams.y) / frame.volumetricParams.w * frame.volumetricLight.z;
}

// This view's integrated fog volume (set 0, binding 10): for each slice, rgb = light the fog adds
// between the camera and the slice's far edge, a = how much of what lies behind still shows. One
// texel deeper than the slices: the last holds each column's unshadowed light (FogColumnSource).
[[vk::binding(10, 0)]] Sampler3D fogVolume;

// The volume at screen position uv (0..1, top-left origin) and a view depth inside it.
float4 FogVolumeAt(float2 uv, float viewDepth) {
    float c = FogSliceCoord(viewDepth);
    // Texel s holds the totals up to slice coordinate s + 1 (the slice's far edge), so depth c
    // sits at texel coordinate c - 1; inside the first slice, fade in from no fog at the camera.
    float4 v = fogVolume.SampleLevel(float3(uv, (max(c, 1.0) - 0.5) / (frame.volumetricLight.z + 1.0)), 0.0);
    return c < 1.0 ? lerp(float4(0.0, 0.0, 0.0, 1.0), v, c) : v;
}

// The light the fog along the line of sight through uv sends toward the camera, per unit of
// density, with nothing shadowed and no point or spot lights: the sky (or fog colour) and the
// directional lights. What the fog beyond the volume and the sky's fog are lit with.
float3 FogColumnSource(float2 uv) {
    float slices = frame.volumetricLight.z;
    return fogVolume.SampleLevel(float3(uv, (slices + 0.5) / (slices + 1.0)), 0.0).rgb;
}

// Extends a (light, transmittance) pair by fog of optical depth `opticalDepth` behind it that
// sends `source` toward the camera per unit of density.
float4 FogAddLit(float4 fog, float3 source, float opticalDepth) {
    float t = exp(-opticalDepth);
    fog.rgb += fog.a * source * (1.0 - t);
    fog.a   *= t;
    return fog;
}

// The analytic fog's "most it can hide" cap, applied to a (light, transmittance) pair.
float4 FogCap(float4 fog) {
    float amount = 1.0 - fog.a;
    float cap    = frame.fogParams.w;
    if (amount > cap) {
        fog.rgb *= cap / max(amount, 1e-5);
        fog.a    = 1.0 - cap;
    }
    return fog;
}

// Fog in front of a surface point, as (light the fog adds, transmittance):
//     colour = colour * fog.a + fog.rgb
// uv is the point's screen position (0..1, top-left origin). From the volume when this view has
// one; past the volume's far depth the rest is the analytic fog's amount, lit but unshadowed.
float4 FogForPoint(float3 worldPos, float2 uv) {
    if (frame.fogColour.a <= 0.0) { return float4(0.0, 0.0, 0.0, 1.0); }
    if (frame.volumetricParams.x < 0.5) {
        float a = FogAmount(worldPos);
        return float4(frame.fogColour.rgb * a, 1.0 - a);
    }

    float  viewDepth = -mul(frame.view, float4(worldPos, 1.0)).z;
    float  far       = frame.volumetricParams.z;
    float4 fog       = FogVolumeAt(uv, min(viewDepth, far));
    if (viewDepth > far) {
        float3 camera   = frame.cameraPosWS.xyz;
        float3 farPoint = camera + (worldPos - camera) * (far / viewDepth);
        fog = FogAddLit(fog, FogColumnSource(uv), max(FogOpticalDepth(worldPos) - FogOpticalDepth(farPoint), 0.0));
    }
    return FogCap(fog);
}

// The same for the sky in direction dir. The sky keeps the analytic sky fog's amount (which,
// below the horizon, pretends there is ground at the fog's base height). With a fog volume, the
// part of it the volume covers is the volume's light (shafts, shadowed fog, light glow), and the
// rest is lit like fog beyond the volume (unshadowed).
float4 FogForSky(float3 dir, float2 uv) {
    if (frame.fogColour.a <= 0.0) { return float4(0.0, 0.0, 0.0, 1.0); }
    float a = FogAmountSky(dir);
    if (frame.volumetricParams.x < 0.5) { return float4(frame.fogColour.rgb * a, 1.0 - a); }

    float4 vol    = FogVolumeAt(uv, frame.volumetricParams.z);
    float  inVol  = 1.0 - vol.a;   // how much the volume's part hides
    float3 light  = a >= inVol
                  ? vol.rgb + FogColumnSource(uv) * (a - inVol)   // the volume, then lit fog for the rest
                  : vol.rgb * (a / max(inVol, 1e-5));             // the sky's fog is thinner (capped): scale down
    return float4(max(light, float3(0.0, 0.0, 0.0)), 1.0 - a);
}
