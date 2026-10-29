#ifndef UGLC_TEST_SHADER_STATIC_VARIANT_RENDER_SET_HPP
#define UGLC_TEST_SHADER_STATIC_VARIANT_RENDER_SET_HPP

#include "UGL.h"

using namespace UGL;

template <class VertexType, uint MaxTextureCount>
/// Exercises RenderSet static variant materialization across component element type and texture component count.
struct StaticVariantRenderSet final : public IRenderSet
{
    /// Creates the render set layout with variant-selected vertex and texture component types.
    constructor(BufferComponent<VertexType> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                (TextureComponent<half4, MaxTextureCount> albedo))
    {
    }
};

using StaticVariantMeshSet = StaticVariantRenderSet<float4, 4u>;
using StaticVariantBillboardSet = StaticVariantRenderSet<float3, 8u>;

/// Hosts two concrete RenderSet specializations so UGLC emits separate static render set layouts.
class ShaderStaticVariantRenderSetRenderer final : public AbstractRenderer
{
    Device device;
    RenderSet<StaticVariantMeshSet> meshSet;
    RenderSet<StaticVariantBillboardSet> billboardSet;

public:
    /// Creates the concrete RenderSet wrappers used by the static variant fixture.
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        meshSet = device->createRenderSet<StaticVariantMeshSet>();
        billboardSet = device->createRenderSet<StaticVariantBillboardSet>();
    }

    /// Leaves rendering empty because this fixture validates generated render set layout code only.
    void render() override
    {
    }

    /// Leaves teardown empty because the fixture owns only reference-counted DSL resources.
    void destroy() override
    {
    }
};

#endif
