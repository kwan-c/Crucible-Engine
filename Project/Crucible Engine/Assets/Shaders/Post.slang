// Crucible Engine post-processing, run once per view after the scene has been drawn into a
// floating-point (HDR) image:
//   1. bloomDownFrag: shrink the bright parts of the image into a chain of ever smaller images;
//   2. bloomUpFrag:   add them back up the chain, each blurred a little more - the glow;
//   3. tonemapFrag:   scene + glow, times exposure, squeezed into 0..1 by the ACES curve, written
//                     to the final (sRGB) image - with jagged edges smoothed (FXAA) on the way.
// All three draw one fullscreen triangle (postVert) and read their input from set 0.

struct PostPush {
    float4 a;   // down/up: xy = size of one source texel; down: z = threshold (< 0 = none), w = knee
                // tonemap: x = exposure, y = bloom strength, z = 1 to anti-alias (FXAA)
    float4 b;   // down: x = exposure (so the threshold is judged on what will be seen)
                // tonemap: xy = size of one texel of the target
};

[[vk::binding(0, 0)]] Sampler2D sourceTex;   // bloom passes: the image to filter; tonemap: the HDR scene
[[vk::binding(1, 0)]] Sampler2D bloomTex;    // tonemap only: the finished glow
[[vk::push_constant]] ConstantBuffer<PostPush> pc;

struct PostVSOut {
    float4 pos : SV_Position;
    float2 uv;
};

// One oversized triangle covering the whole target (no vertex buffer).
[shader("vertex")]
PostVSOut postVert(uint vertexID : SV_VulkanVertexID) {
    PostVSOut o;
    float2 p = float2((vertexID << 1) & 2, vertexID & 2);
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = p;
    return o;
}

// Downsample to half size ("dual filter": centre + four diagonal taps). On the first step it also
// keeps only what is brighter than the threshold, with a soft knee so the cut-off isn't a hard edge.
[shader("fragment")]
float4 bloomDownFrag(PostVSOut input) : SV_Target {
    float2 t = pc.a.xy;
    float3 c = sourceTex.Sample(input.uv).rgb * 4.0;
    c += sourceTex.Sample(input.uv + float2(-t.x, -t.y)).rgb;
    c += sourceTex.Sample(input.uv + float2( t.x, -t.y)).rgb;
    c += sourceTex.Sample(input.uv + float2(-t.x,  t.y)).rgb;
    c += sourceTex.Sample(input.uv + float2( t.x,  t.y)).rgb;
    c *= 0.125;

    if (pc.a.z >= 0.0) {
        c = min(c * pc.b.x, float3(64.0, 64.0, 64.0));   // exposed; clamp single-pixel fireflies
        float brightness = max(c.r, max(c.g, c.b));
        float knee = max(pc.a.w, 1e-4);
        float soft = clamp(brightness - pc.a.z + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee);
        float keep = max(soft, brightness - pc.a.z) / max(brightness, 1e-4);
        c *= keep;
    }
    return float4(c, 1.0);
}

// Upsample to double size (eight taps in a ring); the pipeline adds the result onto the target.
[shader("fragment")]
float4 bloomUpFrag(PostVSOut input) : SV_Target {
    float2 t = pc.a.xy;
    float3 c = float3(0.0, 0.0, 0.0);
    c += sourceTex.Sample(input.uv + float2(-2.0 * t.x, 0.0)).rgb;
    c += sourceTex.Sample(input.uv + float2( 2.0 * t.x, 0.0)).rgb;
    c += sourceTex.Sample(input.uv + float2(0.0, -2.0 * t.y)).rgb;
    c += sourceTex.Sample(input.uv + float2(0.0,  2.0 * t.y)).rgb;
    c += sourceTex.Sample(input.uv + float2(-t.x,  t.y)).rgb * 2.0;
    c += sourceTex.Sample(input.uv + float2( t.x,  t.y)).rgb * 2.0;
    c += sourceTex.Sample(input.uv + float2(-t.x, -t.y)).rgb * 2.0;
    c += sourceTex.Sample(input.uv + float2( t.x, -t.y)).rgb * 2.0;
    return float4(c / 12.0, 1.0);
}

// Krzysztof Narkowicz's fitted ACES filmic curve: maps unbounded HDR radiance into [0,1] with a
// gentle shoulder instead of hard-clipping at 1.0.
float3 TonemapACES(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// One finished (tonemapped) pixel. The glow was thresholded on exposed values, so only the scene
// is multiplied by exposure. SampleLevel: this is called inside branches, where the automatic
// mip selection of Sample is not allowed (and the image has a single level anyway).
float3 Finished(float2 uv, float3 glow) {
    return TonemapACES(sourceTex.SampleLevel(uv, 0.0).rgb * pc.a.x + glow);
}

// Brightness as the eye judges it (the square root approximates the display's gamma curve).
float Luma(float3 c) {
    return sqrt(dot(c, float3(0.299, 0.587, 0.114)));
}

float LumaAt(float2 uv, float3 glow) {
    return Luma(Finished(uv, glow));
}

// How far each step of the search along an edge moves, in pixels (bigger steps further out).
static const int   kFxaaSteps = 10;
static const float kFxaaStep[10] = { 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0 };

// Tonemap, with FXAA (fast approximate anti-aliasing, the "quality" version of FXAA 3.11) folded
// in. Where the brightness of the finished image changes sharply between neighbouring pixels - a
// jagged edge - it works out whether the edge runs across or up the screen, walks along it both
// ways to find where this stair-step ends, and from where the pixel sits on the step shifts the
// lookup by a fraction of a pixel across the edge. The hardware's blend between two pixels then
// turns the step into a ramp. Flat areas return early and cost five samples.
// The glow is smooth by nature, so one sample of it serves every tap.
// The target is *_SRGB, so the hardware does the linear -> sRGB encode after this.
[shader("fragment")]
float4 tonemapFrag(PostVSOut input) : SV_Target {
    float2 uv   = input.uv;
    float3 glow = bloomTex.Sample(uv).rgb * pc.a.y;
    float3 rgbM = Finished(uv, glow);
    if (pc.a.z < 0.5) { return float4(rgbM, 1.0); }

    float2 t = pc.b.xy;
    float lumaM = Luma(rgbM);
    float lumaD = LumaAt(uv + float2(0.0, -t.y), glow);
    float lumaU = LumaAt(uv + float2(0.0,  t.y), glow);
    float lumaL = LumaAt(uv + float2(-t.x, 0.0), glow);
    float lumaR = LumaAt(uv + float2( t.x, 0.0), glow);

    float lumaMin = min(lumaM, min(min(lumaD, lumaU), min(lumaL, lumaR)));
    float lumaMax = max(lumaM, max(max(lumaD, lumaU), max(lumaL, lumaR)));
    float range   = lumaMax - lumaMin;

    // Not an edge worth touching (also keeps fine texture detail from being blurred).
    if (range < max(0.0312, lumaMax * 0.125)) { return float4(rgbM, 1.0); }

    float lumaDL = LumaAt(uv + float2(-t.x, -t.y), glow);
    float lumaUR = LumaAt(uv + float2( t.x,  t.y), glow);
    float lumaUL = LumaAt(uv + float2(-t.x,  t.y), glow);
    float lumaDR = LumaAt(uv + float2( t.x, -t.y), glow);

    float lumaDU = lumaD + lumaU;
    float lumaLR = lumaL + lumaR;
    float lumaLeftCorners  = lumaDL + lumaUL;
    float lumaDownCorners  = lumaDL + lumaDR;
    float lumaRightCorners = lumaDR + lumaUR;
    float lumaUpCorners    = lumaUR + lumaUL;

    // Does the edge run across the screen (horizontal) or up it?
    float edgeH = abs(-2.0 * lumaL + lumaLeftCorners) + abs(-2.0 * lumaM + lumaDU) * 2.0 + abs(-2.0 * lumaR + lumaRightCorners);
    float edgeV = abs(-2.0 * lumaU + lumaUpCorners)   + abs(-2.0 * lumaM + lumaLR) * 2.0 + abs(-2.0 * lumaD + lumaDownCorners);
    bool  isHorizontal = edgeH >= edgeV;

    // Which side of this pixel the edge is on: the neighbour with the bigger brightness jump.
    float luma1 = isHorizontal ? lumaD : lumaL;
    float luma2 = isHorizontal ? lumaU : lumaR;
    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;
    bool  is1Steepest    = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    float stepLength = isHorizontal ? t.y : t.x;
    float lumaLocalAverage;
    if (is1Steepest) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaM);
    }

    // Start half a pixel across, exactly on the edge, and walk along it in both directions until
    // the brightness stops looking like this edge (= the end of the stair-step).
    float2 edgeUv = uv;
    if (isHorizontal) { edgeUv.y += stepLength * 0.5; } else { edgeUv.x += stepLength * 0.5; }
    float2 along = isHorizontal ? float2(t.x, 0.0) : float2(0.0, t.y);

    float2 uv1 = edgeUv - along;
    float2 uv2 = edgeUv + along;
    float lumaEnd1 = LumaAt(uv1, glow) - lumaLocalAverage;
    float lumaEnd2 = LumaAt(uv2, glow) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;

    for (int i = 0; i < kFxaaSteps && !(reached1 && reached2); ++i) {
        if (!reached1) {
            uv1 -= along * kFxaaStep[i];
            lumaEnd1 = LumaAt(uv1, glow) - lumaLocalAverage;
            reached1 = abs(lumaEnd1) >= gradientScaled;
        }
        if (!reached2) {
            uv2 += along * kFxaaStep[i];
            lumaEnd2 = LumaAt(uv2, glow) - lumaLocalAverage;
            reached2 = abs(lumaEnd2) >= gradientScaled;
        }
    }

    // Where along the step this pixel sits decides how far across the edge to shift.
    float distance1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float distance2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
    bool  isDirection1  = distance1 < distance2;
    float distanceFinal = min(distance1, distance2);
    float edgeLength    = distance1 + distance2;
    float pixelOffset   = -distanceFinal / edgeLength + 0.5;

    // Only shift if the nearer end confirms which side of the edge this pixel is on.
    bool isLumaCentreSmaller = lumaM < lumaLocalAverage;
    bool correctVariation    = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCentreSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Single-pixel details (thin lines, specks) have no long edge to follow: soften those by how
    // much the pixel differs from its neighbourhood instead.
    float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDU + lumaLR) + lumaLeftCorners + lumaRightCorners);
    float subPixel1   = saturate(abs(lumaAverage - lumaM) / range);
    float subPixel2   = (-2.0 * subPixel1 + 3.0) * subPixel1 * subPixel1;
    finalOffset = max(finalOffset, subPixel2 * subPixel2 * 0.75);

    float2 finalUv = uv;
    if (isHorizontal) { finalUv.y += finalOffset * stepLength; } else { finalUv.x += finalOffset * stepLength; }
    return float4(Finished(finalUv, glow), 1.0);
}
