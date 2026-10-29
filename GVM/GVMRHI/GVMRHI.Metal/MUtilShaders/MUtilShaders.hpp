#pragma once
#include <EASTL/string.h>
namespace GVM::RHI::Metal
{
    /** Returns the indexed conversion shader with runtime record stride and exact native-field loads. */
    eastl::string getIndirectIndexedRenderCommandConvertShader(const eastl::string &indexType);
    /** Returns the non-indexed conversion shader with runtime record stride and exact native-field loads. */
    eastl::string getIndirectRenderCommandConvertShader();


    eastl::string getCopyBufferToBufferMultipleRegion();

    /** Returns the vertex shader for the shared fullscreen texture draw. */
    eastl::string getFullscreenTextureVertexShader();
    /** Returns the fragment shader, with normal and vertically flipped entry points, for the shared fullscreen texture draw. */
    eastl::string getFullscreenTextureFragmentShader();

    /** Returns the compute kernel that writes one 32-bit value per word across a buffer, matching vkCmdFillBuffer semantics. */

} // namespace GVM::RHI::Metal
