// The magic orb: a golem's joints. A glowing sphere with energy swirling inside it - a pattern
// that turns and drifts with time, a hot core facing the camera and a bright rim.
//
// This is a second mesh shader: its own surface function, then the engine's mesh shader
// (SurfaceTemplate.slang), the same two-part shape as DefaultShader.slang - see Surface.slang.
// A material picks it with   "shader": "MagicOrb"   in its .mat file. Its controls:
//   - the material's usual factors:
//       albedoTint      the orb's body colour (rgb), under the glow
//       emissiveFactor  the glow's colour and strength (above 1 blooms)
//       roughness, metallic   as usual
//   - its own parameters, listed in MagicOrb.params.json (the Inspector shows them, a .mat
//     stores them under "params" and "textures"). The slots below are positions in that list:
//     keep the two in step.
// The object's own glow multiplier (MaterialParams) scales the result, like any emissive.
//
// Built by Engine/CompileShaders.cmake like every shader; saved while the Editor runs, it is
// recompiled and swapped in (ShaderHotReload).

#include "Surface.slang"

// MagicOrb.params.json, "params":
static const uint kWispColour = 0;   // colour: tint of the bright threads (white = the glow's own colour)
static const uint kSwirlSpeed = 1;   // float: how fast the pattern turns
static const uint kTwist      = 2;   // float: how much more the top turns than the bottom
static const uint kDrift      = 3;   // float: how fast it all rises
static const uint kCoreHeat   = 4;   // float: strength of the hot centre facing the camera
static const uint kRimGlow    = 5;   // float: strength of the bright edge
// "textures":
MATERIAL_TEXTURE(0, patternTex);     // greyscale, laid out by the mesh's UVs: dims the glow where it is dark (white = no change)

float OrbHash(float3 p) {   // 0..1, from a lattice point
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.x + p.y) * p.z);
}

float OrbNoise(float3 p) {   // smooth value noise, 0..1
    float3 i = floor(p);
    float3 f = p - i;
    float3 u = f * f * (3.0 - 2.0 * f);
    float x00 = lerp(OrbHash(i),                         OrbHash(i + float3(1.0, 0.0, 0.0)), u.x);
    float x10 = lerp(OrbHash(i + float3(0.0, 1.0, 0.0)), OrbHash(i + float3(1.0, 1.0, 0.0)), u.x);
    float x01 = lerp(OrbHash(i + float3(0.0, 0.0, 1.0)), OrbHash(i + float3(1.0, 0.0, 1.0)), u.x);
    float x11 = lerp(OrbHash(i + float3(0.0, 1.0, 1.0)), OrbHash(i + float3(1.0, 1.0, 1.0)), u.x);
    return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
}

float OrbClouds(float3 p) {   // three layers of it, each finer and fainter: 0..1
    return OrbNoise(p) * 0.57 + OrbNoise(p * 2.03 + 17.0) * 0.29 + OrbNoise(p * 4.01 + 43.0) * 0.14;
}

SurfaceData EvaluateSurface(SurfaceInput s) {
    SurfaceData o;

    // Where on the orb this pixel is: on a sphere, the normal is the direction out from the
    // centre, so the pattern stays on the orb wherever the orb goes.
    float3 dir = s.normalWS;

    // The swirl: the pattern turns around the up axis, by more the higher up the orb (a twist),
    // and all of it drifts upward.
    float angle = s.time * MaterialParam(kSwirlSpeed).x + dir.y * MaterialParam(kTwist).x;
    float sn = sin(angle), cs = cos(angle);
    float3 p = float3(dir.x * cs - dir.z * sn, dir.y, dir.x * sn + dir.z * cs);

    float flow  = OrbClouds(p * 3.0 - float3(0.0, s.time * MaterialParam(kDrift).x, 0.0));   // broad, slow
    float wisps = smoothstep(0.45, 0.75, OrbClouds(p * 6.0 + flow * 1.5));    // bright threads, bent by the flow

    float facing = saturate(dot(s.normalWS, s.viewDir));
    float core   = pow(facing, 3.0);          // hot centre, where the orb faces the camera
    float rim    = pow(1.0 - facing, 3.0);    // bright edge

    float3 glow    = pc.emissiveFactor * patternTex.Sample(s.uv).r;
    float  coreHot = core * MaterialParam(kCoreHeat).x;
    float  energy  = 0.15 + 0.55 * flow + coreHot + rim * MaterialParam(kRimGlow).x;

    o.albedo    = pc.albedoTint.rgb * s.vertexColour * (0.25 + 0.35 * flow);
    o.emissive  = glow * energy
                + glow * MaterialParam(kWispColour).rgb * wisps * 0.9
                + dot(glow, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)) * core * coreHot * 0.75;   // the very centre burns towards white
    o.normalTS  = float3(0.0, 0.0, 1.0);
    o.metallic  = pc.metallicFactor;
    o.roughness = pc.roughnessFactor;
    o.occlusion = 1.0;
    o.alpha     = pc.albedoTint.a;

    return o;
}

#include "SurfaceTemplate.slang"
