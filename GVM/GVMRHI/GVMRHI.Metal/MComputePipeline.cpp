#include "MComputePipeline.hpp"
#include "MDevice.hpp"
#include "MShaderModule.hpp"
#include <string>
namespace GVM::RHI::Metal
{

    MComputePipeline::MComputePipeline() {}

    MComputePipeline::~MComputePipeline()
    {
        if (mNativePipelineState != nullptr)
        {
            mNativePipelineState->release();
            mNativePipelineState = nullptr;
        }
    }

    void MComputePipeline::init(MDevice *device, const ComputePipelineDescriptor &descriptor)
    {
        this->mDevice = device;
        this->mDescriptor = descriptor;

        auto computeLibrary = eastl::static_pointer_cast<MShaderModule>(descriptor.compute.module)->makeLibrary(descriptor.compute.entryPoint);
        NS::Error *error = nullptr;
        this->mNativePipelineState = device->getNativeDevice()->newComputePipelineState(computeLibrary, &error);

        if (error)
        {
            const std::string errorDescription = error->localizedDescription()->utf8String();
            throw std::runtime_error("Failed to create compute pipeline state " + errorDescription);
        }
        mLocalThreadGroupSize = MTL::Size(descriptor.compute.workgroupX, descriptor.compute.workgroupY, descriptor.compute.workgroupZ);
    }

    MTL::ComputePipelineState *MComputePipeline::getNativePipelineState() const
    {
        return this->mNativePipelineState;
    }

    MTL::Size MComputePipeline::getLocalThreadGroupSize() const
    {
        return this->mLocalThreadGroupSize;
    }

} // namespace GVM::RHI::Metal
