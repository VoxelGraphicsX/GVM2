#include "Object3D.hpp"

#include <EASTL/algorithm.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        constexpr float NormalMatrixDeterminantEpsilon = 1.0e-8f;

        /** Returns true when every vector component is finite. */
        bool isFinite(const glm::vec3 &value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        /** Returns true when every quaternion component is finite. */
        bool isFinite(const glm::quat &value)
        {
            return std::isfinite(value.w) && std::isfinite(value.x) &&
                   std::isfinite(value.y) && std::isfinite(value.z);
        }

        /** Returns the inverse-transpose normal matrix, matching Three.js zero output for singular transforms. */
        glm::mat3 makeNormalMatrix(const glm::mat4 &worldMatrix)
        {
            const glm::mat3 upperLeft(worldMatrix);
            const float determinant = glm::determinant(upperLeft);
            if (!std::isfinite(determinant) || std::abs(determinant) <= NormalMatrixDeterminantEpsilon)
            {
                return glm::mat3(0.0f);
            }
            return glm::transpose(glm::inverse(upperLeft));
        }
    } // namespace

    ObjectChangeFlags operator|(ObjectChangeFlags left, ObjectChangeFlags right)
    {
        return static_cast<ObjectChangeFlags>(
            static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
    }

    ObjectChangeFlags operator&(ObjectChangeFlags left, ObjectChangeFlags right)
    {
        return static_cast<ObjectChangeFlags>(
            static_cast<uint32_t>(left) & static_cast<uint32_t>(right));
    }

    ObjectChangeFlags &operator|=(ObjectChangeFlags &left, ObjectChangeFlags right)
    {
        left = left | right;
        return left;
    }

    bool hasAnyChange(ObjectChangeFlags value, ObjectChangeFlags requested)
    {
        return (value & requested) != ObjectChangeFlags::None;
    }

    Layers::Layers()
        : mask(1u)
    {
    }

    void Layers::set(uint32_t channel)
    {
        validateChannel(channel);
        mask = 1u << channel;
    }

    void Layers::enable(uint32_t channel)
    {
        validateChannel(channel);
        mask |= 1u << channel;
    }

    void Layers::disable(uint32_t channel)
    {
        validateChannel(channel);
        mask &= ~(1u << channel);
    }

    void Layers::enableAll()
    {
        mask = std::numeric_limits<uint32_t>::max();
    }

    void Layers::disableAll()
    {
        mask = 0u;
    }

    void Layers::setMask(uint32_t maskValue)
    {
        mask = maskValue;
    }

    uint32_t Layers::getMask() const
    {
        return mask;
    }

    bool Layers::intersects(const Layers &other) const
    {
        return (mask & other.mask) != 0u;
    }

    void Layers::validateChannel(uint32_t channel)
    {
        if (channel >= 32u)
        {
            throw std::out_of_range("Layer channel must be in the inclusive range [0, 31].");
        }
    }

    Object3D::Object3D()
        : id(allocateObjectId())
    {
    }

    Object3D::~Object3D()
    {
        if (parent != nullptr)
        {
            parent->removeChild(*this);
        }
        for (Object3D *child : children)
        {
            child->parent = nullptr;
            child->markWorldTransformDirtyRecursive();
            child->markDirty(ObjectChangeFlags::Hierarchy);
        }
        children.clear();
    }

    ObjectId Object3D::getId() const
    {
        return id;
    }

    bool Object3D::addChild(Object3D &child)
    {
        if (&child == this || hasAncestor(child))
        {
            throw std::invalid_argument("Object3D hierarchy changes must not introduce a cycle.");
        }
        if (child.parent == this)
        {
            return false;
        }
        if (child.parent != nullptr)
        {
            child.parent->removeChild(child);
        }

        children.push_back(&child);
        child.parent = this;
        markDirty(ObjectChangeFlags::Hierarchy | ObjectChangeFlags::Bounds);
        markAncestorsDirty(ObjectChangeFlags::Hierarchy | ObjectChangeFlags::Bounds);
        child.markWorldTransformDirtyRecursive();
        child.markDirty(ObjectChangeFlags::Hierarchy);
        return true;
    }

    bool Object3D::removeChild(Object3D &child)
    {
        const auto positionInChildren = eastl::find(children.begin(), children.end(), &child);
        if (positionInChildren == children.end())
        {
            return false;
        }

        children.erase(positionInChildren);
        child.parent = nullptr;
        markDirty(ObjectChangeFlags::Hierarchy | ObjectChangeFlags::Bounds);
        markAncestorsDirty(ObjectChangeFlags::Hierarchy | ObjectChangeFlags::Bounds);
        child.markWorldTransformDirtyRecursive();
        child.markDirty(ObjectChangeFlags::Hierarchy);
        return true;
    }

    Object3D *Object3D::getParent()
    {
        return parent;
    }

    const Object3D *Object3D::getParent() const
    {
        return parent;
    }

    const eastl::vector<Object3D *> &Object3D::getChildren() const
    {
        return children;
    }

    void Object3D::setLocalTransform(
        const glm::vec3 &positionValue,
        const glm::quat &rotationValue,
        const glm::vec3 &scaleValue)
    {
        if (!isFinite(positionValue) || !isFinite(rotationValue) || !isFinite(scaleValue))
        {
            throw std::invalid_argument("Object3D local transform components must be finite.");
        }
        const float rotationLength = glm::length(rotationValue);
        if (rotationLength <= NormalMatrixDeterminantEpsilon)
        {
            throw std::invalid_argument("Object3D rotation quaternion must have non-zero length.");
        }

        position = positionValue;
        rotation = rotationValue / rotationLength;
        scale = scaleValue;
        invalidateLocalTransform();
    }

    void Object3D::setPosition(const glm::vec3 &positionValue)
    {
        setLocalTransform(positionValue, rotation, scale);
    }

    void Object3D::setRotation(const glm::quat &rotationValue)
    {
        setLocalTransform(position, rotationValue, scale);
    }

    void Object3D::setScale(const glm::vec3 &scaleValue)
    {
        setLocalTransform(position, rotation, scaleValue);
    }

    const glm::vec3 &Object3D::getPosition() const
    {
        return position;
    }

    const glm::quat &Object3D::getRotation() const
    {
        return rotation;
    }

    const glm::vec3 &Object3D::getScale() const
    {
        return scale;
    }

    void Object3D::updateWorldMatrix(bool updateChildren)
    {
        if (parent != nullptr && parent->worldMatrixDirty)
        {
            parent->updateWorldMatrix(false);
        }

        updateLocalMatrix();
        if (worldMatrixDirty)
        {
            worldMatrix = parent != nullptr ? parent->worldMatrix * localMatrix : localMatrix;
            normalMatrix = makeNormalMatrix(worldMatrix);
            worldBounds = localBounds.transformed(worldMatrix);
            worldMatrixDirty = false;
        }

        if (updateChildren)
        {
            for (Object3D *child : children)
            {
                child->updateWorldMatrix(true);
            }
        }
    }

    const glm::mat4 &Object3D::getLocalMatrix() const
    {
        return localMatrix;
    }

    const glm::mat4 &Object3D::getWorldMatrix() const
    {
        return worldMatrix;
    }

    const glm::mat3 &Object3D::getNormalMatrix() const
    {
        return normalMatrix;
    }

    void Object3D::setLocalBounds(const Bounds3 &bounds)
    {
        localBounds = bounds;
        worldBounds = localBounds.transformed(worldMatrix);
        markDirty(ObjectChangeFlags::Bounds);
        markAncestorsDirty(ObjectChangeFlags::Bounds);
    }

    void Object3D::clearLocalBounds()
    {
        if (localBounds.isEmpty())
        {
            return;
        }
        localBounds.clear();
        worldBounds.clear();
        markDirty(ObjectChangeFlags::Bounds);
        markAncestorsDirty(ObjectChangeFlags::Bounds);
    }

    const Bounds3 &Object3D::getLocalBounds() const
    {
        return localBounds;
    }

    const Bounds3 &Object3D::getWorldBounds() const
    {
        return worldBounds;
    }

    Bounds3 Object3D::computeSubtreeWorldBounds() const
    {
        Bounds3 result = worldBounds;
        for (const Object3D *child : children)
        {
            result.include(child->computeSubtreeWorldBounds());
        }
        return result;
    }

    void Object3D::setVisible(bool visibleValue)
    {
        if (visible == visibleValue)
        {
            return;
        }
        visible = visibleValue;
        markVisibilityDirtyRecursive();
    }

    bool Object3D::isVisible() const
    {
        return visible;
    }

    bool Object3D::isWorldVisible() const
    {
        const Object3D *current = this;
        while (current != nullptr)
        {
            if (!current->visible)
            {
                return false;
            }
            current = current->parent;
        }
        return true;
    }

    void Object3D::setLayerMask(uint32_t mask)
    {
        if (layers.getMask() == mask)
        {
            return;
        }
        layers.setMask(mask);
        markDirty(ObjectChangeFlags::Layers);
    }

    void Object3D::enableLayer(uint32_t channel)
    {
        const uint32_t previousMask = layers.getMask();
        layers.enable(channel);
        if (layers.getMask() != previousMask)
        {
            markDirty(ObjectChangeFlags::Layers);
        }
    }

    void Object3D::disableLayer(uint32_t channel)
    {
        const uint32_t previousMask = layers.getMask();
        layers.disable(channel);
        if (layers.getMask() != previousMask)
        {
            markDirty(ObjectChangeFlags::Layers);
        }
    }

    uint32_t Object3D::getLayerMask() const
    {
        return layers.getMask();
    }

    bool Object3D::intersectsLayers(const Layers &selection) const
    {
        return layers.intersects(selection);
    }

    void Object3D::markRenderDataDirty()
    {
        markDirty(ObjectChangeFlags::RenderData);
    }

    ObjectChangeFlags Object3D::getDirtyFlags() const
    {
        return dirtyFlags;
    }

    uint64_t Object3D::getChangeVersion() const
    {
        return changeVersion;
    }

    void Object3D::clearDirtyFlags()
    {
        dirtyFlags = ObjectChangeFlags::None;
    }

    void Object3D::markDirty(ObjectChangeFlags flags)
    {
        if (flags == ObjectChangeFlags::None)
        {
            return;
        }
        dirtyFlags |= flags;
        ++changeVersion;
    }

    ObjectId Object3D::allocateObjectId()
    {
        static ObjectId nextObjectId = 1u;
        if (nextObjectId == std::numeric_limits<ObjectId>::max())
        {
            throw std::overflow_error("Object3D identifier space has been exhausted.");
        }
        return nextObjectId++;
    }

    bool Object3D::hasAncestor(const Object3D &object) const
    {
        const Object3D *current = parent;
        while (current != nullptr)
        {
            if (current == &object)
            {
                return true;
            }
            current = current->parent;
        }
        return false;
    }

    void Object3D::updateLocalMatrix()
    {
        if (!localMatrixDirty)
        {
            return;
        }
        localMatrix = glm::translate(glm::mat4(1.0f), position) *
                      glm::mat4_cast(rotation) *
                      glm::scale(glm::mat4(1.0f), scale);
        localMatrixDirty = false;
    }

    void Object3D::markWorldTransformDirtyRecursive()
    {
        worldMatrixDirty = true;
        markDirty(ObjectChangeFlags::WorldTransform | ObjectChangeFlags::Bounds);
        for (Object3D *child : children)
        {
            child->markWorldTransformDirtyRecursive();
        }
    }

    void Object3D::markVisibilityDirtyRecursive()
    {
        markDirty(ObjectChangeFlags::Visibility);
        for (Object3D *child : children)
        {
            child->markVisibilityDirtyRecursive();
        }
    }

    void Object3D::markAncestorsDirty(ObjectChangeFlags flags)
    {
        Object3D *current = parent;
        while (current != nullptr)
        {
            current->markDirty(flags);
            current = current->parent;
        }
    }

    void Object3D::invalidateLocalTransform()
    {
        localMatrixDirty = true;
        updateLocalMatrix();
        worldMatrixDirty = true;
        markDirty(
            ObjectChangeFlags::LocalTransform |
            ObjectChangeFlags::WorldTransform |
            ObjectChangeFlags::Bounds);
        for (Object3D *child : children)
        {
            child->markWorldTransformDirtyRecursive();
        }
        markAncestorsDirty(ObjectChangeFlags::Bounds);
    }
} // namespace GVM::ThreeSamples::ThreeCompat
