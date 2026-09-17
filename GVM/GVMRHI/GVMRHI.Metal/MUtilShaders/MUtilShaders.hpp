#pragma once
#include <EASTL/string.h>
namespace GVM::RHI::Metal
{
    eastl::string getIndirectIndexedRenderCommandConvertShader(const eastl::string &indexType, uint32_t stride);
    eastl::string getIndirectRenderCommandConvertShader(uint32_t stride);

    eastl::string getQuadVertexShader();
    eastl::string getQuadFragmentShader();
    eastl::string getQuadComputeShader();

    eastl::string getCopyBufferToBufferMultipleRegion();

} // namespace GVM::RHI::Metal