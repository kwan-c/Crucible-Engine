// Crucible Engine's default (and only) material shader: full metallic-roughness PBR, lit by every
// light the Renderer was given this frame (set 0, binding 1 - see Renderer::SubmitLight and the ECS
// Light component) plus a hemisphere ambient term, then exposure + ACES tonemapping. Every material
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

// Per-frame data (descriptor set 0, binding 0). Must match Crucible::FrameUniforms (RenderTypes.h)
// AND Skybox.slang's copy exactly.
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
};

// One instance (set 0, binding 2). Must match Crucible::InstanceData (RenderTypes.h).
struct InstanceData {
    float4x4 model;
    float4   params;   // x = multiplier on the material's emissive, yzw = spare (MaterialParams component)
};

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

// A material's scalar knobs - must match Crucible::MaterialFactors field-for-field (same packing
// rules apply on both sides; see RenderTypes.h's comment on why this exact field order matters).
struct MaterialFactors {
    float4 albedoTint;
    float3 emissiveFactor;
    float  metallicFactor;
    float  roughnessFactor;
};

// Per-draw data. Must match Crucible::PushConstants exactly (RenderSystem.cpp's DrawRenderQueue).
// The model matrix moved out to the instance buffer (one draw now covers many objects); only the
// material's factors - constant across a batch, since batches never cross materials - remain.
struct PushConstants {
    MaterialFactors  factors;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms> frame;

// This frame's lights (Renderer::UpdateLightBuffer). Only the first frame.lightParams.x are valid.
[[vk::binding(1, 0)]]
StructuredBuffer<GpuLight> lights;

// Every object drawn this frame, by every view and pass (Renderer::UploadInstances). A draw's
// instances are contiguous; drawIndexed's firstInstance points at the first one, and
// SV_VulkanInstanceID (gl_InstanceIndex) already includes that offset.
[[vk::binding(2, 0)]]
StructuredBuffer<InstanceData> instances;

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

    return output;
}

static const float kPi = 3.14159265358979323846;

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

// Smoothly windowed inverse-square falloff (the UE4/Frostbite form): physically-based 1/d^2 near
// the light, forced to exactly zero at `range` so a light's influence has a hard bound.
float DistanceAttenuation(float distSq, float range) {
    float ratio  = distSq / max(range * range, 1e-4);           // (d/r)^2
    float window = saturate(1.0 - ratio * ratio);                // 1 - (d/r)^4
    return (window * window) / max(distSq, 1e-4);
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

// Contribution of one GpuLight at this surface point.
float3 EvaluateLight(GpuLight light, float3 worldPos, float3 N, float3 V,
                     float3 albedo, float metallic, float roughness, float3 F0) {
    uint   type     = (uint)(light.directionType.w + 0.5);
    float3 radiance = light.colourIntensity.rgb * light.colourIntensity.w;
    float3 L;

    if (type == kLightDirectional) {
        L = -light.directionType.xyz;
    } else {
        float3 toLight = light.positionRange.xyz - worldPos;
        float  distSq  = dot(toLight, toLight);
        if (distSq >= light.positionRange.w * light.positionRange.w) { return float3(0.0, 0.0, 0.0); }
        L = toLight * rsqrt(max(distSq, 1e-8));
        radiance *= DistanceAttenuation(distSq, light.positionRange.w);

        if (type == kLightSpot) {
            // Angle between the light's axis and the direction from the light to this point.
            float cosAngle = dot(-L, light.directionType.xyz);
            radiance *= smoothstep(light.spotParams.y, light.spotParams.x, cosAngle);
        }
    }

    return EvaluateBRDF(N, V, L, radiance, albedo, metallic, roughness, F0);
}

// Hemisphere ambient: blend ground -> sky by how much `dir` points up.
float3 HemisphereAmbient(float3 dir) {
    float t = saturate(dir.y * 0.5 + 0.5);
    return lerp(frame.ambientGround.rgb, frame.ambientSky.rgb, t);
}

// Roughness-aware Fresnel for the ambient specular term (Lagarde's approximation): rough surfaces
// shouldn't get a bright grazing-angle rim from an environment they can't sharply reflect.
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float3 maxRefl = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (maxRefl - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

[shader("fragment")]
float4 fragMain(VSOutput inVert) : SV_Target {

    float4 albedoSample = albedoTex.Sample(inVert.uv);
    float3 albedo = albedoSample.rgb * pc.factors.albedoTint.rgb * inVert.color;

    float3 mrSample  = metallicRoughnessTex.Sample(inVert.uv).rgb;
    float  roughness = saturate(mrSample.g * pc.factors.roughnessFactor);
    float  metallic  = saturate(mrSample.b * pc.factors.metallicFactor);
    // A perfectly-zero roughness makes D()'s denominator degenerate (a true mirror needs a
    // different specular path entirely); clamp to a small floor instead of special-casing it.
    roughness = max(roughness, 0.045);

    float  occlusion = occlusionTex.Sample(inVert.uv).r;
    float3 emissive  = emissiveTex.Sample(inVert.uv).rgb * pc.factors.emissiveFactor * inVert.instanceParams.x;

    float3 N = normalize(inVert.normalWS);

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
        uint3 dims      = frame.clusterDims.xyz;
        float viewDepth = -mul(frame.view, float4(inVert.worldPos, 1.0)).z;
        // SV_Position.xy is the pixel position, y down; the cluster grid's y runs up.
        uint cx = min(uint(inVert.pos.x / frame.clusterZ.z * float(dims.x)), dims.x - 1);
        uint cy = min(uint((1.0 - inVert.pos.y / frame.clusterZ.w) * float(dims.y)), dims.y - 1);
        float slice = log(max(viewDepth, frame.clusterZ.x) / frame.clusterZ.x) * frame.clusterZ.y;
        uint cz = min(uint(max(slice, 0.0)), dims.z - 1);

        ClusterCell cell = clusterGrid[frame.clusterDims.w + cx + cy * dims.x + cz * dims.x * dims.y];
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
    float3 ambient = (kdA * albedo * HemisphereAmbient(Nmapped) + Fa * HemisphereAmbient(R)) * occlusion;

    // Linear HDR radiance. Exposure, bloom and tonemapping happen once for the whole image in
    // Post.slang's tonemap pass, after everything (meshes, sky, particles) has been drawn.
    float3 colour = direct + ambient + emissive;
    if (frame.time.z > 0.5) { colour *= CascadeDebugTint(inVert.worldPos); }
    return float4(colour, 1.0);
}
