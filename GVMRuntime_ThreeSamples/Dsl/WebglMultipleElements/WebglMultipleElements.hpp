#ifndef GVM_THREE_WEBGLMULTIPLEELEMENTS_HPP
#define GVM_THREE_WEBGLMULTIPLEELEMENTS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebglMultipleElementsVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores one entity camera transform, light state, and material phase. */
struct WebglMultipleElementsObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 viewport;
    float4 baseColorAndFlags;
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebglMultipleElementsInstanceData
{
    float4 reserved;
};

/** Stores one material color and wireframe phase. */
struct WebglMultipleElementsMaterialData
{
    float4 baseColorAndFlags;
};

/** Defines the only RenderSet used by the orientation-transform Scene. */
struct WebglMultipleElementsSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebglMultipleElementsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglMultipleElementsObjectData> objects,
        BufferComponent<WebglMultipleElementsInstanceData> instances,
        BufferComponent<WebglMultipleElementsMaterialData> materials)
    {
    }
};

/** Carries transformed position, normal, barycentric coordinates, and entity id. */
struct WebglMultipleElementsVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
    float background [[Attribute3]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebglMultipleElementsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webglMultipleElementsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666667f) * 1.055f - 0.055f;
}

/** Draws opaque cone and target objects through the unique Scene RenderSet. */
class WebglMultipleElementsScene00Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene01Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene02Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene03Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene04Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene05Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene06Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene07Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene08Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene09Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene10Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene11Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene12Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene13Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene14Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene15Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene16Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene17Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene18Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene19Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene20Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene21Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene22Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene23Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene24Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene25Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene26Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene27Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene28Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene29Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene30Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene31Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene32Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene33Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene34Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene35Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene36Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene37Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene38Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebglMultipleElementsScene39Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        if (inputValue.normalAndFlags.w > 0.5f)
        {
            const float2 cardUv = objectData.viewport.xy +
                (inputValue.position.xy * 0.5f + float2(0.5f)) * objectData.viewport.zw;
            outputValue.position = float4(cardUv * 2.0f - float2(1.0f), 0.999f, 1.0f);
            outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.position = mul(objectData.modelViewProjection, localPosition);
            outputValue.position.y = -outputValue.position.y;
            outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
            const float2 viewNdc = outputValue.position.xy / outputValue.position.w;
            const float2 compositeNdc = objectData.viewport.xy * 2.0f - float2(1.0f) +
                objectData.viewport.zw * (viewNdc + float2(1.0f));
            outputValue.position.xy = compositeNdc * outputValue.position.w;
            const float3 transformedNormal = mul(objectData.normalMatrix,
                float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
            outputValue.viewNormal = normalize(transformedNormal);
        }
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.background = inputValue.normalAndFlags.w;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.8784313725f), half(1.0f));
            return frameBuffer;
        }
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 sky = float3(0.4019777798f);
        const float3 ground = float3(0.0578054302f);
        const float3 hemisphere = (sky * hemisphereWeight + ground * (1.0f - hemisphereWeight)) * 3.0f / 3.14159265359f;
        const float directional = max(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f) *
            1.5f / 3.14159265359f;
        const float3 linearColor = saturate(materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)));
        const float3 srgb = float3(
            webglMultipleElementsLinearToSrgb(linearColor.x),
            webglMultipleElementsLinearToSrgb(linearColor.y),
            webglMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent wireframe control sphere from the same RenderSet. */
class WebglMultipleElementsWireframePass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and preserves opaque color/depth. */
    constructor(RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
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
    WebglMultipleElementsVertexOutput vertex(
        WebglMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMultipleElementsVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates barycentric edge coverage and the locked 0.3 wireframe alpha. */
    WebglMultipleElementsFrameBuffer fragment(
        WebglMultipleElementsVertexOutput inputValue)
    {
        const WebglMultipleElementsObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w < 0.5f)
        {
            discard_fragment();
        }
        const float edge = min(inputValue.barycentric.x,
                               min(inputValue.barycentric.y, inputValue.barycentric.z));
        if (edge > 0.035f)
        {
            discard_fragment();
        }
        WebglMultipleElementsFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.8f), half(0.3f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the two geometry passes. */
/** Owns one RenderSet per independent DOM Scene and the matching scene passes. */
class WebglMultipleElementsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet0;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet1;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet2;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet3;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet4;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet5;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet6;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet7;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet8;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet9;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet10;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet11;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet12;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet13;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet14;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet15;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet16;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet17;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet18;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet19;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet20;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet21;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet22;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet23;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet24;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet25;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet26;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet27;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet28;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet29;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet30;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet31;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet32;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet33;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet34;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet35;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet36;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet37;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet38;
    [[Export]] RenderSet<WebglMultipleElementsSceneRenderSet> sceneSet39;
    RenderClass<WebglMultipleElementsScene00Pass> opaquePass0;
    RenderClass<WebglMultipleElementsScene01Pass> opaquePass1;
    RenderClass<WebglMultipleElementsScene02Pass> opaquePass2;
    RenderClass<WebglMultipleElementsScene03Pass> opaquePass3;
    RenderClass<WebglMultipleElementsScene04Pass> opaquePass4;
    RenderClass<WebglMultipleElementsScene05Pass> opaquePass5;
    RenderClass<WebglMultipleElementsScene06Pass> opaquePass6;
    RenderClass<WebglMultipleElementsScene07Pass> opaquePass7;
    RenderClass<WebglMultipleElementsScene08Pass> opaquePass8;
    RenderClass<WebglMultipleElementsScene09Pass> opaquePass9;
    RenderClass<WebglMultipleElementsScene10Pass> opaquePass10;
    RenderClass<WebglMultipleElementsScene11Pass> opaquePass11;
    RenderClass<WebglMultipleElementsScene12Pass> opaquePass12;
    RenderClass<WebglMultipleElementsScene13Pass> opaquePass13;
    RenderClass<WebglMultipleElementsScene14Pass> opaquePass14;
    RenderClass<WebglMultipleElementsScene15Pass> opaquePass15;
    RenderClass<WebglMultipleElementsScene16Pass> opaquePass16;
    RenderClass<WebglMultipleElementsScene17Pass> opaquePass17;
    RenderClass<WebglMultipleElementsScene18Pass> opaquePass18;
    RenderClass<WebglMultipleElementsScene19Pass> opaquePass19;
    RenderClass<WebglMultipleElementsScene20Pass> opaquePass20;
    RenderClass<WebglMultipleElementsScene21Pass> opaquePass21;
    RenderClass<WebglMultipleElementsScene22Pass> opaquePass22;
    RenderClass<WebglMultipleElementsScene23Pass> opaquePass23;
    RenderClass<WebglMultipleElementsScene24Pass> opaquePass24;
    RenderClass<WebglMultipleElementsScene25Pass> opaquePass25;
    RenderClass<WebglMultipleElementsScene26Pass> opaquePass26;
    RenderClass<WebglMultipleElementsScene27Pass> opaquePass27;
    RenderClass<WebglMultipleElementsScene28Pass> opaquePass28;
    RenderClass<WebglMultipleElementsScene29Pass> opaquePass29;
    RenderClass<WebglMultipleElementsScene30Pass> opaquePass30;
    RenderClass<WebglMultipleElementsScene31Pass> opaquePass31;
    RenderClass<WebglMultipleElementsScene32Pass> opaquePass32;
    RenderClass<WebglMultipleElementsScene33Pass> opaquePass33;
    RenderClass<WebglMultipleElementsScene34Pass> opaquePass34;
    RenderClass<WebglMultipleElementsScene35Pass> opaquePass35;
    RenderClass<WebglMultipleElementsScene36Pass> opaquePass36;
    RenderClass<WebglMultipleElementsScene37Pass> opaquePass37;
    RenderClass<WebglMultipleElementsScene38Pass> opaquePass38;
    RenderClass<WebglMultipleElementsScene39Pass> opaquePass39;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates one RenderSet and one scene pass for every independent DOM scene. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet0 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet1 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet2 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet3 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet4 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet5 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet6 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet7 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet8 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet9 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet10 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet11 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet12 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet13 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet14 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet15 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet16 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet17 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet18 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet19 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet20 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet21 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet22 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet23 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet24 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet25 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet26 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet27 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet28 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet29 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet30 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet31 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet32 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet33 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet34 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet35 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet36 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet37 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet38 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        sceneSet39 = device->createRenderSet<WebglMultipleElementsSceneRenderSet>();
        opaquePass0 = device->createRenderClass<WebglMultipleElementsScene00Pass>(sceneSet0);
        opaquePass1 = device->createRenderClass<WebglMultipleElementsScene01Pass>(sceneSet1);
        opaquePass2 = device->createRenderClass<WebglMultipleElementsScene02Pass>(sceneSet2);
        opaquePass3 = device->createRenderClass<WebglMultipleElementsScene03Pass>(sceneSet3);
        opaquePass4 = device->createRenderClass<WebglMultipleElementsScene04Pass>(sceneSet4);
        opaquePass5 = device->createRenderClass<WebglMultipleElementsScene05Pass>(sceneSet5);
        opaquePass6 = device->createRenderClass<WebglMultipleElementsScene06Pass>(sceneSet6);
        opaquePass7 = device->createRenderClass<WebglMultipleElementsScene07Pass>(sceneSet7);
        opaquePass8 = device->createRenderClass<WebglMultipleElementsScene08Pass>(sceneSet8);
        opaquePass9 = device->createRenderClass<WebglMultipleElementsScene09Pass>(sceneSet9);
        opaquePass10 = device->createRenderClass<WebglMultipleElementsScene10Pass>(sceneSet10);
        opaquePass11 = device->createRenderClass<WebglMultipleElementsScene11Pass>(sceneSet11);
        opaquePass12 = device->createRenderClass<WebglMultipleElementsScene12Pass>(sceneSet12);
        opaquePass13 = device->createRenderClass<WebglMultipleElementsScene13Pass>(sceneSet13);
        opaquePass14 = device->createRenderClass<WebglMultipleElementsScene14Pass>(sceneSet14);
        opaquePass15 = device->createRenderClass<WebglMultipleElementsScene15Pass>(sceneSet15);
        opaquePass16 = device->createRenderClass<WebglMultipleElementsScene16Pass>(sceneSet16);
        opaquePass17 = device->createRenderClass<WebglMultipleElementsScene17Pass>(sceneSet17);
        opaquePass18 = device->createRenderClass<WebglMultipleElementsScene18Pass>(sceneSet18);
        opaquePass19 = device->createRenderClass<WebglMultipleElementsScene19Pass>(sceneSet19);
        opaquePass20 = device->createRenderClass<WebglMultipleElementsScene20Pass>(sceneSet20);
        opaquePass21 = device->createRenderClass<WebglMultipleElementsScene21Pass>(sceneSet21);
        opaquePass22 = device->createRenderClass<WebglMultipleElementsScene22Pass>(sceneSet22);
        opaquePass23 = device->createRenderClass<WebglMultipleElementsScene23Pass>(sceneSet23);
        opaquePass24 = device->createRenderClass<WebglMultipleElementsScene24Pass>(sceneSet24);
        opaquePass25 = device->createRenderClass<WebglMultipleElementsScene25Pass>(sceneSet25);
        opaquePass26 = device->createRenderClass<WebglMultipleElementsScene26Pass>(sceneSet26);
        opaquePass27 = device->createRenderClass<WebglMultipleElementsScene27Pass>(sceneSet27);
        opaquePass28 = device->createRenderClass<WebglMultipleElementsScene28Pass>(sceneSet28);
        opaquePass29 = device->createRenderClass<WebglMultipleElementsScene29Pass>(sceneSet29);
        opaquePass30 = device->createRenderClass<WebglMultipleElementsScene30Pass>(sceneSet30);
        opaquePass31 = device->createRenderClass<WebglMultipleElementsScene31Pass>(sceneSet31);
        opaquePass32 = device->createRenderClass<WebglMultipleElementsScene32Pass>(sceneSet32);
        opaquePass33 = device->createRenderClass<WebglMultipleElementsScene33Pass>(sceneSet33);
        opaquePass34 = device->createRenderClass<WebglMultipleElementsScene34Pass>(sceneSet34);
        opaquePass35 = device->createRenderClass<WebglMultipleElementsScene35Pass>(sceneSet35);
        opaquePass36 = device->createRenderClass<WebglMultipleElementsScene36Pass>(sceneSet36);
        opaquePass37 = device->createRenderClass<WebglMultipleElementsScene37Pass>(sceneSet37);
        opaquePass38 = device->createRenderClass<WebglMultipleElementsScene38Pass>(sceneSet38);
        opaquePass39 = device->createRenderClass<WebglMultipleElementsScene39Pass>(sceneSet39);
    }

    /** Allocates explicit single-sample color and depth targets for all child scenes. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglMultipleElementsColor", width, height, 1u);
        outputDepth = device->createTexture("WebglMultipleElementsDepth", width, height, 1u);
    }

    /** Renders each independent Scene through its own RenderSet in deterministic order. */
    void render() override
    {
        sceneSet0->update();
        sceneSet1->update();
        sceneSet2->update();
        sceneSet3->update();
        sceneSet4->update();
        sceneSet5->update();
        sceneSet6->update();
        sceneSet7->update();
        sceneSet8->update();
        sceneSet9->update();
        sceneSet10->update();
        sceneSet11->update();
        sceneSet12->update();
        sceneSet13->update();
        sceneSet14->update();
        sceneSet15->update();
        sceneSet16->update();
        sceneSet17->update();
        sceneSet18->update();
        sceneSet19->update();
        sceneSet20->update();
        sceneSet21->update();
        sceneSet22->update();
        sceneSet23->update();
        sceneSet24->update();
        sceneSet25->update();
        sceneSet26->update();
        sceneSet27->update();
        sceneSet28->update();
        sceneSet29->update();
        sceneSet30->update();
        sceneSet31->update();
        sceneSet32->update();
        sceneSet33->update();
        sceneSet34->update();
        sceneSet35->update();
        sceneSet36->update();
        sceneSet37->update();
        sceneSet38->update();
        sceneSet39->update();
        WebglMultipleElementsFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue = {1.0f, 1.0f, 1.0f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebglMultipleElementsFrameBuffer loadFrame;
        loadFrame.color = outputColor->createView();
        loadFrame.color.loadOp = LoadOp::Load;
        loadFrame.color.storeOp = StoreOp::Store;
        loadFrame.depth = outputDepth->createView();
        loadFrame.depth.depthLoadOp = LoadOp::Load;
        loadFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglMultipleElementsScene0", opaqueFrame, opaquePass0())
            ->renderPass("WebglMultipleElementsScene1", loadFrame, opaquePass1())
            ->renderPass("WebglMultipleElementsScene2", loadFrame, opaquePass2())
            ->renderPass("WebglMultipleElementsScene3", loadFrame, opaquePass3())
            ->renderPass("WebglMultipleElementsScene4", loadFrame, opaquePass4())
            ->renderPass("WebglMultipleElementsScene5", loadFrame, opaquePass5())
            ->renderPass("WebglMultipleElementsScene6", loadFrame, opaquePass6())
            ->renderPass("WebglMultipleElementsScene7", loadFrame, opaquePass7())
            ->renderPass("WebglMultipleElementsScene8", loadFrame, opaquePass8())
            ->renderPass("WebglMultipleElementsScene9", loadFrame, opaquePass9())
            ->renderPass("WebglMultipleElementsScene10", loadFrame, opaquePass10())
            ->renderPass("WebglMultipleElementsScene11", loadFrame, opaquePass11())
            ->renderPass("WebglMultipleElementsScene12", loadFrame, opaquePass12())
            ->renderPass("WebglMultipleElementsScene13", loadFrame, opaquePass13())
            ->renderPass("WebglMultipleElementsScene14", loadFrame, opaquePass14())
            ->renderPass("WebglMultipleElementsScene15", loadFrame, opaquePass15())
            ->renderPass("WebglMultipleElementsScene16", loadFrame, opaquePass16())
            ->renderPass("WebglMultipleElementsScene17", loadFrame, opaquePass17())
            ->renderPass("WebglMultipleElementsScene18", loadFrame, opaquePass18())
            ->renderPass("WebglMultipleElementsScene19", loadFrame, opaquePass19())
            ->renderPass("WebglMultipleElementsScene20", loadFrame, opaquePass20())
            ->renderPass("WebglMultipleElementsScene21", loadFrame, opaquePass21())
            ->renderPass("WebglMultipleElementsScene22", loadFrame, opaquePass22())
            ->renderPass("WebglMultipleElementsScene23", loadFrame, opaquePass23())
            ->renderPass("WebglMultipleElementsScene24", loadFrame, opaquePass24())
            ->renderPass("WebglMultipleElementsScene25", loadFrame, opaquePass25())
            ->renderPass("WebglMultipleElementsScene26", loadFrame, opaquePass26())
            ->renderPass("WebglMultipleElementsScene27", loadFrame, opaquePass27())
            ->renderPass("WebglMultipleElementsScene28", loadFrame, opaquePass28())
            ->renderPass("WebglMultipleElementsScene29", loadFrame, opaquePass29())
            ->renderPass("WebglMultipleElementsScene30", loadFrame, opaquePass30())
            ->renderPass("WebglMultipleElementsScene31", loadFrame, opaquePass31())
            ->renderPass("WebglMultipleElementsScene32", loadFrame, opaquePass32())
            ->renderPass("WebglMultipleElementsScene33", loadFrame, opaquePass33())
            ->renderPass("WebglMultipleElementsScene34", loadFrame, opaquePass34())
            ->renderPass("WebglMultipleElementsScene35", loadFrame, opaquePass35())
            ->renderPass("WebglMultipleElementsScene36", loadFrame, opaquePass36())
            ->renderPass("WebglMultipleElementsScene37", loadFrame, opaquePass37())
            ->renderPass("WebglMultipleElementsScene38", loadFrame, opaquePass38())
            ->renderPass("WebglMultipleElementsScene39", loadFrame, opaquePass39())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
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

    /** Releases every child RenderSet and both single-sample targets. */
    void destroy() override
    {
        sceneSet0->destroy();
        sceneSet1->destroy();
        sceneSet2->destroy();
        sceneSet3->destroy();
        sceneSet4->destroy();
        sceneSet5->destroy();
        sceneSet6->destroy();
        sceneSet7->destroy();
        sceneSet8->destroy();
        sceneSet9->destroy();
        sceneSet10->destroy();
        sceneSet11->destroy();
        sceneSet12->destroy();
        sceneSet13->destroy();
        sceneSet14->destroy();
        sceneSet15->destroy();
        sceneSet16->destroy();
        sceneSet17->destroy();
        sceneSet18->destroy();
        sceneSet19->destroy();
        sceneSet20->destroy();
        sceneSet21->destroy();
        sceneSet22->destroy();
        sceneSet23->destroy();
        sceneSet24->destroy();
        sceneSet25->destroy();
        sceneSet26->destroy();
        sceneSet27->destroy();
        sceneSet28->destroy();
        sceneSet29->destroy();
        sceneSet30->destroy();
        sceneSet31->destroy();
        sceneSet32->destroy();
        sceneSet33->destroy();
        sceneSet34->destroy();
        sceneSet35->destroy();
        sceneSet36->destroy();
        sceneSet37->destroy();
        sceneSet38->destroy();
        sceneSet39->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
