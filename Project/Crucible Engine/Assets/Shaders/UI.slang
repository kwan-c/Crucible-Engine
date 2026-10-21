// Crucible Engine screen-space UI (UIPass): the game's HUD and menus, drawn over a view's finished,
// tonemapped picture, so exposure, bloom, fog and grading never touch it. Vertices are in canvas
// pixels (origin top-left, +y down) and colours are sRGB - see Graphics/UITypes.h.
//
// The target is an sRGB image, so this shader works in linear values: vertex colours are converted
// here, textures created as sRGB are converted when sampled, and the hardware converts the result
// back when it is written.

// Per draw command. Must match Crucible::UIPush (UIPass.cpp), 64 bytes.
struct UIPush {
    float4 transform;       // xy = scale, zw = offset: canvas pixels -> target pixels
    float2 invTargetSize;   // 1 / the target's size in pixels
    uint   mode;            // UIDrawMode: 0 image, 1 signed distance (alpha), 2 multi-channel, 3 multi-channel + true distance in alpha
    float  pixelRange;      // distance-field atlases: the field's range in atlas texels (0 = unknown)
    // Text effects (Crucible::UITextEffects). Distances are in field units: 0 -> 1 across the
    // pixel range, edge at 0.5. Colours are sRGB RGBA8, red in the lowest byte.
    uint   outlineColour;
    uint   glowColour;
    float  outlineWidth;
    float  glowWidth;
    float  glowSoftness;
    float  dilate;          // moves the fill's edge outward: synthetic bold
    float  shadowSoftness;
    uint   flags;           // bit 0: shadow pass
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

// A packed sRGB RGBA8 colour -> linear rgb, alpha as stored.
float4 UnpackColour(uint c) {
    float4 v = float4(float(c & 0xFFu), float((c >> 8) & 0xFFu), float((c >> 16) & 0xFFu), float(c >> 24)) / 255.0;
    return float4(SrgbToLinear(v.rgb), v.a);
}

// Premultiplied "a over b".
float4 Over(float4 a, float4 b) {
    return a + b * (1.0 - a.a);
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
    // Sampled, and the screen-space derivatives taken, before any branch: implicit mip selection
    // and fwidth need the whole pixel quad.
    float4 texel   = uiTexture.Sample(input.uv);
    float2 uvWidth = fwidth(input.uv);

    if (pc.mode == 0) {
        return texel * input.colour;
    }

    // Distance-field text. `fill` is the letter's own distance (the median keeps MSDF corners
    // sharp); `field` is what the effects use - the true distance in an MTSDF's alpha, which stays
    // smooth further from the edge. The edge is at 0.5.
    float fill  = pc.mode == 1 ? texel.a : Median(texel.r, texel.g, texel.b);
    float field = pc.mode == 3 ? texel.a : fill;

    // Screen pixels per field unit, so every edge below is about one pixel soft at any size.
    float range;
    if (pc.pixelRange > 0.0) {
        // msdf-atlas-gen's formula: the pixel range measured in screen pixels right now.
        uint w, h;
        uiTexture.GetDimensions(w, h);
        float2 unitRange     = pc.pixelRange / float2(w, h);
        float2 screenTexSize = 1.0 / max(uvWidth, float2(1.0e-6));
        range = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    } else {
        range = 1.0 / max(fwidth(fill), 1.0e-5);   // no range given: from how fast the distance changes
    }

    float fillEdge  = 0.5 - pc.dilate;              // bold moves the edge out
    float outerEdge = fillEdge - pc.outlineWidth;   // the outline's outside edge (= fillEdge without one)

    // ---- Shadow pass: the whole shape (fill + outline) flat in the vertex colour, maybe blurred.
    if ((pc.flags & 1u) != 0u) {
        float d = pc.outlineWidth > 0.0 ? field : fill;
        float a = pc.shadowSoftness > 0.0
            ? smoothstep(outerEdge - pc.shadowSoftness, outerEdge + pc.shadowSoftness, d)
            : saturate(range * (d - outerEdge) + 0.5);
        return float4(input.colour.rgb, input.colour.a * a);
    }

    // ---- Glow, outline, fill: composited back to front in premultiplied alpha, then returned as
    // straight alpha (what the pipeline's blend state expects).
    float  fillA  = saturate(range * (fill - fillEdge) + 0.5) * input.colour.a;
    float4 result = float4(input.colour.rgb * fillA, fillA);

    float4 outline = UnpackColour(pc.outlineColour);
    if (outline.a > 0.0 && pc.outlineWidth > 0.0) {
        float a = saturate(range * (field - outerEdge) + 0.5) * outline.a * input.colour.a;
        result = Over(result, float4(outline.rgb * a, a));
    }

    float4 glow = UnpackColour(pc.glowColour);
    if (glow.a > 0.0 && pc.glowWidth > 0.0) {
        // 1 at the outline (or fill) edge, falling to 0 glowWidth further out; softness bends the
        // fall-off from even (0) to quick (1).
        float t = saturate((field - (outerEdge - pc.glowWidth)) / pc.glowWidth);
        float a = pow(t, 1.0 + 3.0 * pc.glowSoftness) * glow.a * input.colour.a;
        result = Over(result, float4(glow.rgb * a, a));
    }

    if (result.a <= 0.0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return float4(result.rgb / result.a, result.a);
}
