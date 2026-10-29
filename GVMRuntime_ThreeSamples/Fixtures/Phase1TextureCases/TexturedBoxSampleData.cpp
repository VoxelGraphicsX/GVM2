#include "TexturedBoxSampleData.hpp"

#include <glm/vec3.hpp>

#include <cmath>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t BoxVertexCount = 24u;
        constexpr uint32_t BoxIndexCount = 36u;

        static_assert(sizeof(TexturedBoxHostFloat4) == 16u);
        static_assert(sizeof(TexturedBoxHostUint4) == 16u);
        static_assert(sizeof(TexturedBoxHostVertex) == 32u);
        static_assert(sizeof(TexturedBoxHostObjectData) == 80u);
        static_assert(sizeof(TexturedBoxHostInstanceData) == 16u);
        static_assert(sizeof(TexturedBoxHostMaterialData) == 16u);

        /** Returns a writable vector component selected by one BoxGeometry axis index. */
        float &boxAxis(glm::vec3 &vector, uint32_t axis)
        {
            if (axis == 0u)
            {
                return vector.x;
            }
            if (axis == 1u)
            {
                return vector.y;
            }
            return vector.z;
        }

        /** Appends one default-segment Three r185 BoxGeometry plane in canonical order. */
        void appendBoxPlane(eastl::vector<TexturedBoxHostVertex> &vertices, eastl::vector<uint32_t> &indices, uint32_t uAxis, uint32_t vAxis, uint32_t wAxis, float uDirection, float vDirection, float width, float height, float depth)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            for (uint32_t yIndex = 0u; yIndex < 2u; ++yIndex)
            {
                const float y = float(yIndex) * height - height * 0.5f;
                for (uint32_t xIndex = 0u; xIndex < 2u; ++xIndex)
                {
                    const float x = float(xIndex) * width - width * 0.5f;
                    glm::vec3 position(0.0f);
                    boxAxis(position, uAxis) = x * uDirection;
                    boxAxis(position, vAxis) = y * vDirection;
                    boxAxis(position, wAxis) = depth * 0.5f;
                    vertices.push_back({
                        .position = {position.x, position.y, position.z, 1.0f},
                        .texCoord = {float(xIndex), 1.0f - float(yIndex), 0.0f, 0.0f},
                    });
                }
            }

            const uint32_t topLeft = baseVertex;
            const uint32_t bottomLeft = baseVertex + 2u;
            const uint32_t bottomRight = baseVertex + 3u;
            const uint32_t topRight = baseVertex + 1u;
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
            indices.push_back(topRight);
        }
    } // namespace

    void buildTexturedBoxGeometry(float width, float height, float depth, eastl::vector<TexturedBoxHostVertex> &vertices, eastl::vector<uint32_t> &indices)
    {
        if (width <= 0.0f || height <= 0.0f || depth <= 0.0f)
        {
            throw std::invalid_argument("Textured BoxGeometry extents must be positive.");
        }
        vertices.clear();
        indices.clear();
        vertices.reserve(BoxVertexCount);
        indices.reserve(BoxIndexCount);
        appendBoxPlane(vertices, indices, 2u, 1u, 0u, -1.0f, -1.0f, depth, height, width);
        appendBoxPlane(vertices, indices, 2u, 1u, 0u, 1.0f, -1.0f, depth, height, -width);
        appendBoxPlane(vertices, indices, 0u, 2u, 1u, 1.0f, 1.0f, width, depth, height);
        appendBoxPlane(vertices, indices, 0u, 2u, 1u, 1.0f, -1.0f, width, depth, -height);
        appendBoxPlane(vertices, indices, 0u, 1u, 2u, 1.0f, -1.0f, width, height, depth);
        appendBoxPlane(vertices, indices, 0u, 1u, 2u, -1.0f, -1.0f, width, height, -depth);
        if (vertices.size() != BoxVertexCount || indices.size() != BoxIndexCount)
        {
            throw std::logic_error("Canonical BoxGeometry construction produced an invalid element count.");
        }
    }

    glm::mat4 makeThreeEulerXyRotation(double rotationX, double rotationY)
    {
        const double sineX = std::sin(rotationX);
        const double cosineX = std::cos(rotationX);
        const double sineY = std::sin(rotationY);
        const double cosineY = std::cos(rotationY);

        glm::mat4 result(1.0f);
        result[0u][0u] = static_cast<float>(cosineY);
        result[0u][1u] = static_cast<float>(sineX * sineY);
        result[0u][2u] = static_cast<float>(-cosineX * sineY);
        result[1u][0u] = 0.0f;
        result[1u][1u] = static_cast<float>(cosineX);
        result[1u][2u] = static_cast<float>(sineX);
        result[2u][0u] = static_cast<float>(sineY);
        result[2u][1u] = static_cast<float>(-sineX * cosineY);
        result[2u][2u] = static_cast<float>(cosineX * cosineY);
        return result;
    }

    glm::mat4 makeThreePerspectiveProjection(uint32_t width, uint32_t height, double verticalFieldOfViewDegrees, double nearDistance, double farDistance)
    {
        if (width == 0u || height == 0u || verticalFieldOfViewDegrees <= 0.0 || verticalFieldOfViewDegrees >= 180.0 || nearDistance <= 0.0 || farDistance <= nearDistance)
        {
            throw std::invalid_argument("Three perspective projection parameters are invalid.");
        }
        const double verticalFieldOfViewRadians = verticalFieldOfViewDegrees * 3.14159265358979323846 / 180.0;
        const double top = nearDistance * std::tan(verticalFieldOfViewRadians * 0.5);
        const double projectionHeight = 2.0 * top;
        const double projectionWidth = (double(width) / double(height)) * projectionHeight;
        const double left = -0.5 * projectionWidth;
        const double projectionDepth = farDistance - nearDistance;

        glm::mat4 projection(0.0f);
        projection[0u][0u] = static_cast<float>(2.0 * nearDistance / projectionWidth);
        projection[1u][1u] = static_cast<float>(2.0 * nearDistance / projectionHeight);
        projection[2u][0u] = static_cast<float>(-(2.0 * left + projectionWidth) / projectionWidth);
        projection[2u][1u] = 0.0f;
        projection[2u][2u] = static_cast<float>(-(farDistance + nearDistance) / projectionDepth);
        projection[2u][3u] = -1.0f;
        projection[3u][2u] = static_cast<float>(-2.0 * farDistance * nearDistance / projectionDepth);
        return projection;
    }

    eastl::pair<double, double> calculateTexturedBoxFrameRotation(uint32_t frameIndex, double rotationXIncrement, double rotationYIncrement)
    {
        const double renderedFrameCount = double(frameIndex) + 1.0;
        return {
            renderedFrameCount * rotationXIncrement,
            renderedFrameCount * rotationYIncrement,
        };
    }
} // namespace GVM::ThreeSamples
