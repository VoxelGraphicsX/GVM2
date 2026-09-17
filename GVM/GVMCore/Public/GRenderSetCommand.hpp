#pragma once
#include <EASTL/intrusive_ptr.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <GVMCore/Public/GVMCore.Defines.hpp>
#include <GVMRHI/GVMRHI.hpp>
namespace GVM::Core
{

    struct RenderSetBufferComponentAllocInfo
    {
        RenderComponentHandle bufferComponentHandle;
        eastl::string bufferName;
        const void *value = nullptr;
        uint64_t dataStorageSize;
        uint32_t instanceCount = 1;
    };

    struct RenderSetTextureComponentTextureInfo
    {
        eastl::string textureName;
        GVM::RHI::TextureFormat format;
        uint32_t width = 0;
        uint32_t height = 0;
        void const *data = nullptr;
        uint64_t dataStorageBytes;
        eastl::vector<uint64_t> mipmapOffsetBytes;
    };

    struct RenderSetTextureComponentAllocInfo
    {
        RenderComponentHandle textureComponentHandle;
        eastl::vector<RenderSetTextureComponentTextureInfo> textures;
    };

    struct RenderSetAllocInfo
    {
        uint32_t verticesCount = 0;
        uint32_t indicesCount = 0;
        uint32_t instanceCount = 1;
        eastl::vector<RenderSetBufferComponentAllocInfo> bufferInfos;
        eastl::vector<RenderSetTextureComponentAllocInfo> textureInfos;
    };


} // namespace GVM::Core
