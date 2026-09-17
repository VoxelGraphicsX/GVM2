#if defined(__APPLE__)

#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.Metal/MUtilShaders/MUtilShaders.hpp>
#include <GVMRHI/GVMRHI.hpp>

namespace
{
    /** Verifies that native indexed indirect records keep the five-field Metal command layout. */
    TEST(MetalIndirectCommandShaderTests, IndexedNativeStrideUsesBaseCommandLayout)
    {
        const eastl::string shader = GVM::RHI::Metal::getIndirectIndexedRenderCommandConvertShader(
            "uint",
            sizeof(GVM::RHI::IndirectIndexedRenderCommand));

        EXPECT_EQ(shader.find("extraUint"), eastl::string::npos);
        EXPECT_NE(shader.find("uint32_t    firstInstance;"), eastl::string::npos);
    }

    /** Verifies that RenderSet entity-info records preserve their 32-byte indexed indirect stride. */
    TEST(MetalIndirectCommandShaderTests, IndexedRenderSetStrideAddsRecordPadding)
    {
        constexpr uint32_t renderEntityInfoStride = 32u;
        const eastl::string shader = GVM::RHI::Metal::getIndirectIndexedRenderCommandConvertShader(
            "uint",
            renderEntityInfoStride);

        EXPECT_NE(shader.find("uint32_t extraUint[3];"), eastl::string::npos);
        EXPECT_NE(shader.find("device IndirectIndexedRenderCommand           *indirectBuffer"), eastl::string::npos);
    }
} // namespace

#endif
