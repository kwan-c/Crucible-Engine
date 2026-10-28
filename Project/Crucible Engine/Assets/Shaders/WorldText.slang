// Crucible Engine world-space text (WorldTextPass): WorldText components, drawn inside the scene
// pass after water and particles, into the HDR image - so the text is fogged, bloomed and
// tonemapped like the rest of the world. Unlit, like a particle: its colour is its brightness
// (times WorldText::intensity; above 1 glows through bloom).
//
// Vertices are glyph corners in the text's own plane (world units, +x right, +y up, relative to
// the anchor), so one vertex buffer serves every view; where the plane goes - fixed by the
// Transform, or turned to the camera - is worked out here per view. Each pixel is shaded by
// TextShading.slang, exactly as the screen-space UI shades it.

// Per-frame data (FrameUniforms, `frame`) and the fog functions.
#include "Common.slang"
// ShadeText: distance-field text with outline, glow, shadow and bold.
#include "TextShading.slang"

// Per draw: one run of one text's glyphs. Must match Crucible::WorldTextPush (WorldTextTypes.h), 112 bytes.
struct WorldTextPush {
    float4 anchor;    // xyz = the text's anchor in the world, w = opacity (distance fade)
    float4 right;     // xyz = the text's +x axis in the world (rotation and scale), w = intensity
    float4 up;        // xyz = its +y axis, w = the atlas's pixel range (distance fields)
    uint4  modes;     // x = draw mode (kTextMode*), y = facing (0 Transform, 1 face camera, 2 upright), z = flags (bit 0: shadow pass)
    uint4  colours;   // x = outline colour, y = glow colour (sRGB RGBA8, red in the lowest byte)
    float4 widths;    // x = outline width, y = glow width, z = glow softness, w = dilate (field units)
    float4 extra;     // x = shadow softness (field units)
};

[[vk::push_constant]] ConstantBuffer<WorldTextPush> pc;
[[vk::binding(0, 1)]] Sampler2D                     textAtlas;

// Must match WorldTextVertex (WorldTextTypes.h) and the vertex input in WorldTextPass.cpp.
struct VSInput {
    float2 local;      // location 0: the text's plane, world units from the anchor, +y up
    float2 uv;         // location 1
    float4 colour;     // location 2: RGBA8 UNORM, sRGB
};

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv;
    float4 colour;     // linear rgb, alpha as given
    float4 fog;        // fog in front of this point: rgb = light it adds, a = how much shows (FogForPoint)
};

// The text plane's axes in the world for this view.
void TextAxes(out float3 right, out float3 up) {
    right = pc.right.xyz;
    up    = pc.up.xyz;
    if (pc.modes.y == 0) { return; }   // as the Transform has it

    float sx = length(pc.right.xyz), sy = length(pc.up.xyz);   // the Transform's scale still applies
    if (pc.modes.y == 1) {
        // Square on to the camera: the view matrix's first two rows are its right and up axes.
        right = frame.view[0].xyz * sx;
        up    = frame.view[1].xyz * sy;
        return;
    }

    // Upright: stands straight up and turns around Y to face the camera's position.
    float3 toCamera = frame.cameraPosWS.xyz - pc.anchor.xyz;
    toCamera.y = 0.0;
    if (dot(toCamera, toCamera) < 1.0e-8) {
        toCamera = float3(frame.view[2].x, 0.0, frame.view[2].z);   // straight above: face the way the camera looks
        if (dot(toCamera, toCamera) < 1.0e-8) { toCamera = float3(0.0, 0.0, 1.0); }
    }
    float3 facing = normalize(toCamera);
    up    = float3(0.0, 1.0, 0.0) * sy;
    right = cross(float3(0.0, 1.0, 0.0), facing) * sx;   // facing +Z gives right = +X, as with no turn
}

[shader("vertex")]
VSOutput worldTextVert(VSInput input) {
    VSOutput output;

    float3 right, up;
    TextAxes(right, up);
    float3 world = pc.anchor.xyz + right * input.local.x + up * input.local.y;

    float4 clip = mul(frame.proj, mul(frame.view, float4(world, 1.0)));
    output.pos    = clip;
    output.uv     = input.uv;
    output.colour = float4(SrgbToLinear(input.colour.rgb), input.colour.a);

    float2 ndc = clip.xy / max(clip.w, 1.0e-4);
    output.fog = FogForPoint(world, float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5));
    return output;
}

// The scene pass has a second output, the toon outline pass's line data. Text leaves it as it is:
// zero with zero alpha, which the blend state (ONE, ONE_MINUS_SRC_ALPHA) keeps unchanged.
struct WorldTextOutput {
    float4 colour : SV_Target0;
    float4 lines  : SV_Target1;
};

[shader("fragment")]
WorldTextOutput worldTextFrag(VSOutput input) {
    TextShading s;
    s.mode           = pc.modes.x;
    s.pixelRange     = pc.up.w;
    s.outlineColour  = pc.colours.x;
    s.glowColour     = pc.colours.y;
    s.outlineWidth   = pc.widths.x;
    s.glowWidth      = pc.widths.y;
    s.glowSoftness   = pc.widths.z;
    s.dilate         = pc.widths.w;
    s.shadowSoftness = pc.extra.x;
    s.flags          = pc.modes.z;
    float4 text = ShadeText(textAtlas, input.uv, input.colour, s);   // linear, straight alpha

    // Lit by nothing, brightened by intensity, then covered by the fog in front of it like a
    // surface. Premultiplied out: the pipeline blends (ONE, ONE_MINUS_SRC_ALPHA).
    float  a   = text.a * pc.anchor.w;
    float3 rgb = text.rgb * pc.right.w * input.fog.a + input.fog.rgb;
    WorldTextOutput output;
    output.colour = float4(rgb * a, a);
    output.lines  = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}
