#include "Camera.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <stdexcept>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        constexpr float InvertibleMatrixEpsilon = 1.0e-8f;

        /** Returns true when one scalar projection parameter is finite. */
        bool isFinite(float value)
        {
            return std::isfinite(value);
        }

        /** Validates clipping distances shared by both projection types. */
        void validateClippingDistances(float nearDistance, float farDistance, bool requirePositiveNear)
        {
            if (!isFinite(nearDistance) || !isFinite(farDistance) ||
                (requirePositiveNear ? nearDistance <= 0.0f : nearDistance < 0.0f) ||
                farDistance <= nearDistance)
            {
                throw std::invalid_argument("Camera clipping distances must be finite with far greater than near.");
            }
        }
    } // namespace

    Camera::Camera(ClipSpaceDepthRange depthRangeValue)
        : depthRange(depthRangeValue)
    {
    }

    void Camera::setClipSpaceDepthRange(ClipSpaceDepthRange depthRangeValue)
    {
        if (depthRange == depthRangeValue)
        {
            return;
        }
        depthRange = depthRangeValue;
        updateProjectionMatrix();
    }

    ClipSpaceDepthRange Camera::getClipSpaceDepthRange() const
    {
        return depthRange;
    }

    void Camera::updateCameraMatrices()
    {
        updateWorldMatrix(false);
        const float determinant = glm::determinant(getWorldMatrix());
        if (!std::isfinite(determinant) || std::abs(determinant) <= InvertibleMatrixEpsilon)
        {
            throw std::runtime_error("Camera world matrix must be finite and invertible.");
        }
        viewMatrix = glm::inverse(getWorldMatrix());
        viewProjectionMatrix = projectionMatrix * viewMatrix;
        frustum.setFromViewProjection(viewProjectionMatrix, depthRange);
    }

    const glm::mat4 &Camera::getProjectionMatrix() const
    {
        return projectionMatrix;
    }

    const glm::mat4 &Camera::getProjectionMatrixInverse() const
    {
        return projectionMatrixInverse;
    }

    const glm::mat4 &Camera::getViewMatrix() const
    {
        return viewMatrix;
    }

    const glm::mat4 &Camera::getViewProjectionMatrix() const
    {
        return viewProjectionMatrix;
    }

    const Frustum &Camera::getFrustum() const
    {
        return frustum;
    }

    void Camera::commitProjectionMatrix(const glm::mat4 &matrix)
    {
        const float determinant = glm::determinant(matrix);
        if (!std::isfinite(determinant) || std::abs(determinant) <= InvertibleMatrixEpsilon)
        {
            throw std::invalid_argument("Camera projection matrix must be finite and invertible.");
        }
        projectionMatrix = matrix;
        projectionMatrixInverse = glm::inverse(matrix);
        markDirty(ObjectChangeFlags::Projection);
    }

    PerspectiveCamera::PerspectiveCamera(
        float verticalFieldOfViewDegreesValue,
        float aspectRatioValue,
        float nearDistanceValue,
        float farDistanceValue,
        ClipSpaceDepthRange depthRange)
        : Camera(depthRange)
    {
        setProjection(
            verticalFieldOfViewDegreesValue,
            aspectRatioValue,
            nearDistanceValue,
            farDistanceValue);
    }

    void PerspectiveCamera::setProjection(
        float verticalFieldOfViewDegreesValue,
        float aspectRatioValue,
        float nearDistanceValue,
        float farDistanceValue,
        float zoomValue)
    {
        if (!isFinite(verticalFieldOfViewDegreesValue) ||
            verticalFieldOfViewDegreesValue <= 0.0f || verticalFieldOfViewDegreesValue >= 180.0f ||
            !isFinite(aspectRatioValue) || aspectRatioValue <= 0.0f ||
            !isFinite(zoomValue) || zoomValue <= 0.0f)
        {
            throw std::invalid_argument("Perspective camera requires finite positive aspect and zoom, and field of view in (0, 180).");
        }
        validateClippingDistances(nearDistanceValue, farDistanceValue, true);

        verticalFieldOfViewDegrees = verticalFieldOfViewDegreesValue;
        aspectRatio = aspectRatioValue;
        nearDistance = nearDistanceValue;
        farDistance = farDistanceValue;
        zoom = zoomValue;
        updateProjectionMatrix();
    }

    void PerspectiveCamera::updateProjectionMatrix()
    {
        const float top = nearDistance *
                          std::tan(glm::radians(verticalFieldOfViewDegrees) * 0.5f) /
                          zoom;
        const float bottom = -top;
        const float right = top * aspectRatio;
        const float left = -right;
        const float width = right - left;
        const float height = top - bottom;
        const float depth = farDistance - nearDistance;

        glm::mat4 projection(0.0f);
        projection[0u][0u] = 2.0f * nearDistance / width;
        projection[1u][1u] = 2.0f * nearDistance / height;
        projection[2u][0u] = (right + left) / width;
        projection[2u][1u] = (top + bottom) / height;
        projection[2u][3u] = -1.0f;
        if (getClipSpaceDepthRange() == ClipSpaceDepthRange::ZeroToOne)
        {
            projection[2u][2u] = -farDistance / depth;
            projection[3u][2u] = -(farDistance * nearDistance) / depth;
        }
        else
        {
            projection[2u][2u] = -(farDistance + nearDistance) / depth;
            projection[3u][2u] = -(2.0f * farDistance * nearDistance) / depth;
        }
        commitProjectionMatrix(projection);
    }

    float PerspectiveCamera::getVerticalFieldOfViewDegrees() const
    {
        return verticalFieldOfViewDegrees;
    }

    float PerspectiveCamera::getAspectRatio() const
    {
        return aspectRatio;
    }

    float PerspectiveCamera::getNearDistance() const
    {
        return nearDistance;
    }

    float PerspectiveCamera::getFarDistance() const
    {
        return farDistance;
    }

    float PerspectiveCamera::getZoom() const
    {
        return zoom;
    }

    OrthographicCamera::OrthographicCamera(
        float leftValue,
        float rightValue,
        float topValue,
        float bottomValue,
        float nearDistanceValue,
        float farDistanceValue,
        ClipSpaceDepthRange depthRange)
        : Camera(depthRange)
    {
        setProjection(
            leftValue,
            rightValue,
            topValue,
            bottomValue,
            nearDistanceValue,
            farDistanceValue);
    }

    void OrthographicCamera::setProjection(
        float leftValue,
        float rightValue,
        float topValue,
        float bottomValue,
        float nearDistanceValue,
        float farDistanceValue,
        float zoomValue)
    {
        if (!isFinite(leftValue) || !isFinite(rightValue) ||
            !isFinite(topValue) || !isFinite(bottomValue) ||
            leftValue == rightValue || topValue == bottomValue ||
            !isFinite(zoomValue) || zoomValue <= 0.0f)
        {
            throw std::invalid_argument("Orthographic camera requires finite distinct edges and positive zoom.");
        }
        validateClippingDistances(nearDistanceValue, farDistanceValue, false);

        left = leftValue;
        right = rightValue;
        top = topValue;
        bottom = bottomValue;
        nearDistance = nearDistanceValue;
        farDistance = farDistanceValue;
        zoom = zoomValue;
        updateProjectionMatrix();
    }

    void OrthographicCamera::updateProjectionMatrix()
    {
        const float centerX = (left + right) * 0.5f;
        const float centerY = (top + bottom) * 0.5f;
        const float halfWidth = (right - left) * 0.5f / zoom;
        const float halfHeight = (top - bottom) * 0.5f / zoom;
        const float zoomedLeft = centerX - halfWidth;
        const float zoomedRight = centerX + halfWidth;
        const float zoomedTop = centerY + halfHeight;
        const float zoomedBottom = centerY - halfHeight;
        const float width = zoomedRight - zoomedLeft;
        const float height = zoomedTop - zoomedBottom;
        const float depth = farDistance - nearDistance;

        glm::mat4 projection(1.0f);
        projection[0u][0u] = 2.0f / width;
        projection[1u][1u] = 2.0f / height;
        projection[3u][0u] = -(zoomedRight + zoomedLeft) / width;
        projection[3u][1u] = -(zoomedTop + zoomedBottom) / height;
        if (getClipSpaceDepthRange() == ClipSpaceDepthRange::ZeroToOne)
        {
            projection[2u][2u] = -1.0f / depth;
            projection[3u][2u] = -nearDistance / depth;
        }
        else
        {
            projection[2u][2u] = -2.0f / depth;
            projection[3u][2u] = -(farDistance + nearDistance) / depth;
        }
        commitProjectionMatrix(projection);
    }

    float OrthographicCamera::getLeft() const
    {
        return left;
    }

    float OrthographicCamera::getRight() const
    {
        return right;
    }

    float OrthographicCamera::getTop() const
    {
        return top;
    }

    float OrthographicCamera::getBottom() const
    {
        return bottom;
    }

    float OrthographicCamera::getNearDistance() const
    {
        return nearDistance;
    }

    float OrthographicCamera::getFarDistance() const
    {
        return farDistance;
    }

    float OrthographicCamera::getZoom() const
    {
        return zoom;
    }
} // namespace GVM::ThreeSamples::ThreeCompat
