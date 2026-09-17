#pragma once

#include "Fixtures/Phase1TextureCases/TexturedBoxSampleData.hpp"

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one colored plane vertex in either reversed-depth DSL shard. */
    struct alignas(16) ReversedDepthHostVertex
    {
        TexturedBoxHostFloat4 position;
        TexturedBoxHostFloat4 color;
    };

    /** Mirrors normal and reversed transforms for one depth-test mesh entity. */
    struct alignas(16) ReversedDepthHostObjectData
    {
        glm::mat4 normalModelViewProjection;
        glm::mat4 reversedModelViewProjection;
        TexturedBoxHostFloat4 normalDepthControl;
    };

    /** Mirrors the required noninstanced component payload. */
    struct alignas(16) ReversedDepthHostInstanceData
    {
        TexturedBoxHostFloat4 reserved;
    };

    /** Mirrors the one vertex-color material multiplier. */
    struct alignas(16) ReversedDepthHostMaterialData
    {
        TexturedBoxHostFloat4 colorMultiplier;
    };
} // namespace GVM::ThreeSamples
