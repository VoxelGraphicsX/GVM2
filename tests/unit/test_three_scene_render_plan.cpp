#include <SceneRenderPlan.hpp>

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        /** Creates a three-vertex non-indexed geometry to exercise canonical index generation. */
        SceneGeometrySource createTriangleGeometry(float xOffset = 0.0f)
        {
            SceneGeometrySource geometry;
            for (uint32_t vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
            {
                SceneVertexData vertex;
                vertex.position = glm::vec3(xOffset + static_cast<float>(vertexIndex), 0.0f, 0.0f);
                geometry.vertices.push_back(vertex);
            }
            return geometry;
        }

        /** Creates an indexed quad whose two triangle groups can select separate materials. */
        SceneGeometrySource createGroupedQuadGeometry()
        {
            SceneGeometrySource geometry;
            const glm::vec3 positions[] = {
                glm::vec3(-1.0f, -1.0f, 0.0f),
                glm::vec3(1.0f, -1.0f, 0.0f),
                glm::vec3(1.0f, 1.0f, 0.0f),
                glm::vec3(-1.0f, 1.0f, 0.0f),
            };
            for (const glm::vec3 &position : positions)
            {
                SceneVertexData vertex;
                vertex.position = position;
                geometry.vertices.push_back(vertex);
            }
            const uint32_t indices[] = {0u, 1u, 2u, 2u, 3u, 0u};
            for (uint32_t index : indices)
            {
                geometry.indices.push_back(index);
            }

            SceneGeometryGroup firstGroup;
            firstGroup.firstIndex = 0u;
            firstGroup.indexCount = 3u;
            firstGroup.materialSlot = 0u;
            geometry.groups.push_back(firstGroup);

            SceneGeometryGroup secondGroup;
            secondGroup.firstIndex = 3u;
            secondGroup.indexCount = 3u;
            secondGroup.materialSlot = 1u;
            geometry.groups.push_back(secondGroup);
            return geometry;
        }
    } // namespace

    TEST(ThreeSceneRenderPlan, PacksTwoObjectsIntoOneDeterministicScenePlan)
    {
        SceneGeometrySource triangle = createTriangleGeometry();
        SceneGeometrySource quad = createGroupedQuadGeometry();
        quad.groups.clear();
        SceneMaterialSource firstMaterial;
        firstMaterial.baseColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        SceneMaterialSource secondMaterial;
        secondMaterial.baseColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

        RenderableObject firstObject(triangle, firstMaterial);
        RenderableObject secondObject(quad, secondMaterial);
        Object3D loaderRoot;
        Scene scene;
        ASSERT_TRUE(scene.addChild(firstObject));
        ASSERT_TRUE(scene.addChild(loaderRoot));
        ASSERT_TRUE(loaderRoot.addChild(secondObject));

        SceneRenderPlanner planner;
        const SceneRenderPlan &plan = planner.plan(scene);
        ASSERT_EQ(planner.getPlanCount(), 1u);
        ASSERT_EQ(plan.getRenderSetCount(), 1u);
        ASSERT_EQ(plan.getEntities().size(), 2u);
        ASSERT_EQ(plan.getGeometries().size(), 2u);
        ASSERT_EQ(plan.getVertices().size(), 7u);
        ASSERT_EQ(plan.getIndices().size(), 9u);
        ASSERT_EQ(plan.getObjects().size(), 2u);
        ASSERT_EQ(plan.getInstances().size(), 2u);
        ASSERT_EQ(plan.getMaterials().size(), 2u);

        const SceneEntityRecord &firstEntity = plan.getEntities()[0u];
        const SceneEntityRecord &secondEntity = plan.getEntities()[1u];
        EXPECT_EQ(firstEntity.sourceObjectId, firstObject.getId());
        EXPECT_EQ(firstEntity.deterministicOrder, 0u);
        EXPECT_EQ(firstEntity.vertexOffset, 0u);
        EXPECT_EQ(firstEntity.vertexCount, 3u);
        EXPECT_EQ(firstEntity.indexOffset, 0u);
        EXPECT_EQ(firstEntity.indexCount, 3u);
        EXPECT_EQ(firstEntity.instanceCount, 1u);
        EXPECT_EQ(secondEntity.sourceObjectId, secondObject.getId());
        EXPECT_EQ(secondEntity.deterministicOrder, 1u);
        EXPECT_EQ(secondEntity.vertexOffset, 3u);
        EXPECT_EQ(secondEntity.vertexCount, 4u);
        EXPECT_EQ(secondEntity.indexOffset, 3u);
        EXPECT_EQ(secondEntity.indexCount, 6u);
        EXPECT_EQ(plan.getIndices()[0u], 0u);
        EXPECT_EQ(plan.getIndices()[2u], 2u);
        EXPECT_EQ(plan.getPlanIdentity(), scene.getId());
        EXPECT_EQ(plan.getSceneIdentity(), scene.getId());
    }

    TEST(ThreeSceneRenderPlan, KeepsMultipleInstancesInOneEntityAndOneContiguousComponentRange)
    {
        SceneGeometrySource geometry = createTriangleGeometry();
        SceneMaterialSource material;
        RenderableObject object(geometry, material);

        eastl::vector<SceneInstanceSource> instances;
        for (uint32_t instanceIndex = 0u; instanceIndex < 3u; ++instanceIndex)
        {
            SceneInstanceSource instance;
            instance.transform = glm::translate(
                glm::mat4(1.0f),
                glm::vec3(static_cast<float>(instanceIndex) * 2.0f, 0.0f, 0.0f));
            instance.color = glm::vec4(static_cast<float>(instanceIndex), 0.5f, 1.0f, 1.0f);
            if (instanceIndex == 1u)
            {
                instance.pickingId = 99u;
            }
            instances.push_back(instance);
        }
        object.setInstances(instances);

        Scene scene;
        scene.addChild(object);
        SceneRenderPlanner planner;
        const SceneRenderPlan &plan = planner.plan(scene);

        ASSERT_EQ(plan.getEntities().size(), 1u);
        ASSERT_EQ(plan.getInstances().size(), 3u);
        const SceneEntityRecord &entity = plan.getEntities()[0u];
        EXPECT_EQ(entity.instanceComponentOffset, 0u);
        EXPECT_EQ(entity.instanceCount, 3u);
        EXPECT_EQ(plan.getInstances()[0u].sourceInstanceIndex, 0u);
        EXPECT_EQ(plan.getInstances()[0u].pickingId, 1u);
        EXPECT_EQ(plan.getInstances()[1u].pickingId, 99u);
        EXPECT_EQ(plan.getInstances()[2u].pickingId, 2u);
        EXPECT_FLOAT_EQ(plan.getInstances()[2u].transform[3u][0u], 4.0f);
        EXPECT_FLOAT_EQ(plan.getInstances()[2u].color.x, 2.0f);
    }

    TEST(ThreeSceneRenderPlan, MapsMaterialGroupsInsideOneEntityWithoutDuplicatingComponents)
    {
        SceneGeometrySource geometry = createGroupedQuadGeometry();
        SceneMaterialSource opaqueMaterial;
        opaqueMaterial.baseColor = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
        SceneMaterialSource transparentMaterial;
        transparentMaterial.baseColor = glm::vec4(1.0f, 0.5f, 0.0f, 0.5f);
        transparentMaterial.opacity = 0.5f;
        transparentMaterial.phase = SceneMaterialPhase::Transparent;

        RenderableObject object(geometry, opaqueMaterial);
        object.addMaterial(transparentMaterial);
        Scene scene;
        scene.addChild(object);
        SceneRenderPlanner planner;
        const SceneRenderPlan &plan = planner.plan(scene);

        ASSERT_EQ(plan.getGeometries().size(), 1u);
        ASSERT_EQ(plan.getVertices().size(), 6u);
        ASSERT_EQ(plan.getIndices().size(), 6u);
        ASSERT_EQ(plan.getEntities().size(), 1u);
        ASSERT_EQ(plan.getObjects().size(), 1u);
        ASSERT_EQ(plan.getInstances().size(), 1u);
        ASSERT_EQ(plan.getMaterials().size(), 2u);
        const SceneEntityRecord &entity = plan.getEntities()[0u];
        EXPECT_EQ(entity.sourceObjectId, object.getId());
        EXPECT_EQ(entity.sourceGroupCount, 2u);
        EXPECT_EQ(entity.geometryIndex, 0u);
        EXPECT_EQ(entity.vertexOffset, 0u);
        EXPECT_EQ(entity.vertexCount, 6u);
        EXPECT_EQ(entity.indexOffset, 0u);
        EXPECT_EQ(entity.indexCount, 6u);
        EXPECT_EQ(entity.materialComponentOffset, 0u);
        EXPECT_EQ(entity.materialComponentCount, 2u);
        EXPECT_EQ(
            entity.materialPhaseMask,
            getSceneMaterialPhaseMask(SceneMaterialPhase::Opaque) |
                getSceneMaterialPhaseMask(SceneMaterialPhase::Transparent));
        EXPECT_EQ(plan.getGeometries()[0u].requiredMaterialSlotCount, 2u);
        EXPECT_EQ(plan.getVertices()[0u].materialSlot, 0u);
        EXPECT_EQ(plan.getVertices()[2u].materialSlot, 0u);
        EXPECT_EQ(plan.getVertices()[3u].materialSlot, 1u);
        EXPECT_EQ(plan.getVertices()[5u].materialSlot, 1u);
        EXPECT_EQ(plan.getMaterials()[0u].sourceMaterialSlot, 0u);
        EXPECT_EQ(plan.getMaterials()[1u].sourceMaterialSlot, 1u);
        EXPECT_EQ(plan.getMaterials()[1u].phase, SceneMaterialPhase::Transparent);
        EXPECT_EQ(plan.getInstances()[0u].pickingId, 1u);

        const ScenePassMetadata &opaquePass = plan.getMainPhaseMetadata(SceneMaterialPhase::Opaque);
        const ScenePassMetadata &transparentPass = plan.getMainPhaseMetadata(SceneMaterialPhase::Transparent);
        EXPECT_EQ(opaquePass.planIdentity, plan.getPlanIdentity());
        EXPECT_EQ(opaquePass.entityCount, 1u);
        EXPECT_EQ(opaquePass.materialPhaseMask, getSceneMaterialPhaseMask(SceneMaterialPhase::Opaque));
        EXPECT_EQ(transparentPass.planIdentity, plan.getPlanIdentity());
        EXPECT_EQ(transparentPass.entityCount, 1u);
        EXPECT_EQ(
            transparentPass.materialPhaseMask,
            getSceneMaterialPhaseMask(SceneMaterialPhase::Transparent));
    }

    TEST(ThreeSceneRenderPlan, LoaderHierarchyAndAllGeometryPassesReuseOneStablePlan)
    {
        SceneGeometrySource sharedGeometry = createTriangleGeometry();
        SceneMaterialSource sharedMaterial;
        RenderableObject firstMesh(sharedGeometry, sharedMaterial);
        RenderableObject secondMesh(sharedGeometry, sharedMaterial);
        firstMesh.setCastShadow(true);
        secondMesh.setReceiveShadow(true);

        Object3D loaderRoot;
        Object3D nestedNode;
        Scene scene;
        scene.addChild(loaderRoot);
        loaderRoot.addChild(firstMesh);
        loaderRoot.addChild(nestedNode);
        nestedNode.addChild(secondMesh);

        SceneRenderPlanner planner;
        const SceneRenderPlan &firstPlan = planner.plan(scene);
        const SceneRenderPlan *firstAddress = &firstPlan;
        const uint64_t firstRevision = firstPlan.getRevision();
        ASSERT_EQ(firstPlan.getEntities().size(), 2u);
        ASSERT_EQ(firstPlan.getGeometries().size(), 1u);
        ASSERT_EQ(firstPlan.getMaterials().size(), 2u);
        EXPECT_EQ(firstPlan.getEntities()[0u].sourceObjectId, firstMesh.getId());
        EXPECT_EQ(firstPlan.getEntities()[1u].sourceObjectId, secondMesh.getId());
        EXPECT_EQ(firstPlan.getObjects()[0u].castShadow, 1u);
        EXPECT_EQ(firstPlan.getObjects()[1u].receiveShadow, 1u);

        for (uint32_t passIndex = 0u; passIndex < static_cast<uint32_t>(ScenePassKind::Count); ++passIndex)
        {
            const ScenePassMetadata &metadata = firstPlan.getPassMetadata(static_cast<ScenePassKind>(passIndex));
            EXPECT_EQ(metadata.sceneIdentity, scene.getId());
            EXPECT_EQ(metadata.planIdentity, firstPlan.getPlanIdentity());
            EXPECT_EQ(metadata.planRevision, firstRevision);
            EXPECT_EQ(metadata.renderSetIndex, 0u);
            EXPECT_EQ(metadata.firstEntity, 0u);
            EXPECT_EQ(metadata.entityCount, 2u);
        }

        firstMesh.setPosition(glm::vec3(3.0f, 0.0f, 0.0f));
        const SceneRenderPlan &rebuiltPlan = planner.plan(scene);
        EXPECT_EQ(&rebuiltPlan, firstAddress);
        EXPECT_EQ(planner.findPlan(scene), firstAddress);
        EXPECT_EQ(rebuiltPlan.getRevision(), firstRevision + 1u);
        EXPECT_FLOAT_EQ(rebuiltPlan.getObjects()[0u].previousWorldTransform[3u][0u], 0.0f);
        EXPECT_FLOAT_EQ(rebuiltPlan.getObjects()[0u].worldTransform[3u][0u], 3.0f);
        EXPECT_EQ(rebuiltPlan.getPassMetadata(ScenePassKind::Shadow).planRevision, rebuiltPlan.getRevision());
        EXPECT_TRUE(planner.releasePlan(scene));
        EXPECT_EQ(planner.getPlanCount(), 0u);
        EXPECT_EQ(planner.findPlan(scene), nullptr);
        EXPECT_FALSE(planner.releasePlan(scene));
    }
} // namespace GVM::ThreeSamples::ThreeCompat
