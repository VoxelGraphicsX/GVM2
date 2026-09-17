#include "MBuffer.hpp"
#include "MDevice.hpp"
#include <cstring>
#include <stdexcept>
namespace GVM::RHI::Metal
{

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

        Detail::IndirectCommandLayoutKind translateIndirectCommandLayoutKind(MTL::IndirectCommandType type)
        {
            switch (type)
            {
            case MTL::IndirectCommandTypeDraw:
                return Detail::IndirectCommandLayoutKind::Draw;
            case MTL::IndirectCommandTypeDrawIndexed:
                return Detail::IndirectCommandLayoutKind::DrawIndexed;
            default:
                throw std::runtime_error("unsupported indirect buffer type");
            }
        }

        MTL::IndirectCommandBuffer *createIndirectRenderCommand(MDevice *device, MTL::IndirectCommandType type, uint64_t bufferSize)
        {
            auto commandDescriptor = MTL::IndirectCommandBufferDescriptor::alloc()->init();
            commandDescriptor->setCommandTypes(type);
            commandDescriptor->setInheritBuffers(true);
            commandDescriptor->setInheritPipelineState(true);
            commandDescriptor->setMaxFragmentBufferBindCount(MBuffer::MaxIndirectBufferBindingCount);
            commandDescriptor->setMaxMeshBufferBindCount(MBuffer::MaxIndirectBufferBindingCount);
            commandDescriptor->setMaxObjectBufferBindCount(MBuffer::MaxIndirectBufferBindingCount);
            commandDescriptor->setMaxVertexBufferBindCount(MBuffer::MaxIndirectBufferBindingCount);

            const uint64_t indirectCommandStride = Detail::getIndirectCommandStride(translateIndirectCommandLayoutKind(type));
            const uint64_t indirectCommandCount = Detail::getIndirectCommandCount(bufferSize, indirectCommandStride);
            if (indirectCommandCount == 0u)
            {
                commandDescriptor->release();
                throw std::runtime_error("Indirect buffers must be large enough to hold at least one native indirect command.");
            }

            auto mCommand = device->getNativeDevice()->newIndirectCommandBuffer(commandDescriptor, indirectCommandCount, 0);
            commandDescriptor->release();

            return mCommand;
        }
        MTL::Buffer *createArgumentBufferForICB(MDevice *device, MTL::IndirectCommandBuffer *icb)
        {
            uint64_t address = icb->gpuResourceID()._impl;
            MTL::Buffer *result = device->getNativeDevice()->newBuffer(sizeof(address), MTL::ResourceStorageModeShared);

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
            if ((descriptor.usage & (BufferUsage::MapRead | BufferUsage::MapWrite)) != 0u)
            {
                this->mNativeBuffer = device->getNativeDevice()->newBuffer(descriptor.size, resourceOptions);
            }
            else
            {
                this->mNativeBuffer = device->getNativeDevice()->newBuffer(descriptor.size, resourceOptions); // | MTL::ResourceHazardTrackingModeUntracked);
            }
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

    MTL::IndirectCommandBuffer *MBuffer::getOrCreateNativeICB(MTL::IndirectCommandType type)
    {
        if (!(mDescriptor.usage & GVM::RHI::BufferUsage::Indirect))
        {
            throw std::runtime_error(std::string("buffer: ") + this->getLabelName().c_str() + " is not created with indirect");
        }

        return getOrCreateICB(type);
    }

    MTL::Buffer *MBuffer::getOrCreateNativeICBArgumentBuffer(MTL::IndirectCommandType type)
    {
        if (!(mDescriptor.usage & GVM::RHI::BufferUsage::Indirect))
        {
            throw std::runtime_error(std::string("buffer: ") + this->getLabelName().c_str() + " is not created with indirect");
        }
        return getOrCreateICBArgumentBuffer(type);
    }

    void MBuffer::destroy()
    {
        if (mNativeBuffer != nullptr)
        {
            mNativeBuffer->release();
            mNativeBuffer = nullptr;
        }
        for (auto &[type, icb] : mICBs)
        {
            if (icb != nullptr)
            {
                icb->release();
            }
        }
        mICBs.clear();

        for (auto &[type, buffer] : mICBArgumentBuffers)
        {
            if (buffer != nullptr)
            {
                buffer->release();
            }
        }
        mICBArgumentBuffers.clear();
        mIsMapped = false;
        mDevice = nullptr;
    }

    MTL::IndirectCommandBuffer *MBuffer::getOrCreateICB(MTL::IndirectCommandType type)
    {
        MTL::IndirectCommandBuffer *result = nullptr;
        if (mICBs.find(type) == mICBs.end())
        {
            result = createIndirectRenderCommand(this->mDevice, type, this->mDescriptor.size);

            result->setLabel(NS::String::string((mDescriptor.label + "_ICB").c_str(), NS::UTF8StringEncoding));
            mICBs.emplace(type, result);
        }
        else
        {
            result = mICBs.at(type);
        }

        return result;
    }

    MTL::Buffer *MBuffer::getOrCreateICBArgumentBuffer(MTL::IndirectCommandType type)
    {
        MTL::Buffer *result = nullptr;
        if (mICBArgumentBuffers.find(type) == mICBArgumentBuffers.end())
        {
            MTL::IndirectCommandBuffer *icb = getOrCreateICB(type);

            result = createArgumentBufferForICB(mDevice, icb);
            mICBArgumentBuffers.emplace(type, result);
            result->setLabel((NS::String::string((mDescriptor.label + "_BindGroup").c_str(), NS::UTF8StringEncoding)));
        }
        else
        {
            result = mICBArgumentBuffers.at(type);
        }
        return result;
    }

} // namespace GVM::RHI::Metal
