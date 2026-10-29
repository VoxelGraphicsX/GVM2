#include "MBuffer.hpp"
#include "MDevice.hpp"

#include <GVMRHI/Private/RHIBufferContracts.hpp>

#include <cstring>
#include <stdexcept>
namespace GVM::RHI::Metal
{
    MBuffer &resolveBufferRange(MDevice &device, BufferRange &range, BufferUsageFlags usage, uint32_t alignment)
    {
        auto *source = dynamic_cast<MBuffer *>(range.buffer.get());
        if (source == nullptr || source->getNativeBuffer() == nullptr || &source->getDevice() != &device)
        {
            throw std::invalid_argument("Buffer operation requires a live buffer from the same device.");
        }
        GVM::RHI::Detail::validateEncoderBufferRange(source->getUsage(), source->getStorageSize(), range, usage, alignment);
        return *source;
    }

    MBuffer::MBuffer()
    {
    }
    MBuffer::~MBuffer()
    {
    }
    namespace
    {
        MTL::ResourceOptions translateBufferUsageToResourceOptionMTL(BufferUsageFlags usage)
        {
            MTL::ResourceOptions mtlUsage = MTL::ResourceStorageModePrivate;
            if (usage & BufferUsage::MapRead || usage & BufferUsage::MapWrite)
            {
                mtlUsage = MTL::ResourceStorageModeShared;
            }
            return mtlUsage;
        }

        /** Allocates exactly the command capacity needed by one cached conversion slot and reports allocation failure. */
        NS::SharedPtr<MTL::IndirectCommandBuffer> createIndirectRenderCommand(MDevice &device, MTL::IndirectCommandType type, uint32_t commandCount)
        {
            const auto commandDescriptor = NS::TransferPtr(MTL::IndirectCommandBufferDescriptor::alloc()->init());
            commandDescriptor->setCommandTypes(type);
            commandDescriptor->setInheritBuffers(true);
            commandDescriptor->setInheritPipelineState(true);
            commandDescriptor->setMaxFragmentBufferBindCount(MBuffer::MaxIndirectBufferBindingCount);
            commandDescriptor->setMaxMeshBufferBindCount(MBuffer::MaxIndirectBufferBindingCount);
            commandDescriptor->setMaxObjectBufferBindCount(MBuffer::MaxIndirectBufferBindingCount);
            commandDescriptor->setMaxVertexBufferBindCount(MBuffer::MaxIndirectBufferBindingCount);

            auto commands = NS::TransferPtr(device.getNativeDevice()->newIndirectCommandBuffer(commandDescriptor.get(), commandCount, 0));
            if (!commands)
            {
                throw std::runtime_error("Metal indirect command buffer allocation failed.");
            }
            return commands;
        }
        /** Encodes a live ICB resource ID into its owned conversion argument buffer. */
        NS::SharedPtr<MTL::Buffer> createArgumentBufferForICB(MDevice &device, MTL::IndirectCommandBuffer &icb)
        {
            const uint64_t address = icb.gpuResourceID()._impl;
            auto result = NS::TransferPtr(device.getNativeDevice()->newBuffer(sizeof(address) + sizeof(MTL::IndirectCommandBufferExecutionRange), MTL::ResourceStorageModeShared));
            if (!result)
            {
                throw std::runtime_error("Metal indirect conversion argument buffer allocation failed.");
            }
            memcpy(result->contents(), &address, sizeof(address));
            return result;
        }

        void validateMappedRangeAccess(const BufferDescriptor &descriptor, const MTL::Buffer *nativeBuffer, bool isMapped, uint64_t offset, uint64_t size, const char *operationName)
        {
            if (!Detail::isBufferMappable(descriptor.usage))
            {
                throw std::logic_error(std::string(operationName) + " was called on a buffer that was not created with MapRead or MapWrite usage.");
            }
            if (nativeBuffer == nullptr)
            {
                throw std::logic_error(std::string(operationName) + " was called after the native Metal buffer had been destroyed.");
            }
            if (!isMapped)
            {
                throw std::logic_error(std::string(operationName) + " requires map() to be called before accessing a mapped range.");
            }

            const auto resolvedRange = Detail::resolveMappedRange(descriptor.size, offset, size);
            if (!resolvedRange.valid)
            {
                throw std::out_of_range(std::string(operationName) + " requested a range outside the buffer storage.");
            }
        }
    } // namespace
    void MBuffer::init(MDevice *device, const BufferDescriptor &descriptor)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        this->mDevice = device;
        this->mLabelName = descriptor.label;
        this->mDescriptor = descriptor;
        this->mIsMapped = false;
        if (device != nullptr)
        {
            const auto resourceOptions = translateBufferUsageToResourceOptionMTL(descriptor.usage);
            // Resources keep Metal's default tracked hazard mode: GVM relies on Metal's automatic hazard tracking between encoders instead of explicit MTLFence/MTLEvent synchronization.
            this->mNativeBuffer = device->getNativeDevice()->newBuffer(descriptor.size, resourceOptions);
        }

        if (this->mNativeBuffer == nullptr)
        {
            throw std::runtime_error(std::string("MBuffer::init failed to create a native Metal buffer for label: ") + descriptor.label.c_str());
        }

        if (this->mNativeBuffer != nullptr)
        {
            this->mNativeBuffer->setLabel(NS::String::string(descriptor.label.c_str(), NS::UTF8StringEncoding));
        }
    }

    uint64_t MBuffer::getStorageSize() const
    {
        return this->mDescriptor.size;
    }

    void MBuffer::map()
    {
        if (!Detail::isBufferMappable(mDescriptor.usage))
        {
            throw std::logic_error("MBuffer::map was called on a buffer that was not created with MapRead or MapWrite usage.");
        }
        if (mNativeBuffer == nullptr)
        {
            throw std::logic_error("MBuffer::map was called after the native Metal buffer had been destroyed.");
        }
        if (mIsMapped)
        {
            throw std::logic_error("MBuffer::map was called while the buffer was already mapped.");
        }
        mIsMapped = true;
    }

    void const *MBuffer::getConstMappedRange(uint64_t offset, uint64_t size) const
    {
        validateMappedRangeAccess(mDescriptor, mNativeBuffer, mIsMapped, offset, size, "MBuffer::getConstMappedRange");
        return static_cast<const uint8_t *>(mNativeBuffer->contents()) + offset;
    }

    void *MBuffer::getMappedRange(uint64_t offset, uint64_t size) const
    {
        validateMappedRangeAccess(mDescriptor, mNativeBuffer, mIsMapped, offset, size, "MBuffer::getMappedRange");
        return static_cast<uint8_t *>(mNativeBuffer->contents()) + offset;
    }

    void MBuffer::unmap()
    {
        if (!Detail::isBufferMappable(mDescriptor.usage))
        {
            throw std::logic_error("MBuffer::unmap was called on a buffer that was not created with MapRead or MapWrite usage.");
        }
        if (mNativeBuffer == nullptr)
        {
            throw std::logic_error("MBuffer::unmap was called after the native Metal buffer had been destroyed.");
        }
        if (!mIsMapped)
        {
            throw std::logic_error("MBuffer::unmap was called while the buffer was not mapped.");
        }
        mIsMapped = false;
    }

    MTL::Buffer *MBuffer::getNativeBuffer() const
    {
        return this->mNativeBuffer;
    }

    BufferUsageFlags MBuffer::getUsage() const
    {
        return mDescriptor.usage;
    }

    bool MBuffer::isMapped() const
    {
        return mIsMapped;
    }

    const NativeIndirectCommandBuffer &MBuffer::getOrCreateNativeICB(
        MTL::IndirectCommandType type,
        const MTL::CommandQueue &commandQueue,
        uint32_t conversionSlot, uint32_t commandCount)
    {
        if (!(mDescriptor.usage & GVM::RHI::BufferUsage::Indirect))
        {
            throw std::runtime_error(std::string("buffer: ") + this->getLabelName().c_str() + " is not created with indirect");
        }

        return Metal::getOrCreateNativeICB(*mDevice, *getIndirectCommandCache(), commandCount,
            type, commandQueue, conversionSlot);
    }

    eastl::shared_ptr<NativeIndirectCommandBufferCache> MBuffer::getIndirectCommandCache()
    {
        if (!mICBs)
        {
            mICBs = eastl::make_shared<NativeIndirectCommandBufferCache>();
        }
        return mICBs;
    }

    const NativeIndirectCommandBuffer &getOrCreateNativeICB(MDevice &device,
        NativeIndirectCommandBufferCache &cache, uint32_t commandCount,
        MTL::IndirectCommandType type, const MTL::CommandQueue &queue, uint32_t slot)
    {
        if (commandCount == 0)
        {
            throw std::invalid_argument("Metal indirect conversion storage requires at least one command.");
        }
        const NativeIndirectCommandBufferKey key{&queue, type, slot};
        const auto existing = cache.find(key);
        if (existing != cache.end() && existing->second.commandCapacity >= commandCount)
        {
            return existing->second;
        }
        NativeIndirectCommandBuffer allocation;
        allocation.commands = createIndirectRenderCommand(device, type, commandCount);
        allocation.argumentBuffer = createArgumentBufferForICB(device, *allocation.commands.get());
        allocation.commandCapacity = commandCount;
        if (existing != cache.end())
        {
            existing->second = eastl::move(allocation);
            return existing->second;
        }
        return cache.emplace(key, eastl::move(allocation)).first->second;
    }

    /** Releases source storage and every cached native conversion allocation. */
    void MBuffer::destroy()
    {
        if (mNativeBuffer != nullptr)
        {
            mNativeBuffer->release();
            mNativeBuffer = nullptr;
        }
        mICBs.reset();
        mIsMapped = false;
        mDevice = nullptr;
    }


} // namespace GVM::RHI::Metal
