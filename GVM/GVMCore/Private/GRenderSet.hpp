#pragma once
#include <atomic>
#include <EASTL/map.h>
#include <GVMRHI/GVMRHI.hpp>

#include "GRenderBufferComponent.hpp"

#include "GRenderSetCommandEncoder.hpp"
#include "GRenderTextureComponent.hpp"
#include <EASTL/intrusive_ptr.h>
#include <EASTL/queue.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
namespace GVM::Core
{
    class RenderSetTestPeer;

    enum class RenderComponentType : int64_t
    {
        BufferComponent,
        TextureComponent
    };

    struct RenderComponentCreateInfo
    {
        RenderComponentType type;
        uint64_t dataElementStorageSize = 0;
        uint64_t maxResourceCount = 1;
        eastl::string componentName;
    };
    struct RenderSetCreateInfo
    {
        eastl::string renderSetName;
        eastl::map<int, RenderComponentCreateInfo> componentInfos;
        eastl::vector<eastl::string> componentNameList;
        eastl::string vertexComponentName;
        eastl::string indexComponentName;
    };
    struct RenderEntityInfo
    {
        uint32_t indexCount = RenderComponentNullIndex;
        uint32_t instanceCount = RenderComponentNullIndex;
        uint32_t firstIndex = RenderComponentNullIndex;
        uint32_t vertexOffset = RenderComponentNullIndex;
        uint32_t globalInstanceBase = RenderComponentNullIndex;
        uint32_t vertexCount = RenderComponentNullIndex;
        uint32_t entityVersion = RenderComponentNullIndex;
        uint32_t cmdParamsOffset = RenderComponentNullIndex;
    };

    struct RenderEntityCMDParam
    {
        uint32_t entity = RenderComponentNullIndex;
        uint32_t instanceIndex = RenderComponentNullIndex;
    };

    struct RenderSetAccessBoundData
    {
        uint32_t componentIndexCount = 0u;
        uint32_t resourceCount = 0u;
    };
    class RenderSet : public GVM::RHI::RefCountedObject
    {
        friend class RenderSetCommandEncoderImpl;
        friend class RenderSetTestPeer;

        struct RenderComponentResourceGenerationInfo
        {
            uint64_t resource = 0;
            uint64_t componentIndex = 0;
        };

        eastl::unordered_map<eastl::string, eastl::intrusive_ptr<IRenderComponent>> mComponentMap;
        eastl::unordered_map<eastl::string, RenderComponentResourceGenerationInfo> mComponentResourceGenerationInfo;
        eastl::vector<eastl::string> mComponentNameList;

        eastl::string mRenderSetName;
        eastl::string mVertexComponentName;
        eastl::string mIndexComponentName;

        GVM::RHI::Device mDevice;
        GVM::RHI::Logger mLogger;
        GVM::RHI::BindGroup mBindGroup;
        GVM::RHI::BindGroupLayout mBindGroupLayout;
        GVM::RHI::Buffer mRenderSetAccessBoundDataBuffer;

        eastl::unique_ptr<GPUVectorBase<uint8_t>> mRenderEntityInfoBuffer;
        ProgressiveData<RenderEntityInfo> mRenderEntityInfos;


        eastl::unique_ptr<GPUVectorBase<uint8_t>> mRenderEntityCMDParamsBuffer;
        ProgressiveData<RenderEntityCMDParam> mRenderEntityCMDParamsInfos;
        uint64_t mRenderEntityInfoBufferGenerationCount = 0;
        uint64_t mRenderEntityCMDParamsBufferGenerationCount = 0;


        xGE::Primitive::xFreeList mRenderEntityCMDParamsFreeList;
        xGE::Primitive::xFreeList mEntityFreeList;

        std::atomic<uint32_t> mMaxEntityCounter = 0;
        std::atomic<uint32_t> mMaxCMDParamsCounter = 0;
        std::atomic<uint32_t> mMaxCMDParamsCounterThreadSafe = 0;
        eastl::queue<RenderSetCommandEncoder> mCMDQueue;
        eastl::queue<RenderSetCommandEncoder> mCMDGCQueue;
        mutable std::mutex mCommandMTX;
        std::mutex mQueueMTX;
        void updateBindGroup();
        void updateRenderSetAccessBoundData();
        void cleanComponentsUnlocked(RenderEntityIndex entity);
        void removeUnlocked(RenderEntityIndex entity, bool removeComponentAllocations);


        RenderComponentIndexCopyInfo getRenderEntityInfoAllocInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator);
        RenderComponentIndexCopyInfo getRenderEntityInfoRemoveInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator);
        RenderComponentIndexCopyInfo getRenderEntityCMDParamsAllocInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator);
        RenderComponentIndexCopyInfo getRenderEntityCMDParamsRemoveInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator);
        void resizeBuffer();

    public:
        RenderSet();
        void create(GVM::RHI::Device device, const RenderSetCreateInfo &createInfo);
        RenderEntityIndex alloc(const RenderSetAllocInfo &info);
        bool check(RenderEntityIndex entity);

        void cleanComponents(RenderEntityIndex entity);
        void remove(RenderEntityIndex entity);

        void executeCommand(const AbstractRenderSetCommandEncoder &command);
        AbstractRenderSetCommandEncoder createEncoder();
        bool checkComponentResourceName(RenderComponentHandle handle, const eastl::string &name);
        template <class T>
        RenderComponentHandle createRenderComponentHandle(T t) const
        {
            return t + 1;
        }

        eastl::intrusive_ptr<BufferComponent> getBufferComponentByName(const eastl::string &name) const;
        eastl::intrusive_ptr<BufferComponent> getBufferComponentByHandle(RenderComponentHandle handle) const;
        eastl::intrusive_ptr<TextureComponent> getTextureComponentByName(const eastl::string &name) const;
        eastl::intrusive_ptr<TextureComponent> getTextureComponentByHandle(RenderComponentHandle handle) const;

        eastl::intrusive_ptr<IRenderComponent> getComponentByName(const eastl::string &name) const;
        eastl::intrusive_ptr<IRenderComponent> getComponentByHandle(RenderComponentHandle handle) const;

        [[nodiscard]]
        eastl::string getVertexComponentName() const;
        [[nodiscard]]
        eastl::string getIndexComponentName() const;

        GVM::RHI::BindGroup getBindGroup();
        GVM::RHI::BindGroupLayout getBindGroupLayout() const;
        GVM::RHI::Buffer getVertexBuffer();
        GVM::RHI::Buffer getIndexBuffer();
        GVM::RHI::Buffer getRenderEntityInfoBuffer();
        GVM::RHI::Buffer getRenderEntityCMDParamsBuffer();
        uint64_t getCMDParamsCount();
        GVM::RHI::IndexFormat getIndexFormat() const;
        int getMaxEntityCount();
        void update();
        void destroy();

    private:
    };

} // namespace GVM::Core
