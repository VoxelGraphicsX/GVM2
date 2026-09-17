#include "SceneRenderPlan.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/unordered_set.h>
#include <EASTL/utility.h>

#include <glm/mat3x3.hpp>

#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        /** Stores the packed range assigned to one geometry source during a plan rebuild. */
        struct GeometryPackState final
        {
            uint32_t geometryIndex = 0u;
            uint32_t vertexOffset = 0u;
            uint32_t vertexCount = 0u;
            uint32_t indexOffset = 0u;
            uint32_t indexCount = 0u;
            uint32_t requiredMaterialSlotCount = 1u;
        };

        /** Converts a container size to the 32-bit RenderSet range domain with validation. */
        uint32_t checkedRangeSize(size_t value, const char *description)
        {
            if (value > std::numeric_limits<uint32_t>::max())
            {
                throw std::overflow_error(std::string(description) + " exceeds the 32-bit Scene packing range.");
            }
            return static_cast<uint32_t>(value);
        }

        /** Converts an Object3D normal matrix to a component-aligned homogeneous matrix. */
        glm::mat4 makeNormalTransform(const glm::mat3 &normalMatrix)
        {
            glm::mat4 result(1.0f);
            for (uint32_t column = 0u; column < 3u; ++column)
            {
                for (uint32_t row = 0u; row < 3u; ++row)
                {
                    result[column][row] = normalMatrix[column][row];
                }
            }
            return result;
        }
    } // namespace

    uint32_t getSceneMaterialPhaseMask(SceneMaterialPhase phase)
    {
        const uint32_t phaseIndex = static_cast<uint32_t>(phase);
        if (phaseIndex >= static_cast<uint32_t>(SceneMaterialPhase::Count))
        {
            throw std::out_of_range("Scene material phase is outside the uber-material pass range.");
        }
        return 1u << phaseIndex;
    }

    RenderableObject::RenderableObject(
        const SceneGeometrySource &geometryValue,
        const SceneMaterialSource &material)
        : geometry(&geometryValue)
    {
        materials.push_back(&material);
    }

    void RenderableObject::setGeometry(const SceneGeometrySource &geometryValue)
    {
        if (geometry == &geometryValue)
        {
            return;
        }
        geometry = &geometryValue;
        markRenderDataDirty();
    }

    const SceneGeometrySource &RenderableObject::getGeometry() const
    {
        return *geometry;
    }

    void RenderableObject::addMaterial(const SceneMaterialSource &material)
    {
        materials.push_back(&material);
        markRenderDataDirty();
    }

    void RenderableObject::replaceMaterials(const SceneMaterialSource &material)
    {
        materials.clear();
        materials.push_back(&material);
        markRenderDataDirty();
    }

    const eastl::vector<const SceneMaterialSource *> &RenderableObject::getMaterials() const
    {
        return materials;
    }

    void RenderableObject::setInstances(const eastl::vector<SceneInstanceSource> &instanceValues)
    {
        if (instanceValues.empty())
        {
            throw std::invalid_argument("Explicit Scene instances must contain at least one element.");
        }
        if (instanceValues.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::overflow_error("Explicit Scene instance count exceeds the 32-bit RenderSet range.");
        }
        instances = instanceValues;
        markRenderDataDirty();
    }

    void RenderableObject::clearInstances()
    {
        if (instances.empty())
        {
            return;
        }
        instances.clear();
        markRenderDataDirty();
    }

    const eastl::vector<SceneInstanceSource> &RenderableObject::getInstances() const
    {
        return instances;
    }

    uint32_t RenderableObject::getInstanceCount() const
    {
        return instances.empty() ? 1u : static_cast<uint32_t>(instances.size());
    }

    void RenderableObject::setCastShadow(bool castShadowValue)
    {
        if (castShadow == castShadowValue)
        {
            return;
        }
        castShadow = castShadowValue;
        markRenderDataDirty();
    }

    bool RenderableObject::castsShadow() const
    {
        return castShadow;
    }

    void RenderableObject::setReceiveShadow(bool receiveShadowValue)
    {
        if (receiveShadow == receiveShadowValue)
        {
            return;
        }
        receiveShadow = receiveShadowValue;
        markRenderDataDirty();
    }

    bool RenderableObject::receivesShadow() const
    {
        return receiveShadow;
    }

    void RenderableObject::setPickingEnabled(bool enabled)
    {
        if (pickingEnabled == enabled)
        {
            return;
        }
        pickingEnabled = enabled;
        markRenderDataDirty();
    }

    bool RenderableObject::isPickingEnabled() const
    {
        return pickingEnabled;
    }

    SceneRenderPlan::SceneRenderPlan(ObjectId sceneIdentityValue)
        : sceneIdentity(sceneIdentityValue),
          planIdentity(sceneIdentityValue)
    {
    }

    ObjectId SceneRenderPlan::getSceneIdentity() const
    {
        return sceneIdentity;
    }

    ObjectId SceneRenderPlan::getPlanIdentity() const
    {
        return planIdentity;
    }

    uint64_t SceneRenderPlan::getRevision() const
    {
        return revision;
    }

    uint32_t SceneRenderPlan::getRenderSetCount() const
    {
        return 1u;
    }

    const eastl::vector<SceneVertexData> &SceneRenderPlan::getVertices() const
    {
        return vertices;
    }

    const eastl::vector<uint32_t> &SceneRenderPlan::getIndices() const
    {
        return indices;
    }

    const eastl::vector<SceneGeometryRecord> &SceneRenderPlan::getGeometries() const
    {
        return geometries;
    }

    const eastl::vector<SceneEntityRecord> &SceneRenderPlan::getEntities() const
    {
        return entities;
    }

    const eastl::vector<SceneObjectComponent> &SceneRenderPlan::getObjects() const
    {
        return objects;
    }

    const eastl::vector<SceneInstanceComponent> &SceneRenderPlan::getInstances() const
    {
        return instances;
    }

    const eastl::vector<SceneMaterialComponent> &SceneRenderPlan::getMaterials() const
    {
        return materials;
    }

    const ScenePassMetadata &SceneRenderPlan::getPassMetadata(ScenePassKind kind) const
    {
        const size_t passIndex = static_cast<size_t>(kind);
        if (passIndex >= passes.size())
        {
            throw std::out_of_range("Scene pass kind is outside the packed pass metadata range.");
        }
        return passes[passIndex];
    }

    const ScenePassMetadata &SceneRenderPlan::getMainPhaseMetadata(SceneMaterialPhase phase) const
    {
        const size_t phaseIndex = static_cast<size_t>(phase);
        if (phaseIndex >= mainPhasePasses.size())
        {
            throw std::out_of_range("Scene material phase is outside the main pass metadata range.");
        }
        return mainPhasePasses[phaseIndex];
    }

    /** Builds SceneRenderPlan internals while keeping mutation unavailable to consumers. */
    class SceneRenderPlanBuilder final
    {
    public:
        /** Rebuilds all packed arrays from the current deterministic Object3D hierarchy. */
        static void rebuild(Scene &scene, SceneRenderPlan &plan)
        {
            if (scene.getId() != plan.sceneIdentity)
            {
                throw std::invalid_argument("A SceneRenderPlan cannot be rebound to a different Scene identity.");
            }

            scene.updateWorldMatrix(true);
            eastl::vector<const RenderableObject *> renderables;
            collectRenderables(scene, renderables);
            validateRenderables(renderables);

            eastl::unordered_map<ObjectId, glm::mat4> previousWorldTransforms;
            for (const SceneObjectComponent &object : plan.objects)
            {
                previousWorldTransforms.emplace(object.sourceObjectId, object.worldTransform);
            }

            plan.vertices.clear();
            plan.indices.clear();
            plan.geometries.clear();
            plan.entities.clear();
            plan.objects.clear();
            plan.instances.clear();
            plan.materials.clear();

            eastl::unordered_map<const SceneGeometrySource *, GeometryPackState> packedGeometries;
            eastl::unordered_set<uint32_t> usedPickingIds;
            uint32_t nextPickingId = 1u;

            for (const RenderableObject *renderable : renderables)
            {
                const GeometryPackState geometry = packGeometry(
                    renderable->getGeometry(),
                    plan,
                    packedGeometries);
                const eastl::vector<SceneInstanceComponent> objectInstances = resolveInstances(
                    *renderable,
                    usedPickingIds,
                    nextPickingId);

                appendEntity(
                    *renderable,
                    geometry,
                    objectInstances,
                    previousWorldTransforms,
                    plan);
            }

            ++plan.revision;
            updatePassMetadata(plan);
        }

    private:
        /** Collects renderable descendants in stable depth-first pre-order and child insertion order. */
        static void collectRenderables(
            const Object3D &object,
            eastl::vector<const RenderableObject *> &renderables)
        {
            const auto *renderable = dynamic_cast<const RenderableObject *>(&object);
            if (renderable != nullptr)
            {
                renderables.push_back(renderable);
            }
            for (const Object3D *child : object.getChildren())
            {
                collectRenderables(*child, renderables);
            }
        }

        /** Validates every geometry, group, material slot, and explicit index before mutating a plan. */
        static void validateRenderables(const eastl::vector<const RenderableObject *> &renderables)
        {
            for (const RenderableObject *renderable : renderables)
            {
                const SceneGeometrySource &geometry = renderable->getGeometry();
                if (geometry.vertices.empty())
                {
                    throw std::invalid_argument("Renderable geometry must contain at least one vertex.");
                }
                checkedRangeSize(geometry.vertices.size(), "Scene vertex count");
                const uint32_t canonicalIndexCount = geometry.indices.empty()
                    ? checkedRangeSize(geometry.vertices.size(), "Generated Scene index count")
                    : checkedRangeSize(geometry.indices.size(), "Scene index count");
                for (uint32_t index : geometry.indices)
                {
                    if (index >= geometry.vertices.size())
                    {
                        throw std::out_of_range("Renderable geometry index references a missing vertex.");
                    }
                }
                if (renderable->getMaterials().empty())
                {
                    throw std::invalid_argument("Renderable objects must retain at least one material slot.");
                }
                if (geometry.groups.empty())
                {
                    for (uint32_t indexPosition = 0u; indexPosition < canonicalIndexCount; ++indexPosition)
                    {
                        const uint32_t vertexIndex = geometry.indices.empty()
                            ? indexPosition
                            : geometry.indices[indexPosition];
                        if (geometry.vertices[vertexIndex].materialSlot >= renderable->getMaterials().size())
                        {
                            throw std::out_of_range("Vertex material slot references a missing renderable material.");
                        }
                    }
                }
                for (const SceneGeometryGroup &group : geometry.groups)
                {
                    if (group.indexCount == 0u || group.firstIndex > canonicalIndexCount ||
                        group.indexCount > canonicalIndexCount - group.firstIndex)
                    {
                        throw std::out_of_range("Geometry group must select a non-empty in-range index span.");
                    }
                    if (group.materialSlot >= renderable->getMaterials().size())
                    {
                        throw std::out_of_range("Geometry group references a missing renderable material slot.");
                    }
                }
            }
        }

        /** Packs one geometry once, expanding groups so one entity can select material per vertex. */
        static GeometryPackState packGeometry(
            const SceneGeometrySource &source,
            SceneRenderPlan &plan,
            eastl::unordered_map<const SceneGeometrySource *, GeometryPackState> &packedGeometries)
        {
            const auto existing = packedGeometries.find(&source);
            if (existing != packedGeometries.end())
            {
                return existing->second;
            }

            GeometryPackState packed;
            packed.geometryIndex = checkedRangeSize(plan.geometries.size(), "Scene geometry count");
            packed.vertexOffset = checkedRangeSize(plan.vertices.size(), "Packed Scene vertex offset");
            packed.indexOffset = checkedRangeSize(plan.indices.size(), "Packed Scene index offset");

            if (source.groups.empty())
            {
                plan.vertices.insert(plan.vertices.end(), source.vertices.begin(), source.vertices.end());
                packed.vertexCount = checkedRangeSize(source.vertices.size(), "Scene geometry vertex count");
                packed.indexCount = source.indices.empty()
                    ? packed.vertexCount
                    : checkedRangeSize(source.indices.size(), "Scene geometry index count");
                if (source.indices.empty())
                {
                    for (uint32_t vertexIndex = 0u; vertexIndex < packed.vertexCount; ++vertexIndex)
                    {
                        plan.indices.push_back(vertexIndex);
                        packed.requiredMaterialSlotCount = eastl::max(
                            packed.requiredMaterialSlotCount,
                            source.vertices[vertexIndex].materialSlot + 1u);
                    }
                }
                else
                {
                    plan.indices.insert(plan.indices.end(), source.indices.begin(), source.indices.end());
                    for (uint32_t vertexIndex : source.indices)
                    {
                        packed.requiredMaterialSlotCount = eastl::max(
                            packed.requiredMaterialSlotCount,
                            source.vertices[vertexIndex].materialSlot + 1u);
                    }
                }
            }
            else
            {
                uint32_t expandedVertexIndex = 0u;
                for (const SceneGeometryGroup &group : source.groups)
                {
                    packed.requiredMaterialSlotCount = eastl::max(
                        packed.requiredMaterialSlotCount,
                        group.materialSlot + 1u);
                    for (uint32_t groupIndex = 0u; groupIndex < group.indexCount; ++groupIndex)
                    {
                        const uint32_t sourceIndexPosition = group.firstIndex + groupIndex;
                        const uint32_t sourceVertexIndex = source.indices.empty()
                            ? sourceIndexPosition
                            : source.indices[sourceIndexPosition];
                        SceneVertexData expandedVertex = source.vertices[sourceVertexIndex];
                        expandedVertex.materialSlot = group.materialSlot;
                        plan.vertices.push_back(expandedVertex);
                        plan.indices.push_back(expandedVertexIndex);
                        ++expandedVertexIndex;
                    }
                }
                packed.vertexCount = expandedVertexIndex;
                packed.indexCount = expandedVertexIndex;
            }

            SceneGeometryRecord geometryRecord;
            geometryRecord.geometryIndex = packed.geometryIndex;
            geometryRecord.vertexOffset = packed.vertexOffset;
            geometryRecord.vertexCount = packed.vertexCount;
            geometryRecord.indexOffset = packed.indexOffset;
            geometryRecord.indexCount = packed.indexCount;
            geometryRecord.requiredMaterialSlotCount = packed.requiredMaterialSlotCount;
            plan.geometries.push_back(geometryRecord);
            packedGeometries.emplace(&source, packed);
            return packed;
        }

        /** Resolves implicit or explicit instances and assigns collision-free deterministic picking IDs. */
        static eastl::vector<SceneInstanceComponent> resolveInstances(
            const RenderableObject &renderable,
            eastl::unordered_set<uint32_t> &usedPickingIds,
            uint32_t &nextPickingId)
        {
            eastl::vector<SceneInstanceComponent> resolved;
            const eastl::vector<SceneInstanceSource> &sources = renderable.getInstances();
            const uint32_t instanceCount = renderable.getInstanceCount();
            resolved.reserve(instanceCount);
            for (uint32_t instanceIndex = 0u; instanceIndex < instanceCount; ++instanceIndex)
            {
                SceneInstanceComponent instance;
                if (!sources.empty())
                {
                    instance.transform = sources[instanceIndex].transform;
                    instance.color = sources[instanceIndex].color;
                }
                instance.sourceInstanceIndex = instanceIndex;
                if (renderable.isPickingEnabled())
                {
                    const uint32_t requestedPickingId = sources.empty() ? 0u : sources[instanceIndex].pickingId;
                    if (requestedPickingId != 0u)
                    {
                        if (!usedPickingIds.insert(requestedPickingId).second)
                        {
                            throw std::invalid_argument("Explicit Scene picking identifiers must be unique.");
                        }
                        instance.pickingId = requestedPickingId;
                    }
                    else
                    {
                        while (nextPickingId != 0u && usedPickingIds.find(nextPickingId) != usedPickingIds.end())
                        {
                            ++nextPickingId;
                        }
                        if (nextPickingId == 0u)
                        {
                            throw std::overflow_error("Automatic Scene picking identifiers exhausted the 32-bit range.");
                        }
                        instance.pickingId = nextPickingId;
                        usedPickingIds.insert(nextPickingId);
                        ++nextPickingId;
                    }
                }
                resolved.push_back(instance);
            }
            return resolved;
        }

        /** Appends exactly one entity and one object/instance range for a renderable object. */
        static void appendEntity(
            const RenderableObject &renderable,
            const GeometryPackState &geometry,
            const eastl::vector<SceneInstanceComponent> &objectInstances,
            const eastl::unordered_map<ObjectId, glm::mat4> &previousWorldTransforms,
            SceneRenderPlan &plan)
        {
            const uint32_t entityIndex = checkedRangeSize(plan.entities.size(), "Scene entity count");
            const uint32_t objectIndex = checkedRangeSize(plan.objects.size(), "Scene object component count");
            const uint32_t instanceOffset = checkedRangeSize(plan.instances.size(), "Scene instance component offset");
            const uint32_t instanceCount = checkedRangeSize(objectInstances.size(), "Scene entity instance count");
            const uint32_t materialOffset = checkedRangeSize(plan.materials.size(), "Scene material component offset");
            const uint32_t materialCount = checkedRangeSize(
                renderable.getMaterials().size(),
                "Scene entity material count");
            if (materialCount < geometry.requiredMaterialSlotCount)
            {
                throw std::out_of_range("Packed geometry requires more material slots than the entity provides.");
            }
            uint32_t materialPhaseMask = 0u;
            for (uint32_t materialSlot = 0u; materialSlot < materialCount; ++materialSlot)
            {
                const SceneMaterialSource &source = *renderable.getMaterials()[materialSlot];
                SceneMaterialComponent material;
                material.baseColor = source.baseColor;
                material.emissiveColor = source.emissiveColor;
                material.roughness = source.roughness;
                material.metallic = source.metallic;
                material.opacity = source.opacity;
                material.phase = source.phase;
                material.textureSlots = source.textureSlots;
                material.materialIndex = checkedRangeSize(plan.materials.size(), "Scene material count");
                material.sourceMaterialSlot = materialSlot;
                plan.materials.push_back(material);
                materialPhaseMask |= getSceneMaterialPhaseMask(source.phase);
            }

            SceneObjectComponent object;
            object.worldTransform = renderable.getWorldMatrix();
            const auto previousTransform = previousWorldTransforms.find(renderable.getId());
            object.previousWorldTransform = previousTransform == previousWorldTransforms.end()
                ? object.worldTransform
                : previousTransform->second;
            object.normalTransform = makeNormalTransform(renderable.getNormalMatrix());
            object.sourceObjectId = renderable.getId();
            object.layerMask = renderable.getLayerMask();
            object.visible = renderable.isWorldVisible() ? 1u : 0u;
            object.castShadow = renderable.castsShadow() ? 1u : 0u;
            object.receiveShadow = renderable.receivesShadow() ? 1u : 0u;
            plan.objects.push_back(object);
            plan.instances.insert(plan.instances.end(), objectInstances.begin(), objectInstances.end());

            SceneEntityRecord entity;
            entity.entityIndex = entityIndex;
            entity.deterministicOrder = entityIndex;
            entity.sourceObjectId = renderable.getId();
            entity.sourceGroupCount = checkedRangeSize(
                renderable.getGeometry().groups.size(),
                "Scene geometry group count");
            entity.geometryIndex = geometry.geometryIndex;
            entity.vertexOffset = geometry.vertexOffset;
            entity.vertexCount = geometry.vertexCount;
            entity.indexOffset = geometry.indexOffset;
            entity.indexCount = geometry.indexCount;
            entity.objectComponentIndex = objectIndex;
            entity.instanceComponentOffset = instanceOffset;
            entity.instanceCount = instanceCount;
            entity.materialComponentOffset = materialOffset;
            entity.materialComponentCount = materialCount;
            entity.materialPhaseMask = materialPhaseMask;
            plan.entities.push_back(entity);
        }

        /** Rebuilds pass views so main, shadow, depth, and picking reuse one Scene entity span. */
        static void updatePassMetadata(SceneRenderPlan &plan)
        {
            const uint32_t entityCount = checkedRangeSize(plan.entities.size(), "Scene pass entity count");
            for (uint32_t passIndex = 0u; passIndex < static_cast<uint32_t>(ScenePassKind::Count); ++passIndex)
            {
                ScenePassMetadata &metadata = plan.passes[passIndex];
                metadata.kind = static_cast<ScenePassKind>(passIndex);
                metadata.sceneIdentity = plan.sceneIdentity;
                metadata.planIdentity = plan.planIdentity;
                metadata.planRevision = plan.revision;
                metadata.renderSetIndex = 0u;
                metadata.firstEntity = 0u;
                metadata.entityCount = entityCount;
                metadata.materialPhaseMask =
                    getSceneMaterialPhaseMask(SceneMaterialPhase::Opaque) |
                    getSceneMaterialPhaseMask(SceneMaterialPhase::AlphaTest) |
                    getSceneMaterialPhaseMask(SceneMaterialPhase::Transparent);
            }
            for (uint32_t phaseIndex = 0u; phaseIndex < static_cast<uint32_t>(SceneMaterialPhase::Count); ++phaseIndex)
            {
                ScenePassMetadata &metadata = plan.mainPhasePasses[phaseIndex];
                metadata = plan.passes[static_cast<size_t>(ScenePassKind::Main)];
                metadata.materialPhaseMask = getSceneMaterialPhaseMask(static_cast<SceneMaterialPhase>(phaseIndex));
            }
        }
    };

    const SceneRenderPlan &SceneRenderPlanner::plan(Scene &scene)
    {
        const ObjectId sceneIdentity = scene.getId();
        auto existing = plans.find(sceneIdentity);
        if (existing == plans.end())
        {
            auto created = eastl::unique_ptr<SceneRenderPlan>(new SceneRenderPlan(sceneIdentity));
            existing = plans.emplace(sceneIdentity, eastl::move(created)).first;
        }
        SceneRenderPlanBuilder::rebuild(scene, *existing->second);
        return *existing->second;
    }

    const SceneRenderPlan *SceneRenderPlanner::findPlan(const Scene &scene) const
    {
        const auto existing = plans.find(scene.getId());
        return existing == plans.end() ? nullptr : existing->second.get();
    }

    bool SceneRenderPlanner::releasePlan(const Scene &scene)
    {
        return plans.erase(scene.getId()) != 0u;
    }

    size_t SceneRenderPlanner::getPlanCount() const
    {
        return plans.size();
    }
} // namespace GVM::ThreeSamples::ThreeCompat
