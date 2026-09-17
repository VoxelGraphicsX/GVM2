#include "MPipelineLayout.hpp"

namespace GVM::RHI::Metal {

MPipelineLayout::MPipelineLayout () {}

void MPipelineLayout::init(MDevice *device, const PipelineLayoutDescriptor &descriptor)
{
    this->mDevice = device;
    this->mDescriptor = descriptor;
}

} // namespace GVM::RHI::Metal
