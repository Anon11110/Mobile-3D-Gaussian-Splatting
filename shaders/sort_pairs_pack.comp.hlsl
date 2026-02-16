struct PushConstants
{
    uint numElements;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] StructuredBuffer<uint> depthKeys;
[[vk::binding(1, 0)]] StructuredBuffer<uint> splatIndices;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint2> outputPairs;

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint id = dispatchThreadId.x;
    if (id >= pc.numElements)
        return;

    // Packs separate depth key and splat index buffers into interleaved uint2 pairs
    // This enables the scatter phase to write a single 64-bit value per element
    // instead of two scattered 32-bit writes to separate buffers
    outputPairs[id] = uint2(depthKeys[id], splatIndices[id]);
}
