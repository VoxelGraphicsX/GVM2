#ifndef UGLC_TEST_INVALID_STRUCTURED_BUFFER_POINTER_MEMBER_HPP
#define UGLC_TEST_INVALID_STRUCTURED_BUFFER_POINTER_MEMBER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidStructuredBufferPointerMemberBindGroup final : public IBindGroup
{
    constructor(StructuredBuffer<uint *> entries [[Binding0]])
    {
    }
};

#endif
