#ifndef UGLC_TEST_INVALID_UNIFORM_BUFFER_RESOURCE_MEMBER_HPP
#define UGLC_TEST_INVALID_UNIFORM_BUFFER_RESOURCE_MEMBER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidUniformBufferResourceMemberNestedPayload
{
    Texture2D<float4> albedo;
};

struct InvalidUniformBufferResourceMemberPayload
{
    InvalidUniformBufferResourceMemberNestedPayload textures;
};

struct InvalidUniformBufferResourceMemberBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<InvalidUniformBufferResourceMemberPayload> payload [[Binding0]])
    {
    }
};

#endif
