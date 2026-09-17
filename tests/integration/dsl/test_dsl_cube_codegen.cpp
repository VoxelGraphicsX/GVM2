#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    /** Returns true when a SPIR-V disassembly declares a 2D storage image with the requested image format token. */
    bool containsTwoDimensionalStorageImageFormat(const std::string &spirvDisassembly, const std::string &formatToken)
    {
        std::istringstream lineStream(spirvDisassembly);
        std::string line;
        while (std::getline(lineStream, line))
        {
            if (line.find("OpTypeImage") == std::string::npos)
            {
                continue;
            }

            std::istringstream tokenStream(line);
            std::vector<std::string> tokens;
            std::string token;
            while (tokenStream >> token)
            {
                tokens.push_back(token);
            }

            if (tokens.size() >= 10u &&
                tokens[2] == "OpTypeImage" &&
                tokens[4] == "2D" &&
                tokens[tokens.size() - 2u] == "2" &&
                tokens.back() == formatToken)
            {
                return true;
            }
        }
        return false;
    }

    TEST(DslCubeCodegenTests, EmitsMultiPassRendererContracts)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto dslSource = GVM::Tests::requireEnvPath("GVM_TEST_DSL_SOURCE");

        const auto generatedHeader = generatedDir / "generate_result.hpp";
        const auto stampFile = generatedDir / "temp_output.abc";

        EXPECT_TRUE(std::filesystem::exists(dslSource));
        ASSERT_TRUE(std::filesystem::exists(stampFile));
        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_FALSE(contents.empty());
        EXPECT_NE(contents.find("CubeDraw"), std::string::npos);
        EXPECT_NE(contents.find("CheckerBoardBackground"), std::string::npos);
        EXPECT_NE(contents.find("Composite"), std::string::npos);
        EXPECT_NE(contents.find("createRenderPipeline"), std::string::npos);
        EXPECT_NE(contents.find("createComputePipeline"), std::string::npos);
        EXPECT_NE(contents.find("createTexture"), std::string::npos);
    }

    TEST(DslCubeCodegenTests, EmitsIntermediateTexturesAndPerPassClasses)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_NE(contents.find("DepthTexture"), std::string::npos);
        EXPECT_NE(contents.find("CompositeRenderPipeline"), std::string::npos);
        EXPECT_NE(contents.find("CheckerBoardBackground"), std::string::npos);
        EXPECT_NE(contents.find("CubeDrawRenderPipeline"), std::string::npos);
        EXPECT_NE(contents.find("GBufferAlbedo"), std::string::npos);
    }

    TEST(DslCubeCodegenTests, UsesComputeVisibilityForCheckerboardStorageTextures)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        const auto bindGroupMarker = contents.find("CheckBoardGBufferBindGroupBindGroupLayout");
        ASSERT_NE(bindGroupMarker, std::string::npos);

        const auto relevantWindow = contents.substr(bindGroupMarker, 1200);
        EXPECT_NE(relevantWindow.find("layoutEntry[0].visibility = GVM::RHI::ShaderStage::Compute;"), std::string::npos);
        EXPECT_NE(relevantWindow.find("layoutEntry[1].visibility = GVM::RHI::ShaderStage::Compute;"), std::string::npos);
    }

    TEST(DslCubeCodegenTests, CameraBindGroupVisibilityCoversVertexAndComputeUsage)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        const auto bindGroupMarker = contents.find("CameraBindGroupBindGroupLayout");
        ASSERT_NE(bindGroupMarker, std::string::npos);

        const auto relevantWindow = contents.substr(bindGroupMarker, 1000);
        EXPECT_NE(relevantWindow.find("GVM::RHI::ShaderStage::Vertex"), std::string::npos);
        EXPECT_NE(relevantWindow.find("GVM::RHI::ShaderStage::Compute"), std::string::npos);
    }

    TEST(DslCubeCodegenTests, UsesHalfWritesForUnormStorageTextures)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_EQ(contents.find("albedoTexture.write(float4("), std::string::npos);
        EXPECT_EQ(contents.find("albedoTexture.write(finalAlbedo"), std::string::npos);
        EXPECT_TRUE(contents.find("albedoTexture.write(half4(col.x, col.y, col.z, 1.0f)") != std::string::npos ||
                    contents.find("albedoTexture.write(half4(half(col.x), half(col.y), half(col.z), half(1.0f))") != std::string::npos);
        EXPECT_TRUE(contents.find("albedoTexture.write(half4(finalAlbedo)") != std::string::npos ||
                    contents.find("albedoTexture.write(half4(half(finalAlbedo.x), half(finalAlbedo.y), half(finalAlbedo.z), half(finalAlbedo.w))") !=
                        std::string::npos);
    }

    TEST(DslCubeCodegenTests, CastsUnormTextureReadsBeforeFloat4Initialization)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_EQ(contents.find("float4 mainGBufferAlbedo = mainGBufferBindGroup->albedoTexture.read"), std::string::npos);
        EXPECT_EQ(contents.find("float4 checkerBoardAlbedo = checkerBoardGBufferBindGroup->albedoTexture.read"), std::string::npos);
        EXPECT_TRUE(contents.find("float4 mainGBufferAlbedo = float4(mainGBufferBindGroup->albedoTexture.read") != std::string::npos ||
                    contents.find("float4 mainGBufferAlbedo = float4(mainGBufferBindGroup_albedoTexture.read") != std::string::npos ||
                    contents.find("float4 mainGBufferAlbedo = float4(half4(mainGBufferBindGroup_albedoTexture.read") != std::string::npos ||
                    contents.find("float4 mainGBufferAlbedo = float4(float(mainGBufferBindGroup->albedoTexture.read") != std::string::npos ||
                    contents.find("float4 mainGBufferAlbedo = float4(float(mainGBufferBindGroup_albedoTexture.read") != std::string::npos);
        EXPECT_TRUE(contents.find("float4 checkerBoardAlbedo = float4(checkerBoardGBufferBindGroup->albedoTexture.read") != std::string::npos ||
                    contents.find("float4 checkerBoardAlbedo = float4(checkerBoardGBufferBindGroup_albedoTexture.read") != std::string::npos ||
                    contents.find("float4 checkerBoardAlbedo = float4(half4(checkerBoardGBufferBindGroup_albedoTexture.read") != std::string::npos ||
                    contents.find("float4 checkerBoardAlbedo = float4(float(checkerBoardGBufferBindGroup->albedoTexture.read") != std::string::npos ||
                    contents.find("float4 checkerBoardAlbedo = float4(float(checkerBoardGBufferBindGroup_albedoTexture.read") != std::string::npos);
    }

    TEST(DslCubeCodegenTests, UGLIREmitsCheckerboardSemanticsAndDisablesHLSL)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto mslDir = generatedDir / "msl";
        if (!std::filesystem::exists(mslDir))
        {
            GTEST_SKIP() << "Experimental UGLIR MSL debug artifacts are not available for this pipeline.";
        }

        const auto checkerMSL = GVM::Tests::readTextFile(mslDir / "CheckerBoardBackground.msl");
        EXPECT_NE(checkerMSL.find("thread float3&"), std::string::npos);
        EXPECT_NE(checkerMSL.find("thread float&"), std::string::npos);

        const auto hlslDir = generatedDir / "hlsl";
        EXPECT_FALSE(std::filesystem::exists(hlslDir));

        const auto generatedHeader = GVM::Tests::readTextFile(generatedDir / "generate_result.hpp");
        EXPECT_NE(generatedHeader.find("eastl::string{},"), std::string::npos);

        const auto uglirDump = GVM::Tests::readTextFile(generatedDir / "uglir" / "CheckerBoardBackground.uglir.txt");
        size_t outParameterCount = 0;
        size_t searchOffset = 0;
        const std::string outParameterMarker = "passing_mode out reference true const_reference false";
        while ((searchOffset = uglirDump.find(outParameterMarker, searchOffset)) != std::string::npos)
        {
            ++outParameterCount;
            searchOffset += outParameterMarker.size();
        }
        EXPECT_GE(outParameterCount, 4u);

        const auto checkerSPV = GVM::Tests::readTextFile(generatedDir / "spv" / "CheckerBoardBackground.spvasm");
        EXPECT_NE(checkerSPV.find("OpImageWrite"), std::string::npos);
        EXPECT_TRUE(containsTwoDimensionalStorageImageFormat(checkerSPV, "Rgba8"));
        EXPECT_TRUE(containsTwoDimensionalStorageImageFormat(checkerSPV, "R32f"));
        EXPECT_TRUE(std::filesystem::exists(generatedDir / "spv" / "CheckerBoardBackground.raw.spv.txt"));
        EXPECT_TRUE(std::filesystem::exists(generatedDir / "spv" / "CheckerBoardBackground.raw.spvasm"));

        const auto compositeSPV = GVM::Tests::readTextFile(generatedDir / "spv" / "Composite.spvasm");
        EXPECT_TRUE(containsTwoDimensionalStorageImageFormat(compositeSPV, "R32f"));
        EXPECT_NE(compositeSPV.find("OpImageFetch %v4float"), std::string::npos);
        EXPECT_NE(compositeSPV.find("OpImageRead %v4float"), std::string::npos);

        const auto cubeFragmentSPV = GVM::Tests::readTextFile(generatedDir / "spv" / "CubeDraw__fragment.spvasm");
        EXPECT_NE(cubeFragmentSPV.find("%_ptr_Output_v4float = OpTypePointer Output %v4float"), std::string::npos);
        EXPECT_EQ(cubeFragmentSPV.find("%_ptr_Output_v4half = OpTypePointer Output %v4half"), std::string::npos);
    }
} // namespace
