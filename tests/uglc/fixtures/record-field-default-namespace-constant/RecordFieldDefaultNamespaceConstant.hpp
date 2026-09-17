#ifndef UGLC_TEST_RECORD_FIELD_DEFAULT_NAMESPACE_CONSTANT_HPP
#define UGLC_TEST_RECORD_FIELD_DEFAULT_NAMESPACE_CONSTANT_HPP

#include "UGL.h"

using namespace UGL;

namespace RecordFieldDefaultNamespaceConstantValues
{
    /** Provides a namespace-scoped constant used by a record field default initializer. */
    static const uint DefaultMode = 7u;
}

/** Provides the output buffer used by the record field default constant regression. */
struct RecordFieldDefaultNamespaceConstantBindGroup final : public IBindGroup
{
    /** Creates the shader-visible output buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Stores a field default initializer that references a namespace-scoped constant. */
struct RecordFieldDefaultNamespaceConstantParams
{
    uint mode = RecordFieldDefaultNamespaceConstantValues::DefaultMode;
};

/** Runs a compute shader that materializes a record with a namespace constant field default. */
class [[LocalWorkGroupSize(1, 1, 1)]] RecordFieldDefaultNamespaceConstantPass final : public IComputeClass
{
public:
    /** Captures the shader-visible output bind group. */
    constructor(BindGroup<RecordFieldDefaultNamespaceConstantBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes the default-initialized mode value to prove the referenced constant is emitted. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        RecordFieldDefaultNamespaceConstantParams params;
        bindGroup->values[threadID.x] = params.mode;
    }
};

#endif
