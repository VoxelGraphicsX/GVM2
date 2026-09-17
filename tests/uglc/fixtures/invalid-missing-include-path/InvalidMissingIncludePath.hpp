#ifndef UGLC_TEST_INVALID_MISSING_INCLUDE_PATH_HPP
#define UGLC_TEST_INVALID_MISSING_INCLUDE_PATH_HPP

#include "UGL.h"

using namespace UGL;

class InvalidMissingIncludePathPass final : public IComputeClass
{
public:
    constructor()
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        (void)threadID;
    }
};

#endif
