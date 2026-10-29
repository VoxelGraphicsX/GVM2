#ifndef GVM_THREE_WEBGPUMULTIPLEELEMENTS_HPP
#define GVM_THREE_WEBGPUMULTIPLEELEMENTS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebgpuMultipleElementsVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores one entity camera transform, light state, and material phase. */
struct WebgpuMultipleElementsObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 viewport;
    float4 baseColorAndFlags;
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebgpuMultipleElementsInstanceData
{
    float4 reserved;
};

/** Stores one material color and wireframe phase. */
struct WebgpuMultipleElementsMaterialData
{
    float4 baseColorAndFlags;
};

/** Defines the only RenderSet used by the orientation-transform Scene. */
struct WebgpuMultipleElementsSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebgpuMultipleElementsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuMultipleElementsObjectData> objects,
        BufferComponent<WebgpuMultipleElementsInstanceData> instances,
        BufferComponent<WebgpuMultipleElementsMaterialData> materials)
    {
    }
};

/** Carries transformed position, normal, barycentric coordinates, and entity id. */
struct WebgpuMultipleElementsVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
    float background [[Attribute3]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebgpuMultipleElementsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webgpuMultipleElementsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666667f) * 1.055f - 0.055f;
}

/** Draws opaque cone and target objects through the unique Scene RenderSet. */
class WebgpuMultipleElementsScene00Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene01Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene02Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene03Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene04Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene05Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene06Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene07Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene08Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene09Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene10Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene11Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene12Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene13Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene14Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene15Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene16Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene17Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene18Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene19Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene20Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene21Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene22Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene23Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene24Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene25Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene26Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene27Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene28Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene29Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene30Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene31Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene32Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene33Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene34Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene35Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene36Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene37Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene38Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleElementsScene39Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the card viewport or the per-scene camera transform. */
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
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

    /** Applies the flat StandardMaterial lighting and the per-card background. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        if (inputValue.background > 0.5f)
        {
            frameBuffer.color = half4(half3(0.9333333333f), half(1.0f));
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
            webgpuMultipleElementsLinearToSrgb(linearColor.x),
            webgpuMultipleElementsLinearToSrgb(linearColor.y),
            webgpuMultipleElementsLinearToSrgb(linearColor.z));
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent wireframe control sphere from the same RenderSet. */
class WebgpuMultipleElementsWireframePass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and preserves opaque color/depth. */
    constructor(RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet [[Slot0]])
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
    WebgpuMultipleElementsVertexOutput vertex(
        WebgpuMultipleElementsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleElementsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleElementsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleElementsVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates barycentric edge coverage and the locked 0.3 wireframe alpha. */
    WebgpuMultipleElementsFrameBuffer fragment(
        WebgpuMultipleElementsVertexOutput inputValue)
    {
        const WebgpuMultipleElementsObjectData objectData =
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
        WebgpuMultipleElementsFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.8f), half(0.3f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the two geometry passes. */
/** Owns one RenderSet per independent DOM Scene and the matching scene passes. */
class WebgpuMultipleElementsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet0;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet1;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet2;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet3;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet4;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet5;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet6;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet7;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet8;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet9;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet10;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet11;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet12;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet13;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet14;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet15;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet16;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet17;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet18;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet19;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet20;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet21;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet22;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet23;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet24;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet25;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet26;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet27;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet28;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet29;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet30;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet31;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet32;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet33;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet34;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet35;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet36;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet37;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet38;
    [[Export]] RenderSet<WebgpuMultipleElementsSceneRenderSet> sceneSet39;
    RenderClass<WebgpuMultipleElementsScene00Pass> opaquePass0;
    RenderClass<WebgpuMultipleElementsScene01Pass> opaquePass1;
    RenderClass<WebgpuMultipleElementsScene02Pass> opaquePass2;
    RenderClass<WebgpuMultipleElementsScene03Pass> opaquePass3;
    RenderClass<WebgpuMultipleElementsScene04Pass> opaquePass4;
    RenderClass<WebgpuMultipleElementsScene05Pass> opaquePass5;
    RenderClass<WebgpuMultipleElementsScene06Pass> opaquePass6;
    RenderClass<WebgpuMultipleElementsScene07Pass> opaquePass7;
    RenderClass<WebgpuMultipleElementsScene08Pass> opaquePass8;
    RenderClass<WebgpuMultipleElementsScene09Pass> opaquePass9;
    RenderClass<WebgpuMultipleElementsScene10Pass> opaquePass10;
    RenderClass<WebgpuMultipleElementsScene11Pass> opaquePass11;
    RenderClass<WebgpuMultipleElementsScene12Pass> opaquePass12;
    RenderClass<WebgpuMultipleElementsScene13Pass> opaquePass13;
    RenderClass<WebgpuMultipleElementsScene14Pass> opaquePass14;
    RenderClass<WebgpuMultipleElementsScene15Pass> opaquePass15;
    RenderClass<WebgpuMultipleElementsScene16Pass> opaquePass16;
    RenderClass<WebgpuMultipleElementsScene17Pass> opaquePass17;
    RenderClass<WebgpuMultipleElementsScene18Pass> opaquePass18;
    RenderClass<WebgpuMultipleElementsScene19Pass> opaquePass19;
    RenderClass<WebgpuMultipleElementsScene20Pass> opaquePass20;
    RenderClass<WebgpuMultipleElementsScene21Pass> opaquePass21;
    RenderClass<WebgpuMultipleElementsScene22Pass> opaquePass22;
    RenderClass<WebgpuMultipleElementsScene23Pass> opaquePass23;
    RenderClass<WebgpuMultipleElementsScene24Pass> opaquePass24;
    RenderClass<WebgpuMultipleElementsScene25Pass> opaquePass25;
    RenderClass<WebgpuMultipleElementsScene26Pass> opaquePass26;
    RenderClass<WebgpuMultipleElementsScene27Pass> opaquePass27;
    RenderClass<WebgpuMultipleElementsScene28Pass> opaquePass28;
    RenderClass<WebgpuMultipleElementsScene29Pass> opaquePass29;
    RenderClass<WebgpuMultipleElementsScene30Pass> opaquePass30;
    RenderClass<WebgpuMultipleElementsScene31Pass> opaquePass31;
    RenderClass<WebgpuMultipleElementsScene32Pass> opaquePass32;
    RenderClass<WebgpuMultipleElementsScene33Pass> opaquePass33;
    RenderClass<WebgpuMultipleElementsScene34Pass> opaquePass34;
    RenderClass<WebgpuMultipleElementsScene35Pass> opaquePass35;
    RenderClass<WebgpuMultipleElementsScene36Pass> opaquePass36;
    RenderClass<WebgpuMultipleElementsScene37Pass> opaquePass37;
    RenderClass<WebgpuMultipleElementsScene38Pass> opaquePass38;
    RenderClass<WebgpuMultipleElementsScene39Pass> opaquePass39;
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
        sceneSet0 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet1 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet2 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet3 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet4 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet5 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet6 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet7 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet8 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet9 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet10 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet11 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet12 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet13 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet14 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet15 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet16 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet17 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet18 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet19 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet20 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet21 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet22 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet23 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet24 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet25 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet26 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet27 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet28 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet29 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet30 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet31 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet32 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet33 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet34 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet35 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet36 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet37 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet38 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        sceneSet39 = device->createRenderSet<WebgpuMultipleElementsSceneRenderSet>();
        opaquePass0 = device->createRenderClass<WebgpuMultipleElementsScene00Pass>(sceneSet0);
        opaquePass1 = device->createRenderClass<WebgpuMultipleElementsScene01Pass>(sceneSet1);
        opaquePass2 = device->createRenderClass<WebgpuMultipleElementsScene02Pass>(sceneSet2);
        opaquePass3 = device->createRenderClass<WebgpuMultipleElementsScene03Pass>(sceneSet3);
        opaquePass4 = device->createRenderClass<WebgpuMultipleElementsScene04Pass>(sceneSet4);
        opaquePass5 = device->createRenderClass<WebgpuMultipleElementsScene05Pass>(sceneSet5);
        opaquePass6 = device->createRenderClass<WebgpuMultipleElementsScene06Pass>(sceneSet6);
        opaquePass7 = device->createRenderClass<WebgpuMultipleElementsScene07Pass>(sceneSet7);
        opaquePass8 = device->createRenderClass<WebgpuMultipleElementsScene08Pass>(sceneSet8);
        opaquePass9 = device->createRenderClass<WebgpuMultipleElementsScene09Pass>(sceneSet9);
        opaquePass10 = device->createRenderClass<WebgpuMultipleElementsScene10Pass>(sceneSet10);
        opaquePass11 = device->createRenderClass<WebgpuMultipleElementsScene11Pass>(sceneSet11);
        opaquePass12 = device->createRenderClass<WebgpuMultipleElementsScene12Pass>(sceneSet12);
        opaquePass13 = device->createRenderClass<WebgpuMultipleElementsScene13Pass>(sceneSet13);
        opaquePass14 = device->createRenderClass<WebgpuMultipleElementsScene14Pass>(sceneSet14);
        opaquePass15 = device->createRenderClass<WebgpuMultipleElementsScene15Pass>(sceneSet15);
        opaquePass16 = device->createRenderClass<WebgpuMultipleElementsScene16Pass>(sceneSet16);
        opaquePass17 = device->createRenderClass<WebgpuMultipleElementsScene17Pass>(sceneSet17);
        opaquePass18 = device->createRenderClass<WebgpuMultipleElementsScene18Pass>(sceneSet18);
        opaquePass19 = device->createRenderClass<WebgpuMultipleElementsScene19Pass>(sceneSet19);
        opaquePass20 = device->createRenderClass<WebgpuMultipleElementsScene20Pass>(sceneSet20);
        opaquePass21 = device->createRenderClass<WebgpuMultipleElementsScene21Pass>(sceneSet21);
        opaquePass22 = device->createRenderClass<WebgpuMultipleElementsScene22Pass>(sceneSet22);
        opaquePass23 = device->createRenderClass<WebgpuMultipleElementsScene23Pass>(sceneSet23);
        opaquePass24 = device->createRenderClass<WebgpuMultipleElementsScene24Pass>(sceneSet24);
        opaquePass25 = device->createRenderClass<WebgpuMultipleElementsScene25Pass>(sceneSet25);
        opaquePass26 = device->createRenderClass<WebgpuMultipleElementsScene26Pass>(sceneSet26);
        opaquePass27 = device->createRenderClass<WebgpuMultipleElementsScene27Pass>(sceneSet27);
        opaquePass28 = device->createRenderClass<WebgpuMultipleElementsScene28Pass>(sceneSet28);
        opaquePass29 = device->createRenderClass<WebgpuMultipleElementsScene29Pass>(sceneSet29);
        opaquePass30 = device->createRenderClass<WebgpuMultipleElementsScene30Pass>(sceneSet30);
        opaquePass31 = device->createRenderClass<WebgpuMultipleElementsScene31Pass>(sceneSet31);
        opaquePass32 = device->createRenderClass<WebgpuMultipleElementsScene32Pass>(sceneSet32);
        opaquePass33 = device->createRenderClass<WebgpuMultipleElementsScene33Pass>(sceneSet33);
        opaquePass34 = device->createRenderClass<WebgpuMultipleElementsScene34Pass>(sceneSet34);
        opaquePass35 = device->createRenderClass<WebgpuMultipleElementsScene35Pass>(sceneSet35);
        opaquePass36 = device->createRenderClass<WebgpuMultipleElementsScene36Pass>(sceneSet36);
        opaquePass37 = device->createRenderClass<WebgpuMultipleElementsScene37Pass>(sceneSet37);
        opaquePass38 = device->createRenderClass<WebgpuMultipleElementsScene38Pass>(sceneSet38);
        opaquePass39 = device->createRenderClass<WebgpuMultipleElementsScene39Pass>(sceneSet39);
    }

    /** Allocates explicit single-sample color and depth targets for all child scenes. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebgpuMultipleElementsColor", width, height, 1u);
        outputDepth = device->createTexture("WebgpuMultipleElementsDepth", width, height, 1u);
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
        WebgpuMultipleElementsFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue = {1.0f, 1.0f, 1.0f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebgpuMultipleElementsFrameBuffer loadFrame;
        loadFrame.color = outputColor->createView();
        loadFrame.color.loadOp = LoadOp::Load;
        loadFrame.color.storeOp = StoreOp::Store;
        loadFrame.depth = outputDepth->createView();
        loadFrame.depth.depthLoadOp = LoadOp::Load;
        loadFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuMultipleElementsScene0", opaqueFrame, opaquePass0())
            ->renderPass("WebgpuMultipleElementsScene1", loadFrame, opaquePass1())
            ->renderPass("WebgpuMultipleElementsScene2", loadFrame, opaquePass2())
            ->renderPass("WebgpuMultipleElementsScene3", loadFrame, opaquePass3())
            ->renderPass("WebgpuMultipleElementsScene4", loadFrame, opaquePass4())
            ->renderPass("WebgpuMultipleElementsScene5", loadFrame, opaquePass5())
            ->renderPass("WebgpuMultipleElementsScene6", loadFrame, opaquePass6())
            ->renderPass("WebgpuMultipleElementsScene7", loadFrame, opaquePass7())
            ->renderPass("WebgpuMultipleElementsScene8", loadFrame, opaquePass8())
            ->renderPass("WebgpuMultipleElementsScene9", loadFrame, opaquePass9())
            ->renderPass("WebgpuMultipleElementsScene10", loadFrame, opaquePass10())
            ->renderPass("WebgpuMultipleElementsScene11", loadFrame, opaquePass11())
            ->renderPass("WebgpuMultipleElementsScene12", loadFrame, opaquePass12())
            ->renderPass("WebgpuMultipleElementsScene13", loadFrame, opaquePass13())
            ->renderPass("WebgpuMultipleElementsScene14", loadFrame, opaquePass14())
            ->renderPass("WebgpuMultipleElementsScene15", loadFrame, opaquePass15())
            ->renderPass("WebgpuMultipleElementsScene16", loadFrame, opaquePass16())
            ->renderPass("WebgpuMultipleElementsScene17", loadFrame, opaquePass17())
            ->renderPass("WebgpuMultipleElementsScene18", loadFrame, opaquePass18())
            ->renderPass("WebgpuMultipleElementsScene19", loadFrame, opaquePass19())
            ->renderPass("WebgpuMultipleElementsScene20", loadFrame, opaquePass20())
            ->renderPass("WebgpuMultipleElementsScene21", loadFrame, opaquePass21())
            ->renderPass("WebgpuMultipleElementsScene22", loadFrame, opaquePass22())
            ->renderPass("WebgpuMultipleElementsScene23", loadFrame, opaquePass23())
            ->renderPass("WebgpuMultipleElementsScene24", loadFrame, opaquePass24())
            ->renderPass("WebgpuMultipleElementsScene25", loadFrame, opaquePass25())
            ->renderPass("WebgpuMultipleElementsScene26", loadFrame, opaquePass26())
            ->renderPass("WebgpuMultipleElementsScene27", loadFrame, opaquePass27())
            ->renderPass("WebgpuMultipleElementsScene28", loadFrame, opaquePass28())
            ->renderPass("WebgpuMultipleElementsScene29", loadFrame, opaquePass29())
            ->renderPass("WebgpuMultipleElementsScene30", loadFrame, opaquePass30())
            ->renderPass("WebgpuMultipleElementsScene31", loadFrame, opaquePass31())
            ->renderPass("WebgpuMultipleElementsScene32", loadFrame, opaquePass32())
            ->renderPass("WebgpuMultipleElementsScene33", loadFrame, opaquePass33())
            ->renderPass("WebgpuMultipleElementsScene34", loadFrame, opaquePass34())
            ->renderPass("WebgpuMultipleElementsScene35", loadFrame, opaquePass35())
            ->renderPass("WebgpuMultipleElementsScene36", loadFrame, opaquePass36())
            ->renderPass("WebgpuMultipleElementsScene37", loadFrame, opaquePass37())
            ->renderPass("WebgpuMultipleElementsScene38", loadFrame, opaquePass38())
            ->renderPass("WebgpuMultipleElementsScene39", loadFrame, opaquePass39())
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
