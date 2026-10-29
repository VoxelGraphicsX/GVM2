#ifndef UGLC_TEST_SHADER_STATIC_VARIANT_RENDER_HPP
#define UGLC_TEST_SHADER_STATIC_VARIANT_RENDER_HPP

#include "UGL.h"

using namespace UGL;

/// Describes the concrete material resources and target format used by the opaque render specialization.
struct OpaqueMaterialPolicy
{
    using SampleType = half4;
    using TargetFormat = TextureFormat::RGBA8Unorm;
    using TexCoordType = float2;
};

/// Describes the concrete material resources and target format used by the cutout render specialization.
struct CutoutMaterialPolicy
{
    using SampleType = half4;
    using TargetFormat = TextureFormat::RGBA16Float;
    using TexCoordType = float3;
};

template <class Policy>
/// Provides a material bind group whose texture type is selected by the shader variant policy.
struct StaticVariantBindGroup final : public IBindGroup
{
    /// Creates the bind group with the concrete texture and sampler resources required by the selected policy.
    constructor(Texture2D<typename Policy::SampleType> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

template <class Policy>
/// Provides a framebuffer layout whose color format is selected by the shader variant policy.
struct StaticVariantFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<typename Policy::TargetFormat> color;
};

template <class Policy>
/// Provides a vertex input layout whose texture-coordinate field type is selected by the shader variant policy.
struct StaticVariantVertexInput
{
    float4 position [[Attribute0]];
    typename Policy::TexCoordType texCoord [[Attribute1]];
};

/// Carries the render variant vertex output into the fragment stage.
struct StaticVariantVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

template <class MaterialPolicy, class BindGroupType, class FrameBufferType, class VertexInputType, uint TileSize>
/// Exercises static shader variant materialization across bind groups, framebuffer layout, vertex input, and NTTP control flow.
class StaticVariantRenderPass final : public IRenderClass
{
public:
    /// Creates the render pass specialization with the concrete bind group layout selected by the template arguments.
    constructor(BindGroup<BindGroupType> bindGroup [[Slot0]])
    {
    }

private:
    /// Generates the variant-specific vertex output and folds the tile-size branch with if constexpr.
    StaticVariantVertexOutput vertex(uint vid [[VertexID]], VertexInputType inputValue [[VertexInput0]])
    {
        StaticVariantVertexOutput outputValue;
        outputValue.position = inputValue.position;
        if constexpr (TileSize == 16u)
        {
            outputValue.uv = inputValue.texCoord.xy;
        }
        else
        {
            outputValue.uv = inputValue.texCoord.xy * 0.5f;
        }
        return outputValue;
    }

    /// Samples the variant-specific material texture and writes the variant-specific framebuffer layout.
    FrameBufferType fragment(StaticVariantVertexOutput inputValue)
    {
        typename MaterialPolicy::SampleType sampled = bindGroup->texture0->sample(bindGroup->sampler0, inputValue.uv);
        FrameBufferType frameBuffer;
        frameBuffer.color = sampled;
        return frameBuffer;
    }
};

using OpaqueStaticVariantPass = StaticVariantRenderPass<OpaqueMaterialPolicy,
                                                        StaticVariantBindGroup<OpaqueMaterialPolicy>,
                                                        StaticVariantFrameBuffer<OpaqueMaterialPolicy>,
                                                        StaticVariantVertexInput<OpaqueMaterialPolicy>,
                                                        16u>;

using CutoutStaticVariantPass = StaticVariantRenderPass<CutoutMaterialPolicy,
                                                        StaticVariantBindGroup<CutoutMaterialPolicy>,
                                                        StaticVariantFrameBuffer<CutoutMaterialPolicy>,
                                                        StaticVariantVertexInput<CutoutMaterialPolicy>,
                                                        32u>;

/// Hosts two concrete render pass specializations so UGLC emits two separate static shader variants.
class ShaderStaticVariantRenderRenderer final : public AbstractRenderer
{
    Device device;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> opaqueTexture;
    Texture<TextureFormat::RGBA16Float, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> cutoutTexture;
    Sampler sampler;
    BindGroup<StaticVariantBindGroup<OpaqueMaterialPolicy>> opaqueBindGroup;
    BindGroup<StaticVariantBindGroup<CutoutMaterialPolicy>> cutoutBindGroup;
    RenderClass<OpaqueStaticVariantPass> opaquePass;
    RenderClass<CutoutStaticVariantPass> cutoutPass;

public:
    /// Creates the resources, bind groups, and render class wrappers used by the static variant fixture.
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        opaqueTexture = device->createTexture("OpaqueVariantTexture", 64, 64, 1);
        cutoutTexture = device->createTexture("CutoutVariantTexture", 64, 64, 1);
        sampler = device->createSampler({});
        opaqueBindGroup = device->createBindGroup<StaticVariantBindGroup<OpaqueMaterialPolicy>>(opaqueTexture->createView(), sampler);
        cutoutBindGroup = device->createBindGroup<StaticVariantBindGroup<CutoutMaterialPolicy>>(cutoutTexture->createView(), sampler);
        opaquePass = device->createRenderClass<OpaqueStaticVariantPass>(opaqueBindGroup);
        cutoutPass = device->createRenderClass<CutoutStaticVariantPass>(cutoutBindGroup);
    }

    /// Leaves rendering empty because this fixture validates generated host and shader code only.
    void render() override
    {
    }

    /// Leaves teardown empty because the fixture owns only reference-counted DSL resources.
    void destroy() override
    {
    }
};

#endif
