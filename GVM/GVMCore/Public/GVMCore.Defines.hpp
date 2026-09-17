#pragma once
#include <EASTL/shared_ptr.h>
#include <inttypes.h>
namespace GVM::Core
{
    using RenderEntityIndex = uint32_t;
    using RenderComponentIndex = uint32_t;
    using RenderComponentHandle = uint32_t;
    using RenderSetHandle = uint32_t;
    class RenderSet;
    class AbstractRenderSetCommandEncoderImpl;
    using AbstractRenderSetCommandEncoder = eastl::shared_ptr<AbstractRenderSetCommandEncoderImpl>;
    static constexpr int RenderTextureMaxTextureCountPerEntity = 8;
} // namespace GVM::Core
