#pragma once
#include "MDefines.hpp"
#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    class MCommandEncoder final : public CommandEncoderImpl
    {
    public:
        // Allows deferred render end() to encode Metal ICB preparation passes before the native render encoder exists.
        class DeferredRenderPrepassScope
        {
        public:
            explicit DeferredRenderPrepassScope(MCommandEncoder *commandEncoder);
            DeferredRenderPrepassScope(const DeferredRenderPrepassScope &) = delete;
            DeferredRenderPrepassScope &operator=(const DeferredRenderPrepassScope &) = delete;
            ~DeferredRenderPrepassScope();

        private:
            MCommandEncoder *mCommandEncoder = nullptr;
        };

        MCommandEncoder();
        ~MCommandEncoder();
        void init(MDevice *device, MTL::CommandBuffer *nativeCommandEncoder, const eastl::string &labelName);
        virtual void begin() override;
        virtual RenderPassEncoder beginRenderPass(const RenderPassDescriptor &pass) override;
        virtual BlitPassEncoder beginBlitPass(const BlitPassDescriptor &pass) override;
        virtual ComputePassEncoder beginComputePass(const ComputePassDescriptor &pass) override;
        virtual void resolveQuerySet(QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, BufferRange destination) override;
        virtual void end() override;
        void commit();
        void waitUntilCompleted();
        MTL::CommandBuffer *getNativeCommandEncoder() const;
        void notifyPassEnded();
        void retainQuerySet(QuerySet querySet);

    private:
        void beginPass(const char *apiName);
        void retainBuffer(Buffer buffer);

        MTL::CommandBuffer *mNativeCommandEncoder = nullptr;
        MDevice *mDevice = nullptr;
        eastl::vector<QuerySet> mRetainedQuerySets;
        eastl::vector<Buffer> mRetainedBuffers;
        bool mHasOpenPass = false;
        bool mEnded = false;
        // eastl::vector<RenderPassEncoder> mRenderPassEncoders;
        // eastl::vector<BlitPassEncoder> mBlitPassEncoders;
        // eastl::vector<ComputePassEncoder> mComputePassEncoders;
    };

} // namespace GVM::RHI::Metal
