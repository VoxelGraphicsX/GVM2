#include <gtest/gtest.h>

#include <Camera.hpp>
#include <DeterministicRandom.hpp>
#include <Math.hpp>
#include <Object3D.hpp>

#include <glm/gtc/quaternion.hpp>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        constexpr float MatrixTolerance = 1.0e-5f;

        /** Verifies every component of a vector with a shared absolute tolerance. */
        void expectVectorNear(const glm::vec3 &actual, const glm::vec3 &expected)
        {
            EXPECT_NEAR(actual.x, expected.x, MatrixTolerance);
            EXPECT_NEAR(actual.y, expected.y, MatrixTolerance);
            EXPECT_NEAR(actual.z, expected.z, MatrixTolerance);
        }

        /** Verifies a 4x4 matrix is approximately the identity matrix. */
        void expectIdentity(const glm::mat4 &matrix)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                for (uint32_t row = 0u; row < 4u; ++row)
                {
                    EXPECT_NEAR(
                        matrix[column][row],
                        column == row ? 1.0f : 0.0f,
                        MatrixTolerance);
                }
            }
        }
    } // namespace

    TEST(ThreeCompatObject3D, ParentChildTransformsAndNormalMatrixAreDeterministic)
    {
        Scene scene;
        Object3D parent;
        Object3D child;

        parent.setLocalTransform(
            glm::vec3(1.0f, 2.0f, 3.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec3(2.0f, 4.0f, 0.5f));
        child.setPosition(glm::vec3(2.0f, 0.0f, -4.0f));
        ASSERT_TRUE(scene.addChild(parent));
        ASSERT_TRUE(parent.addChild(child));

        scene.updateWorldMatrix();

        expectVectorNear(glm::vec3(parent.getWorldMatrix()[3u]), glm::vec3(1.0f, 2.0f, 3.0f));
        expectVectorNear(glm::vec3(child.getWorldMatrix()[3u]), glm::vec3(5.0f, 2.0f, 1.0f));
        EXPECT_NEAR(parent.getNormalMatrix()[0u][0u], 0.5f, MatrixTolerance);
        EXPECT_NEAR(parent.getNormalMatrix()[1u][1u], 0.25f, MatrixTolerance);
        EXPECT_NEAR(parent.getNormalMatrix()[2u][2u], 2.0f, MatrixTolerance);

        const glm::mat4 expectedLocal = child.getLocalMatrix();
        EXPECT_NEAR(expectedLocal[3u][0u], 2.0f, MatrixTolerance);
        EXPECT_NEAR(expectedLocal[3u][2u], -4.0f, MatrixTolerance);
    }

    TEST(ThreeCompatCamera, PerspectiveAndOrthographicMatricesMatchThreeConventions)
    {
        PerspectiveCamera perspective(
            90.0f,
            2.0f,
            1.0f,
            11.0f,
            ClipSpaceDepthRange::NegativeOneToOne);
        const glm::mat4 &perspectiveMatrix = perspective.getProjectionMatrix();
        EXPECT_NEAR(perspectiveMatrix[0u][0u], 0.5f, MatrixTolerance);
        EXPECT_NEAR(perspectiveMatrix[1u][1u], 1.0f, MatrixTolerance);
        EXPECT_NEAR(perspectiveMatrix[2u][2u], -1.2f, MatrixTolerance);
        EXPECT_NEAR(perspectiveMatrix[2u][3u], -1.0f, MatrixTolerance);
        EXPECT_NEAR(perspectiveMatrix[3u][2u], -2.2f, MatrixTolerance);
        expectIdentity(perspectiveMatrix * perspective.getProjectionMatrixInverse());

        perspective.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
        perspective.updateCameraMatrices();
        EXPECT_NEAR(perspective.getViewMatrix()[3u][2u], -5.0f, MatrixTolerance);

        perspective.setClipSpaceDepthRange(ClipSpaceDepthRange::ZeroToOne);
        EXPECT_NEAR(perspective.getProjectionMatrix()[2u][2u], -1.1f, MatrixTolerance);
        EXPECT_NEAR(perspective.getProjectionMatrix()[3u][2u], -1.1f, MatrixTolerance);

        OrthographicCamera orthographic(-2.0f, 2.0f, 1.0f, -1.0f, 1.0f, 11.0f);
        orthographic.setProjection(-2.0f, 2.0f, 1.0f, -1.0f, 1.0f, 11.0f, 2.0f);
        EXPECT_NEAR(orthographic.getProjectionMatrix()[0u][0u], 1.0f, MatrixTolerance);
        EXPECT_NEAR(orthographic.getProjectionMatrix()[1u][1u], 2.0f, MatrixTolerance);
        EXPECT_NEAR(orthographic.getProjectionMatrix()[2u][2u], -0.2f, MatrixTolerance);
        EXPECT_NEAR(orthographic.getProjectionMatrix()[3u][2u], -1.2f, MatrixTolerance);
        expectIdentity(orthographic.getProjectionMatrix() * orthographic.getProjectionMatrixInverse());
    }

    TEST(ThreeCompatBounds, WorldBoundsSubtreeUnionAndFrustumTestsAreStable)
    {
        Scene scene;
        Object3D inside;
        Object3D outside;
        inside.setLocalBounds(Bounds3(glm::vec3(-1.0f), glm::vec3(1.0f)));
        outside.setLocalBounds(Bounds3(glm::vec3(-0.5f), glm::vec3(0.5f)));
        inside.setPosition(glm::vec3(3.0f, 0.0f, -5.0f));
        outside.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
        scene.addChild(inside);
        scene.addChild(outside);
        scene.updateWorldMatrix();

        expectVectorNear(inside.getWorldBounds().getMinimum(), glm::vec3(2.0f, -1.0f, -6.0f));
        expectVectorNear(inside.getWorldBounds().getMaximum(), glm::vec3(4.0f, 1.0f, -4.0f));
        const Bounds3 subtreeBounds = scene.computeSubtreeWorldBounds();
        expectVectorNear(subtreeBounds.getMinimum(), glm::vec3(-0.5f, -1.0f, -6.0f));
        expectVectorNear(subtreeBounds.getMaximum(), glm::vec3(4.0f, 1.0f, 5.5f));

        PerspectiveCamera camera(90.0f, 1.0f, 1.0f, 10.0f);
        camera.updateCameraMatrices();
        EXPECT_TRUE(camera.getFrustum().intersects(inside.getWorldBounds()));
        EXPECT_FALSE(camera.getFrustum().intersects(outside.getWorldBounds()));

        camera.setClipSpaceDepthRange(ClipSpaceDepthRange::ZeroToOne);
        camera.updateCameraMatrices();
        EXPECT_TRUE(camera.getFrustum().intersects(inside.getWorldBounds()));
        EXPECT_FALSE(camera.getFrustum().intersects(outside.getWorldBounds()));
    }

    TEST(ThreeCompatHierarchy, DynamicChangesPreserveIdsAndExposeDirtyState)
    {
        Scene firstScene;
        Scene secondScene;
        Object3D parent;
        Object3D child;
        const ObjectId parentId = parent.getId();
        const ObjectId childId = child.getId();
        ASSERT_NE(parentId, childId);

        firstScene.clearDirtyFlags();
        secondScene.clearDirtyFlags();
        parent.clearDirtyFlags();
        child.clearDirtyFlags();
        const uint64_t childVersion = child.getChangeVersion();

        ASSERT_TRUE(firstScene.addChild(parent));
        ASSERT_TRUE(parent.addChild(child));
        EXPECT_TRUE(hasAnyChange(firstScene.getDirtyFlags(), ObjectChangeFlags::Hierarchy));
        EXPECT_TRUE(hasAnyChange(child.getDirtyFlags(), ObjectChangeFlags::WorldTransform));
        EXPECT_GT(child.getChangeVersion(), childVersion);

        ASSERT_TRUE(secondScene.addChild(child));
        EXPECT_EQ(child.getParent(), &secondScene);
        EXPECT_EQ(child.getId(), childId);
        EXPECT_EQ(parent.getId(), parentId);
        EXPECT_TRUE(parent.getChildren().empty());
        EXPECT_THROW(child.addChild(secondScene), std::invalid_argument);

        ASSERT_TRUE(secondScene.removeChild(child));
        EXPECT_EQ(child.getParent(), nullptr);
        EXPECT_EQ(child.getId(), childId);
        EXPECT_FALSE(secondScene.removeChild(child));
    }

    TEST(ThreeCompatState, VisibilityLayersAndRenderDataChangesAreExplicit)
    {
        Object3D parent;
        Object3D child;
        parent.addChild(child);
        parent.clearDirtyFlags();
        child.clearDirtyFlags();

        parent.setVisible(false);
        EXPECT_FALSE(child.isWorldVisible());
        EXPECT_TRUE(hasAnyChange(child.getDirtyFlags(), ObjectChangeFlags::Visibility));
        parent.setVisible(true);
        EXPECT_TRUE(child.isWorldVisible());

        Layers selection;
        selection.set(3u);
        EXPECT_FALSE(child.intersectsLayers(selection));
        child.enableLayer(3u);
        EXPECT_TRUE(child.intersectsLayers(selection));
        EXPECT_TRUE(hasAnyChange(child.getDirtyFlags(), ObjectChangeFlags::Layers));

        child.clearDirtyFlags();
        const uint64_t version = child.getChangeVersion();
        child.markRenderDataDirty();
        EXPECT_TRUE(hasAnyChange(child.getDirtyFlags(), ObjectChangeFlags::RenderData));
        EXPECT_GT(child.getChangeVersion(), version);
        EXPECT_THROW(child.enableLayer(32u), std::out_of_range);
    }

    TEST(ThreeCompatRandom, MatchesTheReferenceXorshift32Stream)
    {
        DeterministicRandom random(0x12345678u);
        EXPECT_EQ(random.nextUint32(), 0x87985aa5u);
        EXPECT_EQ(random.nextUint32(), 0x155b24a3u);
        random.reset(0x12345678u);
        EXPECT_FLOAT_EQ(random.nextFloat(), 0.5296684503555298f);
        EXPECT_FLOAT_EQ(random.nextFloat(), 0.08342194557189941f);

        random.reset(0u);
        EXPECT_NE(random.getState(), 0u);
        EXPECT_NE(random.nextUint32(), 0u);
    }
} // namespace GVM::ThreeSamples::ThreeCompat
