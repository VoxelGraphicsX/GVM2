#include "MBindGroup.hpp"
#include "MBindGroupLayout.hpp"
#include "MBuffer.hpp"
#include "MBindGroupContracts.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MSampler.hpp"
#include "MTexture.hpp"
#include "MTextureView.hpp"
#include <EASTL/make_intrusive.h>
#include <stdexcept>
namespace GVM::RHI::Metal
{

    namespace
    {

        template <class T>
        void trackUsageImpl(T *encoder, const eastl::vector<eastl::intrusive_ptr<MBindGroup::ResourceUseInfo>> &mAllResourceUseInfos, const eastl::vector<MTL::Buffer *> &mNestedArgumentBuffers)
        {
            for (const auto &resourceInfo : mAllResourceUseInfos)
            {
                if (resourceInfo->resources.empty())
                {
                    continue;
                }

                encoder->useResources(resourceInfo->resources.data(), resourceInfo->resources.size(), resourceInfo->usage);
            }

            for (const auto &buffer : mNestedArgumentBuffers)
            {
                if (buffer != nullptr)
                {
                    encoder->useResource(buffer, MTL::ResourceUsageRead);
                }
            }
        }
    } // namespace

    MBindGroup::MBindGroup()
    {
    }

    MBindGroup::~MBindGroup()
    {
        if (this->mNativeBuffer != nullptr)
        {
            this->mNativeBuffer->release();
            this->mNativeBuffer = nullptr;
        }
        this->mDevice = nullptr;
        this->mDescriptor = {};
        this->mLabelName = "";

        for (auto &buffer : mNestedArgumentBuffers)
        {
            if (buffer != nullptr)
            {
                buffer->release();
            }
        }
        mNestedArgumentBuffers.clear();
        mAllResourceUseInfos.clear();
    }

    void MBindGroup::init(MDevice *device, const BindGroupDescriptor &descriptor)
    {
        this->mDevice = device;
        this->mDescriptor = descriptor;
        this->mLabelName = descriptor.label;

        auto layout = eastl::static_pointer_cast<MBindGroupLayout>(mDescriptor.layout);
        const auto &layoutDescriptor = layout->getDescriptor();
        const auto resolvedEntryIndices = Detail::resolveBindGroupEntryIndices(layoutDescriptor, descriptor.entries);

        eastl::vector<uint64_t> argumentData;
        mAllResourceUseInfos.resize(layoutDescriptor.entries.size());
        mNestedArgumentBuffers.resize(layoutDescriptor.entries.size(), nullptr);
        for (size_t layoutIndex = 0; layoutIndex < layoutDescriptor.entries.size(); ++layoutIndex)
        {
            const auto &layoutEntry = layoutDescriptor.entries[layoutIndex];
            const auto &entry = descriptor.entries[resolvedEntryIndices[layoutIndex]];
            const auto resourceUsage = layout->getResourceUsages()[layoutIndex];
            mAllResourceUseInfos[layoutIndex] = eastl::make_intrusive<ResourceUseInfo>();
            mAllResourceUseInfos[layoutIndex]->usage = resourceUsage;
            if (entry.sampler.empty() == false)
            {
                for (const auto &sampler : entry.sampler)
                {
                    if (sampler.isNull() || sampler.get() == nullptr)
                    {
                        throw std::invalid_argument("MBindGroup::init received a null sampler in a bind group entry.");
                    }
                    argumentData.emplace_back(static_cast<MSampler *>(sampler.get())->getNativeSampler()->gpuResourceID()._impl);
                }
            }
            else if (entry.textureView.empty() == false)
            {
                if (entry.textureView.size() > 1)
                {
                    eastl::vector<uint64_t> nestedArgumentData;
                    MTL::Buffer *nestedArgumentBuffer = nullptr;
                    for (const auto &textureView : entry.textureView)
                    {
                        if (textureView.isNull() || textureView.get() == nullptr)
                        {
                            throw std::invalid_argument("MBindGroup::init received a null texture view in a bind group entry.");
                        }
                        auto nativeTexture = static_cast<MTextureView *>(textureView.get())->getNativeTextureView();
                        nestedArgumentData.emplace_back(nativeTexture->gpuResourceID()._impl);
                        mAllResourceUseInfos[layoutIndex]->resources.emplace_back(nativeTexture);
                    }
                    nestedArgumentBuffer = mDevice->getNativeDevice()->newBuffer(nestedArgumentData.data(), nestedArgumentData.size() * sizeof(nestedArgumentData[0]), MTL::ResourceStorageModeShared);
                    if (nestedArgumentBuffer == nullptr)
                    {
                        throw std::runtime_error("MBindGroup::init failed to allocate a nested Metal argument buffer.");
                    }
                    nestedArgumentBuffer->setLabel(NS::String::string((descriptor.label + "__binding__" + eastl::to_string(layoutEntry.binding)).c_str(), NS::UTF8StringEncoding));
                    argumentData.emplace_back(nestedArgumentBuffer->gpuAddress());
                    mNestedArgumentBuffers[layoutIndex] = nestedArgumentBuffer;
                }
                else
                {
                    for (const auto &textureView : entry.textureView)
                    {
                        if (textureView.isNull() || textureView.get() == nullptr)
                        {
                            throw std::invalid_argument("MBindGroup::init received a null texture view in a bind group entry.");
                        }
                        auto nativeTexture = static_cast<MTextureView *>(textureView.get())->getNativeTextureView();
                        argumentData.emplace_back(nativeTexture->gpuResourceID()._impl);
                        mAllResourceUseInfos[layoutIndex]->resources.emplace_back(nativeTexture);
                    }
                }
            }
            else if (entry.buffer.empty() == false)
            {
                for (const auto &buffer : entry.buffer)
                {
                    if (buffer.buffer.isNull() || buffer.buffer.get() == nullptr)
                    {
                        throw std::invalid_argument("MBindGroup::init received a null buffer in a bind group entry.");
                    }
                    auto nativeBuffer = static_cast<MBuffer *>(buffer.buffer.get())->getNativeBuffer();
                    argumentData.emplace_back(nativeBuffer->gpuAddress());
                    mAllResourceUseInfos[layoutIndex]->resources.emplace_back(nativeBuffer);
                }
            }
            else
            {
                throw std::runtime_error("Unsupported bind group binding type");
            }
        }
        this->mNativeBuffer = mDevice->getNativeDevice()->newBuffer(argumentData.data(), argumentData.size() * sizeof(argumentData[0]), MTL::ResourceStorageModeShared);
        if (this->mNativeBuffer == nullptr)
        {
            throw std::runtime_error("MBindGroup::init failed to allocate the root Metal argument buffer.");
        }
        this->mNativeBuffer->setLabel(NS::String::string(descriptor.label.c_str(), NS::UTF8StringEncoding));
    }

    MTL::Buffer *MBindGroup::getNativeBuffer() const
    {
        return this->mNativeBuffer;
    }

    const BindGroupDescriptor &MBindGroup::getDescriptor() const
    {
        return this->mDescriptor;
    }


    void MBindGroup::trackUsage(MTL::RenderCommandEncoder *encoder)
    {
        trackUsageImpl(encoder, mAllResourceUseInfos, mNestedArgumentBuffers);
    }

    void MBindGroup::trackUsage(MTL::ComputeCommandEncoder *encoder)
    {
        trackUsageImpl(encoder, mAllResourceUseInfos, mNestedArgumentBuffers);
    }


} // namespace GVM::RHI::Metal
