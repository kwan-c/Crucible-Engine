// Text and image shading shared by the screen UI (UI.slang) and world-space text (WorldText.slang):
// one function that turns an atlas sample into a colour, with the distance-field text effects -
// outline, glow, shadow pass and synthetic bold. Nothing here knows about push constants,
// bindings or where the quad is, so both passes give it their own parameters.
// (#include is resolved by slangc relative to this folder; Engine/CompileShaders.cmake treats it
// as a shared include.)
//
// Colours in and out are linear; the result is straight (not premultiplied) alpha.

#ifndef CRUCIBLE_TEXT_SHADING
#define CRUCIBLE_TEXT_SHADING

// UIDrawMode (Graphics/UITypes.h).
static const uint kTextModeImage = 0;   // a plain texture, multiplied by the colour
static const uint kTextModeSdf   = 1;   // single-channel distance field in alpha
static const uint kTextModeMsdf  = 2;   // multi-channel distance field
static const uint kTextModeMtsdf = 3;   // multi-channel + true distance in alpha

// What one draw of text needs to know besides the texture and the vertex colour.
// Mirrors Crucible::UITextEffects plus the draw's mode and pixel range.
struct TextShading {
    uint  mode;             // kTextMode*
    float pixelRange;       // distance-field atlases: the field's range in atlas texels (0 = unknown)
    // Distances are in field units: 0 -> 1 across the pixel range, edge at 0.5. Colours are sRGB
    // RGBA8, red in the lowest byte.
    uint  outlineColour;
    uint  glowColour;
    float outlineWidth;
    float glowWidth;
    float glowSoftness;
    float dilate;           // moves the fill's edge outward: synthetic bold
    float shadowSoftness;
    uint  flags;            // bit 0: shadow pass
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

// The colour of one pixel of a glyph (or image) quad. `colour` is the vertex colour, linear.
// Call it from a fragment shader outside any branch: it samples with implicit mips and takes
// screen-space derivatives, which need the whole pixel quad. Because the softness comes from
// those derivatives, edges stay about one pixel soft at any size, distance or angle - on screen
// or in the world.
float4 ShadeText(Sampler2D atlas, float2 uv, float4 colour, TextShading s) {
    // Sampled, and the screen-space derivatives taken, before any branch.
    float4 texel   = atlas.Sample(uv);
    float2 uvWidth = fwidth(uv);

    if (s.mode == kTextModeImage) {
        return texel * colour;
    }

    // Distance-field text. `fill` is the letter's own distance (the median keeps MSDF corners
    // sharp); `field` is what the effects use - the true distance in an MTSDF's alpha, which stays
    // smooth further from the edge. The edge is at 0.5.
    float fill  = s.mode == kTextModeSdf ? texel.a : Median(texel.r, texel.g, texel.b);
    float field = s.mode == kTextModeMtsdf ? texel.a : fill;

    // Screen pixels per field unit, so every edge below is about one pixel soft at any size.
    float range;
    if (s.pixelRange > 0.0) {
        // msdf-atlas-gen's formula: the pixel range measured in screen pixels right now.
        uint w, h;
        atlas.GetDimensions(w, h);
        float2 unitRange     = s.pixelRange / float2(w, h);
        float2 screenTexSize = 1.0 / max(uvWidth, float2(1.0e-6));
        range = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    } else {
        range = 1.0 / max(fwidth(fill), 1.0e-5);   // no range given: from how fast the distance changes
    }

    float fillEdge  = 0.5 - s.dilate;              // bold moves the edge out
    float outerEdge = fillEdge - s.outlineWidth;   // the outline's outside edge (= fillEdge without one)

    // ---- Shadow pass: the whole shape (fill + outline) flat in the vertex colour, maybe blurred.
    if ((s.flags & 1u) != 0u) {
        float d = s.outlineWidth > 0.0 ? field : fill;
        float a = s.shadowSoftness > 0.0
            ? smoothstep(outerEdge - s.shadowSoftness, outerEdge + s.shadowSoftness, d)
            : saturate(range * (d - outerEdge) + 0.5);
        return float4(colour.rgb, colour.a * a);
    }

    // ---- Glow, outline, fill: composited back to front in premultiplied alpha, then returned as
    // straight alpha (what the pipelines' blend states expect).
    float  fillA  = saturate(range * (fill - fillEdge) + 0.5) * colour.a;
    float4 result = float4(colour.rgb * fillA, fillA);

    float4 outline = UnpackColour(s.outlineColour);
    if (outline.a > 0.0 && s.outlineWidth > 0.0) {
        float a = saturate(range * (field - outerEdge) + 0.5) * outline.a * colour.a;
        result = Over(result, float4(outline.rgb * a, a));
    }

    float4 glow = UnpackColour(s.glowColour);
    if (glow.a > 0.0 && s.glowWidth > 0.0) {
        // 1 at the outline (or fill) edge, falling to 0 glowWidth further out; softness bends the
        // fall-off from even (0) to quick (1).
        float t = saturate((field - (outerEdge - s.glowWidth)) / s.glowWidth);
        float a = pow(t, 1.0 + 3.0 * s.glowSoftness) * glow.a * colour.a;
        result = Over(result, float4(glow.rgb * a, a));
    }

    if (result.a <= 0.0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return float4(result.rgb / result.a, result.a);
}

#endif // CRUCIBLE_TEXT_SHADING
