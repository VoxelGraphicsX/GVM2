#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_VALUE_RESULT_REFERENCE_ADDRESS_SPACE_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_VALUE_RESULT_REFERENCE_ADDRESS_SPACE_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the storage element used as an invalid OUT/INOUT argument. */
struct InvalidValueResultReferenceAddressSpaceBindings final : public IBindGroup
{
    /** Binds the writable storage buffer used by the negative fixture. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Writes an explicit OUT value-result parameter. */
void writeOutValue(uint value OUT)
{
    value = 2u;
}

/** Updates an explicit INOUT value-result parameter. */
void updateInOutValue(uint value INOUT)
{
    value += 1u;
}

/** Passes a storage-buffer element across the SPIR-V address-space boundary. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidValueResultReferenceAddressSpacePass final : public IComputeClass
{
public:
    /** Binds the resource used by the invalid value-result calls. */
    constructor(BindGroup<InvalidValueResultReferenceAddressSpaceBindings> data [[Slot0]])
    {
    }

private:
    /** Triggers the address-space diagnostic for both OUT and INOUT arguments. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0u)
        {
            writeOutValue(data->values[0u]);
            updateInOutValue(data->values[0u]);
        }
    }
};

#endif
