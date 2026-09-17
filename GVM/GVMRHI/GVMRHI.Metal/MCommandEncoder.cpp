#include "MCommandEncoder.hpp"
#include "MBindGroup.hpp"
#include "MBlitPassEncoder.hpp"
#include "MBuffer.hpp"
#include "MComputePassEncoder.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MQuerySet.hpp"
#include "MRenderPassEncoder.hpp"
#include "MRenderPipeline.hpp"
#include "MTextureView.hpp"
#include <EASTL/make_intrusive.h>
#include <stdexcept>
#include <string>
namespace GVM::RHI::Metal
{
    MCommandEncoder::DeferredRenderPrepassScope::DeferredRenderPrepassScope(MCommandEncoder *commandEncoder)
        : mCommandEncoder(commandEncoder)
    {
        if (mCommandEncoder == nullptr)
        {
            throw std::logic_error("MCommandEncoder::DeferredRenderPrepassScope requires a valid command encoder.");
        }
        if (mCommandEncoder->mEnded)
        {
            throw std::logic_error("MCommandEncoder::DeferredRenderPrepassScope was created after the command encoder had ended.");
        }
        if (!mCommandEncoder->mHasOpenPass)
        {
            throw std::logic_error("MCommandEncoder::DeferredRenderPrepassScope requires an open deferred render pass.");
        }
        mCommandEncoder->mHasOpenPass = false;
    }

    MCommandEncoder::DeferredRenderPrepassScope::~DeferredRenderPrepassScope()
    {
        if (mCommandEncoder == nullptr)
        {
            return;
        }
        if (!mCommandEncoder->mEnded && !mCommandEncoder->mHasOpenPass)
        {
            mCommandEncoder->mHasOpenPass = true;
        }
        mCommandEncoder = nullptr;
    }

    MCommandEncoder::MCommandEncoder()
    {
    }

    MCommandEncoder::~MCommandEncoder()
    {
        if (mNativeCommandEncoder != nullptr) { mNativeCommandEncoder->release(); }
    }

    void MCommandEncoder::init(MDevice *device, MTL::CommandBuffer *nativeCommandEncoder, const eastl::string &labelName)
    {
        this->mDevice = device;
        this->mNativeCommandEncoder = nativeCommandEncoder;
        this->mNativeCommandEncoder->retain();
        this->mLabelName = labelName;
        this->mNativeCommandEncoder->setLabel(NS::String::string(this->mLabelName.c_str(), NS::UTF8StringEncoding));
    }

    void MCommandEncoder::begin()
    {
    }

    RenderPassEncoder MCommandEncoder::beginRenderPass(const RenderPassDescriptor &pass)
    {
        beginPass("MCommandEncoder::beginRenderPass");
        eastl::intrusive_ptr<MRenderPassEncoder> renderPassEncoder = eastl::make_intrusive<MRenderPassEncoder>();
        renderPassEncoder->init(this->mDevice, this, pass);
        // mRenderPassEncoders.push_back(renderPassEncoder);
        return renderPassEncoder;
    }

    BlitPassEncoder MCommandEncoder::beginBlitPass(const BlitPassDescriptor &pass)
    {
        beginPass("MCommandEncoder::beginBlitPass");
        eastl::intrusive_ptr<MBlitPassEncoder> blitPassEncoder = eastl::make_intrusive<MBlitPassEncoder>();

        blitPassEncoder->init(this->mDevice, this, pass);

        return blitPassEncoder;
    }

    ComputePassEncoder MCommandEncoder::beginComputePass(const ComputePassDescriptor &pass)
    {
        beginPass("MCommandEncoder::beginComputePass");
        eastl::intrusive_ptr<MComputePassEncoder> computePassEncoder = eastl::make_intrusive<MComputePassEncoder>();
        computePassEncoder->init(this->mDevice, this, pass);
        return computePassEncoder;
    }

    void MCommandEncoder::resolveQuerySet(QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, BufferRange destination)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        if (mNativeCommandEncoder == nullptr)
        {
            throw std::logic_error("MCommandEncoder::resolveQuerySet was called on an uninitialized command encoder.");
        }
        if (mEnded)
        {
            throw std::logic_error("MCommandEncoder::resolveQuerySet was called after the command encoder had ended.");
        }
        if (mHasOpenPass)
        {
            throw std::logic_error("MCommandEncoder::resolveQuerySet cannot be called while a pass encoder is open.");
        }
        if (queryCount == 0u)
        {
            return;
        }
        if (querySet == nullptr)
        {
            throw std::invalid_argument("MCommandEncoder::resolveQuerySet requires a valid query set.");
        }
        auto *metalQuerySet = dynamic_cast<MQuerySet *>(querySet.get());
        if (metalQuerySet == nullptr)
        {
            throw std::invalid_argument("MCommandEncoder::resolveQuerySet received a non-Metal query set.");
        }
        if (firstQuery > metalQuerySet->getCount() || queryCount > (metalQuerySet->getCount() - firstQuery))
        {
            throw std::out_of_range("MCommandEncoder::resolveQuerySet query range exceeds the query set count.");
        }
        if (destination.buffer.isNull())
        {
            throw std::invalid_argument("MCommandEncoder::resolveQuerySet requires a valid destination buffer.");
        }
        const uint32_t resultStrideBytes = metalQuerySet->getResultStrideBytes();
        if (resultStrideBytes == 0u)
        {
            throw std::invalid_argument("MCommandEncoder::resolveQuerySet query result stride must be non-zero.");
        }
        if ((destination.offset % resultStrideBytes) != 0u)
        {
            throw std::invalid_argument("MCommandEncoder::resolveQuerySet requires destination offset alignment matching the query result stride.");
        }
        auto *destinationBuffer = dynamic_cast<MBuffer *>(destination.buffer.get());
        if (destinationBuffer == nullptr)
        {
            throw std::invalid_argument("MCommandEncoder::resolveQuerySet received a non-Metal destination buffer.");
        }
        if ((destinationBuffer->getUsage() & BufferUsage::QueryResolve) == 0u)
        {
            throw std::invalid_argument("MCommandEncoder::resolveQuerySet destination buffer must be created with BufferUsage::QueryResolve.");
        }
        const uint64_t resolveBytes = static_cast<uint64_t>(queryCount) * resultStrideBytes;
        const uint64_t storageSize = destinationBuffer->getStorageSize();
        if (destination.offset > storageSize)
        {
            throw std::out_of_range("MCommandEncoder::resolveQuerySet destination offset exceeds the buffer size.");
        }
        const uint64_t remainingSize = storageSize - destination.offset;
        const uint64_t destinationSize = destination.size == WholeSize ? remainingSize : destination.size;
        if (destinationSize < resolveBytes || remainingSize < resolveBytes)
        {
            throw std::out_of_range("MCommandEncoder::resolveQuerySet destination buffer range is too small.");
        }

        retainQuerySet(querySet);
        retainBuffer(destination.buffer);
        auto *blitEncoder = mNativeCommandEncoder->blitCommandEncoder();
        blitEncoder->resolveCounters(
            metalQuerySet->getNativeCounterSampleBuffer(),
            NS::Range(firstQuery, queryCount),
            destinationBuffer->getNativeBuffer(),
            destination.offset);
        blitEncoder->endEncoding();
    }

    void MCommandEncoder::end()
    {
        if (mHasOpenPass)
        {
            throw std::logic_error("MCommandEncoder::end cannot close a command buffer while a pass encoder is still open.");
        }
        mEnded = true;
    }

    void MCommandEncoder::commit()
    {
        this->mNativeCommandEncoder->commit();
    }

    void MCommandEncoder::waitUntilCompleted()
    {
        this->mNativeCommandEncoder->waitUntilCompleted();
    }

    MTL::CommandBuffer *MCommandEncoder::getNativeCommandEncoder() const
    {
        return this->mNativeCommandEncoder;
    }

    void MCommandEncoder::notifyPassEnded()
    {
        mHasOpenPass = false;
    }

    void MCommandEncoder::beginPass(const char *apiName)
    {
        if (mEnded)
        {
            throw std::logic_error(std::string(apiName) + " was called after the command encoder had ended.");
        }
        if (mHasOpenPass)
        {
            throw std::logic_error(std::string(apiName) + " cannot nest pass encoders.");
        }
        mHasOpenPass = true;
    }

    void MCommandEncoder::retainQuerySet(QuerySet querySet)
    {
        if (querySet != nullptr)
        {
            mRetainedQuerySets.push_back(querySet);
        }
    }

    void MCommandEncoder::retainBuffer(Buffer buffer)
    {
        if (!buffer.isNull())
        {
            mRetainedBuffers.push_back(buffer);
        }
    }

} // namespace GVM::RHI::Metal
