#include "Math.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        constexpr float PlaneNormalizationEpsilon = 1.0e-8f;

        /** Returns true when all components of a vector are finite. */
        bool isFinite(const glm::vec3 &value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        /** Returns one row from a column-major GLM matrix. */
        glm::vec4 matrixRow(const glm::mat4 &matrix, uint32_t row)
        {
            return {matrix[0u][row], matrix[1u][row], matrix[2u][row], matrix[3u][row]};
        }
    } // namespace

    Bounds3::Bounds3()
    {
        clear();
    }

    Bounds3::Bounds3(const glm::vec3 &minimumValue, const glm::vec3 &maximumValue)
        : minimum(minimumValue)
        , maximum(maximumValue)
    {
        if (!isFinite(minimumValue) || !isFinite(maximumValue))
        {
            throw std::invalid_argument("Bounds3 corners must be finite.");
        }
        if (glm::any(glm::greaterThan(minimumValue, maximumValue)))
        {
            throw std::invalid_argument("Bounds3 minimum must not exceed its maximum.");
        }
    }

    bool Bounds3::isEmpty() const
    {
        return glm::any(glm::greaterThan(minimum, maximum));
    }

    void Bounds3::clear()
    {
        const float infinity = std::numeric_limits<float>::infinity();
        minimum = glm::vec3(infinity);
        maximum = glm::vec3(-infinity);
    }

    void Bounds3::expandByPoint(const glm::vec3 &point)
    {
        if (!isFinite(point))
        {
            throw std::invalid_argument("Bounds3 points must be finite.");
        }
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    }

    void Bounds3::include(const Bounds3 &bounds)
    {
        if (bounds.isEmpty())
        {
            return;
        }
        minimum = glm::min(minimum, bounds.minimum);
        maximum = glm::max(maximum, bounds.maximum);
    }

    const glm::vec3 &Bounds3::getMinimum() const
    {
        return minimum;
    }

    const glm::vec3 &Bounds3::getMaximum() const
    {
        return maximum;
    }

    glm::vec3 Bounds3::getCenter() const
    {
        return isEmpty() ? glm::vec3(0.0f) : (minimum + maximum) * 0.5f;
    }

    glm::vec3 Bounds3::getSize() const
    {
        return isEmpty() ? glm::vec3(0.0f) : maximum - minimum;
    }

    Bounds3 Bounds3::transformed(const glm::mat4 &matrix) const
    {
        Bounds3 result;
        if (isEmpty())
        {
            return result;
        }

        for (uint32_t corner = 0u; corner < 8u; ++corner)
        {
            const glm::vec3 point{
                (corner & 1u) != 0u ? maximum.x : minimum.x,
                (corner & 2u) != 0u ? maximum.y : minimum.y,
                (corner & 4u) != 0u ? maximum.z : minimum.z,
            };
            const glm::vec4 transformedPoint = matrix * glm::vec4(point, 1.0f);
            if (std::abs(transformedPoint.w) <= PlaneNormalizationEpsilon)
            {
                throw std::invalid_argument("Bounds3 transformation produced a point at infinity.");
            }
            result.expandByPoint(glm::vec3(transformedPoint) / transformedPoint.w);
        }
        return result;
    }

    Plane::Plane()
        : normal(0.0f)
        , constant(0.0f)
    {
    }

    Plane::Plane(const glm::vec4 &coefficients)
        : Plane()
    {
        setFromCoefficients(coefficients);
    }

    void Plane::setFromCoefficients(const glm::vec4 &coefficients)
    {
        const glm::vec3 candidateNormal(coefficients);
        const float length = glm::length(candidateNormal);
        if (!std::isfinite(length) || length <= PlaneNormalizationEpsilon)
        {
            throw std::invalid_argument("A frustum plane must have a finite non-zero normal.");
        }
        normal = candidateNormal / length;
        constant = coefficients.w / length;
    }

    float Plane::distanceToPoint(const glm::vec3 &point) const
    {
        return glm::dot(normal, point) + constant;
    }

    const glm::vec3 &Plane::getNormal() const
    {
        return normal;
    }

    float Plane::getConstant() const
    {
        return constant;
    }

    Frustum::Frustum() = default;

    void Frustum::setFromViewProjection(
        const glm::mat4 &viewProjection,
        ClipSpaceDepthRange depthRange)
    {
        const glm::vec4 row0 = matrixRow(viewProjection, 0u);
        const glm::vec4 row1 = matrixRow(viewProjection, 1u);
        const glm::vec4 row2 = matrixRow(viewProjection, 2u);
        const glm::vec4 row3 = matrixRow(viewProjection, 3u);

        planes[0u].setFromCoefficients(row3 + row0);
        planes[1u].setFromCoefficients(row3 - row0);
        planes[2u].setFromCoefficients(row3 + row1);
        planes[3u].setFromCoefficients(row3 - row1);
        planes[4u].setFromCoefficients(
            depthRange == ClipSpaceDepthRange::ZeroToOne ? row2 : row3 + row2);
        planes[5u].setFromCoefficients(row3 - row2);
    }

    bool Frustum::intersects(const Bounds3 &bounds) const
    {
        if (bounds.isEmpty())
        {
            return false;
        }

        for (const Plane &plane : planes)
        {
            const glm::vec3 &normal = plane.getNormal();
            const glm::vec3 positiveVertex{
                normal.x >= 0.0f ? bounds.getMaximum().x : bounds.getMinimum().x,
                normal.y >= 0.0f ? bounds.getMaximum().y : bounds.getMinimum().y,
                normal.z >= 0.0f ? bounds.getMaximum().z : bounds.getMinimum().z,
            };
            if (plane.distanceToPoint(positiveVertex) < 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    const eastl::array<Plane, 6u> &Frustum::getPlanes() const
    {
        return planes;
    }
} // namespace GVM::ThreeSamples::ThreeCompat
