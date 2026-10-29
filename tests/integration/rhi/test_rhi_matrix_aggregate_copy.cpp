#define GLM_ENABLE_EXPERIMENTAL

#include <gtest/gtest.h>
#include <EASTL/array.h>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>

#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMShaderReadbackTest.hpp"
#include "generate_result.hpp"

namespace
{
    // Four inputs cross five copy paths; each case owns four numerical observations.
    constexpr size_t MatrixKindCount = 4u;
    constexpr size_t CaseCount = 20u;
    constexpr size_t ObservationCount = 4u;
    constexpr size_t MatrixWordCount = 16u;
    using MatrixWords = eastl::array<float, MatrixWordCount>;
    using CacheRecords = eastl::array<MatrixCopyRecord, CaseCount>;
    using Observations = eastl::array<float4, CaseCount * ObservationCount>;
    using Buffers = eastl::array<GVM::RHI::Buffer, 2u>;
    using Targets = eastl::array<GVM::RHI::Texture, 2u>;

    /** Uses the explicit backend selected by the common headless readback runner. */
    using MatrixAggregateCopyTest = GVM::Tests::ShaderReadbackTest;

    /** Defines coefficients independently of GPU construction and cache operations. */
    MatrixWords expectedMatrix(size_t caseIndex)
    {
        if ((caseIndex % MatrixKindCount & 1u) != 0u)
        {
            return {1.6f, 0.0f, 2.4f, 0.0f,
                    0.0f, 3.0f, 0.0f, 0.0f,
                    -1.2f, 0.0f, 3.2f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f};
        }
        return {1.0f, 2.0f, 3.0f, 4.0f,
                5.0f, 6.0f, 7.0f, 8.0f,
                9.0f, 10.0f, 11.0f, 12.0f,
                13.0f, 14.0f, 15.0f, 16.0f};
    }

    /** Calculates the DSL column-vector product without relying on GLM packing. */
    float4 expectedProduct(size_t caseIndex)
    {
        const MatrixWords matrix = expectedMatrix(caseIndex);
        const eastl::array<float, 4u> point = {1.0f, 2.0f, 3.0f, 1.0f};
        float4 product(0.0f);
        for (size_t row = 0u; row < point.size(); ++row)
        {
            for (size_t column = 0u; column < point.size(); ++column)
            {
                product[row] += matrix[column * point.size() + row] * point[column];
            }
        }
        return product;
    }

    /** Compares every component with a CPU product while permitting float contraction. */
    void expectProduct(const float4 &actual, size_t caseIndex)
    {
        const float4 expected = expectedProduct(caseIndex);
        for (size_t component = 0u; component < 4u; ++component)
        {
            EXPECT_NEAR(actual[component], expected[component], 0.0001f)
                << "case " << caseIndex << ", component " << component;
        }
    }

    /** Checks physical matrix bytes and the accompanying scalar attribute. */
    void expectPlacement(const MatrixCopyPlacement &placement, size_t caseIndex)
    {
        MatrixWords actual = {};
        std::memcpy(actual.data(), &placement.transform, sizeof(actual));
        const MatrixWords expected = expectedMatrix(caseIndex);
        for (size_t word = 0u; word < actual.size(); ++word)
        {
            EXPECT_NEAR(actual[word], expected[word], 0.0001f)
                << "case " << caseIndex << ", physical matrix word " << word;
        }
        const size_t copyPath = caseIndex / MatrixKindCount;
        EXPECT_EQ(placement.marker, copyPath == 1u ? 17u : copyPath);
    }

    /** Allocates a shared aggregate cache and separate floating-point observation storage. */
    Buffers createBuffers(GVM::Core::DeviceProxy &device)
    {
        const auto usage = GVM::RHI::BufferUsage::Storage |
            GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst;
        return {
            device->createBuffer({
                .label = "MatrixAggregateCopyCache", .usage = usage, .size = sizeof(CacheRecords),
            }),
            device->createBuffer({
                .label = "MatrixAggregateCopyObservations", .usage = usage, .size = sizeof(Observations),
            }),
        };
    }

    /** Separates producer and consumer submissions to prevent local load forwarding. */
    void dispatchAndRead(GVM::Core::DeviceProxy &device, const Buffers &buffers,
                         CacheRecords &records, Observations &observations)
    {
        const GVM::RHI::BufferRange cache(buffers[0], 0u, sizeof(records));
        const GVM::RHI::BufferRange output(buffers[1], 0u, sizeof(observations));
        auto bindings = device->createBindGroup<MatrixAggregateCopy::Bindings>(cache, output);
        auto producer = device->createComputeClass<MatrixAggregateCopy::Producer>(bindings);
        auto consumer = device->createComputeClass<MatrixAggregateCopy::Consumer>(bindings);
        ASSERT_TRUE(static_cast<bool>(producer));
        ASSERT_TRUE(static_cast<bool>(consumer));
        auto queue = device->graphicsQueue(0u);
        queue->writeBuffer(cache, records.data(), sizeof(records))->submit();
        queue->writeBuffer(output, observations.data(), sizeof(observations))->submit();
        queue->computePass("MatrixAggregateProducer", producer->run(CaseCount, 1u, 1u))->submit();
        queue->readBuffer(cache, records.data(), sizeof(records))->submit();
        queue->computePass("MatrixAggregateConsumer", consumer->run(CaseCount, 1u, 1u))->submit();
        queue->readBuffer(output, observations.data(), sizeof(observations))->submit();
    }

    /** Verifies every construction path, nested cache field, and compute observation. */
    void expectComputeResults(const CacheRecords &records, const Observations &observations)
    {
        for (size_t caseIndex = 0u; caseIndex < CaseCount; ++caseIndex)
        {
            expectPlacement(records[caseIndex].placement, caseIndex);
            expectPlacement(records[caseIndex].entries[0], caseIndex);
            expectPlacement(records[caseIndex].entries[1], caseIndex);
            for (size_t observation = 0u; observation < ObservationCount; ++observation)
            {
                expectProduct(observations[caseIndex * ObservationCount + observation], caseIndex);
            }
        }
    }

    /** Allocates one floating-point texel per case in each raster-stage result. */
    Targets createTargets(GVM::Core::DeviceProxy &device)
    {
        Targets targets = {};
        for (GVM::RHI::Texture &target : targets)
        {
            target = device->createTexture({
                .label = "MatrixAggregateRasterReadback",
                .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
                .size = {uint32_t(CaseCount), 1u, 1u},
                .format = GVM::RHI::TextureFormat::RGBA32Float,
            });
        }
        return targets;
    }

    /** Verifies independent vertex and fragment reads through exact floating-point attachments. */
    void drawAndRead(GVM::Core::DeviceProxy &device, const Buffers &buffers)
    {
        const Targets targets = createTargets(device);
        ASSERT_FALSE(targets[0].isNull());
        ASSERT_FALSE(targets[1].isNull());
        MatrixAggregateCopy::RasterFrameBuffer framebuffer;
        framebuffer.vertexResult = targets[0]->createView();
        framebuffer.fragmentResult = targets[1]->createView();
        auto bindings = device->createBindGroup<MatrixAggregateCopy::RasterBindings>(
            GVM::RHI::BufferRange(buffers[0], 0u, sizeof(CacheRecords)));
        auto raster = device->createRenderClass<MatrixAggregateCopy::Raster>(bindings);
        ASSERT_TRUE(static_cast<bool>(raster));
        auto queue = device->graphicsQueue(0u);
        // Two triangles form each case's pixel-wide quad.
        queue->renderPass("MatrixAggregateRaster", framebuffer, raster->run(CaseCount * 6u, 1u, 0u, 0u))->submit();
        for (const GVM::RHI::Texture &target : targets)
        {
            eastl::array<float4, CaseCount> pixels = {};
            queue->readTexture(target, pixels.data(), sizeof(pixels))->submit();
            for (size_t caseIndex = 0u; caseIndex < CaseCount; ++caseIndex)
            {
                expectProduct(pixels[caseIndex], caseIndex);
            }
            device->freeTexture(target);
        }
    }

    /** Checks type separation before optimization and before GPU execution. */
    void expectLogicalStorageSeparation()
    {
        const std::filesystem::path path = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR") /
            "spv" / "MatrixAggregateCopy_Producer.raw.spvasm";
        const std::string assembly = GVM::Tests::readTextFile(path);
        EXPECT_NE(assembly.find("; Version: 1.4"), std::string::npos);
        EXPECT_NE(assembly.find("OpCopyLogical"), std::string::npos);
        EXPECT_NE(assembly.find("OpMemberDecorate %Storage_MatrixCopyPlacement 0 RowMajor"), std::string::npos);
        EXPECT_NE(assembly.find("OpMemberDecorate %Storage_MatrixCopyPlacement 0 MatrixStride 16"), std::string::npos);
        EXPECT_EQ(assembly.find("OpMemberDecorate %MatrixCopyPlacement "), std::string::npos);
        EXPECT_EQ(assembly.find("OpMemberDecorate %MatrixCopyRecord "), std::string::npos);
    }

    /** Regresses the returned matrix/cache failure through compute and both raster stages. */
    TEST_F(MatrixAggregateCopyTest, PreservesNestedMatricesAcrossComputeAndRasterCaches)
    {
        expectLogicalStorageSeparation();
        GVM::Core::DeviceProxy deviceProxy(device);
        const Buffers buffers = createBuffers(deviceProxy);
        ASSERT_FALSE(buffers[0].isNull());
        ASSERT_FALSE(buffers[1].isNull());
        CacheRecords records = {};
        Observations observations = {};
        dispatchAndRead(deviceProxy, buffers, records, observations);
        expectComputeResults(records, observations);
        drawAndRead(deviceProxy, buffers);
        device->freeBuffer(buffers[1]);
        device->freeBuffer(buffers[0]);
    }
}
