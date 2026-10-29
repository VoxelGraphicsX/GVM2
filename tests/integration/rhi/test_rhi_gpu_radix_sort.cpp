#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>
#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "generate_result.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
    constexpr uint32_t kHashSeed = 0x12345678u;
    constexpr uint32_t kPayloadSalt = 0x9e3779b9u;
    constexpr uint32_t kDuplicateKeyMask = (1u << 20) - 1u;
    constexpr uint32_t kPatternKeyMultiplier = 17u;
    constexpr uint32_t kPatternKeyBias = 5u;
    constexpr uint32_t kPatternPayloadSalt = 0x6a09e667u;
    constexpr auto kCompletionTimeout = std::chrono::seconds(2);

    uint32_t readEnvUint(const char *name, uint32_t defaultValue)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return defaultValue;
        }

        const long long parsed = std::atoll(value);
        if (parsed <= 0)
        {
            return defaultValue;
        }
        return static_cast<uint32_t>(parsed);
    }

    bool isVerboseEnabled()
    {
        const char *value = std::getenv("GVM_TEST_GPU_RADIX_SORT_VERBOSE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

    uint32_t mixKey(uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }

    std::vector<OneSweepSortTest::SortEntry> makeDeterministicInput(uint32_t count)
    {
        std::vector<OneSweepSortTest::SortEntry> entries(count);
        for (uint32_t index = 0u; index < count; ++index)
        {
            entries[index].sortKey = mixKey(index ^ kHashSeed) & kDuplicateKeyMask;
            entries[index].payload = index ^ kPayloadSalt;
        }
        return entries;
    }

    std::vector<OneSweepSortTest::SortEntry> makeSmallFixedInput()
    {
        return {
            {.sortKey = 7u, .payload = 0u},
            {.sortKey = 3u, .payload = 1u},
            {.sortKey = 7u, .payload = 2u},
            {.sortKey = 1u, .payload = 3u},
            {.sortKey = 3u, .payload = 4u},
            {.sortKey = 5u, .payload = 5u},
            {.sortKey = 1u, .payload = 6u},
            {.sortKey = 9u, .payload = 7u},
            {.sortKey = 5u, .payload = 8u},
            {.sortKey = 3u, .payload = 9u},
            {.sortKey = 1u, .payload = 10u},
            {.sortKey = 7u, .payload = 11u},
            {.sortKey = 0u, .payload = 12u},
            {.sortKey = 9u, .payload = 13u},
            {.sortKey = 0u, .payload = 14u},
            {.sortKey = 5u, .payload = 15u},
        };
    }

    std::vector<OneSweepSortTest::SortEntry> makePatternReference(uint32_t count)
    {
        std::vector<OneSweepSortTest::SortEntry> entries(count);
        for (uint32_t index = 0u; index < count; ++index)
        {
            entries[index].sortKey = index * kPatternKeyMultiplier + kPatternKeyBias;
            entries[index].payload = index ^ kPatternPayloadSalt;
        }
        return entries;
    }

    std::vector<OneSweepSortTest::SortEntry> makeCpuReference(std::vector<OneSweepSortTest::SortEntry> entries)
    {
        std::stable_sort(entries.begin(), entries.end(), [](const OneSweepSortTest::SortEntry &lhs, const OneSweepSortTest::SortEntry &rhs) {
            return lhs.sortKey < rhs.sortKey;
        });
        return entries;
    }

    uint32_t computeActiveRadixPassCount(const std::vector<OneSweepSortTest::SortEntry> &entries)
    {
        uint32_t maxKey = 0u;
        for (const auto &entry : entries)
        {
            maxKey = std::max(maxKey, entry.sortKey);
        }

        uint32_t activePassCount = 1u;
        while (activePassCount < OneSweepSortTest::RadixPassCount)
        {
            const uint32_t coveredBits = activePassCount * OneSweepSortTest::RadixPassBits;
            if (coveredBits >= 32u)
            {
                break;
            }
            if ((maxKey >> coveredBits) == 0u)
            {
                break;
            }
            ++activePassCount;
        }
        return activePassCount;
    }

    void expectEntriesEqual(
        const std::vector<OneSweepSortTest::SortEntry> &actual,
        const std::vector<OneSweepSortTest::SortEntry> &expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (size_t index = 0u; index < expected.size(); ++index)
        {
            ASSERT_EQ(actual[index].sortKey, expected[index].sortKey) << "sort key mismatch at index " << index;
            ASSERT_EQ(actual[index].payload, expected[index].payload) << "payload mismatch at index " << index;
        }
    }

    class GpuOneSweepSortTest : public ::testing::Test
    {
    protected:
        struct CompletionResources
        {
            GVM::RHI::Buffer buffer;
            eastl::intrusive_ptr<OneSweepSortTest::CompletionBindGroup> bindGroup;
            eastl::intrusive_ptr<OneSweepSortTest::SignalCompletionPass> signalPass;
        };

        struct PatternResources
        {
            uint32_t elementCount = 0u;
            GVM::RHI::Buffer outputBuffer;
            GVM::RHI::Buffer readbackBuffer;
            GVM::RHI::Buffer globalsBuffer;
            CompletionResources completion;

            eastl::intrusive_ptr<OneSweepSortTest::WritePatternGlobalsBindGroup> globalsBindGroup;
            eastl::intrusive_ptr<OneSweepSortTest::OutputEntriesBindGroup> outputBindGroup;
            eastl::intrusive_ptr<OneSweepSortTest::WritePatternPass> writePatternPass;
        };

        struct SortResources
        {
            uint32_t elementCount = 0u;
            uint32_t partitionCount = 0u;
            uint32_t activePassCount = OneSweepSortTest::RadixPassCount;

            GVM::RHI::Buffer inputBuffer;
            GVM::RHI::Buffer scratchBuffer;
            GVM::RHI::Buffer readbackBuffer;
            CompletionResources completion;

            std::vector<GVM::RHI::Buffer> globalsBuffers;
            std::vector<GVM::RHI::Buffer> prefixDataBuffers;
            GVM::RHI::Buffer bucketBaseBuffer;

            std::vector<eastl::intrusive_ptr<OneSweepSortTest::SortGlobalsBindGroup>> globalsBindGroups;
            std::vector<eastl::intrusive_ptr<OneSweepSortTest::PrefixDataBindGroup>> prefixDataBindGroups;
            eastl::intrusive_ptr<OneSweepSortTest::BucketBaseBindGroup> bucketBaseBindGroup;

            eastl::intrusive_ptr<OneSweepSortTest::EntryPairBindGroup> pingToPongBindGroup;
            eastl::intrusive_ptr<OneSweepSortTest::EntryPairBindGroup> pongToPingBindGroup;
            std::vector<eastl::intrusive_ptr<OneSweepSortTest::OneSweepPrefixPass>> prefixPingToPongPasses;
            std::vector<eastl::intrusive_ptr<OneSweepSortTest::OneSweepPrefixPass>> prefixPongToPingPasses;
            std::vector<eastl::intrusive_ptr<OneSweepSortTest::OneSweepResolveOffsetsPass>> resolveOffsetsPasses;
            std::vector<eastl::intrusive_ptr<OneSweepSortTest::OneSweepScatterPass>> scatterPingToPongPasses;
            std::vector<eastl::intrusive_ptr<OneSweepSortTest::OneSweepScatterPass>> scatterPongToPingPasses;
        };

        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);

            rawDevice = instance->createDevice();
            ASSERT_NE(rawDevice, nullptr);
            ASSERT_NE(rawDevice->getMainQueue(), nullptr);

            device = GVM::Core::DeviceProxy(rawDevice);
        }

        void TearDown() override
        {
            if (instance != nullptr)
            {
                GVM::RHI::destroyInstance(instance);
                instance = nullptr;
            }
            device = {};
            rawDevice = nullptr;
        }

        CompletionResources createCompletionResources(const char *labelPrefix)
        {
            CompletionResources resources = {};
            resources.buffer = device->createBuffer({
                .label = eastl::string(labelPrefix) + "_CompletionBuffer",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead,
                .size = sizeof(uint32_t),
            });
            EXPECT_FALSE(resources.buffer.isNull());
            if (resources.buffer.isNull())
            {
                return resources;
            }

            resources.bindGroup = device->createBindGroup<OneSweepSortTest::CompletionBindGroup>(GVM::RHI::BufferRange(resources.buffer));
            resources.signalPass = device->createComputeClass<OneSweepSortTest::SignalCompletionPass>(resources.bindGroup);
            EXPECT_TRUE(static_cast<bool>(resources.bindGroup));
            EXPECT_TRUE(static_cast<bool>(resources.signalPass));
            return resources;
        }

        PatternResources createPatternResources(uint32_t elementCount, bool directMappedOutput)
        {
            PatternResources resources = {};
            resources.elementCount = std::max(elementCount, 1u);
            const uint64_t entryBytes = uint64_t(resources.elementCount) * sizeof(OneSweepSortTest::SortEntry);

            GVM::RHI::BufferUsageFlags outputUsage = GVM::RHI::BufferUsage::Storage;
            if (directMappedOutput)
            {
                outputUsage = outputUsage | GVM::RHI::BufferUsage::MapRead;
            }
            else
            {
                outputUsage = outputUsage | GVM::RHI::BufferUsage::CopySrc;
            }

            resources.outputBuffer = device->createBuffer({
                .label = directMappedOutput ? "PatternOutputMapped" : "PatternOutputStorage",
                .usage = outputUsage,
                .size = entryBytes,
            });
            EXPECT_FALSE(resources.outputBuffer.isNull());

            if (!directMappedOutput)
            {
                resources.readbackBuffer = device->createBuffer({
                    .label = "PatternReadbackBuffer",
                    .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead,
                    .size = entryBytes,
                });
                EXPECT_FALSE(resources.readbackBuffer.isNull());
            }

            resources.globalsBuffer = device->createBuffer({
                .label = "PatternGlobalsBuffer",
                .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::MapWrite,
                .size = sizeof(OneSweepSortTest::WritePatternGlobals),
            });
            EXPECT_FALSE(resources.globalsBuffer.isNull());
            resources.completion = createCompletionResources("Pattern");

            if (resources.outputBuffer.isNull() || resources.globalsBuffer.isNull() || resources.completion.buffer.isNull())
            {
                return resources;
            }
            if (!directMappedOutput && resources.readbackBuffer.isNull())
            {
                return resources;
            }

            OneSweepSortTest::WritePatternGlobals globals = {};
            globals.elementCount = resources.elementCount;
            globals.keyMultiplier = kPatternKeyMultiplier;
            globals.keyBias = kPatternKeyBias;
            globals.payloadSalt = kPatternPayloadSalt;
            resources.globalsBuffer->map();
            auto *mappedGlobals = static_cast<OneSweepSortTest::WritePatternGlobals *>(
                resources.globalsBuffer->getMappedRange(0u, sizeof(OneSweepSortTest::WritePatternGlobals)));
            EXPECT_NE(mappedGlobals, nullptr);
            if (mappedGlobals == nullptr)
            {
                resources.globalsBuffer->unmap();
                return resources;
            }
            *mappedGlobals = globals;
            resources.globalsBuffer->unmap();

            resources.globalsBindGroup =
                device->createBindGroup<OneSweepSortTest::WritePatternGlobalsBindGroup>(GVM::RHI::BufferRange(resources.globalsBuffer));
            resources.outputBindGroup =
                device->createBindGroup<OneSweepSortTest::OutputEntriesBindGroup>(GVM::RHI::BufferRange(resources.outputBuffer));
            resources.writePatternPass =
                device->createComputeClass<OneSweepSortTest::WritePatternPass>(resources.globalsBindGroup, resources.outputBindGroup);

            EXPECT_TRUE(static_cast<bool>(resources.globalsBindGroup));
            EXPECT_TRUE(static_cast<bool>(resources.outputBindGroup));
            EXPECT_TRUE(static_cast<bool>(resources.writePatternPass));
            return resources;
        }

        SortResources createSortResources(uint32_t elementCount, uint32_t activePassCount)
        {
            SortResources resources = {};
            resources.elementCount = std::max(elementCount, 1u);
            resources.partitionCount =
                std::max((resources.elementCount + OneSweepSortTest::WorkGroupSize - 1u) / OneSweepSortTest::WorkGroupSize, 1u);
            resources.activePassCount = std::max(1u, std::min(activePassCount, OneSweepSortTest::RadixPassCount));

            const uint64_t entryBytes = uint64_t(resources.elementCount) * sizeof(OneSweepSortTest::SortEntry);
            const uint64_t prefixDataBytes =
                uint64_t(resources.partitionCount) * OneSweepSortTest::RadixBucketCount * sizeof(OneSweepSortTest::PrefixData);

            resources.inputBuffer = device->createBuffer({
                .label = "OneSweepInputBuffer",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapRead |
                    GVM::RHI::BufferUsage::MapWrite,
                .size = entryBytes,
            });
            resources.scratchBuffer = device->createBuffer({
                .label = "OneSweepScratchBuffer",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst |
                    GVM::RHI::BufferUsage::MapRead,
                .size = entryBytes,
            });
            resources.readbackBuffer = device->createBuffer({
                .label = "OneSweepReadbackBuffer",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead,
                .size = entryBytes,
            });
            resources.completion = createCompletionResources("OneSweep");

            EXPECT_FALSE(resources.inputBuffer.isNull());
            EXPECT_FALSE(resources.scratchBuffer.isNull());
            EXPECT_FALSE(resources.readbackBuffer.isNull());
            if (resources.inputBuffer.isNull() || resources.scratchBuffer.isNull() || resources.readbackBuffer.isNull() || resources.completion.buffer.isNull())
            {
                return resources;
            }

            resources.globalsBuffers.resize(OneSweepSortTest::RadixPassCount);
            resources.prefixDataBuffers.resize(OneSweepSortTest::RadixPassCount);
            resources.globalsBindGroups.resize(OneSweepSortTest::RadixPassCount);
            resources.prefixDataBindGroups.resize(OneSweepSortTest::RadixPassCount);
            resources.bucketBaseBuffer = device->createBuffer({
                .label = "OneSweepBucketBaseBuffer",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
                .size = uint64_t(OneSweepSortTest::RadixBucketCount) * sizeof(uint32_t),
            });
            EXPECT_FALSE(resources.bucketBaseBuffer.isNull());
            if (resources.bucketBaseBuffer.isNull())
            {
                return resources;
            }
            resources.bucketBaseBindGroup = device->createBindGroup<OneSweepSortTest::BucketBaseBindGroup>(GVM::RHI::BufferRange(resources.bucketBaseBuffer));
            EXPECT_TRUE(static_cast<bool>(resources.bucketBaseBindGroup));
            if (!static_cast<bool>(resources.bucketBaseBindGroup))
            {
                return resources;
            }

            for (uint32_t passIndex = 0u; passIndex < OneSweepSortTest::RadixPassCount; ++passIndex)
            {
                resources.globalsBuffers[passIndex] = device->createBuffer({
                    .label = "OneSweepGlobalsBuffer",
                    .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapWrite,
                    .size = sizeof(OneSweepSortTest::SortGlobals),
                });
                resources.prefixDataBuffers[passIndex] = device->createBuffer({
                    .label = "OneSweepPrefixDataBuffer",
                    .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
                    .size = prefixDataBytes,
                });

                EXPECT_FALSE(resources.globalsBuffers[passIndex].isNull());
                EXPECT_FALSE(resources.prefixDataBuffers[passIndex].isNull());
                if (resources.globalsBuffers[passIndex].isNull() || resources.prefixDataBuffers[passIndex].isNull())
                {
                    return resources;
                }

                OneSweepSortTest::SortGlobals globals = {};
                globals.elementCount = resources.elementCount;
                globals.passIndex = passIndex;
                globals.partitionCount = resources.partitionCount;
                globals.reserved = 0u;
                resources.globalsBuffers[passIndex]->map();
                auto *mappedGlobals = static_cast<OneSweepSortTest::SortGlobals *>(
                    resources.globalsBuffers[passIndex]->getMappedRange(0u, sizeof(OneSweepSortTest::SortGlobals)));
                EXPECT_NE(mappedGlobals, nullptr);
                if (mappedGlobals == nullptr)
                {
                    resources.globalsBuffers[passIndex]->unmap();
                    return resources;
                }
                *mappedGlobals = globals;
                resources.globalsBuffers[passIndex]->unmap();

                resources.globalsBindGroups[passIndex] = device->createBindGroup<OneSweepSortTest::SortGlobalsBindGroup>(
                    GVM::RHI::BufferRange(resources.globalsBuffers[passIndex]));
                resources.prefixDataBindGroups[passIndex] = device->createBindGroup<OneSweepSortTest::PrefixDataBindGroup>(
                    GVM::RHI::BufferRange(resources.prefixDataBuffers[passIndex]));
                EXPECT_TRUE(static_cast<bool>(resources.globalsBindGroups[passIndex]));
                EXPECT_TRUE(static_cast<bool>(resources.prefixDataBindGroups[passIndex]));
                if (!static_cast<bool>(resources.globalsBindGroups[passIndex]) || !static_cast<bool>(resources.prefixDataBindGroups[passIndex]))
                {
                    return resources;
                }
            }

            resources.pingToPongBindGroup = device->createBindGroup<OneSweepSortTest::EntryPairBindGroup>(
                GVM::RHI::BufferRange(resources.inputBuffer),
                GVM::RHI::BufferRange(resources.scratchBuffer));
            resources.pongToPingBindGroup = device->createBindGroup<OneSweepSortTest::EntryPairBindGroup>(
                GVM::RHI::BufferRange(resources.scratchBuffer),
                GVM::RHI::BufferRange(resources.inputBuffer));
            EXPECT_TRUE(static_cast<bool>(resources.pingToPongBindGroup));
            EXPECT_TRUE(static_cast<bool>(resources.pongToPingBindGroup));
            if (!static_cast<bool>(resources.pingToPongBindGroup) || !static_cast<bool>(resources.pongToPingBindGroup))
            {
                return resources;
            }

            resources.prefixPingToPongPasses.resize(OneSweepSortTest::RadixPassCount);
            resources.prefixPongToPingPasses.resize(OneSweepSortTest::RadixPassCount);
            resources.resolveOffsetsPasses.resize(OneSweepSortTest::RadixPassCount);
            resources.scatterPingToPongPasses.resize(OneSweepSortTest::RadixPassCount);
            resources.scatterPongToPingPasses.resize(OneSweepSortTest::RadixPassCount);
            for (uint32_t passIndex = 0u; passIndex < OneSweepSortTest::RadixPassCount; ++passIndex)
            {
                resources.prefixPingToPongPasses[passIndex] = device->createComputeClass<OneSweepSortTest::OneSweepPrefixPass>(
                    resources.globalsBindGroups[passIndex],
                    resources.pingToPongBindGroup,
                    resources.prefixDataBindGroups[passIndex]);
                resources.prefixPongToPingPasses[passIndex] = device->createComputeClass<OneSweepSortTest::OneSweepPrefixPass>(
                    resources.globalsBindGroups[passIndex],
                    resources.pongToPingBindGroup,
                    resources.prefixDataBindGroups[passIndex]);
                resources.resolveOffsetsPasses[passIndex] = device->createComputeClass<OneSweepSortTest::OneSweepResolveOffsetsPass>(
                    resources.globalsBindGroups[passIndex],
                    resources.prefixDataBindGroups[passIndex],
                    resources.bucketBaseBindGroup);
                resources.scatterPingToPongPasses[passIndex] = device->createComputeClass<OneSweepSortTest::OneSweepScatterPass>(
                    resources.globalsBindGroups[passIndex],
                    resources.pingToPongBindGroup,
                    resources.prefixDataBindGroups[passIndex],
                    resources.bucketBaseBindGroup);
                resources.scatterPongToPingPasses[passIndex] = device->createComputeClass<OneSweepSortTest::OneSweepScatterPass>(
                    resources.globalsBindGroups[passIndex],
                    resources.pongToPingBindGroup,
                    resources.prefixDataBindGroups[passIndex],
                    resources.bucketBaseBindGroup);
            }

            for (uint32_t passIndex = 0u; passIndex < OneSweepSortTest::RadixPassCount; ++passIndex)
            {
                EXPECT_TRUE(static_cast<bool>(resources.prefixPingToPongPasses[passIndex]));
                EXPECT_TRUE(static_cast<bool>(resources.prefixPongToPingPasses[passIndex]));
                EXPECT_TRUE(static_cast<bool>(resources.resolveOffsetsPasses[passIndex]));
                EXPECT_TRUE(static_cast<bool>(resources.scatterPingToPongPasses[passIndex]));
                EXPECT_TRUE(static_cast<bool>(resources.scatterPongToPingPasses[passIndex]));
                if (!static_cast<bool>(resources.prefixPingToPongPasses[passIndex]) ||
                    !static_cast<bool>(resources.prefixPongToPingPasses[passIndex]) ||
                    !static_cast<bool>(resources.resolveOffsetsPasses[passIndex]) ||
                    !static_cast<bool>(resources.scatterPingToPongPasses[passIndex]) ||
                    !static_cast<bool>(resources.scatterPongToPingPasses[passIndex]))
                {
                    return resources;
                }
            }

            return resources;
        }

        bool resetCompletionOnCpu(const CompletionResources &resources) const
        {
            EXPECT_FALSE(resources.buffer.isNull());
            if (resources.buffer.isNull())
            {
                return false;
            }

            resources.buffer->map();
            auto *completionValue =
                static_cast<volatile uint32_t *>(resources.buffer->getMappedRange(0u, sizeof(uint32_t)));
            EXPECT_NE(completionValue, nullptr);
            if (completionValue == nullptr)
            {
                resources.buffer->unmap();
                return false;
            }

            *completionValue = 0u;
            resources.buffer->unmap();
            return true;
        }

        bool waitForCompletionFlag(const CompletionResources &resources) const
        {
            resources.buffer->map();
            const auto *completionValue =
                static_cast<const volatile uint32_t *>(resources.buffer->getConstMappedRange(0u, sizeof(uint32_t)));
            EXPECT_NE(completionValue, nullptr);
            if (completionValue == nullptr)
            {
                resources.buffer->unmap();
                return false;
            }

            const auto start = std::chrono::steady_clock::now();
            while ((std::chrono::steady_clock::now() - start) < kCompletionTimeout)
            {
                if (*completionValue == 1u)
                {
                    resources.buffer->unmap();
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            resources.buffer->unmap();
            return false;
        }

        std::vector<OneSweepSortTest::SortEntry> readMappedEntries(GVM::RHI::Buffer buffer, uint32_t elementCount) const
        {
            std::vector<OneSweepSortTest::SortEntry> result(elementCount);
            buffer->map();
            const auto *mapped = static_cast<const OneSweepSortTest::SortEntry *>(
                buffer->getConstMappedRange(0u, uint64_t(elementCount) * sizeof(OneSweepSortTest::SortEntry)));
            EXPECT_NE(mapped, nullptr);
            if (mapped == nullptr)
            {
                buffer->unmap();
                return {};
            }

            result.assign(mapped, mapped + elementCount);
            buffer->unmap();
            return result;
        }

        bool uploadInput(const SortResources &resources, const std::vector<OneSweepSortTest::SortEntry> &inputEntries) const
        {
            resources.inputBuffer->map();
            auto *mappedInput = static_cast<OneSweepSortTest::SortEntry *>(
                resources.inputBuffer->getMappedRange(0u, uint64_t(inputEntries.size()) * sizeof(OneSweepSortTest::SortEntry)));
            EXPECT_NE(mappedInput, nullptr);
            if (mappedInput == nullptr)
            {
                resources.inputBuffer->unmap();
                return false;
            }

            std::memcpy(mappedInput, inputEntries.data(), uint64_t(inputEntries.size()) * sizeof(OneSweepSortTest::SortEntry));
            resources.inputBuffer->unmap();
            return true;
        }

        double dispatchPattern(PatternResources &resources, bool useReadbackCopy)
        {
            EXPECT_TRUE(static_cast<bool>(resources.writePatternPass));
            EXPECT_TRUE(static_cast<bool>(resources.completion.signalPass));
            if (!static_cast<bool>(resources.writePatternPass) || !static_cast<bool>(resources.completion.signalPass))
            {
                return 0.0;
            }

            EXPECT_TRUE(resetCompletionOnCpu(resources.completion));
            auto *queue = rawDevice->getMainQueue();
            EXPECT_NE(queue, nullptr);
            if (queue == nullptr)
            {
                return 0.0;
            }

            const auto start = std::chrono::steady_clock::now();

            auto commandEncoder = queue->createCommandEncoder();
            EXPECT_TRUE(static_cast<bool>(commandEncoder));
            if (!static_cast<bool>(commandEncoder))
            {
                return 0.0;
            }

            auto computePass = commandEncoder->beginComputePass({.label = "PatternWrite"});
            EXPECT_TRUE(static_cast<bool>(computePass));
            if (!static_cast<bool>(computePass))
            {
                return 0.0;
            }
            resources.writePatternPass->run(resources.elementCount, 1u, 1u).dispatchFn(computePass);
            computePass->end();

            if (useReadbackCopy)
            {
                auto blitPass = commandEncoder->beginBlitPass({.label = "PatternReadbackCopy"});
                EXPECT_TRUE(static_cast<bool>(blitPass));
                if (!static_cast<bool>(blitPass))
                {
                    return 0.0;
                }
                GVM::Core::copyBufferToBuffer(
                    resources.outputBuffer,
                    0u,
                    resources.readbackBuffer,
                    0u,
                    uint64_t(resources.elementCount) * sizeof(OneSweepSortTest::SortEntry))
                    .blitFn(blitPass);
                blitPass->end();
            }

            auto signalPass = commandEncoder->beginComputePass({.label = "PatternSignalCompletion"});
            EXPECT_TRUE(static_cast<bool>(signalPass));
            if (!static_cast<bool>(signalPass))
            {
                return 0.0;
            }
            resources.completion.signalPass->run(1u, 1u, 1u).dispatchFn(signalPass);
            signalPass->end();
            commandEncoder->end();

            eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
            encoders[0] = commandEncoder;
            queue->submit(encoders);

            EXPECT_TRUE(waitForCompletionFlag(resources.completion));

            const auto end = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(end - start).count();
        }

        double sortOnGpu(SortResources &resources, const std::vector<OneSweepSortTest::SortEntry> &inputEntries)
        {
            EXPECT_EQ(inputEntries.size(), resources.elementCount);
            if (inputEntries.size() != resources.elementCount)
            {
                return 0.0;
            }

            auto *queue = rawDevice->getMainQueue();
            EXPECT_NE(queue, nullptr);
            if (queue == nullptr)
            {
                return 0.0;
            }

            if (!uploadInput(resources, inputEntries))
            {
                return 0.0;
            }

            const auto start = std::chrono::steady_clock::now();

            for (uint32_t passIndex = 0u; passIndex < resources.activePassCount; ++passIndex)
            {
                auto currentPrefixPass = (passIndex & 1u) == 0u
                    ? resources.prefixPingToPongPasses[passIndex]
                    : resources.prefixPongToPingPasses[passIndex];
                auto currentScatterPass = (passIndex & 1u) == 0u
                    ? resources.scatterPingToPongPasses[passIndex]
                    : resources.scatterPongToPingPasses[passIndex];

                if (!resetCompletionOnCpu(resources.completion))
                {
                    return 0.0;
                }

                auto commandEncoder = queue->createCommandEncoder();
                EXPECT_TRUE(static_cast<bool>(commandEncoder));
                if (!static_cast<bool>(commandEncoder))
                {
                    return 0.0;
                }

                auto clearPass = commandEncoder->beginBlitPass({.label = "OneSweepClear"});
                EXPECT_TRUE(static_cast<bool>(clearPass));
                if (!static_cast<bool>(clearPass))
                {
                    return 0.0;
                }
                GVM::Core::fillBuffer(
                    GVM::RHI::BufferRange(resources.completion.buffer, 0u, sizeof(uint32_t)),
                    0u)
                    .blitFn(clearPass);
                GVM::Core::fillBuffer(
                    GVM::RHI::BufferRange(resources.bucketBaseBuffer, 0u, uint64_t(OneSweepSortTest::RadixBucketCount) * sizeof(uint32_t)),
                    0u)
                    .blitFn(clearPass);
                GVM::Core::fillBuffer(
                    GVM::RHI::BufferRange(
                        resources.prefixDataBuffers[passIndex],
                        0u,
                        uint64_t(resources.partitionCount) * OneSweepSortTest::RadixBucketCount * sizeof(OneSweepSortTest::PrefixData)),
                    0u)
                    .blitFn(clearPass);
                clearPass->end();

                auto prefixPass = commandEncoder->beginComputePass({.label = "OneSweepPrefix"});
                EXPECT_TRUE(static_cast<bool>(prefixPass));
                if (!static_cast<bool>(prefixPass))
                {
                    return 0.0;
                }
                currentPrefixPass->run(resources.partitionCount * OneSweepSortTest::WorkGroupSize, 1u, 1u).dispatchFn(prefixPass);
                prefixPass->end();

                auto resolveOffsetsPass = commandEncoder->beginComputePass({.label = "OneSweepResolveOffsets"});
                EXPECT_TRUE(static_cast<bool>(resolveOffsetsPass));
                if (!static_cast<bool>(resolveOffsetsPass))
                {
                    return 0.0;
                }
                resources.resolveOffsetsPasses[passIndex]->run(OneSweepSortTest::RadixBucketCount, 1u, 1u).dispatchFn(resolveOffsetsPass);
                resolveOffsetsPass->end();

                auto scatterPass = commandEncoder->beginComputePass({.label = "OneSweepScatter"});
                EXPECT_TRUE(static_cast<bool>(scatterPass));
                if (!static_cast<bool>(scatterPass))
                {
                    return 0.0;
                }
                currentScatterPass->run(resources.partitionCount * OneSweepSortTest::WorkGroupSize, 1u, 1u).dispatchFn(scatterPass);
                scatterPass->end();

                auto signalPass = commandEncoder->beginComputePass({.label = "OneSweepSignalCompletion"});
                EXPECT_TRUE(static_cast<bool>(signalPass));
                if (!static_cast<bool>(signalPass))
                {
                    return 0.0;
                }
                resources.completion.signalPass->run(1u, 1u, 1u).dispatchFn(signalPass);
                signalPass->end();
                commandEncoder->end();

                eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
                encoders[0] = commandEncoder;
                queue->submit(encoders);

                EXPECT_TRUE(waitForCompletionFlag(resources.completion));
            }

            const auto end = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(end - start).count();
        }

        GVM::RHI::Buffer getFinalSortedBuffer(const SortResources &resources) const
        {
            return (resources.activePassCount & 1u) == 0u ? resources.inputBuffer : resources.scratchBuffer;
        }

        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device rawDevice = nullptr;
        GVM::Core::DeviceProxy device;
    };

    TEST_F(GpuOneSweepSortTest, CompletionSignalPassWritesCpuVisibleFlag)
    {
        auto completion = createCompletionResources("CompletionOnly");
        ASSERT_FALSE(completion.buffer.isNull());
        ASSERT_TRUE(static_cast<bool>(completion.signalPass));

        ASSERT_TRUE(resetCompletionOnCpu(completion));
        auto queue = device->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->computePass("CompletionOnlySignal", completion.signalPass->run(1u, 1u, 1u))->submit();

        EXPECT_TRUE(waitForCompletionFlag(completion));
    }

    TEST_F(GpuOneSweepSortTest, ComputeWritePatternIntoMappedStorageBufferMatchesCpuReference)
    {
        constexpr uint32_t kElementCount = 1024u;
        auto resources = createPatternResources(kElementCount, true);
        ASSERT_FALSE(resources.outputBuffer.isNull());
        ASSERT_TRUE(static_cast<bool>(resources.writePatternPass));

        const double gpuMs = dispatchPattern(resources, false);
        const auto gpuResult = readMappedEntries(resources.outputBuffer, kElementCount);
        const auto cpuReference = makePatternReference(kElementCount);
        expectEntriesEqual(gpuResult, cpuReference);

        if (isVerboseEnabled())
        {
            std::fprintf(stderr, "[gvm-rhi-gpu-onesweep-sort] mapped-pattern count=%u gpu_ms=%.3f\n", kElementCount, gpuMs);
            std::fflush(stderr);
        }
    }

    TEST_F(GpuOneSweepSortTest, ComputeWritePatternThenBlitReadbackMatchesCpuReference)
    {
        constexpr uint32_t kElementCount = 1024u;
        auto resources = createPatternResources(kElementCount, false);
        ASSERT_FALSE(resources.outputBuffer.isNull());
        ASSERT_FALSE(resources.readbackBuffer.isNull());
        ASSERT_TRUE(static_cast<bool>(resources.writePatternPass));

        const double gpuMs = dispatchPattern(resources, true);
        const auto gpuResult = readMappedEntries(resources.readbackBuffer, kElementCount);
        const auto cpuReference = makePatternReference(kElementCount);
        expectEntriesEqual(gpuResult, cpuReference);

        if (isVerboseEnabled())
        {
            std::fprintf(stderr, "[gvm-rhi-gpu-onesweep-sort] staged-pattern count=%u gpu_ms=%.3f\n", kElementCount, gpuMs);
            std::fflush(stderr);
        }
    }

    TEST_F(GpuOneSweepSortTest, SortsSmallFixedSequenceAgainstCpuReference)
    {
        const auto inputEntries = makeSmallFixedInput();
        const uint32_t activePassCount = computeActiveRadixPassCount(inputEntries);
        auto resources = createSortResources(static_cast<uint32_t>(inputEntries.size()), activePassCount);

        ASSERT_FALSE(resources.inputBuffer.isNull());
        ASSERT_TRUE(static_cast<bool>(resources.pingToPongBindGroup));

        const auto cpuReference = makeCpuReference(inputEntries);
        const double gpuMs = sortOnGpu(resources, inputEntries);
        const auto gpuResult = readMappedEntries(getFinalSortedBuffer(resources), resources.elementCount);
        expectEntriesEqual(gpuResult, cpuReference);

        if (isVerboseEnabled())
        {
            std::fprintf(stderr, "[gvm-rhi-gpu-onesweep-sort] small-correctness count=%u gpu_ms=%.3f partitions=%u active_passes=%u\n",
                resources.elementCount,
                gpuMs,
                resources.partitionCount,
                resources.activePassCount);
            std::fflush(stderr);
        }
    }

    TEST_F(GpuOneSweepSortTest, SortsRandomMultiPartitionSequenceAgainstCpuReference)
    {
        const uint32_t elementCount = readEnvUint("GVM_TEST_GPU_RADIX_SORT_COUNT", 1u << 18);
        const auto inputEntries = makeDeterministicInput(elementCount);

        const auto cpuStart = std::chrono::steady_clock::now();
        const auto cpuReference = makeCpuReference(inputEntries);
        const auto cpuEnd = std::chrono::steady_clock::now();
        const double cpuMs = std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count();

        auto resources = createSortResources(elementCount, computeActiveRadixPassCount(inputEntries));
        const double gpuMs = sortOnGpu(resources, inputEntries);
        const auto gpuResult = readMappedEntries(getFinalSortedBuffer(resources), resources.elementCount);
        expectEntriesEqual(gpuResult, cpuReference);

        std::fprintf(
            stderr,
            "[gvm-rhi-gpu-onesweep-sort] correctness count=%u gpu_total_ms=%.3f cpu_ms=%.3f partitions=%u active_passes=%u\n",
            elementCount,
            gpuMs,
            cpuMs,
            resources.partitionCount,
            resources.activePassCount);
        std::fflush(stderr);

        RecordProperty("element_count", static_cast<int>(elementCount));
        RecordProperty("gpu_total_ms", gpuMs);
        RecordProperty("cpu_ms", cpuMs);
        RecordProperty("active_passes", static_cast<int>(resources.activePassCount));
    }

    TEST_F(GpuOneSweepSortTest, BenchmarksVeryLargeRandomSequenceAndMatchesCpuReference)
    {
        const uint32_t elementCount = readEnvUint("GVM_TEST_GPU_RADIX_SORT_BENCHMARK_COUNT", 1u << 20);
        const uint32_t iterations = readEnvUint("GVM_TEST_GPU_RADIX_SORT_BENCHMARK_ITERATIONS", 3u);
        const auto inputEntries = makeDeterministicInput(elementCount);

        const auto cpuStart = std::chrono::steady_clock::now();
        const auto cpuReference = makeCpuReference(inputEntries);
        const auto cpuEnd = std::chrono::steady_clock::now();
        const double cpuMs = std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count();

        auto resources = createSortResources(elementCount, computeActiveRadixPassCount(inputEntries));
        double accumulatedGpuMs = 0.0;
        std::vector<OneSweepSortTest::SortEntry> gpuResult;
        for (uint32_t iteration = 0u; iteration < iterations; ++iteration)
        {
            accumulatedGpuMs += sortOnGpu(resources, inputEntries);
            gpuResult = readMappedEntries(getFinalSortedBuffer(resources), resources.elementCount);
        }

        const double averageGpuMs = accumulatedGpuMs / std::max(iterations, 1u);
        const double millionPairsPerSecond = averageGpuMs > 0.0
            ? (double(elementCount) / 1.0e6) / (averageGpuMs / 1000.0)
            : 0.0;

        expectEntriesEqual(gpuResult, cpuReference);

        std::fprintf(
            stderr,
            "[gvm-rhi-gpu-onesweep-sort] benchmark count=%u iterations=%u avg_gpu_total_ms=%.3f cpu_ms=%.3f throughput_mpairs_s=%.3f partitions=%u active_passes=%u\n",
            elementCount,
            iterations,
            averageGpuMs,
            cpuMs,
            millionPairsPerSecond,
            resources.partitionCount,
            resources.activePassCount);
        std::fflush(stderr);

        RecordProperty("benchmark_count", static_cast<int>(elementCount));
        RecordProperty("benchmark_iterations", static_cast<int>(iterations));
        RecordProperty("benchmark_gpu_avg_ms", averageGpuMs);
        RecordProperty("benchmark_cpu_ms", cpuMs);
        RecordProperty("benchmark_throughput_mpairs_s", millionPairsPerSecond);
        RecordProperty("benchmark_active_passes", static_cast<int>(resources.activePassCount));
    }
} // namespace
