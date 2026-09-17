#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>

#include <filesystem>

namespace
{
    TEST(DslRenderFeatureCodegenTests, AvoidsNegativeBindGroupLayoutSentinelForTrianglePipelines)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_EQ(contents.find("bindGroupLayouts(-1)"), std::string::npos);
        EXPECT_NE(contents.find("FeatureGlobalsBindGroupBindGroupLayout"), std::string::npos);
        EXPECT_NE(contents.find("createFeatureGlobalsBindGroup"), std::string::npos);
    }

    TEST(DslRenderFeatureCodegenTests, EmitsSharedGlobalsBindGroupForProceduralAndBufferedModes)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        const auto proceduralMarker = contents.find("ProceduralTrianglePassPipelineLayout");
        ASSERT_NE(proceduralMarker, std::string::npos);
        const auto proceduralWindow = contents.substr(proceduralMarker - 300, 700);
        EXPECT_TRUE(
            proceduralWindow.find("bindGroupLayouts(1)") != std::string::npos ||
            proceduralWindow.find("bindGroupLayouts.resize(1)") != std::string::npos);
        EXPECT_NE(proceduralWindow.find("globalsBindGroup->mBindGroupLayout"), std::string::npos);

        const auto bufferedMarker = contents.find("BufferedTrianglePassPipelineLayout");
        ASSERT_NE(bufferedMarker, std::string::npos);
        const auto bufferedWindow = contents.substr(bufferedMarker - 300, 700);
        EXPECT_TRUE(
            bufferedWindow.find("bindGroupLayouts(1)") != std::string::npos ||
            bufferedWindow.find("bindGroupLayouts.resize(1)") != std::string::npos);
        EXPECT_NE(bufferedWindow.find("globalsBindGroup->mBindGroupLayout"), std::string::npos);
    }

    TEST(DslRenderFeatureCodegenTests, UsesHalfWritesForUnormStorageTextures)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_EQ(contents.find("patternTexture.write(float4("), std::string::npos);
        EXPECT_EQ(contents.find("albedoTexture.write(float4("), std::string::npos);
        EXPECT_EQ(contents.find("albedoTexture.write(finalAlbedo"), std::string::npos);
        EXPECT_TRUE(contents.find("patternTexture.write(half4(color.x, color.y, color.z, 1.0f)") != std::string::npos ||
                    contents.find("patternTexture.write(half4(half(color.x), half(color.y), half(color.z), half(1.0f))") != std::string::npos);
        EXPECT_TRUE(contents.find("albedoTexture.write(half4(col.x, col.y, col.z, 1.0f)") != std::string::npos ||
                    contents.find("albedoTexture.write(half4(half(col.x), half(col.y), half(col.z), half(1.0f))") != std::string::npos);
        EXPECT_TRUE(contents.find("albedoTexture.write(half4(finalAlbedo)") != std::string::npos ||
                    contents.find("albedoTexture.write(half4(half(finalAlbedo.x), half(finalAlbedo.y), half(finalAlbedo.z), half(finalAlbedo.w))") !=
                        std::string::npos);
    }

    TEST(DslRenderFeatureCodegenTests, CastsUnormTextureReadsBeforeFloat4Initialization)
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

    TEST(DslRenderFeatureCodegenTests, UGLIRUsesLegacyMetalPixelLocalABI)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto mslDir = generatedDir / "msl";
        if (!std::filesystem::exists(mslDir))
        {
            GTEST_SKIP() << "Experimental UGLIR MSL debug artifacts are not available for this pipeline.";
        }

        const auto gbufferMSL = GVM::Tests::readTextFile(mslDir / "FeaturePixelLocalGBufferPass__fragment.msl");
        const auto lightingMSL = GVM::Tests::readTextFile(mslDir / "FeaturePixelLocalLightingPass__pixel.msl");
        const auto tonemapMSL = GVM::Tests::readTextFile(mslDir / "FeaturePixelLocalTonemapPass__pixel.msl");
        const auto compositeMSL = GVM::Tests::readTextFile(mslDir / "Composite.msl");
        const auto cubeVertexMSL = GVM::Tests::readTextFile(mslDir / "CubeDraw__vertex.msl");
        const auto texturedFragmentMSL = GVM::Tests::readTextFile(mslDir / "TexturedTrianglePass__fragment.msl");
        const auto bufferedVertexMSL = GVM::Tests::readTextFile(mslDir / "BufferedTrianglePass__vertex.msl");

        EXPECT_NE(cubeVertexMSL.find("const constant CameraBindGroup* camBindGroup [[buffer(1)]]"), std::string::npos);
        EXPECT_NE(texturedFragmentMSL.find("const constant TexturedTriangleBindGroup* bindGroup [[buffer(1)]]"), std::string::npos);
        EXPECT_NE(bufferedVertexMSL.find("const constant FeatureGlobalsBindGroup* globalsBindGroup [[buffer(1)]]"), std::string::npos);
        EXPECT_NE(cubeVertexMSL.find("__REVERSED_VERTEX__OUTPUT__"), std::string::npos);
        EXPECT_NE(bufferedVertexMSL.find("__REVERSED_VERTEX__OUTPUT__"), std::string::npos);

        EXPECT_EQ(lightingMSL.find("FeaturePixelLocalFrameBuffer inputValue [[stage_in]]"), std::string::npos);
        EXPECT_EQ(tonemapMSL.find("FeaturePixelLocalFrameBuffer inputValue [[stage_in]]"), std::string::npos);

        EXPECT_NE(lightingMSL.find("half4 _UGLC_PixelLocalInput_inputValue_gbuffer [[color(0)]]"), std::string::npos);
        EXPECT_NE(lightingMSL.find("float _UGLC_PixelLocalInput_inputValue_gbufferDepth [[color(1)]]"), std::string::npos);
        EXPECT_NE(lightingMSL.find("FeaturePixelLocalFrameBuffer_PixelLocalInput inputValue;"), std::string::npos);
        EXPECT_NE(lightingMSL.find("inputValue.gbuffer = _UGLC_PixelLocalInput_inputValue_gbuffer;"), std::string::npos);
        EXPECT_NE(lightingMSL.find("inputValue.gbufferDepth = _UGLC_PixelLocalInput_inputValue_gbufferDepth;"), std::string::npos);

        EXPECT_NE(tonemapMSL.find("half4 _UGLC_PixelLocalInput_inputValue_lighting [[color(2)]]"), std::string::npos);
        EXPECT_NE(tonemapMSL.find("FeaturePixelLocalFrameBuffer_PixelLocalInput inputValue;"), std::string::npos);
        EXPECT_NE(tonemapMSL.find("inputValue.lighting = _UGLC_PixelLocalInput_inputValue_lighting;"), std::string::npos);

        EXPECT_EQ(gbufferMSL.find("[[depth(any)]]"), std::string::npos);
        EXPECT_EQ(lightingMSL.find("[[depth(any)]]"), std::string::npos);
        EXPECT_EQ(tonemapMSL.find("[[depth(any)]]"), std::string::npos);
        EXPECT_EQ(gbufferMSL.find("half4 lighting [[color(2)]]"), std::string::npos);
        EXPECT_EQ(gbufferMSL.find("half4 present [[color(3)]]"), std::string::npos);
        EXPECT_EQ(lightingMSL.find("half4 gbuffer [[color(0)]]"), std::string::npos);
        EXPECT_EQ(lightingMSL.find("float gbufferDepth [[color(1)]]"), std::string::npos);
        EXPECT_EQ(lightingMSL.find("half4 present [[color(3)]]"), std::string::npos);
        EXPECT_EQ(tonemapMSL.find("half4 gbuffer [[color(0)]]"), std::string::npos);
        EXPECT_EQ(tonemapMSL.find("float gbufferDepth [[color(1)]]"), std::string::npos);
        EXPECT_EQ(tonemapMSL.find("half4 lighting [[color(2)]]"), std::string::npos);

        EXPECT_NE(compositeMSL.find("texture2d<half> albedoTexture [[id(0)]]"), std::string::npos);
        EXPECT_NE(compositeMSL.find("mainGBufferBindGroup->albedoTexture.read(uint2(ThreadID.xy), uint(0))"), std::string::npos);
        EXPECT_NE(compositeMSL.find("checkerBoardGBufferBindGroup->albedoTexture.read(uint2(ThreadID.xy), uint(0))"), std::string::npos);

        const auto hlslDir = generatedDir / "hlsl";
        EXPECT_FALSE(std::filesystem::exists(hlslDir));

        const auto spvDir = generatedDir / "spv";
        if (std::filesystem::exists(spvDir))
        {
            const auto gbufferSPV = GVM::Tests::readTextFile(spvDir / "FeaturePixelLocalGBufferPass__fragment.raw.spvasm");
            const auto lightingSPV = GVM::Tests::readTextFile(spvDir / "FeaturePixelLocalLightingPass__pixel.raw.spvasm");
            const auto tonemapSPV = GVM::Tests::readTextFile(spvDir / "FeaturePixelLocalTonemapPass__pixel.raw.spvasm");
            EXPECT_NE(gbufferSPV.find("OpDecorate %gbuffer Location 0"), std::string::npos);
            EXPECT_NE(gbufferSPV.find("OpDecorate %gbufferDepth Location 1"), std::string::npos);
            EXPECT_EQ(gbufferSPV.find("OpDecorate %lighting Location 2"), std::string::npos);
            EXPECT_EQ(gbufferSPV.find("OpDecorate %present Location 3"), std::string::npos);
            EXPECT_NE(lightingSPV.find("OpDecorate %lighting Location 2"), std::string::npos);
            EXPECT_EQ(lightingSPV.find("OpDecorate %gbuffer Location 0"), std::string::npos);
            EXPECT_EQ(lightingSPV.find("OpDecorate %gbufferDepth Location 1"), std::string::npos);
            EXPECT_EQ(lightingSPV.find("OpDecorate %present Location 3"), std::string::npos);
            EXPECT_NE(tonemapSPV.find("OpDecorate %present Location 3"), std::string::npos);
            EXPECT_EQ(tonemapSPV.find("OpDecorate %gbuffer Location 0"), std::string::npos);
            EXPECT_EQ(tonemapSPV.find("OpDecorate %gbufferDepth Location 1"), std::string::npos);
            EXPECT_EQ(tonemapSPV.find("OpDecorate %lighting Location 2"), std::string::npos);
        }
    }

    TEST(DslRenderFeatureCodegenTests, UsesRenderToSwapchainInsteadOfSwapchainFormatSpecificQuadPipelines)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_NE(contents.find("renderToSwapchain("), std::string::npos);
        EXPECT_EQ(contents.find("getPreferredFormat()"), std::string::npos);
        EXPECT_EQ(contents.find("GVM::RHI::isBGRA8Format"), std::string::npos);
        EXPECT_EQ(contents.find("createRenderClass<QuadBGRA>"), std::string::npos);
        EXPECT_NE(contents.find("FeaturePresentTexture"), std::string::npos);
    }
} // namespace
