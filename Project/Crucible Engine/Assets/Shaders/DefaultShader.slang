// Crucible Engine's default (and only) material shader: full metallic-roughness PBR, lit by every
// light the Renderer was given this frame (set 0, binding 1 - see Renderer::SubmitLight and the ECS
// Light component) plus a hemisphere ambient term, then exposure + ACES tonemapping. With the
// scene's Skybox set to toon shading (frame.toonParams.x = 1), the same inputs are lit the toon way
// instead (ShadeToon below). Every material
// (Renderer::CreateMaterial) binds set 1's five texture slots below; any slot the caller left
// Invalid was already substituted with a neutral placeholder texture on the C++ side
// (Renderer::WriteMaterialDescriptorSet), so this shader never has to branch on "is this map
// present".

struct VSInput {
    float3 inPosition;
    float3 inNormal;
    float2 inUV;
    float3 inColor;
    float4 inTangent;   // xyz = tangent direction, w = bitangent handedness sign (+-1)
};

// Per-frame data (FrameUniforms, `frame`) and the fog functions, shared with the sky and particles.
#include "Common.slang"
// Lights, shadow map, light clusters and the sky as a light source (shared with Water.slang).
#include "Lighting.slang"

// One instance (set 0, binding 2). Must match Crucible::InstanceData (RenderTypes.h).
struct InstanceData {
    float4x4 model;
    float4   params;   // x = multiplier on the material's emissive, yzw = spare (MaterialParams component)
};

// Per-draw data: the current material's scalar settings (constant across a batch, since batches
// never cross materials). Must match Crucible::PushConstants (RenderTypes.h) byte for byte - the
// first five fields are Crucible::MaterialFactors. Written out flat (not as a nested struct) so
// the offsets are the C++ ones under every packing rule.
struct PushConstants {
    float4 albedoTint;
    float3 emissiveFactor;
    float  metallicFactor;
    float  roughnessFactor;
    float  alphaCutoff;   // 0 = solid
    uint   flags;         // bits 0-1: shading (0 = the scene's, 1 = toon, 2 = realistic),
                          // bit 2: toonEdge applies, bit 3: toonShade applies
    float  outline;       // toon outlines on this material (0..1)
    float4 toonEdge;      // x = edge position, y = softness (the material's own, flag bit 2)
    float4 toonShade;     // rgb = shade tint, linear (the material's own, flag bit 3)
    float4 toonLit;       // rgb = tint of the lit side, linear (white = none)
};
static const uint kShadingScene     = 0;
static const uint kShadingToon      = 1;
static const uint kShadingRealistic = 2;
static const uint kFlagOwnEdge      = 4;
static const uint kFlagOwnShade     = 8;

// Cut-out pipelines (ScenePass's second set) are built with this on: pixels whose alpha is below
// pc.alphaCutoff are discarded. Solid materials use pipelines with it off, where the discard is
// compiled out entirely (a shader that can discard loses early depth testing).
[[vk::constant_id(0)]] const bool kCutout = false;

// Every object drawn this frame, by every view and pass (Renderer::UploadInstances). A draw's
// instances are contiguous; drawIndexed's firstInstance points at the first one, and
// SV_VulkanInstanceID (gl_InstanceIndex) already includes that offset.
[[vk::binding(2, 0)]]
StructuredBuffer<InstanceData> instances;

// Ambient occlusion for this view (Post.slang's aoFrag/aoBlurFrag): 1 = open, lower = tucked into
// a crease or corner. Half resolution; all white when the effect is off.
[[vk::binding(9, 0)]] Sampler2D aoTex;

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

// Set 1: the current draw's material. Binding order must match
// VulkanPipeline::CreateMaterialDescriptorSetLayout exactly.
[[vk::binding(0, 1)]] Sampler2D albedoTex;
[[vk::binding(1, 1)]] Sampler2D normalTex;
[[vk::binding(2, 1)]] Sampler2D metallicRoughnessTex;   // glTF convention: G = roughness, B = metallic
[[vk::binding(3, 1)]] Sampler2D emissiveTex;
[[vk::binding(4, 1)]] Sampler2D occlusionTex;           // R channel only, per glTF

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float3 normalWS;
    float3 tangentWS;
    float  tangentSign;
    float2 uv;
    float3 worldPos;
    nointerpolation float4 instanceParams;   // InstanceData::params, the same for the whole object
    float3 linePos;                          // where the outline breakup pattern is read (see vertMain)
};

[shader("vertex")]
VSOutput vertMain(VSInput input, uint instanceID : SV_VulkanInstanceID) {
    VSOutput output;

    float4x4 model = instances[instanceID].model;
    float4 worldPos4 = mul(model, float4(input.inPosition, 1.0));
    output.worldPos = worldPos4.xyz;
    output.pos = mul(frame.proj, mul(frame.view, worldPos4));
    output.color = input.inColor;
    output.uv = input.inUV;
    output.instanceParams = instances[instanceID].params;

    // NOTE: correct for uniform scale only. Non-uniform scale needs the inverse-transpose of the
    // model's upper-left 3x3 for the normal specifically; the tangent, being a surface direction
    // rather than a normal, would need the model's 3x3 directly even then - the two would diverge
    // under non-uniform scale. Both use the same simplified transform here so they stay mutually
    // consistent (a correctly-lit-but-slightly-wrong-under-non-uniform-scale TBN beats a
    // correctly-transformed normal paired with a now-non-orthogonal tangent).
    float3x3 modelUpper = (float3x3)model;
    output.normalWS    = normalize(mul(modelUpper, input.inNormal));
    output.tangentWS   = normalize(mul(modelUpper, input.inTangent.xyz));
    output.tangentSign = input.inTangent.w;

    // Toon outlines: the breakup pattern is read at the mesh's own position, in metres (scaled
    // like the object, but not turned or moved with it), so the gaps stay put on the surface of
    // a moving, turning object. model[r][c]: column c is where the object's axis c points.
    float3 scale = float3(length(float3(model[0][0], model[1][0], model[2][0])),
                          length(float3(model[0][1], model[1][1], model[2][1])),
                          length(float3(model[0][2], model[1][2], model[2][2])));
    output.linePos = input.inPosition * scale;

    return output;
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (float3(1.0, 1.0, 1.0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Trowbridge-Reitz / GGX normal distribution function.
float DistributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, 1e-6);
}

// Schlick-GGX geometry term (direct-lighting k remapping), combined via Smith's method.
float GeometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// Cook-Torrance GGX specular + Lambert diffuse for one light direction L carrying `radiance`.
float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 radiance,
                    float3 albedo, float metallic, float roughness, float3 F0) {
    float NdotL = dot(N, L);
    if (NdotL <= 0.0) { return float3(0.0, 0.0, 0.0); }

    float3 H     = normalize(V + L);
    float  NdotV = max(dot(N, V), 1e-4);
    float  NdotH = max(dot(N, H), 0.0);
    float  VdotH = max(dot(V, H), 0.0);

    float3 F = FresnelSchlick(VdotH, F0);
    float  D = DistributionGGX(NdotH, roughness);
    float  G = GeometrySmith(NdotV, NdotL, roughness);

    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 kd       = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);   // metals have no diffuse term
    float3 diffuse  = kd * albedo / kPi;

    return (diffuse + specular) * radiance * NdotL;
}

// Contribution of one GpuLight at this surface point.
float3 EvaluateLight(GpuLight light, float3 worldPos, float3 N, float3 V,
                     float3 albedo, float metallic, float roughness, float3 F0) {
    float3 L, radiance;
    if (!LightIncoming(light, worldPos, L, radiance)) { return float3(0.0, 0.0, 0.0); }
    return EvaluateBRDF(N, V, L, radiance, albedo, metallic, roughness, F0);
}

// Roughness-aware Fresnel for the ambient specular term (Lagarde's approximation): rough surfaces
// shouldn't get a bright grazing-angle rim from an environment they can't sharply reflect.
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float3 maxRefl = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (maxRefl - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// How much of the reflected sky a surface shows, by its base reflectivity, roughness and viewing
// angle (Karis' fitted approximation - stands in for the usual lookup texture).
float3 EnvBRDFApprox(float3 F0, float roughness, float NdotV) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return F0 * ab.x + ab.y;
}

// ---- Toon shading (the material's shading is Toon, or Scene with the Skybox's toonShading on) --
// Light turns into two flat tones, lit and shaded, split by a hard (slightly soft) edge, with a
// coloured band just inside the lit side as in paintings. Cast shadows go through the same edge, so
// they get the same crisp line and band. The shaded side is lit by the sky's light (flattened by
// toonShade.w) times toonParams.w, tinted (toonShade.rgb), so shade follows the sky without the
// lit side brightening. Sky reflections are off unless toonExtra.z asks for some; smooth and metal
// surfaces can get a hard-edged highlight (toonExtra.y). The lit side keeps toonExtra.w of the
// usual shading by angle, so differently turned faces of one object still read apart.
// A material can bring its own edge (position, softness), shade tint and lit tint - foliage that
// shades as one soft warm-to-cool gradient, for instance (ToonSurface's fields).

struct ToonSurface {
    float3 diffuseAlbedo;   // albedo for the two tones (a little darker on metal)
    float3 albedo;
    float  metallic;
    float  roughness;
    float3 N;               // normal-mapped
    float3 V;
    float  threshold;       // where the light/shade edge is (scene's, or the material's own)
    float  softness;        // how soft it is
    float3 shadeTint;       // tint of the shaded side
    float3 litTint;         // tint of the lit side (the material's; white = none)
};

// 0 = shade, 1 = lit: a smooth step at the edge position, `softness` wide on each side.
float ToonStep(ToonSurface s, float light) {
    float soft = max(s.softness, 1e-4);
    return smoothstep(s.threshold - soft, s.threshold + soft, light);
}

// One light's toon contribution. `shadow` = how much of it reaches the point (1 = all). `lit`
// returns the point's lit (1) / shade (0) split for this light.
float3 ToonLight(ToonSurface s, float3 L, float3 radiance, float shadow, out float lit) {
    float light = saturate(dot(s.N, L)) * shadow;   // 0..1: how much of the light the surface gets
    lit = ToonStep(s, light);

    // The band: strongest at the edge, gone toonEdge.w further into the light. The lit side keeps
    // a little of the shading by angle (toonExtra.w), so faces turned differently still differ.
    float  band    = lit * (1.0 - smoothstep(s.threshold, s.threshold + max(frame.toonEdge.w, 1e-4), light));
    float3 litTone = lerp(float3(1.0, 1.0, 1.0), frame.toonEdge.rgb, band * frame.toonExtra.x)
                   * lerp(1.0, saturate(dot(s.N, L)), frame.toonExtra.w) * s.litTint;
    float3 colour  = s.diffuseAlbedo / kPi * radiance * lit * litTone;

    // Hard-edged highlight, on smooth surfaces only (gone by roughness 0.85).
    float smoothness = 1.0 - smoothstep(0.5, 0.85, s.roughness);
    if (smoothness > 0.0 && frame.toonExtra.y > 0.0 && lit > 0.0) {
        float3 H         = normalize(s.V + L);
        float  r2        = s.roughness * s.roughness;
        float  shininess = min(2.0 / max(r2 * r2, 1e-4) - 2.0, 2048.0);   // Blinn-Phong exponent for this roughness
        float  spec      = pow(saturate(dot(s.N, H)), shininess);
        float  soft      = max(s.softness, 0.02);
        float  shine     = smoothstep(0.5 - soft, 0.5 + soft, spec) * lit * smoothness;
        float3 tint      = lerp(float3(0.5, 0.5, 0.5), s.albedo * 1.5, s.metallic);   // whitish on plain surfaces, coloured on metal
        colour += tint * radiance / kPi * shine * frame.toonExtra.y;
    }
    return colour;
}

// Direct and sky light the toon way (everything but emissive and fog). geomN = the interpolated
// vertex normal (for the shadow lookup), occlusion = the material's own occlusion map.
float3 ShadeToon(VSOutput inVert, ToonSurface s, float3 geomN, float3 F0, float occlusion) {
    float3 direct = float3(0.0, 0.0, 0.0);

    // Directional lights. The shadow-casting one (else the first) is the key light: where it
    // doesn't reach, the sky's light counts as shade too and takes the shade tint.
    uint  dirCount = frame.lightParams.w;
    uint  shadowed = frame.lightParams.y;   // index + 1, 0 = none
    uint  keyLight = shadowed > 0 ? shadowed - 1 : 0;
    float keyLit   = 0.0;
    for (uint i = 0; i < dirCount; ++i) {
        float3 L, radiance;
        LightIncoming(lights[i], inVert.worldPos, L, radiance);
        float shadow = 1.0;
        if (i + 1 == shadowed && dot(s.N, L) > 0.0) { shadow = SampleShadow(inVert.worldPos, geomN, L); }
        float lit;
        direct += ToonLight(s, L, radiance, shadow, lit);
        if (i == keyLight) { keyLit = lit; }
    }

    // Point and spot lights near this pixel: the same edge (their distance falloff stays soft).
    ClusterCell cell = ClusterFor(inVert.pos, inVert.worldPos);
    for (uint k = 0; k < cell.count; ++k) {
        float3 L, radiance;
        if (!LightIncoming(lights[clusterLightIndices[cell.offset + k]], inVert.worldPos, L, radiance)) { continue; }
        float lit;
        direct += ToonLight(s, L, radiance, 1.0, lit);
    }

    // The sky's light, flattened towards its average. The shaded side gets toonParams.w of it,
    // tinted; the lit side gets it as it is, on top of the sun.
    bool   fromSky = frame.ambientParams.x > 0.5;
    float3 sky     = fromSky ? SkyIrradiance(s.N) * frame.ambientParams.z : HemisphereAmbient(s.N);
    float3 flat    = fromSky ? max(frame.skySH[0].rgb, float3(0.0, 0.0, 0.0)) * frame.ambientParams.z
                             : 0.5 * (frame.ambientSky.rgb + frame.ambientGround.rgb);
    sky = lerp(sky, flat, frame.toonShade.w);
    float3 ambient = s.diffuseAlbedo * sky * lerp(s.shadeTint * frame.toonParams.w, float3(1.0, 1.0, 1.0), keyLit);

    // Sky reflections, only if asked for (off is the toon look).
    if (frame.toonExtra.z > 0.0) {
        float  NdotV = max(dot(s.N, s.V), 1e-4);
        float3 R     = reflect(-s.V, s.N);
        float3 refl  = fromSky ? envMap.SampleLevel(R, s.roughness * frame.ambientParams.y).rgb * frame.ambientParams.z
                               : HemisphereAmbient(R);
        ambient += refl * EnvBRDFApprox(F0, s.roughness, NdotV) * frame.toonExtra.z;
    }

    float ao = aoTex.SampleLevel(inVert.pos.xy / frame.clusterZ.zw, 0.0).r;
    return direct + ambient * occlusion * ao;
}

// ---- Toon outlines: what the scene writes for ToonOutlinePass (Post.slang's toonOutlineFrag) ----
// Per pixel: x = 1 / view depth (which changes linearly across the screen on any flat surface,
// so the outline pass can tell shapes apart from steep ground), yz = the surface's direction
// (octahedral), w = how strong a line may be here - the material's Outline (toon materials
// only), times how much of the surface shows through the fog, times the breakup pattern.

float LineHash(float3 p) {   // 0..1, from a lattice point
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.x + p.y) * p.z);
}

float LineNoise(float3 p) {   // smooth value noise, 0..1 (mostly 0.2..0.8)
    float3 i = floor(p);
    float3 f = p - i;
    float3 u = f * f * (3.0 - 2.0 * f);
    float x00 = lerp(LineHash(i),                         LineHash(i + float3(1.0, 0.0, 0.0)), u.x);
    float x10 = lerp(LineHash(i + float3(0.0, 1.0, 0.0)), LineHash(i + float3(1.0, 1.0, 0.0)), u.x);
    float x01 = lerp(LineHash(i + float3(0.0, 0.0, 1.0)), LineHash(i + float3(1.0, 0.0, 1.0)), u.x);
    float x11 = lerp(LineHash(i + float3(0.0, 1.0, 1.0)), LineHash(i + float3(1.0, 1.0, 1.0)), u.x);
    return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
}

float4 LineData(VSOutput inVert, float3 N, bool toon, float fogVisibility) {
    float viewDepth = max(-mul(frame.view, float4(inVert.worldPos, 1.0)).z, 1e-4);
    float strength  = 0.0;
    if (toon && frame.toonLines.x > 0.5) {
        strength = saturate(pc.outline) * fogVisibility;
        float breakup = frame.toonLines.y;
        if (breakup > 0.0 && strength > 0.0) {
            float n = smoothstep(0.2, 0.8, LineNoise(inVert.linePos * frame.toonLines.z));   // spread out to about 0..1
            strength *= smoothstep(breakup - 0.08, breakup + 0.08, n);
        }
    }
    return float4(1.0 / viewDepth, OctEncode(N), strength);
}

struct SceneOutput {
    float4 colour : SV_Target0;   // linear HDR radiance
    float4 lines  : SV_Target1;   // line data (above)
};

[shader("fragment")]
SceneOutput fragMain(VSOutput inVert, bool frontFacing : SV_IsFrontFace) {

    float4 albedoSample = albedoTex.Sample(inVert.uv);
    if (kCutout && albedoSample.a * pc.albedoTint.a < pc.alphaCutoff) { discard; }
    float3 albedo = albedoSample.rgb * pc.albedoTint.rgb * inVert.color;

    float3 mrSample  = metallicRoughnessTex.Sample(inVert.uv).rgb;
    float  roughness = saturate(mrSample.g * pc.roughnessFactor);
    float  metallic  = saturate(mrSample.b * pc.metallicFactor);
    // A perfectly-zero roughness makes D()'s denominator degenerate (a true mirror needs a
    // different specular path entirely); clamp to a small floor instead of special-casing it.
    roughness = max(roughness, 0.045);

    float  occlusion = occlusionTex.Sample(inVert.uv).r;
    float3 emissive  = emissiveTex.Sample(inVert.uv).rgb * pc.emissiveFactor * inVert.instanceParams.x;

    // A double-sided material's back side is lit as a front of its own. (Single-sided materials
    // never get here with a back face: they are culled.)
    float3 N = normalize(inVert.normalWS) * (frontFacing ? 1.0 : -1.0);

    // Re-orthogonalize against N (interpolation across a triangle can drift the incoming tangent
    // slightly off perpendicular to the also-interpolated N) - but guard the result's length
    // first. A tangent that arrives (near-)PARALLEL to N - a genuinely malformed/degenerate
    // TANGENT attribute, or an un-normalized interpolation edge case - collapses this subtraction
    // to (near-)zero, and normalizing a near-zero vector produces NaN/Inf that poisons every
    // value downstream: IEEE754 gives NaN * 0 = NaN, not 0, so even a neutral (0,0,1) normal-map
    // sample (whose x/y components should otherwise zero out T and B entirely) does NOT save you
    // from a NaN tangent. Fall back to an arbitrary vector orthogonal to N instead of ever
    // normalizing something that close to zero.
    float3 Traw = inVert.tangentWS - N * dot(N, inVert.tangentWS);
    float3 T;
    if (dot(Traw, Traw) > 1e-8) {
        T = normalize(Traw);
    } else {
        float3 seed = (abs(N.y) < 0.999) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        T = normalize(cross(seed, N));
    }
    float3 B = cross(N, T) * inVert.tangentSign;

    float3 normalSample = normalTex.Sample(inVert.uv).rgb * 2.0 - 1.0;   // unpack [0,1] -> [-1,1]
    float3 Nmapped = normalize(T * normalSample.x + B * normalSample.y + N * normalSample.z);

    float3 V  = normalize(frame.cameraPosWS.xyz - inVert.worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // Toon or realistic: the material's choice, or the scene's (toonParams.x: 0 = realistic,
    // 1 = toon, 2 = toon switched off everywhere in the Rendering menu).
    uint shading = pc.flags & 3u;
    bool toon = frame.toonParams.x < 1.5
             && (shading == kShadingToon || (shading == kShadingScene && frame.toonParams.x > 0.5));

    float3 colour;
    if (toon) {
        ToonSurface s;
        s.diffuseAlbedo = albedo * lerp(1.0, 0.75, metallic);
        s.albedo        = albedo;
        s.metallic      = metallic;
        s.roughness     = roughness;
        s.N             = Nmapped;
        s.V             = V;
        bool ownEdge    = (pc.flags & kFlagOwnEdge) != 0;
        s.threshold     = ownEdge ? pc.toonEdge.x : frame.toonParams.y;
        s.softness      = ownEdge ? pc.toonEdge.y : frame.toonParams.z;
        s.shadeTint     = (pc.flags & kFlagOwnShade) != 0 ? pc.toonShade.rgb : frame.toonShade.rgb;
        s.litTint       = pc.toonLit.rgb;
        colour = ShadeToon(inVert, s, N, F0, occlusion) + emissive;
    } else {

    // Direct lighting: every light submitted this frame. (The Renderer always supplies at least
    // one - a fallback directional "sun" - when the scene has no Light entities.)
    float3 direct = float3(0.0, 0.0, 0.0);
    // Directional lights reach everything, so they are looped over directly; the Renderer puts
    // them first in the buffer.
    uint   dirCount = frame.lightParams.w;
    uint   shadowed = frame.lightParams.y;   // index + 1, 0 = none
    for (uint i = 0; i < dirCount; ++i) {
        float3 c = EvaluateLight(lights[i], inVert.worldPos, Nmapped, V, albedo, metallic, roughness, F0);
        if (i + 1 == shadowed && any(c > 0.0)) {
            c *= SampleShadow(inVert.worldPos, N, -lights[i].directionType.xyz);
        }
        direct += c;
    }

    // Point and spot lights: only the ones listed for this pixel's cluster.
    {
        ClusterCell cell = ClusterFor(inVert.pos, inVert.worldPos);
        for (uint k = 0; k < cell.count; ++k) {
            uint li = clusterLightIndices[cell.offset + k];
            direct += EvaluateLight(lights[li], inVert.worldPos, Nmapped, V, albedo, metallic, roughness, F0);
        }
    }

    // Ambient: diffuse from the hemisphere around the normal, specular from the hemisphere around
    // the reflection vector, split by a roughness-aware Fresnel. Replaces the old flat 0.25 term;
    // this is the slot real image-based lighting (irradiance + prefiltered skybox) will fill later.
    float  NdotV  = max(dot(Nmapped, V), 1e-4);
    float3 Fa     = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kdA    = (float3(1.0, 1.0, 1.0) - Fa) * (1.0 - metallic);
    float3 R      = reflect(-V, Nmapped);
    float3 ambient;
    if (frame.ambientParams.x > 0.5) {
        // Image-based lighting: diffuse light from the whole sky, plus a reflection of the sky
        // that is blurrier the rougher the surface is.
        float3 skyDiffuse  = SkyIrradiance(Nmapped);
        float3 skySpecular = envMap.SampleLevel(R, roughness * frame.ambientParams.y).rgb;
        // A very rough surface reflects such a wide spread of sky that the result is as smooth
        // as the diffuse light. Past roughness 0.6 the reflection fades over to that smooth
        // version, so no trace of the environment map's texels can show on rough objects.
        skySpecular = lerp(skySpecular, SkyIrradiance(R), smoothstep(0.6, 1.0, roughness));
        ambient = (kdA * albedo * skyDiffuse + skySpecular * EnvBRDFApprox(F0, roughness, NdotV)) * frame.ambientParams.z;
    } else {
        ambient = kdA * albedo * HemisphereAmbient(Nmapped) + Fa * HemisphereAmbient(R);
    }
    // Ambient light is blocked in creases: by the material's own occlusion map, and by the
    // screen-space ambient occlusion computed for this view. Direct light is not affected.
    float ao = aoTex.SampleLevel(inVert.pos.xy / frame.clusterZ.zw, 0.0).r;
    ambient *= occlusion * ao;

    // Linear HDR radiance. Exposure, bloom and tonemapping happen once for the whole image in
    // Post.slang's tonemap pass, after everything (meshes, sky, particles) has been drawn.
    colour = direct + ambient + emissive;
    }   // PBR

    // Fog between the camera and this point: what it hides, and the light it adds (lit by the sun
    // and nearby lights when the view has a fog volume - see Common.slang).
    float4 fog = FogForPoint(inVert.worldPos, inVert.pos.xy / frame.clusterZ.zw);
    colour = colour * fog.a + fog.rgb;
    if (frame.time.z > 0.5) { colour *= CascadeDebugTint(inVert.worldPos); }

    SceneOutput output;
    output.colour = float4(colour, 1.0);
    output.lines  = LineData(inVert, N, toon, fog.a);   // the geometric direction: normal maps add no lines
    return output;
}
