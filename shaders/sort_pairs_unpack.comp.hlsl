struct PushConstants
{
    uint numElements;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] StructuredBuffer<uint2> inputPairs;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> outputIndices;

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint id = dispatchThreadId.x;
    if (id >= pc.numElements)
        return;

    // Extracts splat indices from sorted uint2 pairs into the output indices buffer
    outputIndices[id] = inputPairs[id].y;
}
