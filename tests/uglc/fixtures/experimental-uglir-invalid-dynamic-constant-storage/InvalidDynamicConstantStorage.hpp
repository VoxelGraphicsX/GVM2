#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_DYNAMIC_CONSTANT_STORAGE_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_DYNAMIC_CONSTANT_STORAGE_HPP

#include "UGL.h"

using namespace UGL;

/** Returns a runtime value so callers cannot use it for compile-time constant initialization. */
inline uint experimentalUGLIRInvalidDynamicConstantStorageRuntimeValue()
{
    return 7u;
}

/** Has const qualification but obtains its value through dynamic initialization. */
const uint experimentalUGLIRInvalidDynamicConstantStorageGlobal =
    experimentalUGLIRInvalidDynamicConstantStorageRuntimeValue();

/** Provides the output storage used by the dynamic-constant diagnostic fixture. */
struct ExperimentalUGLIRInvalidDynamicConstantStorageBindings final : public IBindGroup
{
    /** Binds the output buffer written by the shader entry. */
    constructor(RWStructuredBuffer<uint> output [[Binding0]])
    {
    }
};

/** Reads a namespace constant whose initializer is not compile-time proven. */
inline uint experimentalUGLIRInvalidDynamicConstantStorageReadGlobal()
{
    return experimentalUGLIRInvalidDynamicConstantStorageGlobal;
}

/** Reads a static local const whose initializer is not compile-time proven. */
inline uint experimentalUGLIRInvalidDynamicConstantStorageReadStatic()
{
    static const uint dynamicStatic = experimentalUGLIRInvalidDynamicConstantStorageRuntimeValue();
    return dynamicStatic;
}

/** Exercises both dynamic const storage forms from one reachable shader entry. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidDynamicConstantStoragePass final : public IComputeClass
{
public:
    /** Binds the output resource used by the diagnostic fixture. */
    constructor(BindGroup<ExperimentalUGLIRInvalidDynamicConstantStorageBindings> data [[Slot0]])
    {
    }

private:
    /** Calls both dynamic-constant helpers so each storage form is diagnosed. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0u)
        {
            data->output[0u] = experimentalUGLIRInvalidDynamicConstantStorageReadGlobal();
            data->output[1u] = experimentalUGLIRInvalidDynamicConstantStorageReadStatic();
        }
    }
};

#endif
