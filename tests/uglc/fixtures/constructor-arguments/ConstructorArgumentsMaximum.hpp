#pragma once
#include "UGL.h"
using namespace UGL;

/** Exercises the maximum constructor argument count and highest registered attribute. */
class [[LocalWorkGroupSize(1, 1, 1)]] MaximumArguments final : public IComputeClass
{
public:
    /** Creates a shader with every supported argument position. */
    constructor(
        uint value0 [[Args0]],
        uint value1 [[Args1]],
        uint value2 [[Args2]],
        uint value3 [[Args3]],
        uint value4 [[Args4]],
        uint value5 [[Args5]],
        uint value6 [[Args6]],
        uint value7 [[Args7]],
        uint value8 [[Args8]],
        uint value9 [[Args9]],
        uint value10 [[Args10]],
        uint value11 [[Args11]],
        uint value12 [[Args12]],
        uint value13 [[Args13]],
        uint value14 [[Args14]],
        uint value15 [[Args15]],
        uint value16 [[Args16]],
        uint value17 [[Args17]],
        uint value18 [[Args18]],
        uint value19 [[Args19]],
        uint value20 [[Args20]],
        uint value21 [[Args21]],
        uint value22 [[Args22]],
        uint value23 [[Args23]],
        uint value24 [[Args24]],
        uint value25 [[Args25]],
        uint value26 [[Args26]],
        uint value27 [[Args27]],
        uint value28 [[Args28]],
        uint value29 [[Args29]],
        uint value30 [[Args30]],
        uint value31 [[Args31]],
        uint value32 [[Args32]],
        uint value33 [[Args33]],
        uint value34 [[Args34]],
        uint value35 [[Args35]],
        uint value36 [[Args36]],
        uint value37 [[Args37]],
        uint value38 [[Args38]],
        uint value39 [[Args39]],
        uint value40 [[Args40]],
        uint value41 [[Args41]],
        uint value42 [[Args42]],
        uint value43 [[Args43]],
        uint value44 [[Args44]],
        uint value45 [[Args45]],
        uint value46 [[Args46]],
        uint value47 [[Args47]],
        uint value48 [[Args48]],
        uint value49 [[Args49]],
        uint value50 [[Args50]],
        uint value51 [[Args51]],
        uint value52 [[Args52]],
        uint value53 [[Args53]],
        uint value54 [[Args54]],
        uint value55 [[Args55]],
        uint value56 [[Args56]],
        uint value57 [[Args57]],
        uint value58 [[Args58]],
        uint value59 [[Args59]],
        uint value60 [[Args60]],
        uint value61 [[Args61]],
        uint value62 [[Args62]],
        uint value63 [[Args63]]) {}
private:
    /** Reads the first and last positions without truncating or renumbering the contract. */
    void compute(uint3 id [[DispatchThreadID]]) { const uint value = value0 + value63; }
};

/** Verifies that a maximum-size execution prefix is followed by exactly three controls. */
inline void validateMaximumArguments(Device device)
{
    auto compute = device->createComputeClass<MaximumArguments>(0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u, 32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u, 46u, 47u, 48u, 49u, 50u, 51u, 52u, 53u, 54u, 55u, 56u, 57u, 58u, 59u, 60u, 61u, 62u, 63u);
    compute->dispatchThreads(0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u, 32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u, 46u, 47u, 48u, 49u, 50u, 51u, 52u, 53u, 54u, 55u, 56u, 57u, 58u, 59u, 60u, 61u, 62u, 63u, 1, 1, 1);
}
