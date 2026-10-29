#ifndef UGLC_TEST_INVALID_BINDGROUP_BINDING32_HPP
#define UGLC_TEST_INVALID_BINDGROUP_BINDING32_HPP

#include "UGL.h"

using namespace UGL;

#ifndef Binding32
#define Binding32 UGL_ATTR_INDEX(Binding, 32)
#endif

struct InvalidBindGroupBinding32 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding32]])
    {
    }
};

#endif
