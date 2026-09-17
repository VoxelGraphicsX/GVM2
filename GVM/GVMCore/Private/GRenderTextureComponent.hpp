#pragma once
#include "GGPUVector.hpp"
#include "GRenderComponent.hpp"
#include <EASTL/unique_ptr.h>
namespace GVM::Core
{
    class TextureComponentTestPeer;

    struct TextureComponentCreateInfo
    {
        eastl::string textureComponentName;
        uint64_t dataElementIncreamentCount = 10000;
        uint64_t maxTextureCount = 1024;
    };

    class TextureComponent final : public IRenderComponent
    {
        friend class RenderSet;
        friend struct RenderComponentPacker;
        friend class RenderSetCommandEncoderImpl;
        friend class TextureComponentTestPeer;
        struct TextureUseInfo
        {
            RenderComponentIndex index = RenderComponentNullIndex;
            int64_t useCount = 0;
        };

        struct ComponentUseInfo
        {
            eastl::vector<eastl::string> textureNames;
        };
        GVM::RHI::Texture mEmptyTexture;
        GVM::RHI::TextureView mEmptyTextureView;
        eastl::string mTextureComponentName;
        uint64_t mMaxTextureCount = 0;
        eastl::unique_ptr<GPUVectorBase<uint8_t>> mComponentIndexBuffer;
        ProgressiveData<uint32_t> mComponentIndex;
        eastl::vector<GVM::RHI::Texture> mTextureLib;
        eastl::vector<GVM::RHI::TextureView> mTextureViewLib;
        uint64_t mTextureLibGenerationCounter = 0;
        xGE::Primitive::xFreeList mTextureLibFreeList;
        GVM::RHI::Device mDevice;
        ProgressiveData<ComponentUseInfo> mEntities;
        eastl::unordered_map<eastl::string, TextureUseInfo> mTextureUseInfos;
        void alloc(RenderEntityIndex entity, const RenderSetTextureComponentAllocInfo &allocInfo);
        RenderComponentIndexCopyInfo getComponentIndexAllocInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator) override;
        RenderComponentIndexCopyInfo getComponentIndexRemoveInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator) override;

        virtual uint64_t getPerEntityComponentIndexStorageBytes() const override;
        GVM::RHI::Texture createTexture(const RenderSetTextureComponentTextureInfo &info);
        void performTextureCopy(const RenderSetTextureComponentTextureCopyCommand &cmd);
        virtual void resizeBuffer() override;
        mutable std::mutex mMtx;

    public:
        void create(GVM::RHI::Device device, const TextureComponentCreateInfo &info);

        virtual GVM::RHI::Buffer getComponentIndexBuffer() const override;
        /** Appends the component-index buffer and bindless texture array layout entries used by RenderSet. */
        virtual void appendBindGroupLayoutEntries(eastl::vector<GVM::RHI::BindGroupLayoutEntry> &entries, uint32_t &bindingIndex) const override;
        /** Appends the component-index buffer and bindless texture array bindings used by RenderSet. */
        virtual void appendBindGroupEntries(eastl::vector<GVM::RHI::BindGroupEntry> &entries, uint32_t &bindingIndex) const override;
        /** Returns the texture library generation for RenderSet rebinding. */
        virtual RenderComponentBindingGeneration getBindGroupGeneration() const override;
        virtual uint64_t getResourceGenerationCounter() const override;
        uint64_t getComponentIndexBufferGenerationCounter() const;
        GVM::RHI::BindGroupLayoutEntry getBindGroupLayoutEntry() const;
        GVM::RHI::BindGroupEntry getResourceBindGroupEntry() const;
        virtual uint64_t getComponentIndexElementCountForRenderSetAccessBounds() const override;
        virtual uint64_t getResourceElementCountForRenderSetAccessBounds() const override;

        virtual bool check(const eastl::string &textureName) const override;
        virtual bool check(RenderEntityIndex entity) const override;
        virtual void update() override;
        virtual void remove(RenderEntityIndex entity) override;
        virtual void destroy() override;
    };

} // namespace GVM::Core
