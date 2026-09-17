#ifndef UGLC_TEST_CPP_STRUCTURED_BUFFER_HELPER_HOST_DELETE_HPP
#define UGLC_TEST_CPP_STRUCTURED_BUFFER_HELPER_HOST_DELETE_HPP

#include "UGL.h"

using namespace UGL;

namespace StructuredBufferHostDelete
{
    bool readShaderOnlyStatusFlag(RWStructuredBuffer<uint> renderEntityStatus, uint index)
    {
        return renderEntityStatus[index] != 0u;
    }

    void setShaderOnlyStatusFlag(RWStructuredBuffer<uint> renderEntityStatus, uint index, bool value)
    {
        if (value)
        {
            renderEntityStatus[index] |= 1u;
        }
        else
        {
            renderEntityStatus[index] &= ~1u;
        }
    }
} // namespace StructuredBufferHostDelete

struct CppStructuredBufferHelperHostDeleteBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> renderEntityStatus [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] CppStructuredBufferHelperHostDeletePass final : public IComputeClass
{
public:
    constructor(BindGroup<CppStructuredBufferHelperHostDeleteBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = threadID.x;
        const bool currentValue = StructuredBufferHostDelete::readShaderOnlyStatusFlag(bindGroup->renderEntityStatus, index);
        StructuredBufferHostDelete::setShaderOnlyStatusFlag(bindGroup->renderEntityStatus, index, !currentValue);
    }
};

#endif
