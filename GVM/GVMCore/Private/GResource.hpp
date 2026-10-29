#pragma once
#include <EASTL/functional.h>
#include <GVMRHI/GVMRHI.hpp>
namespace GVM::Core
{

class IBindGroup
{
protected:
    GVM::RHI::Device mDevice;
    GVM::RHI::BindGroupLayout bindGroupLayout;
    GVM::RHI::BindGroup bindGroup;
    eastl::function<void()> createBindGroupFn;
    explicit IBindGroup(GVM::RHI::Device device);
    virtual ~IBindGroup() = default;
    void create();

public:
};

struct ColorAttachment : public GVM::RHI::RenderPassColorAttachment
{
    ColorAttachment() = default;
    ColorAttachment(const GVM::RHI::TextureView &textureView);
    ColorAttachment &operator=(const GVM::RHI::TextureView &textureView);
};

struct DepthStencilAttachment : public GVM::RHI::RenderPassDepthStencilAttachment
{
    DepthStencilAttachment() = default;
    DepthStencilAttachment(const GVM::RHI::TextureView &textureView);
    DepthStencilAttachment &operator=(const GVM::RHI::TextureView &textureView);
};

enum class PixelLocalAccess
{
    ReadOnly = 0x00000000,
    WriteOnly = 0x00000001,
    ReadWrite = 0x00000002,
    Force32 = 0x7FFFFFFF
};

enum class PixelLocalStorage
{
    Transient = 0x00000000,
    Persistent = 0x00000001,
    Force32 = 0x7FFFFFFF
};

enum class PixelLocalLoad
{
    DontCare = 0x00000000,
    Clear = 0x00000001,
    Load = 0x00000002,
    Force32 = 0x7FFFFFFF
};

enum class PixelLocalStore
{
    Discard = 0x00000000,
    Store = 0x00000001,
    Force32 = 0x7FFFFFFF
};

struct PixelLocalColorAttachment : public ColorAttachment
{
    PixelLocalColorAttachment()
    {
        pixelLocal = true;
    }
    PixelLocalColorAttachment(const GVM::RHI::TextureView &textureView);
    PixelLocalColorAttachment &operator=(const GVM::RHI::TextureView &textureView);
};

struct PixelLocalDepthAttachment : public DepthStencilAttachment
{
    PixelLocalDepthAttachment()
    {
        pixelLocal = true;
    }
    PixelLocalDepthAttachment(const GVM::RHI::TextureView &textureView);
    PixelLocalDepthAttachment &operator=(const GVM::RHI::TextureView &textureView);
};

} // namespace GVM::Core
