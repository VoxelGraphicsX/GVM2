#ifndef GVM_THREE_WEBGPUMULTIPLECANVAS_HPP
#define GVM_THREE_WEBGPUMULTIPLECANVAS_HPP

#include "UGL.h"
#include "WebgpuMultipleCanvasData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebgpuMultipleCanvasVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores one entity camera transform, light state, and material phase. */
struct WebgpuMultipleCanvasObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 viewport;
    float4 baseColorAndFlags;
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebgpuMultipleCanvasInstanceData
{
    float4 reserved;
};

/** Stores one material color and wireframe phase. */
struct WebgpuMultipleCanvasMaterialData
{
    float4 baseColorAndFlags;
};

/** Defines the only RenderSet used by the orientation-transform Scene. */
struct WebgpuMultipleCanvasGroupedSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebgpuMultipleCanvasVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuMultipleCanvasObjectData> objects,
        BufferComponent<WebgpuMultipleCanvasInstanceData> instances,
        BufferComponent<WebgpuMultipleCanvasMaterialData> materials)
    {
    }
};

/** Carries transformed position, normal, barycentric coordinates, and entity id. */
struct WebgpuMultipleCanvasVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebgpuMultipleCanvasFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Carries a CPU-composed simple Scene vertex into the standalone pass. */
struct WebgpuMultipleCanvasSimpleVertexOutput
{
    float4 position [[Position]];
    float3 normal [[Attribute0]];
    float3 color [[Attribute1]];
    float background [[Attribute2]];
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webgpuMultipleCanvasLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Forwards one CPU-composed standalone vertex to the simple Scene shader. */
WebgpuMultipleCanvasSimpleVertexOutput webgpuMultipleCanvasSimpleVertex(
    WebgpuMultipleCanvasSimpleVertex inputValue)
{
    WebgpuMultipleCanvasSimpleVertexOutput outputValue;
    outputValue.position = inputValue.position;
    // UGLC applies its canonical clip-space Y conversion after the vertex
    // function.  Cancel it here because the CPU composer already emitted the
    // final page-composite coordinates for this ordinary RenderClass path.
    outputValue.position.y = -outputValue.position.y;
    outputValue.normal = inputValue.normalAndFlags.xyz;
    outputValue.color = inputValue.colorAndFlags.xyz;
    outputValue.background = inputValue.colorAndFlags.w;
    return outputValue;
}

/** Shades one simple canvas vertex using the grouped Scene's lighting equation. */
float4 webgpuMultipleCanvasSimpleColor(
    WebgpuMultipleCanvasSimpleVertexOutput inputValue)
{
    const float3 normal = normalize(inputValue.normal);
    const float3 skyIrradiance = float3(0.401978f);
    const float3 groundIrradiance = float3(0.057805f);
    const float hemisphereWeight = normal.y * 0.5f + 0.5f;
    const float3 hemisphere = (groundIrradiance +
        (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
    const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
    const float3 linearColor = inputValue.background > 0.5f
        ? float3(0.854992f)
        : inputValue.color * (hemisphere + float3(directional)) / 3.14159265f;
    const float3 srgb = float3(
        webgpuMultipleCanvasLinearToSrgb(linearColor.x),
        webgpuMultipleCanvasLinearToSrgb(linearColor.y),
        webgpuMultipleCanvasLinearToSrgb(linearColor.z));
    return float4(srgb, 1.0f);
}

/** Declares one ordinary standalone Scene pass for a canvas with one renderable. */
class WebgpuMultipleCanvasSimpleScene1Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene2Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene3Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene4Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene6Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene8Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene14Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene18Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene19Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene21Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene22Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene25Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene26Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene28Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene29Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene32Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene33Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene34Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene37Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene39Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};
class WebgpuMultipleCanvasSimpleScene40Pass final : public IRenderClass
{
public:
    /** Configures the opaque single-sample simple Scene state. */
    constructor() { setCullMode(CullMode::None); setDepthWriteEnabled(true); setDepthCompareFunction(CompareFunction::LessEqual); }
private:
    /** Forwards the simple vertex attributes. */
    WebgpuMultipleCanvasSimpleVertexOutput vertex(WebgpuMultipleCanvasSimpleVertex inputValue [[VertexInput0]]) { return webgpuMultipleCanvasSimpleVertex(inputValue); }
    /** Shades the simple canvas geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(WebgpuMultipleCanvasSimpleVertexOutput inputValue) { WebgpuMultipleCanvasFrameBuffer frameBuffer; frameBuffer.color = half4(webgpuMultipleCanvasSimpleColor(inputValue)); return frameBuffer; }
};

/** Draws opaque cone and target objects through the unique Scene RenderSet. */
class WebgpuMultipleCanvasScene1Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene2Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene3Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene4Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene5Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene6Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene7Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene8Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene9Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene10Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene11Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene12Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene13Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene14Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene15Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene16Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene17Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene18Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene19Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene20Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene21Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene22Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene23Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene24Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene25Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene26Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene27Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene28Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene29Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene30Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene31Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene32Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene33Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene34Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene35Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene36Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene37Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene38Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene39Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuMultipleCanvasScene40Pass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float2 viewUv = outputValue.position.xy /
            outputValue.position.w * 0.5f + float2(0.5f);
        const float2 compositeUv = objectData.viewport.xy +
            viewUv * objectData.viewport.zw;
        outputValue.position.xy = (compositeUv * 2.0f - float2(1.0f)) * outputValue.position.w;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMultipleCanvasMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const bool isCanvasBackground = inputValue.barycentric.x < -0.5f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 skyIrradiance = float3(0.401978f);
        const float3 groundIrradiance = float3(0.057805f);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = (groundIrradiance +
            (skyIrradiance - groundIrradiance) * hemisphereWeight) * 3.0f;
        const float directional = max(dot(normal, normalize(float3(1.0f))), 0.0f) * 1.5f;
        const float3 litMaterial = materialData.baseColorAndFlags.xyz *
            (hemisphere + float3(directional)) / 3.14159265f;
        const float3 normalColor = isCanvasBackground
            ? float3(0.854992f)
            : litMaterial;
        const float3 srgb = float3(
            webgpuMultipleCanvasLinearToSrgb(normalColor.x),
            webgpuMultipleCanvasLinearToSrgb(normalColor.y),
            webgpuMultipleCanvasLinearToSrgb(normalColor.z));
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent wireframe control sphere from the same RenderSet. */
class WebgpuMultipleCanvasWireframePass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and preserves opaque color/depth. */
    constructor(RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet [[Slot0]])
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
    WebgpuMultipleCanvasVertexOutput vertex(
        WebgpuMultipleCanvasVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMultipleCanvasObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMultipleCanvasInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuMultipleCanvasVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates barycentric edge coverage and the locked 0.3 wireframe alpha. */
    WebgpuMultipleCanvasFrameBuffer fragment(
        WebgpuMultipleCanvasVertexOutput inputValue)
    {
        const WebgpuMultipleCanvasObjectData objectData =
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
        WebgpuMultipleCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.8f), half(0.3f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the two geometry passes. */
/** Owns one RenderSet per independent canvas Scene and deterministic viewport passes. */
class WebgpuMultipleCanvasRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet0;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet1;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet2;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet3;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet4;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet5;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet6;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet7;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet8;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet9;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet10;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet11;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet12;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet13;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet14;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet15;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet16;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet17;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet18;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet19;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet20;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet21;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet22;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet23;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet24;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet25;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet26;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet27;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet28;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet29;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet30;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet31;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet32;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet33;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet34;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet35;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet36;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet37;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet38;
    [[Export]] RenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet> sceneSet39;
    RenderClass<WebgpuMultipleCanvasScene1Pass> opaquePass0;
    RenderClass<WebgpuMultipleCanvasScene2Pass> opaquePass1;
    RenderClass<WebgpuMultipleCanvasScene3Pass> opaquePass2;
    RenderClass<WebgpuMultipleCanvasScene4Pass> opaquePass3;
    RenderClass<WebgpuMultipleCanvasScene5Pass> opaquePass4;
    RenderClass<WebgpuMultipleCanvasScene6Pass> opaquePass5;
    RenderClass<WebgpuMultipleCanvasScene7Pass> opaquePass6;
    RenderClass<WebgpuMultipleCanvasScene8Pass> opaquePass7;
    RenderClass<WebgpuMultipleCanvasScene9Pass> opaquePass8;
    RenderClass<WebgpuMultipleCanvasScene10Pass> opaquePass9;
    RenderClass<WebgpuMultipleCanvasScene11Pass> opaquePass10;
    RenderClass<WebgpuMultipleCanvasScene12Pass> opaquePass11;
    RenderClass<WebgpuMultipleCanvasScene13Pass> opaquePass12;
    RenderClass<WebgpuMultipleCanvasScene14Pass> opaquePass13;
    RenderClass<WebgpuMultipleCanvasScene15Pass> opaquePass14;
    RenderClass<WebgpuMultipleCanvasScene16Pass> opaquePass15;
    RenderClass<WebgpuMultipleCanvasScene17Pass> opaquePass16;
    RenderClass<WebgpuMultipleCanvasScene18Pass> opaquePass17;
    RenderClass<WebgpuMultipleCanvasScene19Pass> opaquePass18;
    RenderClass<WebgpuMultipleCanvasScene20Pass> opaquePass19;
    RenderClass<WebgpuMultipleCanvasScene21Pass> opaquePass20;
    RenderClass<WebgpuMultipleCanvasScene22Pass> opaquePass21;
    RenderClass<WebgpuMultipleCanvasScene23Pass> opaquePass22;
    RenderClass<WebgpuMultipleCanvasScene24Pass> opaquePass23;
    RenderClass<WebgpuMultipleCanvasScene25Pass> opaquePass24;
    RenderClass<WebgpuMultipleCanvasScene26Pass> opaquePass25;
    RenderClass<WebgpuMultipleCanvasScene27Pass> opaquePass26;
    RenderClass<WebgpuMultipleCanvasScene28Pass> opaquePass27;
    RenderClass<WebgpuMultipleCanvasScene29Pass> opaquePass28;
    RenderClass<WebgpuMultipleCanvasScene30Pass> opaquePass29;
    RenderClass<WebgpuMultipleCanvasScene31Pass> opaquePass30;
    RenderClass<WebgpuMultipleCanvasScene32Pass> opaquePass31;
    RenderClass<WebgpuMultipleCanvasScene33Pass> opaquePass32;
    RenderClass<WebgpuMultipleCanvasScene34Pass> opaquePass33;
    RenderClass<WebgpuMultipleCanvasScene35Pass> opaquePass34;
    RenderClass<WebgpuMultipleCanvasScene36Pass> opaquePass35;
    RenderClass<WebgpuMultipleCanvasScene37Pass> opaquePass36;
    RenderClass<WebgpuMultipleCanvasScene38Pass> opaquePass37;
    RenderClass<WebgpuMultipleCanvasScene39Pass> opaquePass38;
    RenderClass<WebgpuMultipleCanvasScene40Pass> opaquePass39;
    Buffer<WebgpuMultipleCanvasSimpleVertex, BufferUsage<Vertex, CopyDst>> simpleVertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> simpleIndexBuffer;
    RenderClass<WebgpuMultipleCanvasSimpleScene1Pass> simplePass1;
    RenderClass<WebgpuMultipleCanvasSimpleScene2Pass> simplePass2;
    RenderClass<WebgpuMultipleCanvasSimpleScene3Pass> simplePass3;
    RenderClass<WebgpuMultipleCanvasSimpleScene4Pass> simplePass4;
    RenderClass<WebgpuMultipleCanvasSimpleScene6Pass> simplePass6;
    RenderClass<WebgpuMultipleCanvasSimpleScene8Pass> simplePass8;
    RenderClass<WebgpuMultipleCanvasSimpleScene14Pass> simplePass14;
    RenderClass<WebgpuMultipleCanvasSimpleScene18Pass> simplePass18;
    RenderClass<WebgpuMultipleCanvasSimpleScene19Pass> simplePass19;
    RenderClass<WebgpuMultipleCanvasSimpleScene21Pass> simplePass21;
    RenderClass<WebgpuMultipleCanvasSimpleScene22Pass> simplePass22;
    RenderClass<WebgpuMultipleCanvasSimpleScene25Pass> simplePass25;
    RenderClass<WebgpuMultipleCanvasSimpleScene26Pass> simplePass26;
    RenderClass<WebgpuMultipleCanvasSimpleScene28Pass> simplePass28;
    RenderClass<WebgpuMultipleCanvasSimpleScene29Pass> simplePass29;
    RenderClass<WebgpuMultipleCanvasSimpleScene32Pass> simplePass32;
    RenderClass<WebgpuMultipleCanvasSimpleScene33Pass> simplePass33;
    RenderClass<WebgpuMultipleCanvasSimpleScene34Pass> simplePass34;
    RenderClass<WebgpuMultipleCanvasSimpleScene37Pass> simplePass37;
    RenderClass<WebgpuMultipleCanvasSimpleScene39Pass> simplePass39;
    RenderClass<WebgpuMultipleCanvasSimpleScene40Pass> simplePass40;
    uint simpleFirstIndex1 = 0u;
    uint simpleFirstIndex2 = 0u;
    uint simpleFirstIndex3 = 0u;
    uint simpleFirstIndex4 = 0u;
    uint simpleFirstIndex6 = 0u;
    uint simpleFirstIndex8 = 0u;
    uint simpleFirstIndex14 = 0u;
    uint simpleFirstIndex18 = 0u;
    uint simpleFirstIndex19 = 0u;
    uint simpleFirstIndex21 = 0u;
    uint simpleFirstIndex22 = 0u;
    uint simpleFirstIndex25 = 0u;
    uint simpleFirstIndex26 = 0u;
    uint simpleFirstIndex28 = 0u;
    uint simpleFirstIndex29 = 0u;
    uint simpleFirstIndex32 = 0u;
    uint simpleFirstIndex33 = 0u;
    uint simpleFirstIndex34 = 0u;
    uint simpleFirstIndex37 = 0u;
    uint simpleFirstIndex39 = 0u;
    uint simpleFirstIndex40 = 0u;
    uint simpleIndexCount1 = 0u;
    uint simpleIndexCount2 = 0u;
    uint simpleIndexCount3 = 0u;
    uint simpleIndexCount4 = 0u;
    uint simpleIndexCount6 = 0u;
    uint simpleIndexCount8 = 0u;
    uint simpleIndexCount14 = 0u;
    uint simpleIndexCount18 = 0u;
    uint simpleIndexCount19 = 0u;
    uint simpleIndexCount21 = 0u;
    uint simpleIndexCount22 = 0u;
    uint simpleIndexCount25 = 0u;
    uint simpleIndexCount26 = 0u;
    uint simpleIndexCount28 = 0u;
    uint simpleIndexCount29 = 0u;
    uint simpleIndexCount32 = 0u;
    uint simpleIndexCount33 = 0u;
    uint simpleIndexCount34 = 0u;
    uint simpleIndexCount37 = 0u;
    uint simpleIndexCount39 = 0u;
    uint simpleIndexCount40 = 0u;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates one RenderSet for each independent canvas scene. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet4 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet6 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet8 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet9 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet10 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet11 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet12 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet14 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet15 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet16 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet19 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet22 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet23 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet26 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet29 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet30 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet34 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet35 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        sceneSet37 = device->createRenderSet<WebgpuMultipleCanvasGroupedSceneRenderSet>();
        opaquePass4 = device->createRenderClass<WebgpuMultipleCanvasScene5Pass>(sceneSet4);
        opaquePass6 = device->createRenderClass<WebgpuMultipleCanvasScene7Pass>(sceneSet6);
        opaquePass8 = device->createRenderClass<WebgpuMultipleCanvasScene9Pass>(sceneSet8);
        opaquePass9 = device->createRenderClass<WebgpuMultipleCanvasScene10Pass>(sceneSet9);
        opaquePass10 = device->createRenderClass<WebgpuMultipleCanvasScene11Pass>(sceneSet10);
        opaquePass11 = device->createRenderClass<WebgpuMultipleCanvasScene12Pass>(sceneSet11);
        opaquePass12 = device->createRenderClass<WebgpuMultipleCanvasScene13Pass>(sceneSet12);
        opaquePass14 = device->createRenderClass<WebgpuMultipleCanvasScene15Pass>(sceneSet14);
        opaquePass15 = device->createRenderClass<WebgpuMultipleCanvasScene16Pass>(sceneSet15);
        opaquePass16 = device->createRenderClass<WebgpuMultipleCanvasScene17Pass>(sceneSet16);
        opaquePass19 = device->createRenderClass<WebgpuMultipleCanvasScene20Pass>(sceneSet19);
        opaquePass22 = device->createRenderClass<WebgpuMultipleCanvasScene23Pass>(sceneSet22);
        opaquePass23 = device->createRenderClass<WebgpuMultipleCanvasScene24Pass>(sceneSet23);
        opaquePass26 = device->createRenderClass<WebgpuMultipleCanvasScene27Pass>(sceneSet26);
        opaquePass29 = device->createRenderClass<WebgpuMultipleCanvasScene30Pass>(sceneSet29);
        opaquePass30 = device->createRenderClass<WebgpuMultipleCanvasScene31Pass>(sceneSet30);
        opaquePass34 = device->createRenderClass<WebgpuMultipleCanvasScene35Pass>(sceneSet34);
        opaquePass35 = device->createRenderClass<WebgpuMultipleCanvasScene36Pass>(sceneSet35);
        opaquePass37 = device->createRenderClass<WebgpuMultipleCanvasScene38Pass>(sceneSet37);
        simplePass1 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene1Pass>();
        simplePass2 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene2Pass>();
        simplePass3 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene3Pass>();
        simplePass4 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene4Pass>();
        simplePass6 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene6Pass>();
        simplePass8 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene8Pass>();
        simplePass14 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene14Pass>();
        simplePass18 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene18Pass>();
        simplePass19 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene19Pass>();
        simplePass21 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene21Pass>();
        simplePass22 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene22Pass>();
        simplePass25 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene25Pass>();
        simplePass26 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene26Pass>();
        simplePass28 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene28Pass>();
        simplePass29 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene29Pass>();
        simplePass32 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene32Pass>();
        simplePass33 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene33Pass>();
        simplePass34 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene34Pass>();
        simplePass37 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene37Pass>();
        simplePass39 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene39Pass>();
        simplePass40 = device->createRenderClass<WebgpuMultipleCanvasSimpleScene40Pass>();
    }

    /** Allocates the explicit single-sample canvas composite targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebgpuMultipleCanvasColor", width, height, 1u);
        outputDepth = device->createTexture("WebgpuMultipleCanvasDepth", width, height, 1u);
    }

    /** Uploads the packed standalone geometry used by the simple canvas Scenes. */
    void configureSimpleGeometry(
        const eastl::vector<WebgpuMultipleCanvasSimpleVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<uint> &firstIndices,
        const eastl::vector<uint> &indexCounts)
    {
        simpleVertexBuffer = device->createBuffer(
            "WebgpuMultipleCanvasSimpleVertices",
            max(1u, uint(vertices.size())));
        simpleIndexBuffer = device->createBuffer(
            "WebgpuMultipleCanvasSimpleIndices",
            max(1u, uint(indices.size())));
        if (!vertices.empty())
        {
            graphicsQueue->writeBuffer(
                BufferRange(simpleVertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) * sizeof(WebgpuMultipleCanvasSimpleVertex));
        }
        if (!indices.empty())
        {
            graphicsQueue->writeBuffer(
                BufferRange(simpleIndexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint));
        }
        graphicsQueue->submit();
        simpleFirstIndex1 = firstIndices[0u];
        simpleFirstIndex2 = firstIndices[1u];
        simpleFirstIndex3 = firstIndices[2u];
        simpleFirstIndex4 = firstIndices[3u];
        simpleFirstIndex6 = firstIndices[4u];
        simpleFirstIndex8 = firstIndices[5u];
        simpleFirstIndex14 = firstIndices[6u];
        simpleFirstIndex18 = firstIndices[7u];
        simpleFirstIndex19 = firstIndices[8u];
        simpleFirstIndex21 = firstIndices[9u];
        simpleFirstIndex22 = firstIndices[10u];
        simpleFirstIndex25 = firstIndices[11u];
        simpleFirstIndex26 = firstIndices[12u];
        simpleFirstIndex28 = firstIndices[13u];
        simpleFirstIndex29 = firstIndices[14u];
        simpleFirstIndex32 = firstIndices[15u];
        simpleFirstIndex33 = firstIndices[16u];
        simpleFirstIndex34 = firstIndices[17u];
        simpleFirstIndex37 = firstIndices[18u];
        simpleFirstIndex39 = firstIndices[19u];
        simpleFirstIndex40 = firstIndices[20u];
        simpleIndexCount1 = indexCounts[0u];
        simpleIndexCount2 = indexCounts[1u];
        simpleIndexCount3 = indexCounts[2u];
        simpleIndexCount4 = indexCounts[3u];
        simpleIndexCount6 = indexCounts[4u];
        simpleIndexCount8 = indexCounts[5u];
        simpleIndexCount14 = indexCounts[6u];
        simpleIndexCount18 = indexCounts[7u];
        simpleIndexCount19 = indexCounts[8u];
        simpleIndexCount21 = indexCounts[9u];
        simpleIndexCount22 = indexCounts[10u];
        simpleIndexCount25 = indexCounts[11u];
        simpleIndexCount26 = indexCounts[12u];
        simpleIndexCount28 = indexCounts[13u];
        simpleIndexCount29 = indexCounts[14u];
        simpleIndexCount32 = indexCounts[15u];
        simpleIndexCount33 = indexCounts[16u];
        simpleIndexCount34 = indexCounts[17u];
        simpleIndexCount37 = indexCounts[18u];
        simpleIndexCount39 = indexCounts[19u];
        simpleIndexCount40 = indexCounts[20u];
    }

    /** Renders every canvas Scene while preserving its own RenderSet boundary. */
    void render() override
    {
        sceneSet4->update();
        sceneSet6->update();
        sceneSet8->update();
        sceneSet9->update();
        sceneSet10->update();
        sceneSet11->update();
        sceneSet12->update();
        sceneSet14->update();
        sceneSet15->update();
        sceneSet16->update();
        sceneSet19->update();
        sceneSet22->update();
        sceneSet23->update();
        sceneSet26->update();
        sceneSet29->update();
        sceneSet30->update();
        sceneSet34->update();
        sceneSet35->update();
        sceneSet37->update();
        WebgpuMultipleCanvasFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue = {1.0f, 1.0f, 1.0f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebgpuMultipleCanvasFrameBuffer loadFrame;
        loadFrame.color = outputColor->createView();
        loadFrame.color.loadOp = LoadOp::Load;
        loadFrame.color.storeOp = StoreOp::Store;
        loadFrame.depth = outputDepth->createView();
        loadFrame.depth.depthLoadOp = LoadOp::Load;
        loadFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuMultipleCanvasScene0", opaqueFrame,
                         simplePass1->setVertexBuffer(simpleVertexBuffer),
                         simplePass1->setIndexBuffer(simpleIndexBuffer),
                         simplePass1(simpleIndexCount1, 1u, simpleFirstIndex1, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene1", loadFrame,
                         simplePass2->setVertexBuffer(simpleVertexBuffer),
                         simplePass2->setIndexBuffer(simpleIndexBuffer),
                         simplePass2(simpleIndexCount2, 1u, simpleFirstIndex2, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene2", loadFrame,
                         simplePass3->setVertexBuffer(simpleVertexBuffer),
                         simplePass3->setIndexBuffer(simpleIndexBuffer),
                         simplePass3(simpleIndexCount3, 1u, simpleFirstIndex3, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene3", loadFrame,
                         simplePass4->setVertexBuffer(simpleVertexBuffer),
                         simplePass4->setIndexBuffer(simpleIndexBuffer),
                         simplePass4(simpleIndexCount4, 1u, simpleFirstIndex4, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene4", loadFrame, opaquePass4())
            ->renderPass("WebgpuMultipleCanvasScene5", loadFrame,
                         simplePass6->setVertexBuffer(simpleVertexBuffer),
                         simplePass6->setIndexBuffer(simpleIndexBuffer),
                         simplePass6(simpleIndexCount6, 1u, simpleFirstIndex6, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene6", loadFrame, opaquePass6())
            ->renderPass("WebgpuMultipleCanvasScene7", loadFrame,
                         simplePass8->setVertexBuffer(simpleVertexBuffer),
                         simplePass8->setIndexBuffer(simpleIndexBuffer),
                         simplePass8(simpleIndexCount8, 1u, simpleFirstIndex8, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene8", loadFrame, opaquePass8())
            ->renderPass("WebgpuMultipleCanvasScene9", loadFrame, opaquePass9())
            ->renderPass("WebgpuMultipleCanvasScene10", loadFrame, opaquePass10())
            ->renderPass("WebgpuMultipleCanvasScene11", loadFrame, opaquePass11())
            ->renderPass("WebgpuMultipleCanvasScene12", loadFrame, opaquePass12())
            ->renderPass("WebgpuMultipleCanvasScene13", loadFrame,
                         simplePass14->setVertexBuffer(simpleVertexBuffer),
                         simplePass14->setIndexBuffer(simpleIndexBuffer),
                         simplePass14(simpleIndexCount14, 1u, simpleFirstIndex14, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene14", loadFrame, opaquePass14())
            ->renderPass("WebgpuMultipleCanvasScene15", loadFrame, opaquePass15())
            ->renderPass("WebgpuMultipleCanvasScene16", loadFrame, opaquePass16())
            ->renderPass("WebgpuMultipleCanvasScene17", loadFrame,
                         simplePass18->setVertexBuffer(simpleVertexBuffer),
                         simplePass18->setIndexBuffer(simpleIndexBuffer),
                         simplePass18(simpleIndexCount18, 1u, simpleFirstIndex18, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene18", loadFrame,
                         simplePass19->setVertexBuffer(simpleVertexBuffer),
                         simplePass19->setIndexBuffer(simpleIndexBuffer),
                         simplePass19(simpleIndexCount19, 1u, simpleFirstIndex19, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene19", loadFrame, opaquePass19())
            ->renderPass("WebgpuMultipleCanvasScene20", loadFrame,
                         simplePass21->setVertexBuffer(simpleVertexBuffer),
                         simplePass21->setIndexBuffer(simpleIndexBuffer),
                         simplePass21(simpleIndexCount21, 1u, simpleFirstIndex21, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene21", loadFrame,
                         simplePass22->setVertexBuffer(simpleVertexBuffer),
                         simplePass22->setIndexBuffer(simpleIndexBuffer),
                         simplePass22(simpleIndexCount22, 1u, simpleFirstIndex22, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene22", loadFrame, opaquePass22())
            ->renderPass("WebgpuMultipleCanvasScene23", loadFrame, opaquePass23())
            ->renderPass("WebgpuMultipleCanvasScene24", loadFrame,
                         simplePass25->setVertexBuffer(simpleVertexBuffer),
                         simplePass25->setIndexBuffer(simpleIndexBuffer),
                         simplePass25(simpleIndexCount25, 1u, simpleFirstIndex25, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene25", loadFrame,
                         simplePass26->setVertexBuffer(simpleVertexBuffer),
                         simplePass26->setIndexBuffer(simpleIndexBuffer),
                         simplePass26(simpleIndexCount26, 1u, simpleFirstIndex26, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene26", loadFrame, opaquePass26())
            ->renderPass("WebgpuMultipleCanvasScene27", loadFrame,
                         simplePass28->setVertexBuffer(simpleVertexBuffer),
                         simplePass28->setIndexBuffer(simpleIndexBuffer),
                         simplePass28(simpleIndexCount28, 1u, simpleFirstIndex28, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene28", loadFrame,
                         simplePass29->setVertexBuffer(simpleVertexBuffer),
                         simplePass29->setIndexBuffer(simpleIndexBuffer),
                         simplePass29(simpleIndexCount29, 1u, simpleFirstIndex29, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene29", loadFrame, opaquePass29())
            ->renderPass("WebgpuMultipleCanvasScene30", loadFrame, opaquePass30())
            ->renderPass("WebgpuMultipleCanvasScene31", loadFrame,
                         simplePass32->setVertexBuffer(simpleVertexBuffer),
                         simplePass32->setIndexBuffer(simpleIndexBuffer),
                         simplePass32(simpleIndexCount32, 1u, simpleFirstIndex32, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene32", loadFrame,
                         simplePass33->setVertexBuffer(simpleVertexBuffer),
                         simplePass33->setIndexBuffer(simpleIndexBuffer),
                         simplePass33(simpleIndexCount33, 1u, simpleFirstIndex33, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene33", loadFrame,
                         simplePass34->setVertexBuffer(simpleVertexBuffer),
                         simplePass34->setIndexBuffer(simpleIndexBuffer),
                         simplePass34(simpleIndexCount34, 1u, simpleFirstIndex34, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene34", loadFrame, opaquePass34())
            ->renderPass("WebgpuMultipleCanvasScene35", loadFrame, opaquePass35())
            ->renderPass("WebgpuMultipleCanvasScene36", loadFrame,
                         simplePass37->setVertexBuffer(simpleVertexBuffer),
                         simplePass37->setIndexBuffer(simpleIndexBuffer),
                         simplePass37(simpleIndexCount37, 1u, simpleFirstIndex37, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene37", loadFrame, opaquePass37())
            ->renderPass("WebgpuMultipleCanvasScene38", loadFrame,
                         simplePass39->setVertexBuffer(simpleVertexBuffer),
                         simplePass39->setIndexBuffer(simpleIndexBuffer),
                         simplePass39(simpleIndexCount39, 1u, simpleFirstIndex39, 0, 0u))
            ->renderPass("WebgpuMultipleCanvasScene39", loadFrame,
                         simplePass40->setVertexBuffer(simpleVertexBuffer),
                         simplePass40->setIndexBuffer(simpleIndexBuffer),
                         simplePass40(simpleIndexCount40, 1u, simpleFirstIndex40, 0, 0u))
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

    /** Releases all canvas RenderSets and output targets. */
    void destroy() override
    {
        sceneSet4->destroy();
        sceneSet6->destroy();
        sceneSet8->destroy();
        sceneSet9->destroy();
        sceneSet10->destroy();
        sceneSet11->destroy();
        sceneSet12->destroy();
        sceneSet14->destroy();
        sceneSet15->destroy();
        sceneSet16->destroy();
        sceneSet19->destroy();
        sceneSet22->destroy();
        sceneSet23->destroy();
        sceneSet26->destroy();
        sceneSet29->destroy();
        sceneSet30->destroy();
        sceneSet34->destroy();
        sceneSet35->destroy();
        sceneSet37->destroy();
        device->freeBuffer(simpleVertexBuffer);
        device->freeBuffer(simpleIndexBuffer);
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
