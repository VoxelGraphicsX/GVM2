#pragma once
#include "GProgressiveData.hpp"
#include "GRenderEntity.hpp"
#include "GRenderSetInternalCommand.hpp"
#include "GStagingLinearAllocator.hpp"
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <GVMRHI/GVMRHI.hpp>
#include <xGEFoundation/xFreeList.hpp>
namespace GVM::Core
{
    /**
     * Tracks the GPU resource generations that define one RenderSet component's bind-group entries.
     * Buffer and texture components use both counters when their resource buffer or component-index buffer changes.
     */
    struct RenderComponentBindingGeneration
    {
        uint64_t resource = 0u;
        uint64_t componentIndex = 0u;
    };

    class IRenderComponent : public GVM::RHI::RefCountedObject
    {
    public:
        /** Appends this component's bind-group layout entries and advances the next binding index. */
        virtual void appendBindGroupLayoutEntries(eastl::vector<GVM::RHI::BindGroupLayoutEntry> &entries, uint32_t &bindingIndex) const = 0;
        /** Appends this component's bind-group entries and advances the next binding index. */
        virtual void appendBindGroupEntries(eastl::vector<GVM::RHI::BindGroupEntry> &entries, uint32_t &bindingIndex) const = 0;
        /** Returns the resource generations that require RenderSet bind-group rebuilding when changed. */
        virtual RenderComponentBindingGeneration getBindGroupGeneration() const = 0;
        /** Returns the GPU buffer that stores per-entity component indices for this component. */
        virtual GVM::RHI::Buffer getComponentIndexBuffer() const = 0;
        virtual uint64_t getResourceGenerationCounter() const = 0;
        virtual void resizeBuffer() = 0;
        virtual bool check(const eastl::string &Name) const = 0;
        virtual bool check(RenderComponentIndex index) const = 0;
        virtual void update() = 0;
        virtual void remove(RenderEntityIndex entity) = 0;
        virtual uint64_t getPerEntityComponentIndexStorageBytes() const = 0;
        /** Serializes the component-index page written when an entity is allocated. */
        virtual RenderComponentIndexCopyInfo getComponentIndexAllocInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator) = 0;
        /** Serializes the component-index page written when an entity is removed. */
        virtual RenderComponentIndexCopyInfo getComponentIndexRemoveInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator) = 0;
        /** Returns the component-index element count published through RenderSetAccessBoundData. */
        virtual uint64_t getComponentIndexElementCountForRenderSetAccessBounds() const = 0;
        virtual uint64_t getResourceElementCountForRenderSetAccessBounds() const = 0;
        virtual void destroy() = 0;
        virtual ~IRenderComponent() = default;
    };

} // namespace GVM::Core
