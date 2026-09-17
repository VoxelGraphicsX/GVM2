#pragma once

#include "VKCommon.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKDefines.hpp"
#include "VKTransientDescriptorAllocator.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>

#include <EASTL/shared_ptr.h>

namespace GVM::RHI::Vulkan
{
    class VKQuerySet;

    class VKCommandEncoder final : public CommandEncoderImpl
    {
    public:
        VKCommandEncoder() = default;
        ~VKCommandEncoder() override;

        void init(VKQueue &queue, eastl::shared_ptr<VKCommandBufferContext> commandContext);

        void begin() override;
        RenderPassEncoder beginRenderPass(const RenderPassDescriptor &pass) override;
        BlitPassEncoder beginBlitPass(const BlitPassDescriptor &pass) override;
        ComputePassEncoder beginComputePass(const ComputePassDescriptor &pass) override;
        void resolveQuerySet(QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, BufferRange destination) override;
        void end() override;

        void retainBindGroup(BindGroup bindGroup);
        void retainRenderPipeline(RenderPipeline pipeline);
        void retainComputePipeline(ComputePipeline pipeline);
        void retainQuerySet(QuerySet querySet);
        void retainBuffer(Buffer buffer);
        void retainTexture(Texture texture);
        void retainNativeResource(eastl::shared_ptr<void> resource);
        void notePassSummary();
        void notifyPassEnded();
        void writePassTimestamp(const PassTimestampWrites &timestampWrites, bool beginning, vk::PipelineStageFlagBits stage);
        void onSubmitted();
        void onExecutionCompleted();
        void markStatePendingSubmit() const;
        eastl::shared_ptr<VKCommandBufferContext> takeCommandBufferContext();

        [[nodiscard]]
        VKTaskDependencyResolver &getTaskDependencyResolver();

        [[nodiscard]]
        VKTransientDescriptorAllocator &getTransientDescriptorAllocator();

        [[nodiscard]]
        vk::CommandBuffer getNativeCommandBuffer() const;

        [[nodiscard]]
        bool isEnded() const;

        [[nodiscard]]
        VKQueue *getQueue() const;

        [[nodiscard]]
        size_t getPassSummaryCount() const;

        [[nodiscard]]
        size_t getRetainedBufferCount() const;

        [[nodiscard]]
        size_t getRetainedTextureCount() const;

        [[nodiscard]]
        const eastl::vector<Texture> &getRetainedTextures() const;

    private:
        struct WrittenTimestampQuery
        {
            QuerySet querySet = nullptr;
            uint32_t index = QuerySetIndexUndefined;
        };

        struct ResetTimestampQueryRange
        {
            QuerySet querySet = nullptr;
            uint32_t firstIndex = QuerySetIndexUndefined;
            uint32_t count = 0u;
        };

        void ensureRecordable(const char *apiName) const;
        void beginPass(const char *apiName);
        void resetPassTimestampQueriesIfNeeded(VKQuerySet &vkQuerySet, QuerySet querySet, const PassTimestampWrites &timestampWrites);
        bool hasResetTimestampQuery(QuerySet querySet, uint32_t index) const;
        bool hasWrittenTimestampQuery(QuerySet querySet, uint32_t index) const;

        VKQueue *mQueue = nullptr;
        VKDevice *mDevice = nullptr;
        eastl::shared_ptr<VKCommandBufferContext> mCommandContext;
        vk::CommandBuffer mCommandBuffer;
        eastl::vector<BindGroup> mRetainedBindGroups;
        eastl::vector<RenderPipeline> mRetainedRenderPipelines;
        eastl::vector<ComputePipeline> mRetainedComputePipelines;
        eastl::vector<QuerySet> mRetainedQuerySets;
        eastl::vector<Buffer> mRetainedBuffers;
        eastl::vector<Texture> mRetainedTextures;
        eastl::vector<eastl::shared_ptr<void>> mRetainedNativeResources;
        eastl::vector<ResetTimestampQueryRange> mResetTimestampQueryRanges;
        eastl::vector<WrittenTimestampQuery> mWrittenTimestampQueries;
        size_t mPassSummaryCount = 0u;
        VKTaskDependencyResolver mTaskDependencyResolver;
        VKTransientDescriptorAllocator mTransientDescriptorAllocator;
        bool mHasOpenPass = false;
        bool mBegan = false;
        bool mEnded = false;
        bool mSubmitted = false;
        bool mExecutionCompleted = false;
    };
} // namespace GVM::RHI::Vulkan
