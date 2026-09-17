#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>

#include <filesystem>

namespace
{
    TEST(DslCodegenSmokeTests, GeneratesExpectedArtifacts)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto dslSource = GVM::Tests::requireEnvPath("GVM_TEST_DSL_SOURCE");

        const auto stampFile = generatedDir / "temp_output.abc";
        const auto generatedHeader = generatedDir / "generate_result.hpp";
        const auto dslSingleHeader = generatedDir / "dsl_single_header.hpp";

        EXPECT_TRUE(std::filesystem::exists(dslSource));
        ASSERT_TRUE(std::filesystem::exists(stampFile));
        ASSERT_TRUE(std::filesystem::exists(generatedHeader));
        ASSERT_TRUE(std::filesystem::exists(dslSingleHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_FALSE(contents.empty());
        EXPECT_NE(contents.find("TriangleRenderPipeline"), std::string::npos);
        EXPECT_NE(contents.find("createRenderPipeline"), std::string::npos);
        EXPECT_NE(contents.find("TriangleVertexShader"), std::string::npos);

        const auto dslContents = GVM::Tests::readTextFile(dslSingleHeader);
        EXPECT_FALSE(dslContents.empty());
        EXPECT_NE(dslContents.find("class Triangle final : public IRenderClass"), std::string::npos);
        EXPECT_EQ(dslContents.find("struct ShaderArtifact"), std::string::npos);
    }

    TEST(DslCodegenSmokeTests, EmitsRendererLifecycleAndBindGroupHooks)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_NE(contents.find("createRenderSetCommandEncoder"), std::string::npos);
        EXPECT_NE(contents.find("executeRenderSetCommand"), std::string::npos);
        EXPECT_NE(contents.find("RenderSet"), std::string::npos);
        EXPECT_NE(contents.find("createBindGroupLayout"), std::string::npos);
        EXPECT_NE(contents.find("ExportedRenderSet"), std::string::npos);
    }
} // namespace
