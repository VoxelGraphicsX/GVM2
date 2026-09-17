#pragma once

#include <EASTL/array.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Selects the normalized-device-coordinate depth interval used by a projection matrix. */
    enum class ClipSpaceDepthRange
    {
        NegativeOneToOne,
        ZeroToOne,
    };

    /** Stores a deterministic axis-aligned bounding box, with an explicit empty state. */
    class Bounds3 final
    {
    public:
        /** Creates an empty bound that does not intersect any finite volume. */
        Bounds3();

        /** Creates a bound spanning the inclusive minimum and maximum corners. */
        Bounds3(const glm::vec3 &minimum, const glm::vec3 &maximum);

        /** Returns true when the bound contains no points. */
        [[nodiscard]] bool isEmpty() const;

        /** Resets the bound to its canonical empty representation. */
        void clear();

        /** Expands the bound to include one point. */
        void expandByPoint(const glm::vec3 &point);

        /** Expands the bound to include every point in another bound. */
        void include(const Bounds3 &bounds);

        /** Returns the minimum corner; callers must check isEmpty() before using it. */
        [[nodiscard]] const glm::vec3 &getMinimum() const;

        /** Returns the maximum corner; callers must check isEmpty() before using it. */
        [[nodiscard]] const glm::vec3 &getMaximum() const;

        /** Returns the center of the bound, or the zero vector for an empty bound. */
        [[nodiscard]] glm::vec3 getCenter() const;

        /** Returns the non-negative size of the bound, or the zero vector for an empty bound. */
        [[nodiscard]] glm::vec3 getSize() const;

        /** Returns a new axis-aligned bound containing this bound after matrix transformation. */
        [[nodiscard]] Bounds3 transformed(const glm::mat4 &matrix) const;

    private:
        glm::vec3 minimum;
        glm::vec3 maximum;
    };

    /** Stores one normalized plane using the equation dot(normal, point) + constant = 0. */
    class Plane final
    {
    public:
        /** Creates a degenerate plane that accepts every point. */
        Plane();

        /** Creates and normalizes a plane from its four equation coefficients. */
        explicit Plane(const glm::vec4 &coefficients);

        /** Replaces the plane coefficients and normalizes the resulting equation. */
        void setFromCoefficients(const glm::vec4 &coefficients);

        /** Returns the signed distance from the plane to a point. */
        [[nodiscard]] float distanceToPoint(const glm::vec3 &point) const;

        /** Returns the normalized plane normal. */
        [[nodiscard]] const glm::vec3 &getNormal() const;

        /** Returns the normalized plane constant. */
        [[nodiscard]] float getConstant() const;

    private:
        glm::vec3 normal;
        float constant;
    };

    /** Stores six normalized clipping planes extracted from a view-projection matrix. */
    class Frustum final
    {
    public:
        /** Creates a default frustum whose planes are initialized deterministically. */
        Frustum();

        /** Extracts clipping planes from a column-major view-projection matrix. */
        void setFromViewProjection(
            const glm::mat4 &viewProjection,
            ClipSpaceDepthRange depthRange);

        /** Returns true when an axis-aligned bound is at least partially inside the frustum. */
        [[nodiscard]] bool intersects(const Bounds3 &bounds) const;

        /** Returns the six planes in left, right, bottom, top, near, far order. */
        [[nodiscard]] const eastl::array<Plane, 6u> &getPlanes() const;

    private:
        eastl::array<Plane, 6u> planes;
    };
} // namespace GVM::ThreeSamples::ThreeCompat
