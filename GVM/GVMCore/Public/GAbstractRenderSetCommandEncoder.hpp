#pragma once
#include "GRenderSetCommand.hpp"
#include "GVMCore.Defines.hpp"
namespace GVM::Core
{

    class AbstractRenderSetCommandEncoderImpl
    {


    public:
        virtual RenderEntityIndex allocEntity(const RenderSetAllocInfo &info) = 0;
        /** Updates a contiguous range of one allocated entity BufferComponent without changing entity metadata. */
        virtual void setBufferComponentData(
            RenderEntityIndex entity,
            RenderComponentHandle component,
            const void *value,
            uint64_t dataStorageSize,
            uint32_t instanceStartIndex,
            uint32_t instanceCount) = 0;
        virtual void removeEntity(RenderEntityIndex entity) = 0;


    private:
        virtual void create(GVM::RHI::Device device, RenderSet *renderSet) = 0;
    };

} // namespace GVM::Core
