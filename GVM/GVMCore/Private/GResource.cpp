#include "GResource.hpp"

namespace GVM::Core
{

IBindGroup::IBindGroup(GVM::RHI::Device device) : mDevice(device)
{
}

void IBindGroup::create()
{
    createBindGroupFn();
}

ColorAttachment::ColorAttachment(const GVM::RHI::TextureView &textureView)
{
    this->view = textureView;
}

ColorAttachment &ColorAttachment::operator=(const GVM::RHI::TextureView &textureView)
{
    this->view = textureView;
    return *this;
}

DepthStencilAttachment::DepthStencilAttachment(const GVM::RHI::TextureView &textureView)
{
    this->view = textureView;
}

DepthStencilAttachment &DepthStencilAttachment::operator=(const GVM::RHI::TextureView &textureView)
{
    this->view = textureView;
    return *this;
}

PixelLocalColorAttachment::PixelLocalColorAttachment(const GVM::RHI::TextureView &textureView)
{
    this->view = textureView;
    this->pixelLocal = true;
}

PixelLocalColorAttachment &PixelLocalColorAttachment::operator=(const GVM::RHI::TextureView &textureView)
{
    this->view = textureView;
    this->pixelLocal = true;
    return *this;
}

PixelLocalDepthAttachment::PixelLocalDepthAttachment(const GVM::RHI::TextureView &textureView)
{
    this->view = textureView;
    this->pixelLocal = true;
}

PixelLocalDepthAttachment &PixelLocalDepthAttachment::operator=(const GVM::RHI::TextureView &textureView)
{
    this->view = textureView;
    this->pixelLocal = true;
    return *this;
}

} // namespace GVM::Core
