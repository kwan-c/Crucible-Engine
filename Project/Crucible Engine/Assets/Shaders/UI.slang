// Crucible Engine screen-space UI (UIPass): the game's HUD and menus, drawn over a view's finished,
// tonemapped picture, so exposure, bloom, fog and grading never touch it. Vertices are in canvas
// pixels (origin top-left, +y down) and colours are sRGB - see Graphics/UITypes.h.
//
// The target is an sRGB image, so this shader works in linear values: vertex colours are converted
// here, textures created as sRGB are converted when sampled, and the hardware converts the result
// back when it is written. The per-pixel text shading is shared with world-space text: see
// TextShading.slang.

#include "TextShading.slang"

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
    TextShading s;
    s.mode           = pc.mode;
    s.pixelRange     = pc.pixelRange;
    s.outlineColour  = pc.outlineColour;
    s.glowColour     = pc.glowColour;
    s.outlineWidth   = pc.outlineWidth;
    s.glowWidth      = pc.glowWidth;
    s.glowSoftness   = pc.glowSoftness;
    s.dilate         = pc.dilate;
    s.shadowSoftness = pc.shadowSoftness;
    s.flags          = pc.flags;
    return ShadeText(uiTexture, input.uv, input.colour, s);
}
