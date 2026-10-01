// Crucible Engine selection outline, pass 2 of 2 (pass 1 is OutlineMask.slang): a fullscreen
// triangle over the finished viewport image. Pixels outside the mask but within `thickness`
// pixels of it get the outline colour, alpha-blended over the scene.

// Composite pipeline's own set 0, binding 0: this viewport's mask (Renderer::Viewport::outlineSet).
[[vk::binding(0, 0)]] Texture2D<float> outlineMask;

struct CompositePush {
    float4 color;       // sRGB-authored outline colour, a = opacity
    float  thickness;   // in pixels
};
[[vk::push_constant]] ConstantBuffer<CompositePush> cpc;

struct FullscreenOut {
    float4 pos : SV_Position;
};

// One oversized triangle covering the whole target (no vertex buffer).
[shader("vertex")]
FullscreenOut compositeVert(uint vertexID : SV_VulkanVertexID) {
    FullscreenOut o;
    float2 p = float2((vertexID << 1) & 2, vertexID & 2);
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

static const int kMaxRadius = 3;

[shader("fragment")]
float4 compositeFrag(FullscreenOut input) : SV_Target {
    int2 px = int2(input.pos.xy);
    uint w, h;
    outlineMask.GetDimensions(w, h);
    int2 maxPx = int2(w, h) - 1;

    if (outlineMask.Load(int3(px, 0)) > 0.5) { discard; }   // inside the object: leave it alone

    // Distance (pixels) to the nearest covered pixel within kMaxRadius; anti-aliased edge.
    float r = clamp(cpc.thickness, 0.0, float(kMaxRadius));
    float nearest = 1e9;
    for (int y = -kMaxRadius; y <= kMaxRadius; ++y) {
        for (int x = -kMaxRadius; x <= kMaxRadius; ++x) {
            int2 q = clamp(px + int2(x, y), int2(0, 0), maxPx);
            if (outlineMask.Load(int3(q, 0)) > 0.5) { nearest = min(nearest, length(float2(x, y))); }
        }
    }
    float coverage = saturate(r + 0.5 - nearest);
    if (coverage <= 0.0) { discard; }

    // Render targets are *_SRGB: decode the authored colour so it comes out as written.
    float3 linearColor = pow(saturate(cpc.color.rgb), float3(2.2, 2.2, 2.2));
    return float4(linearColor, cpc.color.a * coverage);
}
