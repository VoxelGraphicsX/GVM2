#include "GRenderTextureComponent.hpp"
#include "GGPUVectorFactory.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <xGEFoundation/xMath.hpp>
namespace GVM::Core
{
    namespace
    {
        /** Describes one compressed or uncompressed texture block for upload byte validation. */
        struct TextureBlockInfo
        {
            uint64_t width = 1u;
            uint64_t height = 1u;
            uint64_t bytes = 1u;
        };

        /** Returns the block footprint used to validate TextureComponent mip upload payloads. */
        TextureBlockInfo getTextureBlockInfo(GVM::RHI::TextureFormat format)
        {
            TextureBlockInfo blockInfo = {};
            switch (format)
            {
            case GVM::RHI::TextureFormat::BC1RGBAUnorm:
            case GVM::RHI::TextureFormat::BC1RGBAUnormSrgb:
            case GVM::RHI::TextureFormat::BC4RUnorm:
            case GVM::RHI::TextureFormat::BC4RSnorm:
            case GVM::RHI::TextureFormat::ETC2RGB8Unorm:
            case GVM::RHI::TextureFormat::ETC2RGB8UnormSrgb:
            case GVM::RHI::TextureFormat::ETC2RGB8A1Unorm:
            case GVM::RHI::TextureFormat::ETC2RGB8A1UnormSrgb:
                blockInfo = {.width = 4u, .height = 4u, .bytes = 8u};
                break;

            case GVM::RHI::TextureFormat::BC2RGBAUnorm:
            case GVM::RHI::TextureFormat::BC2RGBAUnormSrgb:
            case GVM::RHI::TextureFormat::BC3RGBAUnorm:
            case GVM::RHI::TextureFormat::BC3RGBAUnormSrgb:
            case GVM::RHI::TextureFormat::BC5RGUnorm:
            case GVM::RHI::TextureFormat::BC5RGSnorm:
            case GVM::RHI::TextureFormat::BC6HRGBUfloat:
            case GVM::RHI::TextureFormat::BC6HRGBFloat:
            case GVM::RHI::TextureFormat::BC7RGBAUnorm:
            case GVM::RHI::TextureFormat::BC7RGBAUnormSrgb:
            case GVM::RHI::TextureFormat::EACR11Unorm:
            case GVM::RHI::TextureFormat::EACR11Snorm:
            case GVM::RHI::TextureFormat::EACRG11Unorm:
            case GVM::RHI::TextureFormat::EACRG11Snorm:
            case GVM::RHI::TextureFormat::ASTC4x4Unorm:
            case GVM::RHI::TextureFormat::ASTC4x4UnormSrgb:
                blockInfo = {.width = 4u, .height = 4u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC5x4Unorm:
            case GVM::RHI::TextureFormat::ASTC5x4UnormSrgb:
                blockInfo = {.width = 5u, .height = 4u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC5x5Unorm:
            case GVM::RHI::TextureFormat::ASTC5x5UnormSrgb:
                blockInfo = {.width = 5u, .height = 5u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC6x5Unorm:
            case GVM::RHI::TextureFormat::ASTC6x5UnormSrgb:
                blockInfo = {.width = 6u, .height = 5u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC6x6Unorm:
            case GVM::RHI::TextureFormat::ASTC6x6UnormSrgb:
                blockInfo = {.width = 6u, .height = 6u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC8x5Unorm:
            case GVM::RHI::TextureFormat::ASTC8x5UnormSrgb:
                blockInfo = {.width = 8u, .height = 5u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC8x6Unorm:
            case GVM::RHI::TextureFormat::ASTC8x6UnormSrgb:
                blockInfo = {.width = 8u, .height = 6u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC8x8Unorm:
            case GVM::RHI::TextureFormat::ASTC8x8UnormSrgb:
                blockInfo = {.width = 8u, .height = 8u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC10x5Unorm:
            case GVM::RHI::TextureFormat::ASTC10x5UnormSrgb:
                blockInfo = {.width = 10u, .height = 5u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC10x6Unorm:
            case GVM::RHI::TextureFormat::ASTC10x6UnormSrgb:
                blockInfo = {.width = 10u, .height = 6u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC10x8Unorm:
            case GVM::RHI::TextureFormat::ASTC10x8UnormSrgb:
                blockInfo = {.width = 10u, .height = 8u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC10x10Unorm:
            case GVM::RHI::TextureFormat::ASTC10x10UnormSrgb:
                blockInfo = {.width = 10u, .height = 10u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC12x10Unorm:
            case GVM::RHI::TextureFormat::ASTC12x10UnormSrgb:
                blockInfo = {.width = 12u, .height = 10u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::ASTC12x12Unorm:
            case GVM::RHI::TextureFormat::ASTC12x12UnormSrgb:
                blockInfo = {.width = 12u, .height = 12u, .bytes = 16u};
                break;

            case GVM::RHI::TextureFormat::R8Unorm:
            case GVM::RHI::TextureFormat::R8Snorm:
            case GVM::RHI::TextureFormat::R8Uint:
            case GVM::RHI::TextureFormat::R8Sint:
                blockInfo = {.width = 1u, .height = 1u, .bytes = 1u};
                break;

            case GVM::RHI::TextureFormat::R16Uint:
            case GVM::RHI::TextureFormat::R16Sint:
            case GVM::RHI::TextureFormat::R16Float:
            case GVM::RHI::TextureFormat::Depth16Unorm:
            case GVM::RHI::TextureFormat::RG8Unorm:
            case GVM::RHI::TextureFormat::RG8Snorm:
            case GVM::RHI::TextureFormat::RG8Uint:
            case GVM::RHI::TextureFormat::RG8Sint:
                blockInfo = {.width = 1u, .height = 1u, .bytes = 2u};
                break;

            case GVM::RHI::TextureFormat::R32Float:
            case GVM::RHI::TextureFormat::Depth32Float:
            case GVM::RHI::TextureFormat::R32Uint:
            case GVM::RHI::TextureFormat::R32Sint:
            case GVM::RHI::TextureFormat::RG16Uint:
            case GVM::RHI::TextureFormat::RG16Sint:
            case GVM::RHI::TextureFormat::RG16Float:
            case GVM::RHI::TextureFormat::RGBA8Unorm:
            case GVM::RHI::TextureFormat::RGBA8UnormSrgb:
            case GVM::RHI::TextureFormat::RGBA8Snorm:
            case GVM::RHI::TextureFormat::RGBA8Uint:
            case GVM::RHI::TextureFormat::RGBA8Sint:
            case GVM::RHI::TextureFormat::BGRA8Unorm:
            case GVM::RHI::TextureFormat::BGRA8UnormSrgb:
            case GVM::RHI::TextureFormat::RGB10A2Uint:
            case GVM::RHI::TextureFormat::RGB10A2Unorm:
            case GVM::RHI::TextureFormat::RG11B10Ufloat:
            case GVM::RHI::TextureFormat::RGB9E5Ufloat:
                blockInfo = {.width = 1u, .height = 1u, .bytes = 4u};
                break;

            case GVM::RHI::TextureFormat::RG32Float:
            case GVM::RHI::TextureFormat::RG32Uint:
            case GVM::RHI::TextureFormat::RG32Sint:
            case GVM::RHI::TextureFormat::RGBA16Uint:
            case GVM::RHI::TextureFormat::RGBA16Sint:
            case GVM::RHI::TextureFormat::RGBA16Float:
                blockInfo = {.width = 1u, .height = 1u, .bytes = 8u};
                break;

            case GVM::RHI::TextureFormat::RGBA32Float:
            case GVM::RHI::TextureFormat::RGBA32Uint:
            case GVM::RHI::TextureFormat::RGBA32Sint:
                blockInfo = {.width = 1u, .height = 1u, .bytes = 16u};
                break;

            default:
                throw std::runtime_error("TextureComponent::performTextureCopy encountered an unsupported texture format.");
            }
            return blockInfo;
        }

        /** Computes the byte count required by one TextureComponent mip upload. */
        uint64_t computeMipUploadByteSize(GVM::RHI::TextureFormat format, uint32_t width, uint32_t height)
        {
            const auto blockInfo = getTextureBlockInfo(format);
            const uint64_t blocksX = (static_cast<uint64_t>(width) + blockInfo.width - 1u) / blockInfo.width;
            const uint64_t blocksY = (static_cast<uint64_t>(height) + blockInfo.height - 1u) / blockInfo.height;
            return blocksX * blocksY * blockInfo.bytes;
        }

        /** Validates that a queued TextureComponent copy command has a complete in-bounds staging payload. */
        void validateTextureCopyCommand(const RenderSetTextureComponentTextureCopyCommand &cmd)
        {
            if (cmd.stagingBuffer.isNull())
            {
                throw std::invalid_argument("TextureComponent::performTextureCopy requires a valid staging buffer.");
            }
            if (cmd.width == 0u || cmd.height == 0u)
            {
                throw std::invalid_argument("TextureComponent::performTextureCopy requires non-zero texture dimensions.");
            }
            if (cmd.mipmapOffsetBytes.empty())
            {
                throw std::invalid_argument("TextureComponent::performTextureCopy requires at least one mip offset.");
            }

            uint64_t previousOffset = 0u;
            for (size_t level = 0; level < cmd.mipmapOffsetBytes.size(); ++level)
            {
                const uint64_t mipOffset = cmd.mipmapOffsetBytes[level];
                const uint32_t mipWidth = std::max(1u, cmd.width >> level);
                const uint32_t mipHeight = std::max(1u, cmd.height >> level);
                const uint64_t mipByteSize = computeMipUploadByteSize(cmd.format, mipWidth, mipHeight);
                if (level > 0u && mipOffset < previousOffset)
                {
                    throw std::invalid_argument("TextureComponent::performTextureCopy requires mip offsets in ascending order.");
                }
                if (mipOffset + mipByteSize > cmd.copyByteSize)
                {
                    throw std::invalid_argument("TextureComponent::performTextureCopy received mip offsets that exceed the recorded staging payload.");
                }
                previousOffset = mipOffset;
            }
        }

        /** Returns the bindless sampled texture descriptor count, including the extra fallback descriptor. */
        uint32_t getTextureComponentBindlessDescriptorCountOrThrow(uint64_t maxTextureCount)
        {
            if (maxTextureCount == 0u)
            {
                throw std::invalid_argument("TextureComponent requires maxTextureCount > 0 so RenderSet can reserve bindless texture descriptors.");
            }
            if (maxTextureCount >= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
            {
                throw std::out_of_range("TextureComponent maxTextureCount is too large for a uint32_t bindless descriptor count.");
            }
            return static_cast<uint32_t>(maxTextureCount + 1u);
        }

        /** Returns the byte count occupied by one entity's fixed texture slot index page. */
        uint64_t getPerEntityTextureIndexBytes()
        {
            return sizeof(RenderComponentIndex) * static_cast<uint64_t>(RenderTextureMaxTextureCountPerEntity);
        }
    } // namespace

    void TextureComponent::alloc(RenderEntityIndex entity, const RenderSetTextureComponentAllocInfo &allocInfo)
    {
        if (allocInfo.textures.size() > static_cast<size_t>(RenderTextureMaxTextureCountPerEntity))
        {
            throw std::invalid_argument("TextureComponent supports at most 8 texture slots per entity.");
        }

        eastl::vector<eastl::string> textureNames(allocInfo.textures.size());
        std::lock_guard lock(mMtx);
        for (size_t textureIndex = 0; textureIndex < allocInfo.textures.size(); textureIndex++)
        {
            const auto &textureInfo = allocInfo.textures[textureIndex];
            if (textureInfo.textureName.empty())
            {
                throw std::runtime_error("Texture name can't be empty");
            }

            const auto existingInfoIt = mTextureUseInfos.find(textureInfo.textureName);
            RenderComponentIndex componentIndex = RenderComponentNullIndex;
            if (existingInfoIt != mTextureUseInfos.end())
            {
                existingInfoIt->second.useCount++;
                componentIndex = existingInfoIt->second.index;
            }
            else
            {
                componentIndex = mTextureLibFreeList.allocIndex();
                if (componentIndex >= mTextureLib.size())
                {
                    mTextureLibFreeList.collectIndex(componentIndex);
                    throw std::out_of_range("TextureComponent exhausted the configured bindless texture descriptor capacity.");
                }

                GVM::RHI::Texture texture = createTexture(textureInfo);
                GVM::RHI::TextureView view = texture->createView();
                mTextureLib[componentIndex] = texture;
                mTextureViewLib[componentIndex] = view;
                mTextureUseInfos.emplace(textureInfo.textureName, TextureUseInfo{.index = componentIndex, .useCount = 1});
                ++mTextureLibGenerationCounter;
            }

            mComponentIndex.write(
                entity * static_cast<uint64_t>(RenderTextureMaxTextureCountPerEntity) + textureIndex,
                componentIndex);
            textureNames[textureIndex] = textureInfo.textureName;
        }

        mEntities.write(entity, (ComponentUseInfo){.textureNames = textureNames});
        if (mComponentIndexBuffer != nullptr)
        {
            mComponentIndexBuffer->reserveGPUBytes((entity + 1u) * getPerEntityTextureIndexBytes());
        }
    }

    RenderComponentIndexCopyInfo TextureComponent::getComponentIndexAllocInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator)
    {
        RenderComponentIndexCopyInfo info{};
        if (!check(entity))
        {
            return info;
        }

        RenderComponentIndex componentIndices[RenderTextureMaxTextureCountPerEntity] = {};
        std::fill_n(componentIndices, RenderTextureMaxTextureCountPerEntity, RenderComponentNullIndex);
        mComponentIndex.copyRangeTo(
            entity * static_cast<uint64_t>(RenderTextureMaxTextureCountPerEntity),
            componentIndices,
            RenderTextureMaxTextureCountPerEntity);

        info.GPUBufferDstOffset = static_cast<uint32_t>(entity * getPerEntityTextureIndexBytes());
        info.size = static_cast<uint32_t>(getPerEntityTextureIndexBytes());
        info.CPUDataSrcOffset = allocator.appendRaw(componentIndices, info.size, alignof(RenderComponentIndex));
        return info;
    }

    RenderComponentIndexCopyInfo TextureComponent::getComponentIndexRemoveInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator)
    {
        RenderComponentIndexCopyInfo info{};
        if (!mEntities.contains(entity))
        {
            return info;
        }

        RenderComponentIndex componentIndices[RenderTextureMaxTextureCountPerEntity] = {};
        std::fill_n(componentIndices, RenderTextureMaxTextureCountPerEntity, RenderComponentNullIndex);
        info.GPUBufferDstOffset = static_cast<uint32_t>(entity * getPerEntityTextureIndexBytes());
        info.size = static_cast<uint32_t>(getPerEntityTextureIndexBytes());
        info.CPUDataSrcOffset = allocator.appendRaw(componentIndices, info.size, alignof(RenderComponentIndex));
        return info;
    }

    uint64_t TextureComponent::getPerEntityComponentIndexStorageBytes() const
    {
        return getPerEntityTextureIndexBytes();
    }

    GVM::RHI::Texture TextureComponent::createTexture(const RenderSetTextureComponentTextureInfo &info)
    {
        GVM::RHI::TextureDescriptor textureDescriptor = {};
        textureDescriptor.label = info.textureName;
        textureDescriptor.arrayLayerCount = 1u;
        textureDescriptor.mipLevelCount = static_cast<uint32_t>(info.mipmapOffsetBytes.size());
        textureDescriptor.size.width = info.width;
        textureDescriptor.size.height = info.height;
        textureDescriptor.size.depth = 1u;
        textureDescriptor.dimension = GVM::RHI::TextureDimension::e2D;
        textureDescriptor.format = info.format;
        textureDescriptor.usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst;

        return mDevice->createTexture(textureDescriptor);
    }

    void TextureComponent::performTextureCopy(const RenderSetTextureComponentTextureCopyCommand &cmd)
    {
        validateTextureCopyCommand(cmd);

        std::lock_guard lock(mMtx);
        auto &textureInfo = mTextureUseInfos.at(cmd.textureName);
        mDevice->getMainQueue()->uploadTexture(
            mTextureLib.at(textureInfo.index),
            GVM::RHI::BufferRange(cmd.stagingBuffer, cmd.stagingBufferOffset, cmd.copyByteSize),
            cmd.mipmapOffsetBytes);
    }

    void TextureComponent::resizeBuffer()
    {
        if (mComponentIndexBuffer != nullptr)
        {
            mComponentIndexBuffer->commitStorage();
        }
    }

    void TextureComponent::create(GVM::RHI::Device device, const TextureComponentCreateInfo &info)
    {
        (void)getTextureComponentBindlessDescriptorCountOrThrow(info.maxTextureCount);
        mDevice = device;
        mTextureComponentName = info.textureComponentName;
        mMaxTextureCount = info.maxTextureCount;

        GVM::RHI::TextureDescriptor textureDescriptor = {};
        textureDescriptor.label = info.textureComponentName + "_EmptyTexture";
        textureDescriptor.arrayLayerCount = 1u;
        textureDescriptor.mipLevelCount = 1u;
        textureDescriptor.size.width = 1u;
        textureDescriptor.size.height = 1u;
        textureDescriptor.size.depth = 1u;
        textureDescriptor.dimension = GVM::RHI::TextureDimension::e2D;
        textureDescriptor.format = GVM::RHI::TextureFormat::RGBA8Unorm;
        textureDescriptor.usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst;
        mEmptyTexture = mDevice->createTexture(textureDescriptor);
        mEmptyTextureView = mEmptyTexture->createView();

        mTextureLib.resize(static_cast<size_t>(mMaxTextureCount), mEmptyTexture);
        mTextureViewLib.resize(static_cast<size_t>(mMaxTextureCount), mEmptyTextureView);

        const uint64_t indexGrowthBytes = getPerEntityTextureIndexBytes() * info.dataElementIncreamentCount;
        mComponentIndexBuffer = createGPUVectorStorage<uint8_t>(
            device,
            info.textureComponentName + "_Index",
            GVM::RHI::BufferUsage::Storage,
            indexGrowthBytes,
            indexGrowthBytes);
        mComponentIndexBuffer->reserveGPUBytes(indexGrowthBytes);
        mComponentIndexBuffer->commitStorage();

        mComponentIndex.create(
            {.dataName = info.textureComponentName + "_IndexData",
             .dataElementStorageSize = sizeof(RenderComponentIndex),
             .dataElementIncreamentCount = info.dataElementIncreamentCount * static_cast<uint64_t>(RenderTextureMaxTextureCountPerEntity)},
            RenderComponentNullIndex);

        mEntities.create({.dataName = info.textureComponentName + "_mEntities", .dataElementStorageSize = sizeof(ComponentUseInfo), .dataElementIncreamentCount = info.dataElementIncreamentCount}, {});
        ++mTextureLibGenerationCounter;
    }

    GVM::RHI::Buffer TextureComponent::getComponentIndexBuffer() const
    {
        return mComponentIndexBuffer != nullptr ? mComponentIndexBuffer->gpuBuffer() : GVM::RHI::Buffer{};
    }

    uint64_t TextureComponent::getResourceGenerationCounter() const
    {
        return mTextureLibGenerationCounter;
    }

    uint64_t TextureComponent::getComponentIndexBufferGenerationCounter() const
    {
        return mComponentIndexBuffer != nullptr ? mComponentIndexBuffer->generation() : 0u;
    }

    void TextureComponent::appendBindGroupLayoutEntries(eastl::vector<GVM::RHI::BindGroupLayoutEntry> &entries, uint32_t &bindingIndex) const
    {
        GVM::RHI::BindGroupLayoutEntry componentIndexEntry = {};
        componentIndexEntry.binding = bindingIndex++;
        componentIndexEntry.visibility = GVM::RHI::ShaderStage::Compute | GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
        componentIndexEntry.buffer.type = GVM::RHI::BufferBindingType::Storage;
        componentIndexEntry.buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
        entries.push_back(componentIndexEntry);

        GVM::RHI::BindGroupLayoutEntry resourceEntry = getBindGroupLayoutEntry();
        resourceEntry.binding = bindingIndex++;
        resourceEntry.visibility = GVM::RHI::ShaderStage::Compute | GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
        entries.push_back(resourceEntry);
    }

    void TextureComponent::appendBindGroupEntries(eastl::vector<GVM::RHI::BindGroupEntry> &entries, uint32_t &bindingIndex) const
    {
        GVM::RHI::BindGroupEntry componentIndexEntry = {};
        componentIndexEntry.binding = bindingIndex++;
        componentIndexEntry.buffer = GVM::RHI::BufferRange(getComponentIndexBuffer());
        entries.push_back(componentIndexEntry);

        GVM::RHI::BindGroupEntry resourceEntry = getResourceBindGroupEntry();
        resourceEntry.binding = bindingIndex++;
        entries.push_back(resourceEntry);
    }

    RenderComponentBindingGeneration TextureComponent::getBindGroupGeneration() const
    {
        return {
            .resource = getResourceGenerationCounter(),
            .componentIndex = getComponentIndexBufferGenerationCounter(),
        };
    }

    GVM::RHI::BindGroupLayoutEntry TextureComponent::getBindGroupLayoutEntry() const
    {
        GVM::RHI::BindGroupLayoutEntry entry = {};
        entry.visibility = GVM::RHI::ShaderStage::Compute | GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
        entry.texture.sampleType = GVM::RHI::TextureSampleType::Float;
        entry.texture.viewDimension = GVM::RHI::TextureViewDimension::e2D;
        entry.maxCount = getTextureComponentBindlessDescriptorCountOrThrow(mMaxTextureCount);

        return entry;
    }

    GVM::RHI::BindGroupEntry TextureComponent::getResourceBindGroupEntry() const
    {
        GVM::RHI::BindGroupEntry entry = {};
        eastl::vector<GVM::RHI::TextureView> textureViews = mTextureViewLib;
        textureViews.push_back(mEmptyTextureView);
        entry.textureView = textureViews;

        return entry;
    }

    uint64_t TextureComponent::getResourceElementCountForRenderSetAccessBounds() const
    {
        return getTextureComponentBindlessDescriptorCountOrThrow(mMaxTextureCount);
    }

    uint64_t TextureComponent::getComponentIndexElementCountForRenderSetAccessBounds() const
    {
        return mComponentIndexBuffer != nullptr ? (mComponentIndexBuffer->gpuCapacityBytes() / sizeof(RenderComponentIndex)) : 0u;
    }

    bool TextureComponent::check(const eastl::string &textureName) const
    {
        std::lock_guard lock(mMtx);
        if (textureName.empty())
        {
            return false;
        }
        auto res = mTextureUseInfos.find(textureName);
        return res != mTextureUseInfos.end();
    }

    bool TextureComponent::check(RenderEntityIndex entity) const
    {
        std::lock_guard lock(mMtx);
        return mEntities.contains(entity) && !mEntities.read(entity).textureNames.empty();
    }

    void TextureComponent::update()
    {
        resizeBuffer();
    }

    void TextureComponent::remove(RenderEntityIndex entity)
    {
        std::lock_guard lock(mMtx);
        if (!mEntities.contains(entity))
        {
            return;
        }

        auto componentInfo = mEntities.read(entity);
        for (size_t textureIndex = 0; textureIndex < componentInfo.textureNames.size(); textureIndex++)
        {
            const auto &textureName = componentInfo.textureNames[textureIndex];
            auto textureUseInfoIt = mTextureUseInfos.find(textureName);
            if (textureUseInfoIt == mTextureUseInfos.end())
            {
                continue;
            }

            auto &textureUseInfo = textureUseInfoIt->second;
            textureUseInfo.useCount--;
            if (textureUseInfo.useCount == 0)
            {
                if (textureUseInfo.index < mTextureLib.size() && !mTextureLib[textureUseInfo.index].isNull() && mTextureLib[textureUseInfo.index] != mEmptyTexture)
                {
                    mDevice->freeTexture(mTextureLib[textureUseInfo.index]);
                }
                if (textureUseInfo.index < mTextureLib.size())
                {
                    mTextureLib[textureUseInfo.index] = mEmptyTexture;
                    mTextureViewLib[textureUseInfo.index] = mEmptyTextureView;
                }
                mTextureLibFreeList.collectIndex(textureUseInfo.index);
                mTextureUseInfos.erase(textureUseInfoIt);
                ++mTextureLibGenerationCounter;
            }
            mComponentIndex.write(
                entity * static_cast<uint64_t>(RenderTextureMaxTextureCountPerEntity) + textureIndex,
                RenderComponentNullIndex);
        }

        mEntities.write(entity, {});
    }

    void TextureComponent::destroy()
    {
        for (auto &texture : mTextureLib)
        {
            if (!texture.isNull() && texture != mEmptyTexture)
            {
                mDevice->freeTexture(texture);
            }
        }
        if (!mEmptyTexture.isNull())
        {
            mDevice->freeTexture(mEmptyTexture);
        }
        if (mComponentIndexBuffer != nullptr)
        {
            mComponentIndexBuffer->destroy();
            mComponentIndexBuffer.reset();
        }
        mComponentIndex.clear();
        mEntities.clear();
        mTextureUseInfos.clear();
        mTextureLib.clear();
        mTextureViewLib.clear();
        mEmptyTexture.reset();
        mEmptyTextureView.reset();
    }

} // namespace GVM::Core
