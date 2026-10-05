// Crucible Engine post-processing, run once per view after the scene has been drawn into a
// floating-point (HDR) image:
//   1. bloomDownFrag: shrink the bright parts of the image into a chain of ever smaller images;
//   2. bloomUpFrag:   add them back up the chain, each blurred a little more - the glow;
//   3. tonemapFrag:   scene + glow, times exposure, squeezed into 0..1 by the ACES curve, written
//                     to the final (sRGB) image.
// All three draw one fullscreen triangle (postVert) and read their input from set 0.

struct PostPush {
    float4 a;   // down/up: xy = size of one source texel; down: z = threshold (< 0 = none), w = knee
                // tonemap: x = exposure, y = bloom strength
    float4 b;   // down: x = exposure (so the threshold is judged on what will be seen)
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

// The glow was thresholded on exposed values, so only the scene is multiplied by exposure here.
// The target is *_SRGB, so the hardware does the linear -> sRGB encode after this.
[shader("fragment")]
float4 tonemapFrag(PostVSOut input) : SV_Target {
    float3 scene = sourceTex.Sample(input.uv).rgb * pc.a.x;
    float3 glow  = bloomTex.Sample(input.uv).rgb * pc.a.y;
    return float4(TonemapACES(scene + glow), 1.0);
}
