#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>

#include <filesystem>

namespace
{
    TEST(DslTextureCodegenTests, EmitsTextureAndSamplerBindingContracts)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto dslSource = GVM::Tests::requireEnvPath("GVM_TEST_DSL_SOURCE");

        const auto generatedHeader = generatedDir / "generate_result.hpp";
        ASSERT_TRUE(std::filesystem::exists(dslSource));
        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_NE(contents.find("TriangleBindGroup"), std::string::npos);
        EXPECT_NE(contents.find("TextureSampleType::Float"), std::string::npos);
        EXPECT_NE(contents.find("SamplerBindingType::Filtering"), std::string::npos);
        EXPECT_NE(contents.find("textureView = texture0"), std::string::npos);
        EXPECT_NE(contents.find("sampler = sampler0"), std::string::npos);
    }

    TEST(DslTextureCodegenTests, EmitsTextureSamplingPipelineContracts)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_NE(contents.find("sampleType = GVM::RHI::TextureSampleType::Float"), std::string::npos);
        EXPECT_NE(contents.find("SamplerBindingType::Filtering"), std::string::npos);
        EXPECT_NE(contents.find("TriangleFragmentShader"), std::string::npos);
        EXPECT_NE(contents.find("createRenderPipeline"), std::string::npos);
    }
} // namespace
