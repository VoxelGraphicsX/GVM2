#pragma once

#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one shader float4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) TexturedBoxHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors one shader uint4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) TexturedBoxHostUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    /** Mirrors the shared 32-byte textured BoxGeometry vertex payload packed into a RenderSet. */
    struct alignas(16) TexturedBoxHostVertex
    {
        TexturedBoxHostFloat4 position;
        TexturedBoxHostFloat4 texCoord;
    };

    /** Mirrors the shared 80-byte per-entity transform and material-selection payload. */
    struct alignas(16) TexturedBoxHostObjectData
    {
        glm::mat4 modelViewProjection;
        TexturedBoxHostUint4 materialAndFlags;
    };

    /** Mirrors the shared 16-byte per-instance tint payload. */
    struct alignas(16) TexturedBoxHostInstanceData
    {
        TexturedBoxHostFloat4 tint;
    };

    /** Mirrors the shared 16-byte base-color material payload. */
    struct alignas(16) TexturedBoxHostMaterialData
    {
        TexturedBoxHostFloat4 baseColor;
    };

    /** Creates Three r185's default-segment 24-vertex, 36-index BoxGeometry at an explicit extent. */
    void buildTexturedBoxGeometry(float width, float height, float depth, eastl::vector<TexturedBoxHostVertex> &vertices, eastl::vector<uint32_t> &indices);

    /** Builds the same XYZ Euler rotation matrix that Three r185 composes when z rotation is zero. */
    [[nodiscard]] glm::mat4 makeThreeEulerXyRotation(double rotationX, double rotationY);

    /** Builds Three r185's symmetric perspective matrix with double intermediates before float upload. */
    [[nodiscard]] glm::mat4 makeThreePerspectiveProjection(uint32_t width, uint32_t height, double verticalFieldOfViewDegrees, double nearDistance, double farDistance);

    /** Returns accumulated upstream Euler angles for one zero-based frame and explicit increments. */
    [[nodiscard]] eastl::pair<double, double> calculateTexturedBoxFrameRotation(uint32_t frameIndex, double rotationXIncrement, double rotationYIncrement);
} // namespace GVM::ThreeSamples
