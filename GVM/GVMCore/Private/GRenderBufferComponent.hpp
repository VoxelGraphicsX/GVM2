#pragma once
#include "GGPUVector.hpp"
#include "GRenderComponent.hpp"
#include <EASTL/unique_ptr.h>
namespace GVM::Core
{
    class BufferComponentTestPeer;

    class RenderComponent final
    {
    public:
        RenderComponent();

    private:
    };

    struct BufferComponentCreateInfo
    {
        eastl::string bufferComponentName;
        uint64_t dataElementStorageSize = 0;
        uint64_t dataElementCountInBlock = 1;
        uint64_t dataElementIncreamentCount = 10000;
        GVM::RHI::BufferUsageFlags bufferUsage = GVM::RHI::BufferUsage::None;
    };

    class RenderSet;
    class BufferComponent final : public IRenderComponent
    {
        friend class RenderSet;
        friend struct RenderComponentPacker;
        friend class RenderSetCommandEncoderImpl;
        friend class BufferComponentTestPeer;
        struct BufferUseInfo
        {
            RenderComponentIndex index;
            uint64_t blockCount = 0;
            int64_t useCount = 0;
            uint64_t bufferStorageSize = 0;
        };
        eastl::string mBufferComponentName;
        struct ComponentUseInfo
        {
            eastl::string bufferName;
            RenderComponentIndex index;
            uint64_t blockCount = 0;
            uint64_t bufferStorageSize = 0;
        };

        eastl::unique_ptr<GPUVectorBase<uint8_t>> mDataBuffer;

        eastl::unique_ptr<GPUVectorBase<uint8_t>> mComponentIndexBuffer;
        ProgressiveData<uint32_t> mComponentIndex;

        ProgressiveData<ComponentUseInfo> mEntities;
        eastl::unordered_map<eastl::string, BufferUseInfo> mBufferUseInfos;
        xGE::Primitive::xFreeList mFreeList;
        uint64_t mElementStorageSize = 0;
        uint64_t mElementCountInBlock = 0;
        uint64_t getBlockCountFromDataStorageSize(uint64_t dataStorageSize) const;
        GVM::RHI::Device mDevice;
        RenderComponentIndex alloc(RenderEntityIndex entity, const eastl::string &bufferName, uint64_t dataStorageSize, uint32_t instanceCount);
        RenderComponentIndexCopyInfo getComponentIndexAllocInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator) override;
        RenderComponentIndexCopyInfo getComponentIndexRemoveInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator) override;

        virtual uint64_t getPerEntityComponentIndexStorageBytes() const override;


        uint32_t getDataBufferCopyOffset(RenderEntityIndex entity, uint32_t instanceStartIndex) const;
        virtual void update() override;
        mutable std::mutex mMtx;

    public:
        void create(GVM::RHI::Device device, const BufferComponentCreateInfo &info);

        GVM::RHI::Buffer getDataBuffer() const;
        virtual GVM::RHI::Buffer getComponentIndexBuffer() const override;
        /** Appends the component-index buffer and data buffer layout entries used by RenderSet. */
        virtual void appendBindGroupLayoutEntries(eastl::vector<GVM::RHI::BindGroupLayoutEntry> &entries, uint32_t &bindingIndex) const override;
        /** Appends the component-index buffer and data buffer bindings used by RenderSet. */
        virtual void appendBindGroupEntries(eastl::vector<GVM::RHI::BindGroupEntry> &entries, uint32_t &bindingIndex) const override;
        /** Returns the BufferComponent data and component-index generations for RenderSet rebinding. */
        virtual RenderComponentBindingGeneration getBindGroupGeneration() const override;
        virtual uint64_t getResourceGenerationCounter() const override;
        uint64_t getComponentIndexBufferGenerationCounter() const;
        virtual bool check(const eastl::string &bufferName) const override;
        virtual bool check(RenderEntityIndex entity) const override;
        virtual RenderComponentIndex getComponentIndexByEntity(RenderEntityIndex entity) const;
        GVM::RHI::BindGroupLayoutEntry getBindGroupLayoutEntry() const;
        GVM::RHI::BindGroupEntry getResourceBindGroupEntry() const;
        /** Returns the live component-index capacity published through RenderSetAccessBoundData. */
        virtual uint64_t getComponentIndexElementCountForRenderSetAccessBounds() const override;
        virtual uint64_t getResourceElementCountForRenderSetAccessBounds() const override;
        uint64_t getComponentStorageByteByName(const eastl::string &bufferName) const;
        uint64_t getComponentElementStorageBytes() const;
        virtual void resizeBuffer() override;
        virtual void remove(RenderEntityIndex entity) override;
        virtual void destroy() override;
    };
} // namespace GVM::Core
