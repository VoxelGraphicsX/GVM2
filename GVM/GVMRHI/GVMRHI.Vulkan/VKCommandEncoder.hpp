#pragma once

#include "VKCommon.hpp"
#include "VKArguments.hpp"
#include <EASTL/unique_ptr.h>
#include "VKTaskDependencyResolver.hpp"
#include "VKDefines.hpp"
#include "VKTransientDescriptorAllocator.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>
#include <EASTL/variant.h>

#include <EASTL/shared_ptr.h>

namespace GVM::RHI::Vulkan
{
    struct VKOwnershipRelease;

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
        /// Closes and natively records this encoder on the calling thread; different encoders may finish concurrently.
        void end() override;
        /** Copies validated Args to an interval retained until timeline completion. */
        VKArgumentAllocation allocateArguments(Arguments arguments);

        void retainBindGroup(BindGroup bindGroup);
        void retainRenderPipeline(RenderPipeline pipeline);
        void retainComputePipeline(ComputePipeline pipeline);
        void retainQuerySet(QuerySet querySet);
        /** Drops the encoder's references on every retained buffer and texture; handles whose impl is already gone are skipped, which cannot happen while the encoder still holds its reference. */
        void releaseRetainedResourceReferences();

        void retainBuffer(Buffer buffer);
        void retainTexture(Texture texture);
        void retainNativeResource(eastl::shared_ptr<void> resource);
        void notePassSummary();
        void notifyPassEnded();
        /** Records a timestamp; call the beginning phase first to check and reset both immutable indices. */
        void writePassTimestamp(const PassTimestampWrites &timestampWrites, bool beginning, vk::PipelineStageFlagBits2 stage);
        void onSubmitted();
        void onExecutionCompleted();
        /// Lowers a closed encoder using only its exclusive native context and local resource history.
        void encodeNative();
        /// Appends entry synchronization followed by the body, updating only the pending submission history.
        void appendSubmissionCommands(VKResourceStateDB &state, eastl::vector<vk::CommandBuffer> &commands);
        /// Records a release-only native command buffer retaining the consumer captures until the release itself retires.
        void recordOwnershipRelease(const VKOwnershipRelease &release);
        /// Appends presentation synchronization after the last body using the submission's complete resource history.
        void appendPresentTransition(VKResourceStateDB &state, const VKTexture &texture, eastl::vector<vk::CommandBuffer> &commands);
        /// Appends a closed render pass, retaining its commands until execution completes.
        void appendPass(RenderPassEncoder pass);
        /// Appends a closed compute pass with its dispatch-local bindings.
        void appendPass(ComputePassEncoder pass);
        /// Appends a closed copy pass and its retained upload references.
        void appendPass(BlitPassEncoder pass);
        eastl::shared_ptr<VKCommandBufferContext> takeCommandBufferContext();

        [[nodiscard]]
        VKTaskDependencyResolver &getTaskDependencyResolver();

        [[nodiscard]]
        VKTransientDescriptorAllocator &getTransientDescriptorAllocator();

        [[nodiscard]]
        vk::CommandBuffer getNativeCommandBuffer() const;

        /// Reports successful native completion; a failed end is never eligible for submission.
        [[nodiscard]] bool isEnded() const;

        /** Returns whether this encoder was already handed to a queue submission. */
        [[nodiscard]]
        bool isSubmitted() const;

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
        /// Lists query pools referenced by this recording for cross-queue synchronization at acceptance.
        const eastl::vector<QuerySet> &getRetainedQuerySets() const;

    private:
        /// Records submission-only barriers in a cached command buffer after body recording has finished.
        void recordSubmissionBarriers(vk::CommandBuffer &commandBuffer,
            const eastl::vector<vk::BufferMemoryBarrier2> &buffers, const eastl::vector<vk::ImageMemoryBarrier2> &images);
        /** Borrows the identity of a query set retained by its timestamp command. */
        struct WrittenTimestampQuery
        {
            const QuerySetImpl *querySet = nullptr;
            uint32_t index = QuerySetIndexUndefined;
        };

        struct ResetTimestampQueryRange
        {
            QuerySet querySet = nullptr;
            uint32_t firstIndex = QuerySetIndexUndefined;
            uint32_t count = 0u;
        };

        /// Records a timestamp write without issuing native commands during recording.
        struct TimestampCommand
        {
            QuerySet querySet;
            uint32_t index;
            vk::PipelineStageFlagBits2 stage;
        };
        /// Retains a query resolve and its destination until submission.
        struct ResolveQueryCommand
        {
            QuerySet querySet;
            uint32_t first, count;
            BufferRange destination;
        };
        using RecordedCommand = eastl::variant<RenderPassEncoder, ComputePassEncoder, BlitPassEncoder,
            ResetTimestampQueryRange, TimestampCommand, ResolveQueryCommand>;
        eastl::vector<RecordedCommand> mCommands;
        eastl::unique_ptr<VKArgumentStorage> mArgumentStorage;
        bool mNativeEncoded = false;
        bool mNativeEncodingFailed = false;
        void ensureRecordable(const char *apiName) const;
        void beginPass(const char *apiName);
        void resetPassTimestampQueriesIfNeeded(VKQuerySet &vkQuerySet, QuerySet querySet, const PassTimestampWrites &timestampWrites);
        bool hasResetTimestampQuery(const QuerySet &querySet, uint32_t index) const;
        bool hasWrittenTimestampQuery(const QuerySet &querySet, uint32_t index) const;

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
