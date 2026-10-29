#pragma once

#include "Object3D.hpp"

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Stores backend-neutral camera transforms, projection matrices, and a deterministic frustum. */
    class Camera : public Object3D
    {
    public:
        /** Destroys a camera through its concrete projection type. */
        ~Camera() override = default;

        /** Rebuilds this camera's concrete projection matrix from its current parameters. */
        virtual void updateProjectionMatrix() = 0;

        /** Selects the clip-space depth interval and immediately rebuilds the projection. */
        void setClipSpaceDepthRange(ClipSpaceDepthRange depthRange);

        /** Returns the clip-space depth interval used by projection and frustum extraction. */
        [[nodiscard]] ClipSpaceDepthRange getClipSpaceDepthRange() const;

        /** Updates world, view, view-projection, and frustum state for deterministic culling. */
        void updateCameraMatrices();

        /** Returns the current projection matrix. */
        [[nodiscard]] const glm::mat4 &getProjectionMatrix() const;

        /** Returns the inverse current projection matrix. */
        [[nodiscard]] const glm::mat4 &getProjectionMatrixInverse() const;

        /** Returns the inverse camera world matrix after updateCameraMatrices(). */
        [[nodiscard]] const glm::mat4 &getViewMatrix() const;

        /** Returns projection multiplied by view after updateCameraMatrices(). */
        [[nodiscard]] const glm::mat4 &getViewProjectionMatrix() const;

        /** Returns the world-space clipping frustum after updateCameraMatrices(). */
        [[nodiscard]] const Frustum &getFrustum() const;

    protected:
        /** Creates camera state for a concrete projection type and clip-space convention. */
        explicit Camera(ClipSpaceDepthRange depthRange);

        /** Commits a validated concrete projection and records projection dirtiness. */
        void commitProjectionMatrix(const glm::mat4 &matrix);

    private:
        ClipSpaceDepthRange depthRange;
        glm::mat4 projectionMatrix = glm::mat4(1.0f);
        glm::mat4 projectionMatrixInverse = glm::mat4(1.0f);
        glm::mat4 viewMatrix = glm::mat4(1.0f);
        glm::mat4 viewProjectionMatrix = glm::mat4(1.0f);
        Frustum frustum;
    };

    /** Implements a right-handed perspective camera compatible with Three.js projection formulas. */
    class PerspectiveCamera final : public Camera
    {
    public:
        /** Creates a perspective camera from vertical field of view, aspect, and clipping distances. */
        PerspectiveCamera(
            float verticalFieldOfViewDegrees = 50.0f,
            float aspectRatio = 1.0f,
            float nearDistance = 0.1f,
            float farDistance = 2000.0f,
            ClipSpaceDepthRange depthRange = ClipSpaceDepthRange::NegativeOneToOne);

        /** Replaces all perspective parameters and immediately rebuilds the projection matrix. */
        void setProjection(
            float verticalFieldOfViewDegrees,
            float aspectRatio,
            float nearDistance,
            float farDistance,
            float zoom = 1.0f);

        /** Rebuilds the perspective matrix from current parameters and clip-space depth range. */
        void updateProjectionMatrix() override;

        /** Returns the vertical field of view in degrees. */
        [[nodiscard]] float getVerticalFieldOfViewDegrees() const;

        /** Returns the projection aspect ratio. */
        [[nodiscard]] float getAspectRatio() const;

        /** Returns the near clipping distance. */
        [[nodiscard]] float getNearDistance() const;

        /** Returns the far clipping distance. */
        [[nodiscard]] float getFarDistance() const;

        /** Returns the projection zoom multiplier. */
        [[nodiscard]] float getZoom() const;

    private:
        float verticalFieldOfViewDegrees;
        float aspectRatio;
        float nearDistance;
        float farDistance;
        float zoom;
    };

    /** Implements a right-handed orthographic camera compatible with Three.js projection formulas. */
    class OrthographicCamera final : public Camera
    {
    public:
        /** Creates an orthographic camera from view-plane edges and clipping distances. */
        OrthographicCamera(
            float left = -1.0f,
            float right = 1.0f,
            float top = 1.0f,
            float bottom = -1.0f,
            float nearDistance = 0.1f,
            float farDistance = 2000.0f,
            ClipSpaceDepthRange depthRange = ClipSpaceDepthRange::NegativeOneToOne);

        /** Replaces all orthographic parameters and immediately rebuilds the projection matrix. */
        void setProjection(
            float left,
            float right,
            float top,
            float bottom,
            float nearDistance,
            float farDistance,
            float zoom = 1.0f);

        /** Rebuilds the orthographic matrix from current parameters and clip-space depth range. */
        void updateProjectionMatrix() override;

        /** Returns the unzoomed left view-plane edge. */
        [[nodiscard]] float getLeft() const;

        /** Returns the unzoomed right view-plane edge. */
        [[nodiscard]] float getRight() const;

        /** Returns the unzoomed top view-plane edge. */
        [[nodiscard]] float getTop() const;

        /** Returns the unzoomed bottom view-plane edge. */
        [[nodiscard]] float getBottom() const;

        /** Returns the near clipping distance. */
        [[nodiscard]] float getNearDistance() const;

        /** Returns the far clipping distance. */
        [[nodiscard]] float getFarDistance() const;

        /** Returns the projection zoom multiplier. */
        [[nodiscard]] float getZoom() const;

    private:
        float left;
        float right;
        float top;
        float bottom;
        float nearDistance;
        float farDistance;
        float zoom;
    };
} // namespace GVM::ThreeSamples::ThreeCompat
