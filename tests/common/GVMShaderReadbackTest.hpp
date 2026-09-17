#pragma once
#include <gtest/gtest.h>
#include <chrono>
#include "GVMTestCommon.hpp"

namespace GVM::Tests
{
    /** Holds the explicit CLI duration for sustained readback validation; zero performs one iteration. */
    inline uint32_t shaderReadbackDurationSeconds = 0;
    /** Selects the explicit backend requested by the readback executable's command line. */
    inline GVM::RHI::GraphicsBackend shaderReadbackBackend = GVM::RHI::GraphicsBackend::Undefined;
    /** Enables explicit GPU timestamp measurement for paired performance experiments. */
    inline bool shaderReadbackMeasureGpuTime = false;
    /** Owns a headless device for exact shader readback tests on the runner-selected backend. */
    class ShaderReadbackTest : public ::testing::Test
    {
    protected:
        /** Creates an instance and device before each independently isolated readback test. */
        void SetUp() override
        {
            GVM::RHI::InstanceDescriptor descriptor = {};
            descriptor.preferredBackend = shaderReadbackBackend;
            instance = GVM::RHI::createInstance(descriptor);
            ASSERT_NE(instance, nullptr);
            ASSERT_EQ(instance->getBackend(), shaderReadbackBackend);
            RecordProperty("backend", shaderReadbackBackend == GVM::RHI::GraphicsBackend::Metal ? "metal" : "vulkan");
            device = instance->createDevice();
            ASSERT_NE(device, nullptr);
        }

        /** Releases backend state after resources owned by the test body leave scope. */
        void TearDown() override
        {
            device = nullptr;
            if (instance != nullptr) { GVM::RHI::destroyInstance(instance); }
        }

        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device device = nullptr;
    };
}
