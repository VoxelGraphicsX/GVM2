#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_MUTABLE_REFERENCE_NON_MEMORY_OBJECT_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_MUTABLE_REFERENCE_NON_MEMORY_OBJECT_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the storage element used as an invalid ordinary mutable-reference argument. */
struct InvalidMutableReferenceNonMemoryObjectBindings final : public IBindGroup
{
    /** Binds the writable storage buffer used by the negative fixture. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Mutates an ordinary C++ reference so the caller must preserve its identity. */
void incrementMutableReference(uint &value)
{
    value += 1u;
}

/** Passes a storage-buffer element to an ordinary mutable reference. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidMutableReferenceNonMemoryObjectPass final : public IComputeClass
{
public:
    /** Binds the resource used by the invalid reference call. */
    constructor(BindGroup<InvalidMutableReferenceNonMemoryObjectBindings> data [[Slot0]])
    {
    }

private:
    /** Triggers the diagnostic for a reference argument that would require a value-result temporary. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0u)
        {
            incrementMutableReference(data->values[0u]);
        }
    }
};

#endif
