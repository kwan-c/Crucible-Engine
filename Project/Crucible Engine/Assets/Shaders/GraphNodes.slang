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

#endif // CRUCIBLE_GRAPH_NODES_SLANG
