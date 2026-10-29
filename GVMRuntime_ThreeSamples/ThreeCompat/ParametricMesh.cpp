#include "ParametricMesh.hpp"

#include <glm/geometric.hpp>

#include <stdexcept>

namespace GVM::ThreeSamples::ThreeCompat
{
    ParametricMesh buildParametricMesh(
        ParametricSurfaceEvaluator evaluator,
        const void *context,
        uint32_t slices,
        uint32_t stacks)
    {
        if (evaluator == nullptr)
        {
            throw std::invalid_argument(
                "A parametric mesh evaluator cannot be null.");
        }
        if (slices == 0u || stacks == 0u)
        {
            throw std::invalid_argument(
                "A parametric mesh requires nonzero slices and stacks.");
        }

        constexpr double NormalEpsilon = 0.00001;
        ParametricMesh mesh;
        const uint32_t sliceCount = slices + 1u;
        const size_t vertexCount =
            static_cast<size_t>(sliceCount) *
            (stacks + 1u);
        mesh.positions.reserve(vertexCount);
        mesh.normals.reserve(vertexCount);
        mesh.textureCoordinates.reserve(vertexCount);
        mesh.indices.reserve(
            static_cast<size_t>(slices) *
            stacks *
            6u);

        for (uint32_t stack = 0u;
             stack <= stacks;
             ++stack)
        {
            const double v =
                double(stack) / double(stacks);
            for (uint32_t slice = 0u;
                 slice <= slices;
                 ++slice)
            {
                const double u =
                    double(slice) / double(slices);
                const glm::dvec3 point =
                    evaluator(u, v, context);
                const glm::dvec3 tangentU =
                    u - NormalEpsilon >= 0.0
                    ? point -
                        evaluator(
                            u - NormalEpsilon,
                            v,
                            context)
                    : evaluator(
                            u + NormalEpsilon,
                            v,
                            context) -
                        point;
                const glm::dvec3 tangentV =
                    v - NormalEpsilon >= 0.0
                    ? point -
                        evaluator(
                            u,
                            v - NormalEpsilon,
                            context)
                    : evaluator(
                            u,
                            v + NormalEpsilon,
                            context) -
                        point;
                const glm::dvec3 crossNormal =
                    glm::cross(tangentU, tangentV);
                const double normalLength =
                    glm::length(crossNormal);
                const glm::dvec3 normal =
                    normalLength > 0.0
                    ? crossNormal / normalLength
                    : glm::dvec3(0.0);
                mesh.positions.push_back(
                    glm::vec3(point));
                mesh.normals.push_back(
                    glm::vec3(normal));
                mesh.textureCoordinates.push_back(
                    glm::vec2(float(u), float(v)));
            }
        }

        for (uint32_t stack = 0u;
             stack < stacks;
             ++stack)
        {
            for (uint32_t slice = 0u;
                 slice < slices;
                 ++slice)
            {
                const uint32_t a =
                    stack * sliceCount + slice;
                const uint32_t b = a + 1u;
                const uint32_t c =
                    (stack + 1u) * sliceCount +
                    slice + 1u;
                const uint32_t d = c - 1u;
                mesh.indices.insert(
                    mesh.indices.end(),
                    {a, b, d, b, c, d});
            }
        }
        return mesh;
    }
}
