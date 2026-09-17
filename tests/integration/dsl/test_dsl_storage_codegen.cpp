#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>

#include <filesystem>

namespace
{
    TEST(DslStorageCodegenTests, EmitsStorageBufferBindingContracts)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto dslSource = GVM::Tests::requireEnvPath("GVM_TEST_DSL_SOURCE");

        const auto generatedHeader = generatedDir / "generate_result.hpp";
        ASSERT_TRUE(std::filesystem::exists(dslSource));
        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_NE(contents.find("TriangleBindGroup"), std::string::npos);
        EXPECT_TRUE(
            contents.find("StorageBufferAccess::ReadOnly") != std::string::npos ||
            contents.find("StorageBufferAccess::ReadWrite") != std::string::npos);
        EXPECT_NE(contents.find("createBindGroupLayout"), std::string::npos);
        EXPECT_NE(contents.find("createBindGroup"), std::string::npos);
        EXPECT_NE(contents.find("buffer0"), std::string::npos);
    }

    TEST(DslStorageCodegenTests, EmitsStorageBufferStageVisibility)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_NE(contents.find("ShaderStage::Fragment"), std::string::npos);
        EXPECT_NE(contents.find("BufferBindingType::Storage"), std::string::npos);
        EXPECT_TRUE(
            contents.find("StorageBufferAccess::ReadOnly") != std::string::npos ||
            contents.find("StorageBufferAccess::ReadWrite") != std::string::npos);
        EXPECT_NE(contents.find("TriangleRenderPipeline"), std::string::npos);
    }
} // namespace
