// Crucible Engine water (WaterPass): one draw per water surface (a Water component), after the
// opaque scene and the sky and before the particles (the second half of ScenePass). Styled after
// the "Umiko" reference: clear teal shallows over a visible, wobbling bottom, saturated blue deep
// water, crisp white foam where the water is thin, toon-lit ripples with hard sun glints, and the
// sky reflected at grazing angles.
//
// The water does not blend with what is behind it. It reads copies of what was drawn before it
// (set 1: the scene's colour and depth, made just before this pass) and writes its final colour.
// Two distances, both from the copied depth along this pixel's line of sight, decide the look:
//   - depthBelow: how deep the water is under this point of the surface. Picks the colour
//     (shallow -> deep); with how fast it deepens, it gives the distance from the water's
//     edge, which places the shore foam.
//   - path: how far the line of sight travels through the water to the bottom. Decides how
//     clearly the bottom shows (the clarity setting).
// It writes depth (particles behind the water are hidden) and the toon outline data of a flat
// surface with no line strength, so the water itself never gets outlines, while rocks standing out
// of it are outlined against it.

struct VSInput {
    float3 inPosition;
    float3 inNormal;
    float2 inUV;
    float3 inColor;
    float4 inTangent;   // xyz = tangent direction, w = bitangent handedness sign (+-1)
};

// Per-frame data (`frame`), fog, and the lights, shadows and sky light surfaces see.
#include "Common.slang"
#include "Lighting.slang"

// Set 1: copies of the scene drawn so far (opaque meshes and sky), made by ScenePass just before
// the water is drawn.
[[vk::binding(0, 1)]] Sampler2D        sceneColour;   // linear HDR, fog included
[[vk::binding(1, 1)]] Sampler2D<float> sceneDepth;    // hardware depth: 0 = near plane, 1 = far plane (nothing drawn)

// Per surface. Must match Crucible::WaterPush (RenderTypes.h) byte for byte: 128 bytes (the most
// every GPU guarantees), vertex and fragment stages. The six colours are linear RGB as half floats,
// two to a uint (low half first), in this order: shallow, deep, foam, reflection tint, ripple
// line, glint (WaterColours() below unpacks them).
struct WaterPush {
    float4 model0;        // the surface's object -> world transform, as three rows
    float4 model1;
    float4 model2;
    uint4  coloursA;      // shallow r g | shallow b, deep r | deep g b | foam r g
    uint4  coloursB;      // foam b, reflection r | reflection g b | line r g | line b, glint r
    float4 water;         // x = water depth (m) at which it is fully the deep colour, y = clarity: how far (m) through
                          // the water the bottom still shows, z = refraction (0..1), w = shore foam width (m, 0 = none)
    float4 ripple;        // x = ripple size (m), y = ripple speed (m/s), z = how far ripples tilt the surface, w = foam breakup (0..1)
    uint   glintGB;       // glint g | glint b. The glint colour comes already multiplied by the glint strength.
    float  reflection;    // sky reflection strength (0..1)
    float  rippleLines;   // drawn ripple lines (0..1)
    float  unused;
};
[[vk::push_constant]] ConstantBuffer<WaterPush> pc;

// A half float (16 bits) as a float, by moving its bits, so the shader needs no 16-bit types:
// Slang's f16tof32 would make it declare the Float16 and Int16 capabilities, which need device
// features the engine doesn't enable. Infinity and NaN never occur here (they are colours).
float HalfToFloat(uint h) {
    uint exponent = h & 0x7C00u;
    if (exponent == 0u) { return float(h & 0x03FFu) * 5.9604645e-8 * ((h & 0x8000u) != 0u ? -1.0 : 1.0); }   // zero, or too small for a float's exponent shift: 2^-24 steps
    return asfloat(((h & 0x8000u) << 16) | ((exponent + 0x1C000u) << 13) | ((h & 0x03FFu) << 13));
}
// The two half floats in a uint, low half first.
float2 Halves(uint u) { return float2(HalfToFloat(u & 0xFFFFu), HalfToFloat(u >> 16)); }

struct WaterColours { float3 shallow; float3 deep; float3 foam; float3 reflection; float3 line; float3 glint; };
WaterColours UnpackWaterColours() {
    float2 a0 = Halves(pc.coloursA.x), a1 = Halves(pc.coloursA.y), a2 = Halves(pc.coloursA.z), a3 = Halves(pc.coloursA.w);
    float2 b0 = Halves(pc.coloursB.x), b1 = Halves(pc.coloursB.y), b2 = Halves(pc.coloursB.z), b3 = Halves(pc.coloursB.w);
    float2 g  = Halves(pc.glintGB);
    WaterColours c;
    c.shallow    = float3(a0.x, a0.y, a1.x);
    c.deep       = float3(a1.y, a2.x, a2.y);
    c.foam       = float3(a3.x, a3.y, b0.x);
    c.reflection = float3(b0.y, b1.x, b1.y);
    c.line       = float3(b2.x, b2.y, b3.x);
    c.glint      = float3(b3.y, g.x, g.y);
    return c;
}

struct VSOutput {
    float4 pos : SV_Position;
    float3 worldPos;
    float3 normalWS;    // the surface's up
    float3 tangentWS;   // one direction along it (the ripples are laid out along it and across it)
};

[shader("vertex")]
VSOutput waterVert(VSInput input) {
    VSOutput output;

    float4 p = float4(input.inPosition, 1.0);
    output.worldPos = float3(dot(pc.model0, p), dot(pc.model1, p), dot(pc.model2, p));
    output.pos      = mul(frame.proj, mul(frame.view, float4(output.worldPos, 1.0)));

    // Uniform scale assumed, as for meshes (DefaultShader's vertMain).
    float3x3 upper   = float3x3(pc.model0.xyz, pc.model1.xyz, pc.model2.xyz);
    output.normalWS  = normalize(mul(upper, input.inNormal));
    output.tangentWS = normalize(mul(upper, input.inTangent.xyz));
    return output;
}

// ---- Noise ---------------------------------------------------------------------------------------

float WaterHash(float2 p) {   // 0..1, from a lattice point
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// Smooth value noise, 0..1 (mostly 0.2..0.8), with its slope: (value, d/dx, d/dy).
float3 WaterNoise(float2 p) {
    float2 i  = floor(p);
    float2 f  = p - i;
    float2 u  = f * f * (3.0 - 2.0 * f);
    float2 du = 6.0 * f * (1.0 - f);
    float a = WaterHash(i);
    float b = WaterHash(i + float2(1.0, 0.0));
    float c = WaterHash(i + float2(0.0, 1.0));
    float d = WaterHash(i + float2(1.0, 1.0));
    float k = a - b - c + d;
    float value = a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y;
    float2 grad = du * float2((b - a) + k * u.y, (c - a) + k * u.x);
    return float3(value, grad);
}

// The ripple pattern at q (metres along the surface): two layers of noise drifting different
// ways, the second finer and turned. Returns (height 0..1, slope along q.x, slope along q.y), with
// the slopes per ripple size (so ripple strength means the same at any size).
float3 Ripples(float2 q, float size, float t) {
    float2 p = q / size;
    float3 a = WaterNoise(p + float2(0.8, 0.6) * t);

    const float2x2 turn = float2x2(0.8, -0.6, 0.6, 0.8);
    float3 b  = WaterNoise(mul(turn, p) * 1.9 - float2(0.5, 0.7) * (t * 1.3));
    float2 gb = 1.9 * mul(transpose(turn), b.yz);   // b's slope, back in p's directions

    return float3(a.x * 0.6 + b.x * 0.4, a.yz * 0.6 + gb * 0.4);
}

// ---- Depth ---------------------------------------------------------------------------------------

// View depth (m, along the camera's forward axis) of a hardware depth value. proj[r][c] is row r,
// column c: proj[2][3] is glm's proj[3][2].
float ViewDepthOf(float d) {
    if (frame.proj[3][3] > 0.5) { return -(d - frame.proj[2][3]) / frame.proj[2][2]; }   // orthographic
    return frame.proj[2][3] / (d + frame.proj[2][2]);
}

// The world position of what lies behind this water pixel, `depth` metres from the camera, on the
// same line of sight as the water point (viewPos = the water point in view space).
float3 BehindPoint(float3 viewPos, float3 worldPos, float depth) {
    bool     ortho      = frame.proj[3][3] > 0.5;
    float3   behindView = ortho ? float3(viewPos.xy, -depth) : viewPos * (depth / max(-viewPos.z, 1e-4));
    float3x3 rotation   = (float3x3)frame.view;   // world -> view; its transpose turns view back to world
    return worldPos + mul(transpose(rotation), behindView - viewPos);
}

// Whether a shore lies within `reach` metres of p (a point of the surface) in direction `dir`
// across the surface: walks there in six steps and looks at what the depth copy shows under each
// point. A shore = the bottom at or above the surface, or something in front of that point of the
// surface: the bank rising out of the water beyond the waterline, or anything else hiding it
// (can't tell; nor off screen) - yes, so foam is never lost for want of a look.
bool ShoreWithin(float3 p, float3 up, float3 dir, float reach) {
    [loop] for (int k = 1; k <= 6; ++k) {
        float3 q    = p + dir * (reach * float(k) / 6.0);
        float3 view = mul(frame.view, float4(q, 1.0)).xyz;
        float4 clip = mul(frame.proj, float4(view, 1.0));
        if (clip.w <= 1e-4) { return true; }
        float2 ndc  = clip.xy / clip.w;
        float2 uvQ  = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);   // the viewport flips y
        if (any(uvQ < 0.0) || any(uvQ > 1.0)) { return true; }
        float  d    = sceneDepth.SampleLevel(uvQ, 0.0);
        if (d >= 1.0) { continue; }                              // only sky behind: open water
        float  z    = ViewDepthOf(d);
        if (z < -view.z) { return true; }                        // something in front of this point of the surface
        if (dot(q - BehindPoint(view, q, z), up) <= 0.0) { return true; }   // the bottom reaches the surface
    }
    return false;
}

// ---- Light ---------------------------------------------------------------------------------------

// A hard-edged sparkle where a ripple faces halfway between the eye and the light.
float Glint(float3 N, float3 V, float3 L, float sharpness) {
    float3 H = normalize(V + L);
    float  s = pow(saturate(dot(N, H)), sharpness);
    return smoothstep(0.3, 0.5, s);
}

struct WaterOutput {
    float4 colour : SV_Target0;   // linear HDR radiance
    float4 lines  : SV_Target1;   // toon outline data (DefaultShader's LineData)
};

[shader("fragment")]
[require(spvDerivativeControl)]   // ddx_fine / ddy_fine below (core Vulkan)
WaterOutput waterFrag(VSOutput input) {

    float2 target = frame.clusterZ.zw;
    float2 uv     = input.pos.xy / target;
    float3 up     = normalize(input.normalWS);
    float3 T      = normalize(input.tangentWS - up * dot(up, input.tangentWS));
    float3 B      = cross(T, up);
    float3 V      = normalize(frame.cameraPosWS.xyz - input.worldPos);
    WaterColours colours = UnpackWaterColours();

    // ---- How much water lies over what is behind this pixel ----
    float3 viewPos    = mul(frame.view, float4(input.worldPos, 1.0)).xyz;
    float  waterZ     = max(-viewPos.z, 1e-3);
    float  behindD    = sceneDepth.SampleLevel(uv, 0.0);
    float3 behind     = BehindPoint(viewPos, input.worldPos, ViewDepthOf(behindD));
    float  depthBelow = max(dot(input.worldPos - behind, up), 0.0);
    float  path       = length(behind - input.worldPos);
    if (behindD >= 1.0) { depthBelow = 1e4; path = 1e4; }   // only sky behind: bottomless
    // Something in front of the water here instead of under it: a pixel on the outline of an
    // object standing in front of the water (the depth copy gave it to the object), or a
    // neighbouring pixel the GPU runs only to work out the slopes below. Where that thing is no
    // higher above the water than the foam band is wide, it touches the water: a shore like any
    // other. Higher up, this pixel says nothing about how deep the water is: it gets no foam, and
    // the slopes leave it out (otherwise the foam traced the outline of anything in front of a
    // line of foam).
    bool  inFront = behindD < 1.0 && ViewDepthOf(behindD) < waterZ;
    bool  unknown = inFront && dot(behind - input.worldPos, up) > max(pc.water.w, 0.05);
    if (unknown) { depthBelow = 1e4; path = 1e4; }

    // ---- Ripples: a slowly drifting tilt of the surface ----
    float2 q          = float2(dot(input.worldPos, T), dot(input.worldPos, B));
    float  size       = max(pc.ripple.x, 0.05);
    float  t          = frame.time.x * pc.ripple.y / size;
    float3 rip        = Ripples(q, size, t);
    // Ripples smaller than a couple of pixels would only flicker: fade them out with distance.
    float  rippleFade = 1.0 - smoothstep(0.3, 0.7, length(fwidth(q / size)));
    float2 tilt       = rip.yz * pc.ripple.z * rippleFade;
    float3 N          = normalize(up - T * tilt.x - B * tilt.y);

    // ---- The bottom, seen through the surface: the scene copy, nudged by the ripples ----
    // Less bending at the very edge, so the shore line and its foam stay put.
    float3 bend   = mul((float3x3)frame.view, N - up);   // how the ripples turn the surface, in view space
    float2 offset = float2(bend.x, -bend.y) * (pc.water.z * 0.1) * saturate(depthBelow / 0.3) / max(waterZ * 0.1, 1.0);
    float2 uvR    = clamp(uv + offset, 0.5 / target, 1.0 - 0.5 / target);
    // Only bend towards things behind the water: something standing in front of it there (a rock
    // out of the water) would otherwise smear into the surface.
    if (ViewDepthOf(sceneDepth.SampleLevel(uvR, 0.0)) < waterZ) { uvR = uv; }
    float3 bottom = sceneColour.SampleLevel(uvR, 0.0).rgb;

    // ---- Colour: shallow -> deep with the water's depth; the bottom fades with the distance ----
    float3 shallowC   = colours.shallow;
    float  deepMix    = 1.0 - exp(-3.0 * depthBelow / max(pc.water.x, 0.01));
    float3 albedo     = lerp(shallowC, colours.deep, deepMix);
    float  clarityLen = path / max(pc.water.y, 0.01);   // distance through the water, in clarity lengths
    float  seen       = exp(-2.5 * clarityLen);         // how much of the bottom shows (1 = all of it)
    // The water's own colour soaks into the bottom: a little even in the shallows (so shallow
    // water reads as water, not as bare ground), more the further it is seen through.
    float3 tint       = shallowC / max(max(shallowC.r, max(shallowC.g, shallowC.b)), 1e-3);
    float3 bottomSeen = bottom * pow(max(tint, float3(1e-3, 1e-3, 1e-3)), 0.3 + clarityLen * 0.7);

    // ---- Light on the water's body, its foam and its drawn lines (as on a flat surface) ----
    // The scene's toon edge when the scene is toon-shaded (sharp shadows on the water), plain
    // light otherwise. The shadow-casting sun (else the first directional light) is the key light:
    // where it doesn't reach, the sky's light takes the toon shade tint, as on toon materials.
    bool   toon      = frame.toonParams.x > 0.5 && frame.toonParams.x < 1.5;
    float  threshold = frame.toonParams.y;
    float  soft      = max(frame.toonParams.z, 1e-4);
    uint   dirCount  = frame.lightParams.w;
    uint   shadowed  = frame.lightParams.y;   // index + 1, 0 = none
    uint   keyLight  = shadowed > 0 ? shadowed - 1 : 0;
    float  keyLit    = 0.0;
    float3 direct    = float3(0.0, 0.0, 0.0);
    float3 glint     = float3(0.0, 0.0, 0.0);
    // Cast shadows wobble with the ripples as if the surface rose and fell with them: the shadow
    // map is read at this point raised or lowered by the ripples' height, straight up from the
    // water. A shadow on a surface that moves up and down slides back and forth along the
    // direction the light comes from, most at its far end, while where it meets what casts it
    // stays put, and nothing standing in the water can shadow its own sunlit side (the raised or
    // lowered point is still outside it). How far it rises and falls follows the ripples' size and
    // strength and the Refraction setting (the bottom's shadows already wobble with that); never
    // more than half way down to the bottom (the read would land in the ground and shadow itself);
    // still at the very edge, like the refraction, so it meets the shore's own shadows there.
    float swell = (rip.x - 0.5) * size * pc.ripple.z * 1.5 * pc.water.z * rippleFade;   // how far the ripples lift the surface here (m)
    float bob   = max(swell * saturate(depthBelow / 0.3), -0.5 * depthBelow);
    float3 shadowPos = input.worldPos + up * bob;
    for (uint i = 0; i < dirCount; ++i) {
        float3 L, radiance;
        LightIncoming(lights[i], input.worldPos, L, radiance);
        float NdotL  = saturate(dot(up, L));
        float shadow = (i + 1 == shadowed && NdotL > 0.0) ? SampleShadow(shadowPos, up, L) : 1.0;
        float amount = NdotL * shadow;
        float edgeLit = smoothstep(threshold - soft, threshold + soft, amount);
        direct += radiance / kPi * (toon ? edgeLit * lerp(1.0, NdotL, frame.toonExtra.w) : amount);
        if (i == keyLight) { keyLit = toon ? edgeLit : shadow; }
        glint += radiance * Glint(N, V, L, 900.0) * shadow;
    }
    // Point and spot lights near this pixel (lanterns): soft light, and their own glints.
    ClusterCell cell = ClusterFor(input.pos, input.worldPos);
    for (uint k = 0; k < cell.count; ++k) {
        float3 L, radiance;
        if (!LightIncoming(lights[clusterLightIndices[cell.offset + k]], input.worldPos, L, radiance)) { continue; }
        direct += radiance / kPi * saturate(dot(up, L));
        glint  += radiance * Glint(N, V, L, 250.0);
    }
    bool   fromSky = frame.ambientParams.x > 0.5;
    float3 sky     = fromSky ? SkyIrradiance(up) * frame.ambientParams.z : HemisphereAmbient(up);
    if (toon) {
        float3 flatSky = fromSky ? max(frame.skySH[0].rgb, float3(0.0, 0.0, 0.0)) * frame.ambientParams.z
                                 : 0.5 * (frame.ambientSky.rgb + frame.ambientGround.rgb);
        sky = lerp(sky, flatSky, frame.toonShade.w) * lerp(frame.toonShade.rgb * frame.toonParams.w, float3(1.0, 1.0, 1.0), keyLit);
    }
    float3 lightOn = direct + sky;   // the light a white, level surface here receives

    // ---- The sky, reflected at grazing angles ----
    float  NdotV   = saturate(dot(N, V));
    float  fresnel = (0.02 + 0.98 * pow(1.0 - NdotV, 5.0)) * saturate(pc.reflection);
    float3 R       = reflect(-V, N);
    if (dot(R, up) < 0.0) { R -= 2.0 * dot(R, up) * up; }   // a ripple can't reflect what is under the water
    float3 reflection = envMap.SampleLevel(R, 0.0).rgb * frame.renderParams.y * colours.reflection;   // the sky as it is drawn, tinted

    // ---- Shore foam: a crisp band along the water's edge, and a line lapping further out ----
    // Measured as the distance (m) from the water's edge across the surface, so the band is as
    // wide on a gentle beach as against a steep rock: the water's depth here divided by how fast
    // it deepens across the surface (worked out from screen-space slopes of the depth and of the
    // surface position). Taken here, outside any branch that differs between pixels. Each slope
    // is the difference with the neighbouring pixel across (fine derivatives). It is dropped when
    // one of the two pixels is unknown (above), and across a jump in what lies under the water:
    // more than 10 m deeper per metre across is two different surfaces (the edge of something
    // sunken or standing in the water against the deeper bottom beside it), not a slope, and
    // would draw foam along that edge.
    float  unknownF  = unknown ? 1.0 : 0.0;
    float2 dqx       = ddx_fine(q), dqy = ddy_fine(q);
    float  dhx       = ddx_fine(depthBelow), dhy = ddy_fine(depthBelow);
    float  okX       = ddx_fine(unknownF) == 0.0 && abs(dhx) < 10.0 * length(dqx) ? 1.0 : 0.0;
    float  okY       = ddy_fine(unknownF) == 0.0 && abs(dhy) < 10.0 * length(dqy) ? 1.0 : 0.0;
    dhx *= okX;
    dhy *= okY;
    float  det       = dqx.x * dqy.y - dqx.y * dqy.x;
    float2 deepening = abs(det) > 1e-12 ? float2(dqy.y * dhx - dqx.y * dhy, dqx.x * dhy - dqy.x * dhx) / det : float2(0.0, 0.0);
    float  fromEdge  = depthBelow / max(length(deepening), 1e-3);
    float  aa        = max(abs(ddx_fine(fromEdge)) * okX + abs(ddy_fine(fromEdge)) * okY, 1e-4);
    float  lineWidth = 0.03 + fwidth(rip.x);
    // The distance from the edge assumes the bottom keeps its slope all the way to the shore. Where
    // it bends to a steeper slope further out (a bank, an edge loop of a rock's mesh), the estimate
    // drops again out there and would draw a second band: walk from here towards the shallow side
    // and see whether a shore really is that close (ShoreWithin).
    float foam  = 0.0;
    float width = pc.water.w;
    float3 shallower = -(T * deepening.x + B * deepening.y);
    if (width > 0.0 && depthBelow < 2.0 * width && fromEdge < 5.0 * width && dot(shallower, shallower) > 1e-12
        && ShoreWithin(input.worldPos, up, normalize(shallower), fromEdge + 0.5 * width)) {
        float n     = WaterNoise(q / max(width * 6.0, 0.05) + float2(0.13, 0.07) * frame.time.x).x;
        float edge  = width * lerp(1.0, 0.25 + 1.2 * n, saturate(pc.ripple.w));   // breakup: the band's width wanders
        // The ripples lift and lower the water against the shore, as they do for the cast
        // shadows: the band follows the moving waterline (most on a gentle slope, least against a
        // steep rock), by no more than half its width either way.
        float shore = fromEdge + clamp(swell / max(length(deepening), 1e-3), -0.5 * width, 0.5 * width);
        foam = 1.0 - smoothstep(edge - aa, edge + aa, shore);

        // The lapping line starts at the band's edge, drifts out to about three times the band's
        // width while thinning, and fades; each stretch of shore at its own moment.
        float phase = frac(frame.time.x * 0.2 + n * 0.35);
        float at    = edge * (1.4 + 1.6 * phase);
        float thick = width * 0.12 * (1.0 - phase);
        float lap   = 1.0 - smoothstep(thick - aa, thick + aa, abs(shore - at));
        float gaps  = smoothstep(0.35, 0.55, WaterNoise(q / max(width * 3.0, 0.05) - 0.05 * frame.time.x).x);
        foam = max(foam, lap * (1.0 - phase) * lerp(1.0, gaps, saturate(pc.ripple.w)));

        // Only where the water really is shallow: the edge of something deep under the water (a
        // sunken rock seen from the side) also makes the depth jump, which would otherwise read
        // as a nearby shore.
        foam *= 1.0 - smoothstep(width * 1.2, width * 2.0, depthBelow);
    }

    // ---- Drawn ripple lines: thin wobbly strokes on open water ----
    float strokes = 0.0;
    if (pc.rippleLines > 0.0) {
        float onLine    = 1.0 - smoothstep(lineWidth * 0.5, lineWidth, abs(rip.x - 0.5));
        float someOnly  = smoothstep(0.55, 0.7, WaterNoise(q / (size * 2.7) - float2(0.03, 0.05) * frame.time.x).x);
        strokes = onLine * someOnly * rippleFade * saturate(pc.rippleLines) * saturate(fromEdge / max(pc.water.w * 3.0, 0.05));
    }

    // ---- Together ----
    float3 body    = albedo * lightOn;
    float3 under   = lerp(body, bottomSeen, seen);   // what comes up through the surface
    float3 colour  = lerp(under, reflection, fresnel);
    float  cover   = max(foam, strokes);
    // Shore foam and ripple lines, each in its own colour (mixed by their shares where they overlap).
    float3 coverC  = (colours.foam * foam + colours.line * strokes) / max(foam + strokes, 1e-4);
    colour = lerp(colour, coverC * lightOn, cover);
    colour += glint * colours.glint * (1.0 - cover);   // colours.glint: the glint colour times the glint strength

    // Fog. The bottom seen through the water was drawn with the fog in front of it already; only
    // the water's own light gets the fog in front of the surface.
    float  through = seen * (1.0 - fresnel) * (1.0 - cover);   // share of the result that is the bottom
    float3 own     = colour - bottomSeen * through;
    float4 fog     = FogForPoint(input.worldPos, uv);
    colour = own * fog.a + fog.rgb * (1.0 - through) + bottomSeen * through;

    WaterOutput output;
    output.colour = float4(max(colour, float3(0.0, 0.0, 0.0)), 1.0);
    output.lines  = float4(1.0 / waterZ, OctEncode(up), 0.0);   // the surface itself never gets a line
    return output;
}
