// Crucible Engine post-processing, run once per view after the scene has been drawn into a
// floating-point (HDR) image:
//   1. bloomDownFrag: shrink the bright parts of the image into a chain of ever smaller images;
//   2. bloomUpFrag:   add them back up the chain, each blurred a little more - the glow;
//   3. tonemapFrag:   scene + glow, times exposure, squeezed into 0..1 by the ACES curve, written
//                     to the final (sRGB) image - with jagged edges smoothed (FXAA) on the way.
// All three draw one fullscreen triangle (postVert) and read their input from set 0.
//
// Also here, run BEFORE the scene is drawn: ambient occlusion (aoFrag, aoBlurFrag). A half-size
// depth-only image of the scene is drawn first; aoFrag works out, per pixel, how much of the
// space just above the surface is blocked by nearby geometry; aoBlurFrag smooths the result. The
// scene shader then dims ambient light by it (DefaultShader2.slang's aoTex).

struct PostPush {
    float4 a;   // down/up: xy = size of one source texel; down: z = threshold (< 0 = none), w = knee
                // tonemap: x = exposure, y = bloom strength, z = 1 to anti-alias (FXAA)
    float4 b;   // down: x = exposure (so the threshold is judged on what will be seen)
                // tonemap: xy = size of one texel of the target
                // ao:      a.xy = size of one texel, a.z = radius in metres, a.w = strength;
                //          b = the projection matrix's [0][0], [1][1], [2][2], [3][2]
                // ao blur: a.xy = size of one texel; b as for ao
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

// ---- Ambient occlusion -------------------------------------------------------------------------

// Distance in front of the camera, in metres, from a value of the depth image (0..1, near..far).
float AoLinearDepth(float depth) {
    return pc.b.w / (depth + pc.b.z);
}

// Position in view space (camera at the origin, looking down -Z, +Y up) of the surface seen at uv.
// The scene is drawn with a flipped viewport, so the top row of the image (v = 0) is +Y.
float3 AoViewPos(float2 uv, float linearDepth) {
    return float3((uv.x * 2.0 - 1.0) * linearDepth / pc.b.x, (1.0 - 2.0 * uv.y) * linearDepth / pc.b.y, -linearDepth);
}

// For each pixel: take a handful of points in the half-sphere above the surface and count how
// many are hidden behind other geometry (according to the depth image). Many hidden = a crease,
// a corner, the gap under an object = little ambient light gets in. sourceTex = the depth image.
[shader("fragment")]
float4 aoFrag(PostVSOut input) : SV_Target {
    float depth = sourceTex.SampleLevel(input.uv, 0.0).r;
    if (depth >= 1.0) { return float4(1.0, 1.0, 1.0, 1.0); }   // sky

    float  centre = AoLinearDepth(depth);
    float3 P      = AoViewPos(input.uv, centre);

    // Surface direction from how the position changes across neighbouring pixels.
    float3 N = normalize(cross(ddx(P), ddy(P)));
    // Make it face the camera. "Facing the camera" means pointing back along the line of sight
    // to this pixel (-P), not along the view axis: towards the edges of the view those differ,
    // and a visible surface can have a normal that points slightly away from the view axis.
    if (dot(N, P) > 0.0) { N = -N; }
    float3 T = normalize(cross(N, abs(N.x) < 0.9 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0)));
    float3 B = cross(N, T);

    // A different rotation of the sample pattern per pixel turns banding into fine noise, which
    // the blur pass then removes.
    float noise = frac(52.9829189 * frac(dot(input.pos.xy, float2(0.06711056, 0.00583715))));

    const int kSamples = 12;
    float radius   = pc.a.z;
    float bias     = 0.02 * radius + 0.002 * centre;
    float occluded = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        // Points spiralling out over the half-sphere, more of them close to the surface point.
        float  t     = (float(i) + 0.5) / float(kSamples);
        float  angle = float(i) * 2.399963 + noise * 6.2831853;
        float  r     = sqrt(t);
        float3 dir   = T * (r * cos(angle)) + B * (r * sin(angle)) + N * sqrt(1.0 - t);
        float3 S     = P + dir * (radius * lerp(0.15, 1.0, t * t));

        float sampleDepth = -S.z;
        if (sampleDepth <= 1e-3) { continue; }
        float2 suv = float2(S.x * pc.b.x / sampleDepth * 0.5 + 0.5, 0.5 - S.y * pc.b.y / sampleDepth * 0.5);
        // Off the screen there is no depth to compare with (the sampler would repeat the edge
        // pixel, inventing geometry that changes as the camera turns): count it as open.
        if (any(suv < 0.0) || any(suv > 1.0)) { continue; }
        float  sceneDepth = AoLinearDepth(sourceTex.SampleLevel(suv, 0.0).r);

        // Hidden if the scene is in front of the point - but only count geometry that is nearby,
        // or a far-away wall would darken everything in front of it.
        float nearby = smoothstep(0.0, 1.0, radius / max(abs(centre - sceneDepth), 1e-4));
        occluded += (sceneDepth < sampleDepth - bias ? 1.0 : 0.0) * nearby;
    }

    float ao = saturate(1.0 - pc.a.w * occluded / float(kSamples));
    ao = lerp(ao, 1.0, saturate((centre - 50.0) / 30.0));   // fades out in the distance, where depth is too coarse
    return float4(ao, ao, ao, 1.0);
}

// Smooths aoFrag's noise: the average of a 4x4 block, skipping neighbours at a clearly different
// depth so the darkening doesn't smear across the edges of objects.
// sourceTex = the raw occlusion, bloomTex = the depth image.
[shader("fragment")]
float4 aoBlurFrag(PostVSOut input) : SV_Target {
    float centre = AoLinearDepth(bloomTex.SampleLevel(input.uv, 0.0).r);
    float limit  = 0.05 + 0.03 * centre;

    float sum = 0.0, weight = 0.0;
    for (int y = -2; y < 2; ++y) {
        for (int x = -2; x < 2; ++x) {
            float2 uv = input.uv + (float2(x, y) + 0.5) * pc.a.xy;
            float  d  = AoLinearDepth(bloomTex.SampleLevel(uv, 0.0).r);
            float  w  = abs(d - centre) < limit ? 1.0 : 0.0;
            sum    += sourceTex.SampleLevel(uv, 0.0).r * w;
            weight += w;
        }
    }
    float ao = weight > 0.0 ? sum / weight : sourceTex.SampleLevel(input.uv, 0.0).r;
    return float4(ao, ao, ao, 1.0);
}
