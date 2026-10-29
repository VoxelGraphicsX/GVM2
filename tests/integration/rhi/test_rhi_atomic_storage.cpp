#include <gtest/gtest.h>
#include <EASTL/array.h>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMShaderReadbackTest.hpp"
#include "generate_result.hpp"

namespace
{
    constexpr size_t StateCount = AtomicStoragePublicationPairCount;
    constexpr size_t VectorGroupCount = 2u;
    constexpr size_t InvocationCount = 64u;
    constexpr size_t ObservationStride = 4u;
    using States = eastl::array<AtomicStorageState, StateCount>;
    using Observations = eastl::array<uint32_t, InvocationCount * ObservationStride>;
    using Buffers = eastl::array<GVM::RHI::Buffer, 2u>;
    using AtomicStorageTest = GVM::Tests::ShaderReadbackTest;

    /** Allocates bounded storage for independent publication messages and per-invocation observations. */
    Buffers createAtomicStorageBuffers(GVM::Core::DeviceProxy &device)
    {
        const auto usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst;
        return {
            device->createBuffer({.label = "AtomicStorageStates", .usage = usage, .size = sizeof(States)}),
            device->createBuffer({.label = "AtomicStorageObservations", .usage = usage, .size = sizeof(Observations)}),
        };
    }

    /** Verifies component isolation, signed atomics, strong CAS, and ordinary value copies through GPU readback. */
    TEST_F(AtomicStorageTest, PreservesVectorAddressesAndOrdinaryCopies)
    {
        GVM::Core::DeviceProxy proxy(device);
        const Buffers buffers = createAtomicStorageBuffers(proxy);
        ASSERT_FALSE(buffers[0].isNull());
        ASSERT_FALSE(buffers[1].isNull());
        States states = {};
        Observations observations = {};
        const GVM::RHI::BufferRange stateRange(buffers[0], 0u, sizeof(states));
        const GVM::RHI::BufferRange observationRange(buffers[1], 0u, sizeof(observations));
        auto bindings = proxy->createBindGroup<AtomicStorageBindings>(stateRange, observationRange);
        auto pass = proxy->createComputeClass<AtomicStorageVectorPass>(bindings);
        ASSERT_TRUE(static_cast<bool>(pass));
        auto queue = proxy->graphicsQueue(0u);
        queue->writeBuffer(stateRange, states.data(), sizeof(states))->submit();
        queue->writeBuffer(observationRange, observations.data(), sizeof(observations))->submit();
        queue->computePass("AtomicStorageVectors", pass->run(64u, 1u, 1u))->submit();
        queue->readBuffer(stateRange, states.data(), sizeof(states))->submit();
        queue->readBuffer(observationRange, observations.data(), sizeof(observations))->submit();
        for (size_t stateIndex = 0u; stateIndex < VectorGroupCount; ++stateIndex)
        {
            const AtomicStorageState &state = states[stateIndex];
            EXPECT_EQ(state.counters.x, 51u);
            EXPECT_EQ(state.counters.y, 19u);
            EXPECT_EQ(state.counters.z, 19u);
            EXPECT_EQ(state.counters.w, 19u);
            EXPECT_EQ(state.signedCounters.x, -32);
            EXPECT_EQ(state.signedCounters.y, -1);
            EXPECT_EQ(state.signedCounters.z, -31);
            EXPECT_EQ(state.signedCounters.w, 31);
            EXPECT_EQ(state.scalar, 32u);
            EXPECT_EQ(state.ordinary, 11u);
            EXPECT_EQ(state.winner, 1u);
            EXPECT_EQ(state.successfulClaims, 1u);
        }
        for (size_t invocation = 0u; invocation < InvocationCount; ++invocation)
        {
            const size_t base = invocation * ObservationStride;
            EXPECT_EQ(observations[base], 8u) << invocation;
            EXPECT_EQ(observations[base + 1u], 64u) << invocation;
            EXPECT_EQ(observations[base + 2u], invocation % 32u + invocation / 32u + 21u) << invocation;
            EXPECT_EQ(observations[base + 3u], 1u) << invocation;
        }
        device->freeBuffer(buffers[1]);
        device->freeBuffer(buffers[0]);
    }

    /** Verifies payload visibility after release publication and explicit fences, with finite GPU polling. */
    TEST_F(AtomicStorageTest, PublishesPayloadBetweenWorkgroups)
    {
        GVM::Core::DeviceProxy proxy(device);
        const Buffers buffers = createAtomicStorageBuffers(proxy);
        ASSERT_FALSE(buffers[0].isNull());
        ASSERT_FALSE(buffers[1].isNull());
        States states = {};
        Observations observations = {};
        const GVM::RHI::BufferRange stateRange(buffers[0], 0u, sizeof(states));
        const GVM::RHI::BufferRange observationRange(buffers[1], 0u, sizeof(observations));
        auto bindings = proxy->createBindGroup<AtomicStorageBindings>(stateRange, observationRange);
        auto pass = proxy->createComputeClass<AtomicStoragePublicationPass>(bindings);
        ASSERT_TRUE(static_cast<bool>(pass));
        auto queue = proxy->graphicsQueue(0u);
        queue->writeBuffer(stateRange, states.data(), sizeof(states))->submit();
        queue->writeBuffer(observationRange, observations.data(), sizeof(observations))->submit();
        queue->computePass("AtomicStoragePublication", pass->run(StateCount * 64u, 1u, 1u))->submit();
        queue->readBuffer(stateRange, states.data(), sizeof(states))->submit();
        queue->readBuffer(observationRange, observations.data(), sizeof(observations))->submit();
        size_t releaseObservations = 0u;
        size_t fenceObservations = 0u;
        for (size_t pair = 0u; pair < StateCount; ++pair)
        {
            EXPECT_EQ(states[pair].ready, 1u) << pair;
            EXPECT_EQ(states[pair].readError, 0u) << pair;
            EXPECT_LE(observations[pair], 1u) << pair;
            EXPECT_EQ(states[pair].acknowledged, observations[pair]) << pair;
            if ((pair & 1u) == 0u) releaseObservations += observations[pair];
            else fenceObservations += observations[pair];
        }
        EXPECT_GT(releaseObservations, 0u);
        EXPECT_GT(fenceObservations, 0u);
        RecordProperty("releasePublicationsObserved", releaseObservations);
        RecordProperty("fencePublicationsObserved", fenceObservations);
        device->freeBuffer(buffers[1]);
        device->freeBuffer(buffers[0]);
    }
}
