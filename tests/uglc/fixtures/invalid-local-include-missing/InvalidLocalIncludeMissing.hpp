#ifndef UGLC_TEST_INVALID_LOCAL_INCLUDE_MISSING_HPP
#define UGLC_TEST_INVALID_LOCAL_INCLUDE_MISSING_HPP

#include "UGL.h"
#include "MissingLocalHeader.hpp"

using namespace UGL;

class InvalidLocalIncludeMissingPass final : public IComputeClass
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
