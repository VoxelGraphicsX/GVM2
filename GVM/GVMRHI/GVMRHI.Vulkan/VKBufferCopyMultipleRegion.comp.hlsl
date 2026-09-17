struct BufferCopyRegion
{
    uint srcOffsetBytes;
    uint dstOffsetBytes;
    uint storageBytes;
};

struct CopyPushConstants
{
    uint copyRegionCount;
};

[[vk::binding(0, 0)]] StructuredBuffer<uint> bindGroup_srcWords : register(t0, space0);
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> bindGroup_dstWords : register(u1, space0);
[[vk::binding(2, 0)]] StructuredBuffer<BufferCopyRegion> bindGroup_copyRegions : register(t2, space0);
[[vk::push_constant]] CopyPushConstants gPushConstants;

[numthreads(64, 1, 1)]
void computeMain(uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= gPushConstants.copyRegionCount)
    {
        return;
    }

    const BufferCopyRegion region = bindGroup_copyRegions[threadID.x];
    const uint srcOffsetWords = region.srcOffsetBytes / 4u;
    const uint dstOffsetWords = region.dstOffsetBytes / 4u;
    const uint storageWords = region.storageBytes / 4u;
    for (uint wordIndex = 0u; wordIndex < storageWords; ++wordIndex)
    {
        bindGroup_dstWords[dstOffsetWords + wordIndex] = bindGroup_srcWords[srcOffsetWords + wordIndex];
    }
}
