#include "VKPipelineCache.hpp"

#include <cstring>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        /** "GVPC" in little-endian byte order; identifies GVM pipeline cache data. */
        constexpr uint32_t PipelineCacheMagic = 0x43505647u;
        /** Bumped whenever the header layout or the meaning of the payload changes, invalidating older files. */
        constexpr uint32_t PipelineCacheFormatVersion = 1u;
        /** GraphicsBackend::Vulkan, stored so Vulkan data is never fed to another backend. */
        constexpr uint32_t PipelineCacheBackendId = static_cast<uint32_t>(GraphicsBackend::Vulkan);

        /** Fixed-size prefix written before the driver's VkPipelineCache payload. */
        struct PipelineCacheHeader
        {
            uint32_t magic = PipelineCacheMagic;
            uint32_t formatVersion = PipelineCacheFormatVersion;
            uint32_t backend = PipelineCacheBackendId;
            uint32_t vendorID = 0u;
            uint32_t deviceID = 0u;
            uint32_t driverVersion = 0u;
            uint8_t pipelineCacheUUID[VK_UUID_SIZE] = {};
            uint64_t payloadBytes = 0u;
        };

        /** Builds the header that describes data produced on the device with properties. */
        PipelineCacheHeader makeHeader(const vk::PhysicalDeviceProperties &properties, uint64_t payloadBytes)
        {
            PipelineCacheHeader header;
            header.vendorID = properties.vendorID;
            header.deviceID = properties.deviceID;
            header.driverVersion = properties.driverVersion;
            std::memcpy(header.pipelineCacheUUID, properties.pipelineCacheUUID.data(), VK_UUID_SIZE);
            header.payloadBytes = payloadBytes;
            return header;
        }

        /** Returns the driver payload of data when its header matches properties, or an empty span otherwise. */
        eastl::span<const uint8_t> findCompatiblePayload(eastl::span<const uint8_t> data, const vk::PhysicalDeviceProperties &properties)
        {
            if (data.size() < sizeof(PipelineCacheHeader))
            {
                return {};
            }
            PipelineCacheHeader stored;
            std::memcpy(&stored, data.data(), sizeof(stored));
            const PipelineCacheHeader expected = makeHeader(properties, data.size() - sizeof(PipelineCacheHeader));
            if (stored.magic != expected.magic ||
                stored.formatVersion != expected.formatVersion ||
                stored.backend != expected.backend ||
                stored.vendorID != expected.vendorID ||
                stored.deviceID != expected.deviceID ||
                stored.driverVersion != expected.driverVersion ||
                std::memcmp(stored.pipelineCacheUUID, expected.pipelineCacheUUID, VK_UUID_SIZE) != 0 ||
                stored.payloadBytes != expected.payloadBytes)
            {
                return {};
            }
            return data.subspan(sizeof(PipelineCacheHeader));
        }
    } // namespace

    void VKPipelineCache::init(vk::Device device, const vk::PhysicalDeviceProperties &properties, eastl::span<const uint8_t> initialData)
    {
        mDevice = device;
        mProperties = properties;
        const eastl::span<const uint8_t> payload = findCompatiblePayload(initialData, properties);
        vk::PipelineCacheCreateInfo createInfo = {};
        createInfo.initialDataSize = payload.size();
        createInfo.pInitialData = payload.empty() ? nullptr : payload.data();
        mPipelineCache = device.createPipelineCacheUnique(createInfo);
        mLoadedInitialData = !payload.empty();
    }

    void VKPipelineCache::destroy()
    {
        mPipelineCache.reset();
        mDevice = nullptr;
        mLoadedInitialData = false;
    }

    vk::PipelineCache VKPipelineCache::getNativePipelineCache() const
    {
        return mPipelineCache.get();
    }

    eastl::vector<uint8_t> VKPipelineCache::serialize() const
    {
        if (!mPipelineCache)
        {
            return {};
        }
        size_t payloadBytes = 0u;
        if (vkGetPipelineCacheData(static_cast<VkDevice>(mDevice), static_cast<VkPipelineCache>(mPipelineCache.get()), &payloadBytes, nullptr) != VK_SUCCESS ||
            payloadBytes == 0u)
        {
            return {};
        }
        eastl::vector<uint8_t> data(sizeof(PipelineCacheHeader) + payloadBytes);
        if (vkGetPipelineCacheData(static_cast<VkDevice>(mDevice), static_cast<VkPipelineCache>(mPipelineCache.get()), &payloadBytes,
                data.data() + sizeof(PipelineCacheHeader)) != VK_SUCCESS)
        {
            return {};
        }
        data.resize(sizeof(PipelineCacheHeader) + payloadBytes);
        const PipelineCacheHeader header = makeHeader(mProperties, payloadBytes);
        std::memcpy(data.data(), &header, sizeof(header));
        return data;
    }

    bool VKPipelineCache::loadedInitialData() const
    {
        return mLoadedInitialData;
    }
} // namespace GVM::RHI::Vulkan
