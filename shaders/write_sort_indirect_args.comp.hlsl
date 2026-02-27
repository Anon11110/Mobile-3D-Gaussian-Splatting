[[vk::binding(0, 0)]] ByteAddressBuffer globalCounter;
[[vk::binding(1, 0)]] RWByteAddressBuffer indirectArgs;

struct WriteArgsPC
{
    uint maxTileInstances;
};
[[vk::push_constant]] WriteArgsPC pc;

#define SORT_WORKGROUP_SIZE 256
#define MAX_SORT_WORKGROUPS 256
#define RADIX_SORT_BINS 256
#define ELEMENTS_PER_THREAD 4

// IndirectArgs buffer layout (matches splat_precompute.comp.hlsl):
//   Offset  0: SortParams {numElements, numWorkgroups, numBlocksPerWorkgroup, numScanElements, numScanWorkgroups} (20 bytes, padded to 32)
//   Offset 32: DispatchIndirect for histogram/scatter: {numWorkgroups, 1, 1}
//   Offset 44: DispatchIndirect for scan blocks/add offsets: {numScanWorkgroups, 1, 1}
//   Offset 56: DispatchIndirect for scan block sums: {1, 1, 1}
//   Offset 68: DispatchIndirect for pack/unpack/identify_ranges: {ceil(N/256), 1, 1}

[numthreads(1, 1, 1)]
void main()
{
    uint tileInstanceCount = globalCounter.Load(0);
    uint numElements = min(tileInstanceCount, pc.maxTileInstances);
    numElements = max(numElements, 1);

    // Compute sort dispatch parameters
    uint numWorkgroups = (numElements + SORT_WORKGROUP_SIZE - 1) / SORT_WORKGROUP_SIZE;
    numWorkgroups = min(numWorkgroups, MAX_SORT_WORKGROUPS);
    numWorkgroups = max(numWorkgroups, 1);

    uint elementsPerWorkgroup  = (numElements + numWorkgroups - 1) / numWorkgroups;
    uint numBlocksPerWorkgroup = (elementsPerWorkgroup + SORT_WORKGROUP_SIZE - 1) / SORT_WORKGROUP_SIZE;
    numBlocksPerWorkgroup = max(numBlocksPerWorkgroup, 1);

    uint numScanElements   = numWorkgroups * RADIX_SORT_BINS;
    uint elementsPerScanWG = SORT_WORKGROUP_SIZE * ELEMENTS_PER_THREAD;
    uint numScanWorkgroups = (numScanElements + elementsPerScanWG - 1) / elementsPerScanWG;
    numScanWorkgroups = max(numScanWorkgroups, 1);

    // SortParams at offset 0
    indirectArgs.Store(0,  numElements);
    indirectArgs.Store(4,  numWorkgroups);
    indirectArgs.Store(8,  numBlocksPerWorkgroup);
    indirectArgs.Store(12, numScanElements);
    indirectArgs.Store(16, numScanWorkgroups);

    // DispatchIndirect for histogram/scatter at offset 32
    indirectArgs.Store(32, numWorkgroups);
    indirectArgs.Store(36, 1);
    indirectArgs.Store(40, 1);

    // DispatchIndirect for scan blocks/add offsets at offset 44
    indirectArgs.Store(44, numScanWorkgroups);
    indirectArgs.Store(48, 1);
    indirectArgs.Store(52, 1);

    // DispatchIndirect for scan block sums at offset 56
    indirectArgs.Store(56, 1);
    indirectArgs.Store(60, 1);
    indirectArgs.Store(64, 1);

    // DispatchIndirect for pack/unpack/identify_ranges at offset 68
    uint packUnpackGroups = (numElements + SORT_WORKGROUP_SIZE - 1) / SORT_WORKGROUP_SIZE;
    packUnpackGroups = max(packUnpackGroups, 1);
    indirectArgs.Store(68, packUnpackGroups);
    indirectArgs.Store(72, 1);
    indirectArgs.Store(76, 1);
}
