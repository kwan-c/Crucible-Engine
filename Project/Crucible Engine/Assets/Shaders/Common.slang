// Shared by the shaders that need the whole per-frame block: DefaultShader2, Skybox, Particle.
// (#include is resolved by slangc relative to this folder; nothing changes in CompileShaders.bat.)

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

// 0 = clear, 1 = fully fog-coloured, for a surface point.
float FogAmount(float3 worldPos) {
    if (frame.fogColour.a <= 0.0) { return 0.0; }
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

    return min(1.0 - exp(-frame.fogColour.a * average * fogged), frame.fogParams.w);
}

// The same for the sky. Above the horizon the line of sight climbs out of the fog for ever, so
// the fog is thickest at the horizon and thins upward. Below the horizon (seen where there is no
// ground) it runs down to the fog's base height, as if the ground were there - so both sides
// approach full fog at the horizon itself and the two meet without a line.
float FogAmountSky(float3 dir) {
    if (frame.fogColour.a <= 0.0) { return 0.0; }
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
    return min(1.0 - exp(-frame.fogColour.a * amount), frame.fogParams.w);
}
