#pragma once

#include "Object3D.hpp"

#include <EASTL/array.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Identifies the immutable pass role that consumes one Scene RenderSet plan. */
    enum class ScenePassKind : uint32_t
    {
        Main = 0u,
        Shadow = 1u,
        Depth = 2u,
        Picking = 3u,
        Count = 4u,
    };

    /** Classifies material state into deterministic RenderClass pass phases. */
    enum class SceneMaterialPhase : uint32_t
    {
        Opaque = 0u,
        AlphaTest = 1u,
        Transparent = 2u,
        Count = 3u,
    };

    /** Returns the single-bit mask used to select one material phase in an uber-material pass. */
    [[nodiscard]] uint32_t getSceneMaterialPhaseMask(SceneMaterialPhase phase);

    /** Stores the sample-private union vertex layout used by a packed logical Scene. */
    struct SceneVertexData final
    {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec4 tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        glm::vec2 textureCoordinate = glm::vec2(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        glm::uvec4 jointIndices = glm::uvec4(0u);
        glm::vec4 jointWeights = glm::vec4(0.0f);
        uint32_t materialSlot = 0u;
    };

    /** Selects a contiguous index range and material slot from one geometry source. */
    struct SceneGeometryGroup final
    {
        uint32_t firstIndex = 0u;
        uint32_t indexCount = 0u;
        uint32_t materialSlot = 0u;
    };

    /** Owns canonical CPU geometry consumed by one or more non-owning renderable objects. */
    struct SceneGeometrySource final
    {
        eastl::vector<SceneVertexData> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<SceneGeometryGroup> groups;
    };

    /** Owns one material payload referenced by a renderable object's local material slot. */
    struct SceneMaterialSource final
    {
        static constexpr uint32_t InvalidTextureSlot = std::numeric_limits<uint32_t>::max();

        glm::vec4 baseColor = glm::vec4(1.0f);
        glm::vec3 emissiveColor = glm::vec3(0.0f);
        float roughness = 1.0f;
        float metallic = 0.0f;
        float opacity = 1.0f;
        SceneMaterialPhase phase = SceneMaterialPhase::Opaque;
        eastl::array<uint32_t, 4u> textureSlots = {
            InvalidTextureSlot,
            InvalidTextureSlot,
            InvalidTextureSlot,
            InvalidTextureSlot,
        };
    };

    /** Supplies one object-local instance transform, color, and optional stable picking identifier. */
    struct SceneInstanceSource final
    {
        glm::mat4 transform = glm::mat4(1.0f);
        glm::vec4 color = glm::vec4(1.0f);
        uint32_t pickingId = 0u;
    };

    /** Represents a renderable Object3D while retaining non-owning geometry and material sources. */
    class RenderableObject : public Object3D
    {
    public:
        /** Creates a renderable with one required geometry and one required material source. */
        RenderableObject(
            const SceneGeometrySource &geometry,
            const SceneMaterialSource &material);

        /** Destroys the renderable without taking ownership of geometry or material sources. */
        ~RenderableObject() override = default;

        /** Replaces the non-owning geometry source used by subsequent planning passes. */
        void setGeometry(const SceneGeometrySource &geometry);

        /** Returns the non-owning geometry source that must outlive planning. */
        [[nodiscard]] const SceneGeometrySource &getGeometry() const;

        /** Appends a non-owning material slot in deterministic insertion order. */
        void addMaterial(const SceneMaterialSource &material);

        /** Removes every material slot and immediately restores the required replacement slot. */
        void replaceMaterials(const SceneMaterialSource &material);

        /** Returns non-owning material slots referenced by geometry groups. */
        [[nodiscard]] const eastl::vector<const SceneMaterialSource *> &getMaterials() const;

        /** Replaces implicit single-instance behavior with one or more explicit instances. */
        void setInstances(const eastl::vector<SceneInstanceSource> &instances);

        /** Restores one implicit identity instance for this renderable. */
        void clearInstances();

        /** Returns explicit instances; an empty array denotes one implicit identity instance. */
        [[nodiscard]] const eastl::vector<SceneInstanceSource> &getInstances() const;

        /** Returns the effective RenderSet instance count, which is never zero. */
        [[nodiscard]] uint32_t getInstanceCount() const;

        /** Selects whether shadow passes should consume this object's entities. */
        void setCastShadow(bool castShadow);

        /** Returns whether shadow passes should consume this object's entities. */
        [[nodiscard]] bool castsShadow() const;

        /** Selects whether this object receives scene shadowing in its main material. */
        void setReceiveShadow(bool receiveShadow);

        /** Returns whether this object receives scene shadowing in its main material. */
        [[nodiscard]] bool receivesShadow() const;

        /** Selects whether deterministic picking identifiers are emitted for this object. */
        void setPickingEnabled(bool enabled);

        /** Returns whether deterministic picking identifiers are emitted for this object. */
        [[nodiscard]] bool isPickingEnabled() const;

    private:
        const SceneGeometrySource *geometry;
        eastl::vector<const SceneMaterialSource *> materials;
        eastl::vector<SceneInstanceSource> instances;
        bool castShadow = false;
        bool receiveShadow = false;
        bool pickingEnabled = true;
    };

    /** Records one unique packed geometry range in first-use order. */
    struct SceneGeometryRecord final
    {
        uint32_t geometryIndex = 0u;
        uint32_t vertexOffset = 0u;
        uint32_t vertexCount = 0u;
        uint32_t indexOffset = 0u;
        uint32_t indexCount = 0u;
        uint32_t requiredMaterialSlotCount = 1u;
    };

    /** Stores per-entity Object3D data intended for a RenderSet object component. */
    struct SceneObjectComponent final
    {
        glm::mat4 worldTransform = glm::mat4(1.0f);
        glm::mat4 previousWorldTransform = glm::mat4(1.0f);
        glm::mat4 normalTransform = glm::mat4(1.0f);
        ObjectId sourceObjectId = 0u;
        uint32_t layerMask = 1u;
        uint32_t visible = 1u;
        uint32_t castShadow = 0u;
        uint32_t receiveShadow = 0u;
    };

    /** Stores per-entity-instance data intended for a RenderSet instance component. */
    struct SceneInstanceComponent final
    {
        glm::mat4 transform = glm::mat4(1.0f);
        glm::vec4 color = glm::vec4(1.0f);
        uint32_t sourceInstanceIndex = 0u;
        uint32_t pickingId = 0u;
    };

    /** Stores one entity-local material payload intended for a RenderSet material component. */
    struct SceneMaterialComponent final
    {
        glm::vec4 baseColor = glm::vec4(1.0f);
        glm::vec3 emissiveColor = glm::vec3(0.0f);
        float roughness = 1.0f;
        float metallic = 0.0f;
        float opacity = 1.0f;
        SceneMaterialPhase phase = SceneMaterialPhase::Opaque;
        eastl::array<uint32_t, 4u> textureSlots = {
            SceneMaterialSource::InvalidTextureSlot,
            SceneMaterialSource::InvalidTextureSlot,
            SceneMaterialSource::InvalidTextureSlot,
            SceneMaterialSource::InvalidTextureSlot,
        };
        uint32_t materialIndex = 0u;
        uint32_t sourceMaterialSlot = 0u;
    };

    /** Maps one deterministic RenderSet entity to packed geometry and component ranges. */
    struct SceneEntityRecord final
    {
        uint32_t entityIndex = 0u;
        uint32_t deterministicOrder = 0u;
        ObjectId sourceObjectId = 0u;
        uint32_t sourceGroupCount = 0u;
        uint32_t geometryIndex = 0u;
        uint32_t vertexOffset = 0u;
        uint32_t vertexCount = 0u;
        uint32_t indexOffset = 0u;
        uint32_t indexCount = 0u;
        uint32_t objectComponentIndex = 0u;
        uint32_t instanceComponentOffset = 0u;
        uint32_t instanceCount = 1u;
        uint32_t materialComponentOffset = 0u;
        uint32_t materialComponentCount = 1u;
        uint32_t materialPhaseMask = 0u;
    };

    /** Describes one Scene pass as a view of the same single RenderSet entity span. */
    struct ScenePassMetadata final
    {
        ScenePassKind kind = ScenePassKind::Main;
        ObjectId sceneIdentity = 0u;
        ObjectId planIdentity = 0u;
        uint64_t planRevision = 0u;
        uint32_t renderSetIndex = 0u;
        uint32_t firstEntity = 0u;
        uint32_t entityCount = 0u;
        uint32_t materialPhaseMask = 0u;
    };

    class SceneRenderPlanBuilder;

    /** Owns the complete CPU packing contract for exactly one logical Scene and one RenderSet. */
    class SceneRenderPlan final
    {
    public:
        /** Returns the stable Object3D identity of the Scene represented by this plan. */
        [[nodiscard]] ObjectId getSceneIdentity() const;

        /** Returns the stable plan identity, equal to the owning Scene identity. */
        [[nodiscard]] ObjectId getPlanIdentity() const;

        /** Returns the monotonic revision incremented whenever the cached plan is rebuilt. */
        [[nodiscard]] uint64_t getRevision() const;

        /** Returns one because a logical Scene cannot be split into multiple RenderSets. */
        [[nodiscard]] uint32_t getRenderSetCount() const;

        /** Returns the unified packed vertex storage. */
        [[nodiscard]] const eastl::vector<SceneVertexData> &getVertices() const;

        /** Returns the unified packed index storage using geometry-local index values. */
        [[nodiscard]] const eastl::vector<uint32_t> &getIndices() const;

        /** Returns unique geometry ranges in deterministic first-use order. */
        [[nodiscard]] const eastl::vector<SceneGeometryRecord> &getGeometries() const;

        /** Returns one entity per renderable in deterministic depth-first hierarchy order. */
        [[nodiscard]] const eastl::vector<SceneEntityRecord> &getEntities() const;

        /** Returns object component data indexed by SceneEntityRecord::objectComponentIndex. */
        [[nodiscard]] const eastl::vector<SceneObjectComponent> &getObjects() const;

        /** Returns instance component data referenced by each entity's contiguous instance range. */
        [[nodiscard]] const eastl::vector<SceneInstanceComponent> &getInstances() const;

        /** Returns contiguous per-entity material ranges in source slot order. */
        [[nodiscard]] const eastl::vector<SceneMaterialComponent> &getMaterials() const;

        /** Returns metadata for a Scene geometry pass that reuses this plan's entity span. */
        [[nodiscard]] const ScenePassMetadata &getPassMetadata(ScenePassKind kind) const;

        /** Returns one main RenderClass phase view that discards fragments from other material phases. */
        [[nodiscard]] const ScenePassMetadata &getMainPhaseMetadata(SceneMaterialPhase phase) const;

    private:
        friend class SceneRenderPlanBuilder;
        friend class SceneRenderPlanner;

        /** Creates an empty cached plan whose identity is permanently bound to one Scene. */
        explicit SceneRenderPlan(ObjectId sceneIdentity);

        ObjectId sceneIdentity;
        ObjectId planIdentity;
        uint64_t revision = 0u;
        eastl::vector<SceneVertexData> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<SceneGeometryRecord> geometries;
        eastl::vector<SceneEntityRecord> entities;
        eastl::vector<SceneObjectComponent> objects;
        eastl::vector<SceneInstanceComponent> instances;
        eastl::vector<SceneMaterialComponent> materials;
        eastl::array<ScenePassMetadata, static_cast<size_t>(ScenePassKind::Count)> passes;
        eastl::array<ScenePassMetadata, static_cast<size_t>(SceneMaterialPhase::Count)> mainPhasePasses;
    };

    /** Caches and rebuilds exactly one stable SceneRenderPlan object for each logical Scene. */
    class SceneRenderPlanner final
    {
    public:
        /** Builds or replaces packed data while preserving the Scene's unique plan address and identity. */
        [[nodiscard]] const SceneRenderPlan &plan(Scene &scene);

        /** Returns the existing cached plan for a Scene without rebuilding it, or null when absent. */
        [[nodiscard]] const SceneRenderPlan *findPlan(const Scene &scene) const;

        /** Releases cached CPU data for a Scene and returns whether a plan existed. */
        bool releasePlan(const Scene &scene);

        /** Returns the number of logical Scenes currently owning one cached plan each. */
        [[nodiscard]] size_t getPlanCount() const;

    private:
        eastl::unordered_map<ObjectId, eastl::unique_ptr<SceneRenderPlan>> plans;
    };
} // namespace GVM::ThreeSamples::ThreeCompat
