#include <gtest/gtest.h>
#include <CodeGen/SPIRVEmitter/SPIRVValidation.hpp>
#include <spirv-tools/libspirv.hpp>

using namespace UGLC::CodeGen::SPIRVEmitter;

namespace
{
    /** Assembles an independent minimal compute module for validator and optimizer boundary tests. */
    std::vector<uint32_t> assembleValidComputeModule()
    {
        spvtools::SpirvTools assembler(SPV_ENV_VULKAN_1_2);
        std::vector<uint32_t> words;
        const char *assembly = R"(
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
%void = OpTypeVoid
%function = OpTypeFunction %void
%main = OpFunction %void None %function
%entry = OpLabel
OpReturn
OpFunctionEnd
)";
        if (!assembler.Assemble(assembly, &words)) { return {}; }
        return words;
    }

    TEST(UGLCSPIRVValidation, ValidatesActualOptimizedWords)
    {
        const auto words = assembleValidComputeModule();
        ASSERT_FALSE(words.empty());
        ASSERT_TRUE(validateAndDisassembleSPIRVWords(words).valid);
        const auto optimized = optimizeSPIRVWordsForRuntime(words);
        ASSERT_TRUE(optimized.optimized) << optimized.diagnostics;
        const auto finalValidation = validateAndDisassembleSPIRVWords(optimized.words);
        EXPECT_TRUE(finalValidation.valid) << finalValidation.diagnostics;
        EXPECT_FALSE(finalValidation.disassembly.empty());
    }

    TEST(UGLCSPIRVValidation, RejectsCorruptionAfterSuccessfulOptimization)
    {
        auto optimized = optimizeSPIRVWordsForRuntime(assembleValidComputeModule());
        ASSERT_TRUE(optimized.optimized);
        ASSERT_FALSE(optimized.words.empty());
        optimized.words.back() = 0u;
        const auto finalValidation = validateAndDisassembleSPIRVWords(optimized.words);
        EXPECT_FALSE(finalValidation.valid);
        EXPECT_FALSE(finalValidation.diagnostics.empty());
    }

    TEST(UGLCSPIRVValidation, RejectsInvalidOptimizerInputAndDisassembly)
    {
        const std::vector<uint32_t> invalid = {0x07230203u, 0x00010300u, 0u, 1u, 0u, 0u};
        EXPECT_FALSE(optimizeSPIRVWordsForRuntime(invalid).optimized);
        EXPECT_FALSE(validateAndDisassembleSPIRVWords(invalid).valid);
        EXPECT_FALSE(disassembleSPIRVWords(invalid).disassembled);
    }
}
