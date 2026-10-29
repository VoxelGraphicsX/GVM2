#ifndef UGLC_TEST_HLSL_RESOURCE_ELEMENT_METHOD_RECORD_ORDERING_HPP
#define UGLC_TEST_HLSL_RESOURCE_ELEMENT_METHOD_RECORD_ORDERING_HPP

#include "UGL.h"

using namespace UGL;

namespace HLSLResourceElementMethodRecordOrderingTypes
{
    /** Adds one to a key value so record methods need an early helper prototype. */
    inline uint incrementKey(uint value)
    {
        return value + 1u;
    }

    /** Stores a structured-buffer element that also owns shader-side behavior methods. */
    struct KeyData
    {
        uint key;

        /** Returns the incremented key through a namespace helper call. */
        uint readIncremented() const
        {
            return incrementKey(key);
        }

        /** Replaces the stored key value. */
        void replace(uint value)
        {
            key = value;
        }
    };

    /** Processes a resource element after the element record definition is available. */
    inline KeyData processKey(KeyData data, uint delta)
    {
        KeyData result = data;
        result.replace(result.readIncremented() + delta);
        return result;
    }
} // namespace HLSLResourceElementMethodRecordOrderingTypes

/** Provides structured buffers whose element record has user-authored methods. */
struct HLSLResourceElementMethodRecordOrderingBindGroup final : public IBindGroup
{
    /** Creates the input, output, and scalar verification buffers. */
    constructor(StructuredBuffer<HLSLResourceElementMethodRecordOrderingTypes::KeyData> inputKeys [[Binding0]],
                RWStructuredBuffer<HLSLResourceElementMethodRecordOrderingTypes::KeyData> outputKeys [[Binding1]],
                RWStructuredBuffer<uint> values [[Binding2]])
    {
    }
};

/** Verifies HLSL emits resource element records before resource declarations and dependent helper signatures. */
class [[LocalWorkGroupSize(1, 1, 1)]] HLSLResourceElementMethodRecordOrderingPass final : public IComputeClass
{
public:
    /** Captures the shader-visible bind group with structured-buffer element records. */
    constructor(BindGroup<HLSLResourceElementMethodRecordOrderingBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Reads, processes, and writes a structured-buffer element that owns methods. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0)
        {
            HLSLResourceElementMethodRecordOrderingTypes::KeyData data = bindGroup->inputKeys[0];
            HLSLResourceElementMethodRecordOrderingTypes::KeyData result =
                HLSLResourceElementMethodRecordOrderingTypes::processKey(data, 3u);
            bindGroup->outputKeys[0] = result;
            bindGroup->values[0] = result.readIncremented();
        }
    }
};

#endif
