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

float3 ModelNormal(float3x3 transform, float3 normal) {
    float3x3 cofactors = float3x3(cross(transform[1], transform[2]),
                                cross(transform[2], transform[0]),
                                cross(transform[0], transform[1]));
    float determinant = dot(transform[0], cofactors[0]);
    float3 result = mul(cofactors, normal) * (determinant < 0.0 ? -1.0 : 1.0);
    float lengthSquared = dot(result, result);
    return lengthSquared > 1e-16 ? result * rsqrt(lengthSquared) : float3(0.0, 1.0, 0.0);
}
