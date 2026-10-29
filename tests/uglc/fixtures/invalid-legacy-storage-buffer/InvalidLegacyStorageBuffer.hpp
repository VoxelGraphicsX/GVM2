#ifndef UGLC_TEST_INVALID_LEGACY_STORAGE_BUFFER_HPP
#define UGLC_TEST_INVALID_LEGACY_STORAGE_BUFFER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidLegacyStorageBufferBindGroup final : public IBindGroup
{
    constructor(StorageBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidLegacyStorageBufferPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidLegacyStorageBufferBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        (void)threadID;
    }
};

#endif
