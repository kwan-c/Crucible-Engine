// Shared vertex deformation for scene, shadows, selection and depth/normals.
// Palette matrices occupy entries in the SAME frame-owned buffer as instances.
// They are appended once per render part before any view is built.
struct InstanceData {
    float4x4 model;
    float4 params;
    uint4 skin; // first palette entry, joint count, reserved, reserved (96-byte stride)
};
[[vk::binding(2, 0)]] StructuredBuffer<InstanceData> instances;

float4x4 ModelVertexTransform(uint instanceID, uint4 joints, float4 weights) {
    InstanceData instance = instances[instanceID];
    if (instance.skin.y == 0 || dot(weights, float4(1.0)) <= 0.0) return instance.model;
    float4x4 deformation = float4x4(0.0);
    for (uint i = 0; i < 4; ++i) {
        if (weights[i] > 0.0 && joints[i] < instance.skin.y)
            deformation += instances[instance.skin.x + joints[i]].model * weights[i];
    }
    return mul(instance.model, deformation);
}

float3 ModelUnit(float3 value, float3 fallback) {
    float largest = max(max(abs(value.x), abs(value.y)), abs(value.z));
    if (!(largest > 0.0) || !isfinite(largest)) return fallback;
    float3 scaled = value / largest;
    return scaled * rsqrt(dot(scaled, scaled));
}

float3x3 ModelDirectionTransform(float3x3 transform) {
    float largest = max(max(max(abs(transform[0].x), abs(transform[0].y)), abs(transform[0].z)),
                        max(max(max(abs(transform[1].x), abs(transform[1].y)), abs(transform[1].z)),
                            max(max(abs(transform[2].x), abs(transform[2].y)), abs(transform[2].z))));
    return largest > 0.0 && isfinite(largest) ? transform / largest : float3x3(0.0);
}

float3 ModelNormal(float3x3 transform, float3 normal) {
    // Scaling by a positive common factor preserves direction and orientation,
    // but prevents tiny/large model scales from under/overflowing the cofactors.
    float3x3 direction = ModelDirectionTransform(transform);
    float3x3 cofactors = float3x3(cross(direction[1], direction[2]),
                                cross(direction[2], direction[0]),
                                cross(direction[0], direction[1]));
    float determinant = dot(direction[0], cofactors[0]);
    float3 result = mul(cofactors, normal) * (determinant < 0.0 ? -1.0 : 1.0);
    return ModelUnit(result, float3(0.0, 1.0, 0.0));
}

float3 ModelTangent(float3x3 transform, float3 normal, float3 tangent) {
    float3 direction = ModelUnit(mul(ModelDirectionTransform(transform), tangent), float3(0.0));
    float3 seed = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 fallback = ModelUnit(cross(seed, normal), float3(1.0, 0.0, 0.0));
    float3 projected = direction - normal * dot(normal, direction);
    // Near-parallel inputs leave only rounding noise after Gram-Schmidt.
    // Normalizing that noise can produce a tangent parallel to the normal.
    return dot(projected, projected) > 1e-8 ? ModelUnit(projected, fallback) : fallback;
}
