#ifndef UGLC_HLSL_AGGREGATE_CALL_ARG_REGRESSION_HPP
#define UGLC_HLSL_AGGREGATE_CALL_ARG_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

namespace AggregateCallArgRegression
{
    struct StackFrame
    {
        uint payload;
        int childIndex;
    };

    template <class StackItem, int N>
    class LocalStack
    {
        StackItem items[N];
        int stackPtr;

    public:
        void init()
        {
            stackPtr = -1;
        }

        void push(StackItem value)
        {
            items[++stackPtr] = value;
        }
    };

    class [[LocalWorkGroupSize(4, 1, 1)]] AggregateCallArgPass final : public IComputeClass
    {
    public:
        constructor()
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            LocalStack<StackFrame, 4> stack;
            stack.init();
            stack.push({threadID.x + 1u, 1});
        }
    };
} // namespace AggregateCallArgRegression

#endif
