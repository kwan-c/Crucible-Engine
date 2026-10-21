// Crucible Engine screen-space UI (UIPass): the game's HUD and menus, drawn over a view's finished,
// tonemapped picture, so exposure, bloom, fog and grading never touch it. Vertices are in canvas
// pixels (origin top-left, +y down) and colours are sRGB - see Graphics/UITypes.h.
//
// The target is an sRGB image, so this shader works in linear values: vertex colours are converted
// here, textures created as sRGB are converted when sampled, and the hardware converts the result
// back when it is written.

// Per draw command. Must match Crucible::UIPush (UIPass.cpp), 32 bytes.
struct UIPush {
    float4 transform;       // xy = scale, zw = offset: canvas pixels -> target pixels
    float2 invTargetSize;   // 1 / the target's size in pixels
    uint   mode;            // UIDrawMode: 0 image, 1 signed distance (alpha), 2 multi-channel signed distance
    float  pixelRange;      // distance-field atlases: the field's range in atlas texels (0 = unknown)
};

[[vk::push_constant]] ConstantBuffer<UIPush> pc;
[[vk::binding(0, 0)]] Sampler2D              uiTexture;

// Must match UIVertex (UITypes.h) and the vertex input in UIPass.cpp.
struct VSInput {
    float2 position;   // location 0: canvas pixels
    float2 uv;         // location 1
    float4 colour;     // location 2: RGBA8 UNORM, sRGB
};

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv;
    float4 colour;     // linear rgb, alpha as given
};

// The exact sRGB curve (not a 2.2 power), so a designer's #FF8800 lands on screen as #FF8800.
float3 SrgbToLinear(float3 c) {
    float3 low  = c / 12.92;
    float3 high = pow((c + 0.055) / 1.055, 2.4);
    return lerp(high, low, step(c, float3(0.04045)));
}

float Median(float a, float b, float c) {
    return max(min(a, b), min(max(a, b), c));
}

[shader("vertex")]
VSOutput uiVert(VSInput input) {
    VSOutput output;

    // Canvas pixels -> target pixels -> clip space. The UI pass uses a plain (not flipped) viewport,
    // so -1 is the top edge, matching +y down.
    float2 target = input.position * pc.transform.xy + pc.transform.zw;
    float2 ndc    = target * (2.0 * pc.invTargetSize) - 1.0;

    output.pos    = float4(ndc, 0.0, 1.0);
    output.uv     = input.uv;
    output.colour = float4(SrgbToLinear(input.colour.rgb), input.colour.a);
    return output;
}

[shader("fragment")]
float4 uiFrag(VSOutput input) : SV_Target {
    // Sampled before any branch, so its implicit mip selection sees the whole pixel quad.
    float4 texel = uiTexture.Sample(input.uv);

    if (pc.mode == 0) {
        return texel * input.colour;
    }

    // Distance-field text. The edge is where the stored distance is 0.5.
    float dist = pc.mode == 2 ? Median(texel.r, texel.g, texel.b) : texel.a;
    float coverage;
    if (pc.pixelRange > 0.0) {
        // msdf-atlas-gen's formula: the field's range in SCREEN pixels, from how many screen pixels
        // one atlas texel covers right now (fwidth of the UV). Exact, so the edge stays one pixel
        // soft at any size, scale or angle - and it doesn't depend on the (noisy, at corners)
        // derivative of the median.
        uint w, h;
        uiTexture.GetDimensions(w, h);
        float2 unitRange     = pc.pixelRange / float2(w, h);
        float2 screenTexSize = 1.0 / max(fwidth(input.uv), float2(1.0e-6));
        float  screenRange   = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        coverage = saturate(screenRange * (dist - 0.5) + 0.5);
    } else {
        // No range given: about one pixel of softness from how fast the distance changes.
        float width = max(fwidth(dist), 1.0e-5);
        coverage = saturate((dist - 0.5) / width + 0.5);
    }
    return float4(input.colour.rgb, input.colour.a * coverage);
}
