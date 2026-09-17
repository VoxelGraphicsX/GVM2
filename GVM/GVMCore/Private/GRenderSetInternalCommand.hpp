#pragma once
#include <EASTL/RefCountedObject.h>
#include <EASTL/intrusive_ptr.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <GVMCore/Public/GRenderSetCommand.hpp>
#include <GVMCore/Public/GVMCore.Defines.hpp>
namespace GVM::Core
{

    /*  enum class RenderSetCommandType
     {
         Alloc,
         SetBuffer,
         Remove
     };

     struct RenderSetRemoveCommand
     {
         RenderEntityIndex entity;
     };
     struct RenderSetBufferCopyCommand
     {
         RenderComponentHandle bufferComponentHandle;
         RenderEntityIndex entity;
         eastl::string bufferName;
         GVM::RHI::BufferRange tempDataRange;
         uint32_t instanceStartIndex = 0;
         uint32_t instanceCount = 1;
     };


     struct RenderSetTextureComponentTextureCopyCommand
     {
         eastl::string textureName;
         GVM::RHI::TextureFormat format;
         uint32_t width = 0;
         uint32_t height = 0;
         GVM::RHI::BufferRange tempDataRange;
         eastl::vector<uint64_t> mipmapOffsetBytes;
     };

     struct RenderSetTextureComponentCopyCommand
     {
         RenderComponentHandle textureComponentHandle;
         eastl::vector<RenderSetTextureComponentTextureCopyCommand> textures;
     };

     struct RenderSetAllocCommand
     {
         RenderEntityIndex entity;
         uint32_t verticesCount;
         uint32_t indicesCount;
         uint32_t instanceCount = 1;
         RenderComponentIndex verticeIndex;
         RenderComponentIndex indiceIndex;
         eastl::vector<RenderSetBufferCopyCommand> bufferCMDs;
         eastl::vector<RenderSetTextureComponentCopyCommand> textureCMDs;
     };

     struct RenderSetCommand
     {
         friend class RenderSet;
         RenderSetCommandType type;
         RenderSetAllocCommand alloc;
         RenderSetBufferCopyCommand setBuffer;
         RenderSetTextureComponentTextureCopyCommand setTexture;
         RenderSetRemoveCommand remove;
     };

     struct RenderSetUserCommand
     {
         RenderSetCommandType type;
         void *tempData = nullptr;
         uint64_t tempDataBytes = 0;
         RenderEntityIndex entity;
         RenderSetAllocInfo alloc;
         RenderSetBufferComponentAllocInfo setBuffer;
         RenderSetTextureComponentTextureInfo setTexture;
         RenderSetRemoveCommand remove;
     };

     struct RenderSetGPUCopyCommand
     {
         void *CPUTempData = nullptr;
         void *currentTempData = nullptr;
         std::atomic<uint64_t> usedBytes = 0;
         uint64_t alignedBytes = 1;
         GVM::RHI::BufferCopyRegion copyRegion;
         void create(void *data, uint64_t alignedBytes);
         void *useData(uint64_t bytes);
         uint64_t getCurrentOffsetBytes() const;
     }; */
    struct RenderSetTextureComponentTextureCopyCommand
    {
        eastl::string textureName;
        GVM::RHI::TextureFormat format;
        uint32_t width = 0;
        uint32_t height = 0;
        // GVM::RHI::BufferRange tempDataRange;
        RHI::Buffer stagingBuffer;
        uint32_t stagingBufferOffset = 0;
        uint32_t copyByteSize = 0;
        eastl::vector<uint64_t> mipmapOffsetBytes;
    };

    struct RenderSetTextureComponentCopyCommand
    {
        RenderComponentHandle textureComponentHandle;
        eastl::vector<RenderSetTextureComponentTextureCopyCommand> textures;
    };
    struct RenderComponentIndexCopyInfo
    {
        uint32_t GPUBufferDstOffset = 0;
        uint32_t CPUDataSrcOffset = 0;
        uint32_t size = 0;
        const void *data = nullptr;
    };

} // namespace GVM::Core
