#pragma once
#include <EASTL/numeric_limits.h>
#include <GVMCore/Public/GVMCore.Defines.hpp>
#include <inttypes.h>
namespace GVM::Core
{

    static constexpr uint64_t RenderComponentVertexBufferSizeAlign = 256;
    static constexpr uint64_t RenderComponentIndexBufferSizeAlign = 256;
    static constexpr RenderComponentIndex RenderComponentNullIndex = eastl::numeric_limits<RenderComponentIndex>::max();

} // namespace GVM::Core
