#pragma once

#include "GRenderComponentBatchedCopyCommand.hpp"
#include "GRenderSetInternalCommand.hpp"
#include "GStagingLinearAllocator.hpp"
#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/vector.h>
#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <xGEFoundation/xMath.hpp>

namespace GVM::Core
{


    // ======================================================================================
    // 渲染指令编码器实现
    // ======================================================================================
    class RenderSetCommandEncoderImpl final : public AbstractRenderSetCommandEncoderImpl
    {
        friend class RenderSet;
        friend class RenderSetCommandEncoderTestPeer;

    public:
        virtual RenderEntityIndex allocEntity(const RenderSetAllocInfo &info) override;
        /** Records an in-place upload for a validated entity BufferComponent element range. */
        virtual void setBufferComponentData(
            RenderEntityIndex entity,
            RenderComponentHandle component,
            const void *value,
            uint64_t dataStorageSize,
            uint32_t instanceStartIndex,
            uint32_t instanceCount) override;
        virtual void removeEntity(RenderEntityIndex entity) override;

        virtual void create(GVM::RHI::Device device, RenderSet *renderSet) override;
        void processInUserThread(); // 阶段1：提交数据到 GPU Staging
        void processInternal();     // 阶段2：提交 GPU Copy 指令
        void destroy();

        /** Returns the highest RenderEntityCMDParams element that this encoder has uploaded for publication. */
        [[nodiscard]]
        uint32_t getPublishableCMDParamsCount() const;

        void recordIndexCopyCommand(const RenderComponentIndexCopyInfo &allocInfo, RenderComponentBatchedCopyCommand &copyCMD);
        void executeBufferCopyCommand(RenderComponentBatchedCopyCommand &copyCommand, RHI::Buffer dstBuffer);

    private:
        // 辅助函数：处理具体的 Buffer/Texture 录制逻辑
        void recordBufferCopy(RenderEntityIndex entity, const RenderSetBufferComponentAllocInfo &info);
        void recordTextureCopy(RenderEntityIndex entity, const RenderSetTextureComponentTextureInfo &info);
        void recordRemove(RenderEntityIndex entity);

    private:
        std::mutex mMTX;
        GVM::RHI::Device mDevice;
        GVM::RHI::Logger mLogger;
        RenderSet *mRenderSet = nullptr;

        // --- 核心优化数据结构 ---

        // 1. CPU 端线性数据堆 (替代了零散的 new/delete)
        StagingLinearAllocator mCPUStaging;

        // 2. GPU 端 Staging Buffer (对应 mTotalBuffer)
        GVM::RHI::Buffer mGPUStagingBuffer;


        eastl::unordered_map<RenderComponentHandle, RenderComponentBatchedCopyCommand> mComponentDataBufferPendingCopies;
        eastl::unordered_map<RenderComponentHandle, RenderComponentBatchedCopyCommand> mComponentIndexBufferPendingCopies;
        RenderComponentBatchedCopyCommand mRenderEntityInfoPendingCopies;
        RenderComponentBatchedCopyCommand mRenderEntityCMDParamsPendingCopies;
        eastl::unordered_map<RenderComponentHandle, RenderSetTextureComponentCopyCommand> mComponentTexturePendingCopies;


        eastl::unordered_set<RenderEntityIndex> mPendingRemoves;

        uint64_t mCommandCount = 0;
        uint32_t mPublishableCMDParamsCount = 0;
        const uint64_t mAlign = 64; // GPU 最佳对齐粒度
    };
    using RenderSetCommandEncoder = eastl::shared_ptr<RenderSetCommandEncoderImpl>;


} // namespace GVM::Core
