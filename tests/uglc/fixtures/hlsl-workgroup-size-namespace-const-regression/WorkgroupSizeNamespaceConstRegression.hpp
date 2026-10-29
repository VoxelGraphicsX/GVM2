#ifndef UGLC_HLSL_WORKGROUP_SIZE_NAMESPACE_CONST_REGRESSION_HPP
#define UGLC_HLSL_WORKGROUP_SIZE_NAMESPACE_CONST_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

namespace WorkgroupSizeNamespaceConstRegression
{
    static const uint WorkGroupSize = 64u;

    class [[LocalWorkGroupSize(WorkgroupSizeNamespaceConstRegression::WorkGroupSize, 1, 1)]] WorkgroupSizeNamespaceConstPass final : public IComputeClass
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
}

#endif
