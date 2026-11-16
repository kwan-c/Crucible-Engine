// Functions for shader graph nodes that are too long to be one line of the node table
// (Editor/src/ShaderGraph/ShaderGraphNodes.cpp). Every shader a graph generates includes this
// file after Surface.slang; nothing else does. Names start with "Graph" so they cannot meet a
// name in the engine's mesh shader.
//
// (A .slang that CompileShaders.cmake doesn't list is a shared include: editing it recompiles the
// shaders that include it.)

#ifndef CRUCIBLE_GRAPH_NODES_SLANG
#define CRUCIBLE_GRAPH_NODES_SLANG

// A number in 0..1 that looks random but is always the same for the same p.
float GraphHash(float2 p) {
    float3 q = frac(float3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}

// Noise node: soft blotches in 0..1, one per unit of p (value noise, smoothed between the
// corners of each unit square).
float GraphNoise(float2 p) {
    float2 cell = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = GraphHash(cell);
    float b = GraphHash(cell + float2(1.0, 0.0));
    float c = GraphHash(cell + float2(0.0, 1.0));
    float d = GraphHash(cell + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Fresnel node: 0 where the surface faces the camera, 1 where it turns away at its edges.
float GraphFresnel(float3 normal, float3 viewDir, float power) {
    float facing = saturate(dot(normalize(normal), viewDir));
    return pow(1.0 - facing, max(power, 0.0001));
}

// NormalMap node: a normal map's x and y (0..1) as a tangent-space normal. z is rebuilt from x
// and y, as the built-in surface does (BC5 normal maps store only those two). Strength 0 gives
// the mesh's own normal, 1 the map's, above 1 exaggerates it.
float3 GraphUnpackNormal(float2 xy, float strength) {
    float2 n = xy * 2.0 - 1.0;
    float z = sqrt(saturate(1.0 - dot(n, n)));
    return normalize(float3(n * strength, max(z, 0.0001)));
}

// Two numbers in 0..1 that look random but are always the same for the same p.
float2 GraphHash2(float2 p) {
    float3 q = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return frac((q.xx + q.yz) * q.zy);
}

// GradientNoise node: Perlin's noise, brought into 0..1. Smoother than GraphNoise: it blends
// slopes at the corners of each unit square, not values.
float GraphGradientNoise(float2 p) {
    float2 cell = floor(p);
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(GraphHash2(cell) * 2.0 - 1.0, f);
    float b = dot(GraphHash2(cell + float2(1.0, 0.0)) * 2.0 - 1.0, f - float2(1.0, 0.0));
    float c = dot(GraphHash2(cell + float2(0.0, 1.0)) * 2.0 - 1.0, f - float2(0.0, 1.0));
    float d = dot(GraphHash2(cell + float2(1.0, 1.0)) * 2.0 - 1.0, f - float2(1.0, 1.0));
    return saturate(lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) * 0.7071 + 0.5);
}

// Clouds node: four layers of GraphNoise, each twice as fine and half as strong. 0..1.
float GraphClouds(float2 p) {
    float sum = 0.0;
    float strength = 0.5;
    for (int layer = 0; layer < 4; ++layer) {
        sum += strength * GraphNoise(p);
        p = p * 2.03 + float2(17.0, 31.0);
        strength *= 0.5;
    }
    return sum / 0.9375;
}

// Voronoi node: x = the distance to the nearest cell centre (one centre per unit square, placed at
// random inside it), y = that cell's own random number.
float2 GraphVoronoi(float2 p) {
    float2 cell = floor(p);
    float2 f = frac(p);
    float nearest = 8.0;
    float id = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 neighbour = float2(float(x), float(y));
            float2 toCentre = neighbour + GraphHash2(cell + neighbour) - f;
            float distanceSq = dot(toCentre, toCentre);
            if (distanceSq < nearest) {
                nearest = distanceSq;
                id = GraphHash(cell + neighbour);
            }
        }
    }
    return float2(sqrt(nearest), id);
}

// Checker node: 0 and 1 on alternate unit squares.
float GraphChecker(float2 p) {
    float2 cell = floor(p);
    return frac((cell.x + cell.y) * 0.5) * 2.0;
}

// RotateUV node: uv turned round centre by a number of whole turns.
float2 GraphRotate(float2 uv, float2 centre, float turns) {
    float angle = turns * 6.2831853;
    float sine = sin(angle);
    float cosine = cos(angle);
    float2 p = uv - centre;
    return float2(p.x * cosine - p.y * sine, p.x * sine + p.y * cosine) + centre;
}

// PolarUV node: x = the angle round centre as 0..1, y = the distance from it.
float2 GraphPolar(float2 uv, float2 centre) {
    float2 p = uv - centre;
    return float2(atan2(p.y, p.x) / 6.2831853 + 0.5, length(p));
}

// TriplanarTexture node: the texture laid along each world axis by world position, and the
// three blended by how much the surface faces each axis. Sharpness narrows the blend.
float4 GraphTriplanar(Sampler2D tex, float3 worldPos, float3 normal, float scale, float sharpness) {
    float3 weight = pow(abs(normal), float3(max(sharpness, 0.0001)));
    weight /= max(weight.x + weight.y + weight.z, 0.0001);
    float3 p = worldPos * scale;
    return tex.Sample(p.zy) * weight.x + tex.Sample(p.xz) * weight.y + tex.Sample(p.xy) * weight.z;
}

// Luminance node (and Saturation): how bright a linear colour looks.
float GraphLuminance(float3 colour) {
    return dot(colour, float3(0.2126, 0.7152, 0.0722));
}

// HueShift node: the colour turned about the grey axis by a number of whole turns.
float3 GraphHueShift(float3 colour, float turns) {
    float angle = turns * 6.2831853;
    float3 axis = float3(0.57735027, 0.57735027, 0.57735027);
    return colour * cos(angle) + cross(axis, colour) * sin(angle) + axis * dot(axis, colour) * (1.0 - cos(angle));
}

// NormalBlend node: a detail normal laid over a base normal (both tangent space).
float3 GraphBlendNormals(float3 base, float3 detail) {
    return normalize(float3(base.xy + detail.xy, base.z * detail.z));
}

#endif // CRUCIBLE_GRAPH_NODES_SLANG
