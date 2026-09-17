#ifndef UGLC_TEST_INVALID_COMPUTE_MISSING_CREATE_HPP
#define UGLC_TEST_INVALID_COMPUTE_MISSING_CREATE_HPP

#include "UGL.h"

using namespace UGL;

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidComputeMissingCreatePass final : public IComputeClass
{
private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        (void)threadID;
    }
};

#endif
