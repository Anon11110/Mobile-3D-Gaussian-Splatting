// Binding 0: Positions buffer
[[vk::binding(0, 0)]] StructuredBuffer<float3> positions;

// Binding 1: Output buffer for sortable depth keys
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> splatKeys;

// Binding 2: Camera data
[[vk::binding(2, 0)]] cbuffer CameraUBO
{
    float4x4 viewMatrix;
};

// Binding 3: Per-splat mesh indices for model matrix lookup
[[vk::binding(3, 0)]] StructuredBuffer<uint> meshIndices;

// Binding 4: Per-mesh model matrices
[[vk::binding(4, 0)]] StructuredBuffer<float4x4> modelMatrices;

struct PushConstants
{
    uint numElements;
};
[[vk::push_constant]] PushConstants pc;

// Converts a float to a sortable uint. For standard right-handed view spaces,
// larger Z is further away. To sort far-to-near (descending depth), we want
// larger Z values to have smaller uint keys.
uint FloatToSortableUint(float val)
{
    uint u = asuint(val);
    uint mask = (u & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0x80000000u;
    return 0xFFFFFFFFu - (u ^ mask);
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint splatID = dispatchThreadId.x;
    if (splatID >= pc.numElements)
        return;

    // Get local position and transform to world space
    float3 localPos = positions[splatID];
    uint meshIdx = meshIndices[splatID];
    float4x4 modelMat = modelMatrices[meshIdx];
    float4 worldPos4 = mul(modelMat, float4(localPos, 1.0));

    float4 viewPos = mul(viewMatrix, worldPos4);

    // RH camera with -Z forward: farther -> larger depth value
    float depth = -viewPos.z;

    splatKeys[splatID] = FloatToSortableUint(depth);
}
