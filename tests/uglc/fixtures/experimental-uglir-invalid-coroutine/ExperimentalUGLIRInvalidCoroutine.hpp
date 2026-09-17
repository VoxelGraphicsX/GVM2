#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_COROUTINE_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_COROUTINE_HPP

#include "UGL.h"

#include <coroutine>

using namespace UGL;

struct ExperimentalUGLIRCoroutineTask
{
    struct promise_type
    {
        ExperimentalUGLIRCoroutineTask get_return_object()
        {
            return {};
        }

        std::suspend_never initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_never final_suspend() noexcept
        {
            return {};
        }

        void return_void() noexcept
        {
        }

        void unhandled_exception() noexcept
        {
        }
    };
};

inline ExperimentalUGLIRCoroutineTask experimentalUGLIRMakeCoroutine(uint value)
{
    (void)value;
    co_return;
}

struct ExperimentalUGLIRInvalidCoroutineBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidCoroutinePass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidCoroutineBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        (void)experimentalUGLIRMakeCoroutine(threadID.x);
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
