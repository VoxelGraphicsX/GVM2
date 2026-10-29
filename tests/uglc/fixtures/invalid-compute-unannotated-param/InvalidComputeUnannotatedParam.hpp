#ifndef UGLC_TEST_INVALID_COMPUTE_UNANNOTATED_PARAM_HPP
#define UGLC_TEST_INVALID_COMPUTE_UNANNOTATED_PARAM_HPP

#include "UGL.h"

using namespace UGL;

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidComputeUnannotatedParamPass final : public IComputeClass
{
public:
    constructor()
    {
    }

private:
    void compute(uint tid)
    {
        (void)tid;
    }
};

#endif
