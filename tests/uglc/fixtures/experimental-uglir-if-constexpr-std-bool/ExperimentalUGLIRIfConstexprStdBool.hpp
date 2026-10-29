#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_IF_CONSTEXPR_STD_BOOL_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_IF_CONSTEXPR_STD_BOOL_HPP

#include <type_traits>

#include "UGL.h"

using namespace UGL;

/** Provides the writable storage buffer used by the dependent if constexpr fixture. */
struct ExperimentalUGLIRIfConstexprStdBoolBindGroup final : public IBindGroup
{
    /** Binds the output buffer used by the selected template branch. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace ExperimentalUGLIRIfConstexprStdBoolHelpers
{
    /** Selects a shader value through a type-dependent branch that is resolved at specialization time. */
    template <typename T>
    uint selectValue(T value)
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            return value ? 1u : 0u;
        }
        else
        {
            return 2u;
        }
    }
} // namespace ExperimentalUGLIRIfConstexprStdBoolHelpers

/** Exercises dependent if constexpr selection with an explicitly instantiated bool shader helper. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRIfConstexprStdBoolPass final : public IComputeClass
{
public:
    /** Binds the output resource used by the selected helper specialization. */
    constructor(BindGroup<ExperimentalUGLIRIfConstexprStdBoolBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Instantiates the bool branch so the experimental pipeline must resolve std::is_same_v before emission. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = ExperimentalUGLIRIfConstexprStdBoolHelpers::selectValue<bool>(true);
    }
};

#endif
