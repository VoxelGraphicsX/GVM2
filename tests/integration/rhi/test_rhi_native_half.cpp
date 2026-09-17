#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMShaderReadbackTest.hpp"
#include "generate_result.hpp"

#include <cstdint>
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{
    constexpr uint32_t kSpirvMagicNumber = 0x07230203u;
    constexpr uint32_t kOpCapability = 17u;
    constexpr uint32_t kOpTypeFloat = 22u;
    constexpr uint32_t kCapabilityFloat16 = 9u;

    std::vector<uint32_t> readGeneratedComputeSpirv()
    {
        const std::filesystem::path generatedHeader =
            GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR") / "generate_result.hpp";
        const std::string contents = GVM::Tests::readTextFile(generatedHeader);
        const std::string marker = "static constexpr uint32_t computeShaderArtifact_SpirvWords[] = {";
        const size_t begin = contents.find(marker);
        if (begin == std::string::npos)
        {
            throw std::runtime_error("Generated native-half fixture did not contain computeShaderArtifact_SpirvWords.");
        }
        const size_t end = contents.find("\n    };", begin);
        if (end == std::string::npos)
        {
            throw std::runtime_error("Generated native-half fixture had an unterminated computeShaderArtifact_SpirvWords array.");
        }

        const std::string arrayText = contents.substr(begin, end - begin);
        const std::regex wordPattern("0x([0-9A-Fa-f]+)u?");
        std::vector<uint32_t> words;
        for (auto iter = std::sregex_iterator(arrayText.begin(), arrayText.end(), wordPattern);
             iter != std::sregex_iterator();
             ++iter)
        {
            words.push_back(static_cast<uint32_t>(std::stoul((*iter)[1].str(), nullptr, 16)));
        }
        return words;
    }

    template <typename Predicate>
    bool spirvHasInstruction(const std::vector<uint32_t> &words, uint32_t opcode, Predicate predicate)
    {
        if (words.size() < 5u || words[0] != kSpirvMagicNumber)
        {
            return false;
        }

        for (size_t offset = 5u; offset < words.size();)
        {
            const uint32_t instruction = words[offset];
            const uint32_t wordCount = instruction >> 16u;
            const uint32_t currentOpcode = instruction & 0xffffu;
            if (wordCount == 0u || offset + wordCount > words.size())
            {
                return false;
            }

            if (currentOpcode == opcode && predicate(words, offset, wordCount))
            {
                return true;
            }
            offset += wordCount;
        }

        return false;
    }

    bool spirvHasCapability(const std::vector<uint32_t> &words, uint32_t capability)
    {
        return spirvHasInstruction(words, kOpCapability, [capability](const auto &moduleWords, size_t offset, uint32_t wordCount) {
            return wordCount >= 2u && moduleWords[offset + 1u] == capability;
        });
    }

    bool spirvHasTypeFloatWidth(const std::vector<uint32_t> &words, uint32_t width)
    {
        return spirvHasInstruction(words, kOpTypeFloat, [width](const auto &moduleWords, size_t offset, uint32_t wordCount) {
            return wordCount >= 3u && moduleWords[offset + 2u] == width;
        });
    }

    /** Uses the explicit backend and device lifecycle shared by compiler readback fixtures. */
    using RhiNativeHalfTest = GVM::Tests::ShaderReadbackTest;

    TEST_F(RhiNativeHalfTest, DispatchesDslNativeHalfComputeThroughShaderArtifact)
    {
        const std::vector<uint32_t> generatedSpirv = readGeneratedComputeSpirv();
        ASSERT_TRUE(spirvHasCapability(generatedSpirv, kCapabilityFloat16));
        ASSERT_TRUE(spirvHasTypeFloatWidth(generatedSpirv, 16u));
        ASSERT_TRUE(spirvHasTypeFloatWidth(generatedSpirv, 32u));

        constexpr uint64_t kByteSize = sizeof(uint32_t) * 4u;
        auto outputBuffer = device->createBuffer({
            .label = "NativeHalfOutput",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc,
            .size = kByteSize,
        });
        ASSERT_FALSE(outputBuffer.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto bindGroup = deviceProxy->createBindGroup<NativeHalfComputeBindGroup>(GVM::RHI::BufferRange(outputBuffer, 0u, kByteSize));
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        auto nativeHalfPass = deviceProxy->createComputeClass<NativeHalfComputePass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(nativeHalfPass));

        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        uint32_t output[4] = {};
        queue->computePass("CompilerShaderReadback", nativeHalfPass->run(4u, 1u, 1u))->submit();
        queue->readBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, kByteSize), output, sizeof(output))
            ->submit();
        EXPECT_EQ(output[0], 52u);
        EXPECT_EQ(output[1], 84u);
        EXPECT_EQ(output[2], 116u);
        EXPECT_EQ(output[3], 148u);


        device->freeBuffer(outputBuffer);
    }
} // namespace
