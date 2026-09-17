#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_MEMBER_POINTER_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_MEMBER_POINTER_HPP

#include "UGL.h"

using namespace UGL;

struct ExperimentalUGLIRMemberPointerPayload
{
    uint value = 0u;
};

struct ExperimentalUGLIRInvalidMemberPointerBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidMemberPointerPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidMemberPointerBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint ExperimentalUGLIRMemberPointerPayload::*member = &ExperimentalUGLIRMemberPointerPayload::value;
        (void)member;
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
