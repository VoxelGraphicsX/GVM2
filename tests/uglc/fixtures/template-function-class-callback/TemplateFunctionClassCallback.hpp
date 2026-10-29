#ifndef UGLC_TEST_TEMPLATE_FUNCTION_CLASS_CALLBACK_HPP
#define UGLC_TEST_TEMPLATE_FUNCTION_CLASS_CALLBACK_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the writable storage buffer used by the callback-template compute fixture. */
struct TemplateFunctionClassCallbackBindGroup final : public IBindGroup
{
    /** Creates the bind group with the storage buffer written by callback policies. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace TemplateFunctionClassCallbackHelpers
{
    /** Carries per-thread state between callback policy methods. */
    struct Context
    {
        uint index;
        uint value;
    };

    /** Writes only even thread indices and applies a small policy-specific increment. */
    struct EvenCallback
    {
        /** Builds the context used by later callback policy methods. */
        Context init(uint3 threadID)
        {
            Context context;
            context.index = threadID.x;
            context.value = threadID.x + 1u;
            return context;
        }

        /** Returns true only when this policy should write the current thread. */
        bool shouldWrite(Context context)
        {
            return (context.index & 1u) == 0u;
        }

        /** Computes the value written by this callback policy. */
        uint transform(Context context)
        {
            return context.value + 17u;
        }
    };

    /** Writes every thread index and applies a distinct policy-specific increment. */
    struct AlwaysCallback
    {
        /** Builds the context used by later callback policy methods. */
        Context init(uint3 threadID)
        {
            Context context;
            context.index = threadID.x;
            context.value = threadID.x + 3u;
            return context;
        }

        /** Returns true for every thread handled by this policy. */
        bool shouldWrite(Context context)
        {
            return context.value > 0u;
        }

        /** Computes the value written by this callback policy. */
        uint transform(Context context)
        {
            return context.value + 29u;
        }
    };

    /** Runs one class callback policy through a function template instantiated by shader code. */
    template <class CallbackType>
    void runCallback(uint3 threadID, BindGroup<TemplateFunctionClassCallbackBindGroup> bindGroup)
    {
        CallbackType callback;
        const Context context = callback.init(threadID);
        if (callback.shouldWrite(context))
        {
            bindGroup->values[context.index] = callback.transform(context);
        }
    }
} // namespace TemplateFunctionClassCallbackHelpers

/** Exercises function-template callback instantiation from a compute shader entry point. */
class [[LocalWorkGroupSize(4, 1, 1)]] TemplateFunctionClassCallbackPass final : public IComputeClass
{
public:
    /** Binds the storage buffer used by both callback-policy instantiations. */
    constructor(BindGroup<TemplateFunctionClassCallbackBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Invokes the same function template with two concrete callback policy classes. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        TemplateFunctionClassCallbackHelpers::runCallback<TemplateFunctionClassCallbackHelpers::EvenCallback>(threadID, bindGroup);
        TemplateFunctionClassCallbackHelpers::runCallback<TemplateFunctionClassCallbackHelpers::AlwaysCallback>(threadID, bindGroup);
    }
};

#endif
