#pragma once

#include "Math.hpp"

#include <EASTL/vector.h>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Identifies one Object3D for its complete lifetime and is never reused in a process. */
    using ObjectId = uint64_t;

    /** Describes changes that a RenderSet synchronization pass may need to consume. */
    enum class ObjectChangeFlags : uint32_t
    {
        None = 0u,
        LocalTransform = 1u << 0u,
        WorldTransform = 1u << 1u,
        Visibility = 1u << 2u,
        Layers = 1u << 3u,
        Hierarchy = 1u << 4u,
        Bounds = 1u << 5u,
        RenderData = 1u << 6u,
        Projection = 1u << 7u,
        All = (1u << 8u) - 1u,
    };

    /** Combines two object change masks without converting them to integers at call sites. */
    [[nodiscard]] ObjectChangeFlags operator|(ObjectChangeFlags left, ObjectChangeFlags right);

    /** Intersects two object change masks without converting them to integers at call sites. */
    [[nodiscard]] ObjectChangeFlags operator&(ObjectChangeFlags left, ObjectChangeFlags right);

    /** Adds change bits to an existing object change mask. */
    ObjectChangeFlags &operator|=(ObjectChangeFlags &left, ObjectChangeFlags right);

    /** Returns true when a change mask contains at least one requested bit. */
    [[nodiscard]] bool hasAnyChange(ObjectChangeFlags value, ObjectChangeFlags requested);

    /** Stores the 32-bit layer mask used for camera and raycast selection. */
    class Layers final
    {
    public:
        /** Creates a layer mask with only channel zero enabled, matching Three.js defaults. */
        Layers();

        /** Replaces the layer mask with exactly one enabled channel. */
        void set(uint32_t channel);

        /** Enables one channel without changing the remaining layer mask. */
        void enable(uint32_t channel);

        /** Disables one channel without changing the remaining layer mask. */
        void disable(uint32_t channel);

        /** Enables every available layer channel. */
        void enableAll();

        /** Disables every available layer channel. */
        void disableAll();

        /** Replaces the complete 32-bit layer mask. */
        void setMask(uint32_t mask);

        /** Returns the complete 32-bit layer mask. */
        [[nodiscard]] uint32_t getMask() const;

        /** Returns true when this mask and another mask share an enabled channel. */
        [[nodiscard]] bool intersects(const Layers &other) const;

    private:
        /** Validates a layer channel before it is converted to a bit shift. */
        static void validateChannel(uint32_t channel);

        uint32_t mask;
    };

    /**
     * Stores a non-owning scene-graph node with deterministic transforms and change tracking.
     * Children must outlive their attachment or detach before destruction; destruction safely
     * disconnects any still-attached parent and children.
     */
    class Object3D
    {
    public:
        /** Creates an identity node with a stable process-local object identifier. */
        Object3D();

        /** Detaches this node and its non-owning children without destroying related objects. */
        virtual ~Object3D();

        /** Prevents copying because hierarchy links and stable identifiers are object-specific. */
        Object3D(const Object3D &) = delete;

        /** Prevents copy assignment because hierarchy links and stable identifiers are object-specific. */
        Object3D &operator=(const Object3D &) = delete;

        /** Prevents moving because attached nodes must retain stable addresses. */
        Object3D(Object3D &&) = delete;

        /** Prevents move assignment because attached nodes must retain stable addresses. */
        Object3D &operator=(Object3D &&) = delete;

        /** Returns the stable identifier used to map this node to a RenderSet entity. */
        [[nodiscard]] ObjectId getId() const;

        /** Attaches a child, reparenting it if necessary, and rejects hierarchy cycles. */
        bool addChild(Object3D &child);

        /** Detaches a direct child and returns false when the object was not attached. */
        bool removeChild(Object3D &child);

        /** Returns the optional non-owning parent pointer. */
        [[nodiscard]] Object3D *getParent();

        /** Returns the optional non-owning parent pointer for a const node. */
        [[nodiscard]] const Object3D *getParent() const;

        /** Returns direct children in stable insertion order. */
        [[nodiscard]] const eastl::vector<Object3D *> &getChildren() const;

        /** Replaces the local translation, normalized rotation, and scale in one deterministic update. */
        void setLocalTransform(
            const glm::vec3 &position,
            const glm::quat &rotation,
            const glm::vec3 &scale);

        /** Replaces only the local translation and marks dependent world transforms dirty. */
        void setPosition(const glm::vec3 &position);

        /** Replaces only the normalized local rotation and marks dependent world transforms dirty. */
        void setRotation(const glm::quat &rotation);

        /** Replaces only the local scale and marks dependent world transforms dirty. */
        void setScale(const glm::vec3 &scale);

        /** Returns the local translation. */
        [[nodiscard]] const glm::vec3 &getPosition() const;

        /** Returns the normalized local rotation. */
        [[nodiscard]] const glm::quat &getRotation() const;

        /** Returns the local scale. */
        [[nodiscard]] const glm::vec3 &getScale() const;

        /** Updates this node and optionally all descendants from current local transforms. */
        void updateWorldMatrix(bool updateChildren = true);

        /** Returns the composed local matrix after the latest local transform change. */
        [[nodiscard]] const glm::mat4 &getLocalMatrix() const;

        /** Returns the parent-composed world matrix after updateWorldMatrix(). */
        [[nodiscard]] const glm::mat4 &getWorldMatrix() const;

        /** Returns the inverse-transpose world normal matrix after updateWorldMatrix(). */
        [[nodiscard]] const glm::mat3 &getNormalMatrix() const;

        /** Replaces the optional object-space bound used for deterministic culling. */
        void setLocalBounds(const Bounds3 &bounds);

        /** Removes this node's object-space bound. */
        void clearLocalBounds();

        /** Returns the object-space bound, which may be empty. */
        [[nodiscard]] const Bounds3 &getLocalBounds() const;

        /** Returns the transformed object-space bound after updateWorldMatrix(). */
        [[nodiscard]] const Bounds3 &getWorldBounds() const;

        /** Returns the union of this node and all descendant world bounds in insertion order. */
        [[nodiscard]] Bounds3 computeSubtreeWorldBounds() const;

        /** Replaces local visibility and propagates inherited visibility dirtiness to descendants. */
        void setVisible(bool visible);

        /** Returns this node's local visibility value. */
        [[nodiscard]] bool isVisible() const;

        /** Returns true only when this node and every ancestor are locally visible. */
        [[nodiscard]] bool isWorldVisible() const;

        /** Replaces the complete object layer mask. */
        void setLayerMask(uint32_t mask);

        /** Enables one object layer channel. */
        void enableLayer(uint32_t channel);

        /** Disables one object layer channel. */
        void disableLayer(uint32_t channel);

        /** Returns the complete object layer mask. */
        [[nodiscard]] uint32_t getLayerMask() const;

        /** Returns true when this object shares a channel with a selection layer mask. */
        [[nodiscard]] bool intersectsLayers(const Layers &selection) const;

        /** Explicitly marks geometry, material, or instance payload owned by this object as changed. */
        void markRenderDataDirty();

        /** Returns accumulated changes not yet acknowledged by an external synchronizer. */
        [[nodiscard]] ObjectChangeFlags getDirtyFlags() const;

        /** Returns the monotonic object-local version incremented whenever dirty state is recorded. */
        [[nodiscard]] uint64_t getChangeVersion() const;

        /** Acknowledges all accumulated changes without altering transforms or scene state. */
        void clearDirtyFlags();

    protected:
        /** Records dirty flags from derived camera or renderable node state changes. */
        void markDirty(ObjectChangeFlags flags);

    private:
        /** Allocates the next monotonic stable identifier on the scene-management thread. */
        static ObjectId allocateObjectId();

        /** Returns true when the supplied object is already an ancestor of this node. */
        [[nodiscard]] bool hasAncestor(const Object3D &object) const;

        /** Rebuilds the local matrix when transform components have changed. */
        void updateLocalMatrix();

        /** Marks this node and every descendant as requiring world-derived data updates. */
        void markWorldTransformDirtyRecursive();

        /** Marks inherited visibility on this node and every descendant as changed. */
        void markVisibilityDirtyRecursive();

        /** Marks all ancestors for hierarchy or aggregate-bound changes. */
        void markAncestorsDirty(ObjectChangeFlags flags);

        /** Records a local transform mutation and invalidates every dependent matrix and bound. */
        void invalidateLocalTransform();

        ObjectId id;
        Object3D *parent = nullptr;
        eastl::vector<Object3D *> children;
        glm::vec3 position = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
        glm::mat4 localMatrix = glm::mat4(1.0f);
        glm::mat4 worldMatrix = glm::mat4(1.0f);
        glm::mat3 normalMatrix = glm::mat3(1.0f);
        Bounds3 localBounds;
        Bounds3 worldBounds;
        Layers layers;
        ObjectChangeFlags dirtyFlags = ObjectChangeFlags::All;
        uint64_t changeVersion = 1u;
        bool localMatrixDirty = false;
        bool worldMatrixDirty = false;
        bool visible = true;
    };

    /** Provides a semantic root for one logical Three.js scene and its single RenderSet. */
    class Scene final : public Object3D
    {
    public:
        /** Creates an empty logical scene root. */
        Scene() = default;
    };
} // namespace GVM::ThreeSamples::ThreeCompat
