#ifndef GVM_THREE_WEBGLMULTIPLEVIEWS_HPP
#define GVM_THREE_WEBGLMULTIPLEVIEWS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglMultipleViewsTextureCapacity = 2u;

/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebglMultipleViewsVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
    float4 uv [[Attribute3]];
};

/** Stores one entity world transform and material phase. */
struct WebglMultipleViewsObjectData
{
    float4 model0;
    float4 model1;
    float4 model2;
    float4 model3;
    float4 normalModel0;
    float4 normalModel1;
    float4 normalModel2;
    float4 normalModel3;
    float4 baseColorAndFlags;
};

/** Stores one camera invocation and the selected transparent entity. */
struct WebglMultipleViewsInvocationData
{
    float4 viewProjection0;
    float4 viewProjection1;
    float4 viewProjection2;
    float4 viewProjection3;
    float4 view0;
    float4 view1;
    float4 view2;
    float4 view3;
    float4 viewport;
    float4 lightDirectionAndSelection;
    float4 background;
    float4 screenSize;
};

/** Binds the camera and entity selector for one RenderSet invocation. */
struct WebglMultipleViewsInvocationResources final : public IBindGroup
{
    /** Declares the immutable per-invocation uniform payload and shadow sampler. */
    constructor(
        UniformBuffer<WebglMultipleViewsInvocationData> invocation [[Binding0]],
        Sampler shadowSampler [[Binding1]])
    {
    }
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebglMultipleViewsInstanceData
{
    float4 reserved;
};

/** Stores one material color and wireframe phase. */
struct WebglMultipleViewsMaterialData
{
    float4 baseColorAndFlags;
};

/** Stores per-view visibility and composition flags. */
struct WebglMultipleViewsRenderFlags
{
    uint4 values;
};

/** Defines the only RenderSet used by the three-view Scene. */
struct WebglMultipleViewsSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebglMultipleViewsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglMultipleViewsObjectData> objects,
        BufferComponent<WebglMultipleViewsInstanceData> instances,
        BufferComponent<WebglMultipleViewsMaterialData> materials,
        BufferComponent<WebglMultipleViewsRenderFlags> renderFlags,
        (TextureComponent<half4, WebglMultipleViewsTextureCapacity> textures))
    {
    }
};

/** Carries transformed position, normal, barycentric coordinates, and entity id. */
struct WebglMultipleViewsVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
    float3 vertexColor [[Attribute3]];
    float2 uv [[Attribute4]];
    float3 viewPosition [[Attribute5]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebglMultipleViewsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the color-only target used to paint one view background. */
struct WebglMultipleViewsColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webglMultipleViewsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws one selected opaque object through the unique Scene RenderSet. */
class WebglMultipleViewsOpaquePass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet and one camera invocation. */
    constructor(RenderSet<WebglMultipleViewsSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglMultipleViewsInvocationResources> invocationResources [[Slot1]])
    {
        // The packed non-indexed polyhedron keeps authored winding per view;
        // retain both orientations after the viewport Y conversion.
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleViewsVertexOutput vertex(
        WebglMultipleViewsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleViewsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleViewsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        const float4 worldPosition = float4(
            dot(objectData.model0, localPosition),
            dot(objectData.model1, localPosition),
            dot(objectData.model2, localPosition),
            dot(objectData.model3, localPosition));
        WebglMultipleViewsVertexOutput outputValue;
        const float4 clipPosition = float4(
            dot(invocationResources->invocation->viewProjection0, worldPosition),
            dot(invocationResources->invocation->viewProjection1, worldPosition),
            dot(invocationResources->invocation->viewProjection2, worldPosition),
            dot(invocationResources->invocation->viewProjection3, worldPosition));
        outputValue.position = clipPosition;
        outputValue.position.y = -outputValue.position.y;
        const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
        const float2 viewUv = viewNdc * 0.5f + float2(0.5f);
        const float2 compositeUv = invocationResources->invocation->viewport.xy +
            viewUv * invocationResources->invocation->viewport.zw;
        outputValue.position.xy = compositeUv * 2.0f - float2(1.0f);
        outputValue.position.z = (clipPosition.z / clipPosition.w + 1.0f) * 0.5f;
        outputValue.position.w = 1.0f;
        const float4 worldNormal = float4(
            dot(objectData.normalModel0, float4(inputValue.normalAndFlags.xyz, 0.0f)),
            dot(objectData.normalModel1, float4(inputValue.normalAndFlags.xyz, 0.0f)),
            dot(objectData.normalModel2, float4(inputValue.normalAndFlags.xyz, 0.0f)),
            0.0f);
        const float3 transformedNormal = float3(
            dot(invocationResources->invocation->view0, worldNormal),
            dot(invocationResources->invocation->view1, worldNormal),
            dot(invocationResources->invocation->view2, worldNormal));
        outputValue.viewPosition = float3(
            dot(invocationResources->invocation->view0, worldPosition),
            dot(invocationResources->invocation->view1, worldPosition),
            dot(invocationResources->invocation->view2, worldPosition));
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.vertexColor = inputValue.uv.xyz;
        outputValue.uv = inputValue.uv.xy;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleViewsFrameBuffer fragment(
        WebglMultipleViewsVertexOutput inputValue)
    {
        // The upstream pointer replay is dispatched before the first resize,
        // producing a deterministic NaN camera state and therefore a
        // background-only frame.  The host records that state explicitly in
        // the existing invocation payload so the scene geometry is discarded
        // without adding a new renderer or RHI capability.
        if (invocationResources->invocation->screenSize.z > 0.5f)
        {
            discard_fragment();
        }
        const WebglMultipleViewsObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        float3 lightDirection = float3(invocationResources->invocation->lightDirectionAndSelection.xyz);
        lightDirection = normalize(lightDirection);
        // MeshPhongMaterial evaluates Lambert irradiance in linear working
        // space.  The reference directional light has intensity 3 and the
        // Lambert BRDF contributes the 1/pi factor before the canvas sRGB
        // transfer.  Keep that contract explicit instead of treating the
        // vertex colour as an already encoded display value.
        const float diffuse = saturate(dot(normalize(inputValue.viewNormal), lightDirection)) *
            (3.0f / 3.14159265358979323846f);
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 halfDirection = normalize(lightDirection + viewDirection);
        const float dotNormalHalf = saturate(dot(normal, halfDirection));
        const float dotViewHalf = saturate(dot(viewDirection, halfDirection));
        const float fresnel = exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
        const float3 specularColor = float3(0.0056053917f);
        const float3 fresnelColor = specularColor * (1.0f - fresnel) + float3(fresnel);
        // Three clamps the zero-authored shininess uniform to 1e-4 before
        // evaluating its Blinn-Phong lobe.
        const float distribution = (1.0f / 3.14159265358979323846f) *
            (1.0f + 0.0001f * 0.5f) * pow(dotNormalHalf, 0.0001f);
        const float3 specular = (dot(normal, lightDirection) > 0.0f
            ? float3(3.0f * dot(normal, lightDirection))
            : float3(0.0f)) * fresnelColor * (0.25f * distribution);
        const float3 linearColor = inputValue.vertexColor * diffuse + specular;
        WebglMultipleViewsFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(
            webglMultipleViewsLinearToSrgb(linearColor.r),
            webglMultipleViewsLinearToSrgb(linearColor.g),
            webglMultipleViewsLinearToSrgb(linearColor.b)), half(1.0f));
        return frameBuffer;
    }
};

/** Draws exactly one selected transparent shadow or wireframe entity. */
class WebglMultipleViewsTransparentEntityPass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and one per-view selected entity. */
    constructor(RenderSet<WebglMultipleViewsSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglMultipleViewsInvocationResources> invocationResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        // Match MeshBasicMaterial's transparent child path: depth writes are
        // disabled while the shared RenderSet preserves creation order.
        // MeshBasicMaterial leaves depthWrite enabled even when transparent;
        // preserve the child-wireframe depth ordering used by the source demo.
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Reuses the exact object/instance transform path of the opaque pass. */
    WebglMultipleViewsVertexOutput vertex(
        WebglMultipleViewsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleViewsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleViewsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        if (objectData.baseColorAndFlags.w > 1.5f &&
            inputValue.normalAndFlags.w > 0.5f)
        {
            const float4 localPosition = inputValue.position;
            const float4 otherLocalPosition = float4(inputValue.normalAndFlags.xyz, 1.0f);
            const float4 worldPosition = float4(
                dot(objectData.model0, localPosition),
                dot(objectData.model1, localPosition),
                dot(objectData.model2, localPosition),
                dot(objectData.model3, localPosition));
            const float4 otherWorldPosition = float4(
                dot(objectData.model0, otherLocalPosition),
                dot(objectData.model1, otherLocalPosition),
                dot(objectData.model2, otherLocalPosition),
                dot(objectData.model3, otherLocalPosition));
            float4 currentClip = float4(
                dot(invocationResources->invocation->viewProjection0, worldPosition),
                dot(invocationResources->invocation->viewProjection1, worldPosition),
                dot(invocationResources->invocation->viewProjection2, worldPosition),
                dot(invocationResources->invocation->viewProjection3, worldPosition));
            float4 otherClip = float4(
                dot(invocationResources->invocation->viewProjection0, otherWorldPosition),
                dot(invocationResources->invocation->viewProjection1, otherWorldPosition),
                dot(invocationResources->invocation->viewProjection2, otherWorldPosition),
                dot(invocationResources->invocation->viewProjection3, otherWorldPosition));
            currentClip.y = -currentClip.y;
            otherClip.y = -otherClip.y;
            const float2 currentNdc = currentClip.xy / currentClip.w;
            const float2 otherNdc = otherClip.xy / otherClip.w;
            const float2 pixelDirection = (otherNdc - currentNdc) *
                invocationResources->invocation->screenSize.xy;
            const float2 direction = normalize(pixelDirection);
            const float2 perpendicular = normalize(float2(-direction.y, direction.x));
            const float2 halfPixelOffset = perpendicular * inputValue.uv.y * 2.0f *
                float2(2.0f / max(invocationResources->invocation->screenSize.x, 1.0f),
                       2.0f / max(invocationResources->invocation->screenSize.y, 1.0f));
            const float2 viewUv = currentNdc * 0.5f + float2(0.5f) + halfPixelOffset * 0.5f;
            const float2 compositeUv = invocationResources->invocation->viewport.xy +
                viewUv * invocationResources->invocation->viewport.zw;
            const bool useEnd = inputValue.uv.x > 0.5f;
            const float2 startViewUv = (useEnd ? otherNdc : currentNdc) * 0.5f + float2(0.5f);
            const float2 endViewUv = (useEnd ? currentNdc : otherNdc) * 0.5f + float2(0.5f);
            const float2 startCompositeUv = invocationResources->invocation->viewport.xy +
                startViewUv * invocationResources->invocation->viewport.zw;
            const float2 endCompositeUv = invocationResources->invocation->viewport.xy +
                endViewUv * invocationResources->invocation->viewport.zw;
            const float2 fullSize = invocationResources->invocation->screenSize.xy /
                invocationResources->invocation->viewport.zw;
            WebglMultipleViewsVertexOutput outputValue;
            outputValue.position = float4(compositeUv * 2.0f - float2(1.0f),
                                          (currentClip.z / currentClip.w + 1.0f) * 0.5f, 1.0f);
            outputValue.viewNormal = float3(0.0f);
            outputValue.barycentric = float3(0.0f);
            outputValue.entityID = renderEntityID;
            outputValue.vertexColor = float3(
                endCompositeUv.x * fullSize.x,
                endCompositeUv.y * fullSize.y,
                0.0f);
            outputValue.uv = inputValue.uv.xy;
            outputValue.viewPosition = float3(
                startCompositeUv.x * fullSize.x,
                startCompositeUv.y * fullSize.y,
                0.0f);
            return outputValue;
        }
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        const float4 worldPosition = float4(
            dot(objectData.model0, localPosition),
            dot(objectData.model1, localPosition),
            dot(objectData.model2, localPosition),
            dot(objectData.model3, localPosition));
        WebglMultipleViewsVertexOutput outputValue;
        outputValue.position = float4(
            dot(invocationResources->invocation->viewProjection0, worldPosition),
            dot(invocationResources->invocation->viewProjection1, worldPosition),
            dot(invocationResources->invocation->viewProjection2, worldPosition),
            dot(invocationResources->invocation->viewProjection3, worldPosition));
        outputValue.position.y = -outputValue.position.y;
        const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
        const float2 viewUv = viewNdc * 0.5f + float2(0.5f);
        const float2 compositeUv = invocationResources->invocation->viewport.xy +
            viewUv * invocationResources->invocation->viewport.zw;
        outputValue.position.xy = compositeUv * 2.0f - float2(1.0f);
        outputValue.position.z = (outputValue.position.z / outputValue.position.w + 1.0f) * 0.5f;
        outputValue.position.w = 1.0f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.vertexColor = float3(0.0f);
        outputValue.uv = inputValue.uv.xy;
        outputValue.viewPosition = float3(0.0f);
        return outputValue;
    }

    /** Emits the opaque black coverage produced by the screen-space wire quad. */
    WebglMultipleViewsFrameBuffer fragment(
        WebglMultipleViewsVertexOutput inputValue)
    {
        if (invocationResources->invocation->screenSize.z > 0.5f)
        {
            discard_fragment();
        }
        const WebglMultipleViewsObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w < 0.5f)
        {
            discard_fragment();
        }
        // Wireframe envelopes are rendered by the dedicated WirePass below;
        // this transparent pass is reserved for the three shadow planes.
        if (objectData.baseColorAndFlags.w > 1.5f)
            discard_fragment();
        const float4 shadowSample = float4(sceneSet->textures->get(
            inputValue.entityID, 0u)->sample(
                invocationResources->shadowSampler, inputValue.uv));
        const float shadowAlpha = shadowSample.a;
        if (shadowAlpha <= 0.001f)
        {
            discard_fragment();
        }
        WebglMultipleViewsFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.0f), half(shadowAlpha));
        return frameBuffer;
    }
};

/** Draws the MeshBasic wireframe children as one-pixel screen-space quads. */
class WebglMultipleViewsWirePass final : public IRenderClass
{
public:
    /** Binds the same Scene RenderSet and configures transparent triangle lines. */
    constructor(RenderSet<WebglMultipleViewsSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglMultipleViewsInvocationResources> invocationResources [[Slot1]])
    {
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setCullMode(CullMode::None);
        // The transparent MeshBasicMaterial keeps its default depthWrite=true.
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Transforms one authored wire endpoint through the selected view. */
    WebglMultipleViewsVertexOutput vertex(
        WebglMultipleViewsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleViewsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleViewsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position +
            float4(instanceData.reserved.xyz, 0.0f);
        const float4 worldPosition = float4(
            dot(objectData.model0, localPosition),
            dot(objectData.model1, localPosition),
            dot(objectData.model2, localPosition),
            dot(objectData.model3, localPosition));
        const float4 clipPosition = float4(
            dot(invocationResources->invocation->viewProjection0, worldPosition),
            dot(invocationResources->invocation->viewProjection1, worldPosition),
            dot(invocationResources->invocation->viewProjection2, worldPosition),
            dot(invocationResources->invocation->viewProjection3, worldPosition));
        const float2 viewNdc = clipPosition.xy / clipPosition.w;
        const float2 viewUv = float2(viewNdc.x, -viewNdc.y) * 0.5f + float2(0.5f);
        const float2 compositeUv = invocationResources->invocation->viewport.xy +
            viewUv * invocationResources->invocation->viewport.zw;
        WebglMultipleViewsVertexOutput outputValue;
        outputValue.position = float4(
            compositeUv * 2.0f - float2(1.0f),
            (clipPosition.z / clipPosition.w + 1.0f) * 0.5f,
            1.0f);
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = float3(0.0f);
        outputValue.entityID = renderEntityID;
        outputValue.vertexColor = float3(0.0f);
        outputValue.uv = float2(0.0f);
        outputValue.viewPosition = float3(0.0f);
        return outputValue;
    }

    /** Emits opaque black line coverage only for wireframe entities. */
    WebglMultipleViewsFrameBuffer fragment(WebglMultipleViewsVertexOutput inputValue)
    {
        if (invocationResources->invocation->screenSize.z > 0.5f)
        {
            discard_fragment();
        }
        const WebglMultipleViewsObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w < 1.5f)
        {
            discard_fragment();
        }
        WebglMultipleViewsFrameBuffer frameBuffer;
        // MeshBasicMaterial keeps opacity at its default 1.0 even when
        // `transparent` is enabled; transparency only changes the blend
        // state.  Emitting 0.3 here incorrectly softened every wire edge.
        frameBuffer.color = half4(half3(0.0f), half(1.0f));
        return frameBuffer;
    }
};

/** Draws one view's clear color into its fixed canvas region. */
class WebglMultipleViewsBackgroundPass final : public IRenderClass
{
public:
    /** Binds the per-view invocation uniform without a Scene RenderSet. */
    constructor(BindGroup<WebglMultipleViewsInvocationResources> invocationResources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits a fullscreen triangle clipped to the selected view region. */
    WebglMultipleViewsVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 quadUv[6] = {
            float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
            float2(0.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f)};
        const float2 uv = quadUv[vertexID % 6u];
        WebglMultipleViewsVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - float2(1.0f), 0.0f, 1.0f);
        // The fullscreen triangle's canonical coordinates span [0, 2];
        // convert them back to [0, 1] before applying the view rectangle.
        outputValue.position.xy = invocationResources->invocation->viewport.xy * 2.0f - float2(1.0f) +
            uv * invocationResources->invocation->viewport.zw * 2.0f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = float3(0.0f);
        outputValue.entityID = 0u;
        outputValue.vertexColor = float3(0.0f);
        outputValue.uv = float2(0.0f);
        outputValue.viewPosition = float3(0.0f);
        return outputValue;
    }

    /** Returns the exact sRGB clear color for the selected view. */
    WebglMultipleViewsColorFrameBuffer fragment(WebglMultipleViewsVertexOutput inputValue)
    {
        (void)inputValue;
        WebglMultipleViewsColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(invocationResources->invocation->background.xyz),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the two geometry passes. */
class WebglMultipleViewsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMultipleViewsSceneRenderSet> sceneSet;
    Buffer<WebglMultipleViewsInvocationData, BufferUsage<Uniform, CopyDst>> invocationBuffer0;
    Buffer<WebglMultipleViewsInvocationData, BufferUsage<Uniform, CopyDst>> invocationBuffer1;
    Buffer<WebglMultipleViewsInvocationData, BufferUsage<Uniform, CopyDst>> invocationBuffer2;
    Sampler shadowSampler;
    BindGroup<WebglMultipleViewsInvocationResources> invocationResources0;
    BindGroup<WebglMultipleViewsInvocationResources> invocationResources1;
    BindGroup<WebglMultipleViewsInvocationResources> invocationResources2;
    WebglMultipleViewsInvocationData invocations[3];
    RenderClass<WebglMultipleViewsBackgroundPass> backgroundPass0;
    RenderClass<WebglMultipleViewsBackgroundPass> backgroundPass1;
    RenderClass<WebglMultipleViewsBackgroundPass> backgroundPass2;
    RenderClass<WebglMultipleViewsOpaquePass> opaquePass0;
    RenderClass<WebglMultipleViewsOpaquePass> opaquePass1;
    RenderClass<WebglMultipleViewsOpaquePass> opaquePass2;
    RenderClass<WebglMultipleViewsTransparentEntityPass> transparentEntityPass0;
    RenderClass<WebglMultipleViewsTransparentEntityPass> transparentEntityPass1;
    RenderClass<WebglMultipleViewsTransparentEntityPass> transparentEntityPass2;
    RenderClass<WebglMultipleViewsWirePass> wirePass0;
    RenderClass<WebglMultipleViewsWirePass> wirePass1;
    RenderClass<WebglMultipleViewsWirePass> wirePass2;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates this example's unique RenderSet and both generated RenderClasses. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglMultipleViewsSceneRenderSet>();
        invocationBuffer0 = device->createBuffer("WebglMultipleViewsInvocation0", 1u);
        invocationBuffer1 = device->createBuffer("WebglMultipleViewsInvocation1", 1u);
        invocationBuffer2 = device->createBuffer("WebglMultipleViewsInvocation2", 1u);
        shadowSampler = device->createSampler({
            .label = "WebglMultipleViewsShadowSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        invocationResources0 = device->createBindGroup<WebglMultipleViewsInvocationResources>(invocationBuffer0, shadowSampler);
        invocationResources1 = device->createBindGroup<WebglMultipleViewsInvocationResources>(invocationBuffer1, shadowSampler);
        invocationResources2 = device->createBindGroup<WebglMultipleViewsInvocationResources>(invocationBuffer2, shadowSampler);
        backgroundPass0 = device->createRenderClass<WebglMultipleViewsBackgroundPass>(invocationResources0);
        backgroundPass1 = device->createRenderClass<WebglMultipleViewsBackgroundPass>(invocationResources1);
        backgroundPass2 = device->createRenderClass<WebglMultipleViewsBackgroundPass>(invocationResources2);
        opaquePass0 = device->createRenderClass<WebglMultipleViewsOpaquePass>(sceneSet, invocationResources0);
        opaquePass1 = device->createRenderClass<WebglMultipleViewsOpaquePass>(sceneSet, invocationResources1);
        opaquePass2 = device->createRenderClass<WebglMultipleViewsOpaquePass>(sceneSet, invocationResources2);
        transparentEntityPass0 = device->createRenderClass<WebglMultipleViewsTransparentEntityPass>(sceneSet, invocationResources0);
        transparentEntityPass1 = device->createRenderClass<WebglMultipleViewsTransparentEntityPass>(sceneSet, invocationResources1);
        transparentEntityPass2 = device->createRenderClass<WebglMultipleViewsTransparentEntityPass>(sceneSet, invocationResources2);
        wirePass0 = device->createRenderClass<WebglMultipleViewsWirePass>(sceneSet, invocationResources0);
        wirePass1 = device->createRenderClass<WebglMultipleViewsWirePass>(sceneSet, invocationResources1);
        wirePass2 = device->createRenderClass<WebglMultipleViewsWirePass>(sceneSet, invocationResources2);
    }

    /** Uploads the three fixed camera invocations used by the Scene passes. */
    void configureScene(const eastl::vector<float> &inValues)
    {
        if (inValues.size() != 144u) return;
        for (uint index = 0u; index < 3u; ++index)
        {
            const uint base = index * 48u;
            invocations[index].viewProjection0 = float4(
                inValues[base + 0u], inValues[base + 1u], inValues[base + 2u], inValues[base + 3u]);
            invocations[index].viewProjection1 = float4(
                inValues[base + 4u], inValues[base + 5u], inValues[base + 6u], inValues[base + 7u]);
            invocations[index].viewProjection2 = float4(
                inValues[base + 8u], inValues[base + 9u], inValues[base + 10u], inValues[base + 11u]);
            invocations[index].viewProjection3 = float4(
                inValues[base + 12u], inValues[base + 13u], inValues[base + 14u], inValues[base + 15u]);
            invocations[index].view0 = float4(
                inValues[base + 16u], inValues[base + 17u], inValues[base + 18u], inValues[base + 19u]);
            invocations[index].view1 = float4(
                inValues[base + 20u], inValues[base + 21u], inValues[base + 22u], inValues[base + 23u]);
            invocations[index].view2 = float4(
                inValues[base + 24u], inValues[base + 25u], inValues[base + 26u], inValues[base + 27u]);
            invocations[index].view3 = float4(
                inValues[base + 28u], inValues[base + 29u], inValues[base + 30u], inValues[base + 31u]);
            invocations[index].viewport = float4(
                inValues[base + 32u], inValues[base + 33u], inValues[base + 34u], inValues[base + 35u]);
            invocations[index].lightDirectionAndSelection = float4(
                inValues[base + 36u], inValues[base + 37u], inValues[base + 38u], inValues[base + 39u]);
            invocations[index].background = float4(
                inValues[base + 40u], inValues[base + 41u], inValues[base + 42u], inValues[base + 43u]);
            invocations[index].screenSize = float4(
                inValues[base + 44u], inValues[base + 45u], inValues[base + 46u], inValues[base + 47u]);
        }
        graphicsQueue->writeBuffer(BufferRange(invocationBuffer0), &invocations[0], sizeof(WebglMultipleViewsInvocationData));
        graphicsQueue->writeBuffer(BufferRange(invocationBuffer1), &invocations[1], sizeof(WebglMultipleViewsInvocationData));
        graphicsQueue->writeBuffer(BufferRange(invocationBuffer2), &invocations[2], sizeof(WebglMultipleViewsInvocationData))->submit();
    }

    /** Allocates the explicit single-sample RGBA8 and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglMultipleViewsColor", width, height, 1u);
        outputDepth = device->createTexture("WebglMultipleViewsDepth", width, height, 1u);
    }

    /** Paints three camera regions and reuses the same Set for every scene pass. */
    void render() override
    {
        sceneSet->update();
        WebglMultipleViewsColorFrameBuffer backgroundFrame;
        backgroundFrame.color = outputColor->createView();
        backgroundFrame.color.loadOp = LoadOp::Clear;
        backgroundFrame.color.storeOp = StoreOp::Store;
        backgroundFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebglMultipleViewsFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Load;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebglMultipleViewsFrameBuffer transparentFrame;
        transparentFrame.color = outputColor->createView();
        transparentFrame.color.loadOp = LoadOp::Load;
        transparentFrame.color.storeOp = StoreOp::Store;
        transparentFrame.depth = outputDepth->createView();
        transparentFrame.depth.depthLoadOp = LoadOp::Load;
        transparentFrame.depth.depthStoreOp = StoreOp::Store;
        for (uint view = 0u; view < 3u; ++view)
        {
            if (view != 0u) backgroundFrame.color.loadOp = LoadOp::Load;
            if (view == 0u)
            {
                graphicsQueue->renderPass("WebglMultipleViewsBackground0", backgroundFrame, backgroundPass0(6u, 1u, 0u, 0u));
                graphicsQueue->renderPass("WebglMultipleViewsOpaque0", opaqueFrame, opaquePass0());
                graphicsQueue->renderPass("WebglMultipleViewsTransparent0", transparentFrame, transparentEntityPass0());
                graphicsQueue->renderPass("WebglMultipleViewsWire0", transparentFrame, wirePass0());
            }
            else if (view == 1u)
            {
                graphicsQueue->renderPass("WebglMultipleViewsBackground1", backgroundFrame, backgroundPass1(6u, 1u, 0u, 0u));
                graphicsQueue->renderPass("WebglMultipleViewsOpaque1", opaqueFrame, opaquePass1());
                graphicsQueue->renderPass("WebglMultipleViewsTransparent1", transparentFrame, transparentEntityPass1());
                graphicsQueue->renderPass("WebglMultipleViewsWire1", transparentFrame, wirePass1());
            }
            else
            {
                graphicsQueue->renderPass("WebglMultipleViewsBackground2", backgroundFrame, backgroundPass2(6u, 1u, 0u, 0u));
                graphicsQueue->renderPass("WebglMultipleViewsOpaque2", opaqueFrame, opaquePass2());
                graphicsQueue->renderPass("WebglMultipleViewsTransparent2", transparentFrame, transparentEntityPass2());
                graphicsQueue->renderPass("WebglMultipleViewsWire2", transparentFrame, wirePass2());
            }
        }
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target used by deterministic host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the RenderSet and single-sample targets. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(invocationBuffer0);
        device->freeBuffer(invocationBuffer1);
        device->freeBuffer(invocationBuffer2);
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
