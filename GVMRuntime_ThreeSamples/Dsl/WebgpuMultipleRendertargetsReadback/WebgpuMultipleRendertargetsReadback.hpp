#ifndef GVM_THREE_WEBGPU_MULTIPLE_RENDERTARGETS_READBACK_HPP
#define GVM_THREE_WEBGPU_MULTIPLE_RENDERTARGETS_READBACK_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one exact TorusKnotGeometry vertex. */
struct WebgpuMultipleRendertargetsReadbackVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Stores camera, animated model, viewport, and deterministic output selection. */
struct WebgpuMultipleRendertargetsReadbackUniforms
{
    float4x4 modelViewProjection;
    float4x4 model;
    float4 viewportSelection;
};

/** Binds the hardwood texture and immutable Scene controls. */
struct WebgpuMultipleRendertargetsReadbackSceneResources final : public IBindGroup
{
    /** Declares the locked diffuse image, repeat sampler, and transform. */
    constructor(
        Texture2D<float4> diffuse [[Binding0]],
        Sampler diffuseSampler [[Binding1]],
        UniformBuffer<WebgpuMultipleRendertargetsReadbackUniforms> uniforms [[Binding2]])
    {
    }
};

/** Carries sampled UV and world normal to the true MRT fragment stage. */
struct WebgpuMultipleRendertargetsReadbackVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float3 normalWorld [[Attribute1]];
};

/** Defines two simultaneous float color attachments and one depth target. */
struct WebgpuMultipleRendertargetsReadbackFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> output;
    ColorAttachment<TextureFormat::RGBA16Float> normal;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the two RGBA8 attachments used by the explicit readback target. */
struct WebgpuMultipleRendertargetsReadbackByteFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> output;
    ColorAttachment<TextureFormat::RGBA8Unorm> normal;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final RGBA8 composite attachment. */
struct WebgpuMultipleRendertargetsReadbackOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Binds the full-size MRT attachments and uploaded readback texture. */
struct WebgpuMultipleRendertargetsReadbackCompositeResources final : public IBindGroup
{
    /** Declares full-size attachments, round-tripped bytes, and controls. */
    constructor(
        Texture2D<float4> outputTexture [[Binding0]],
        Texture2D<float4> normalTexture [[Binding1]],
        Texture2D<float4> uploadedReadbackTexture [[Binding2]],
        Sampler attachmentSampler [[Binding3]],
        UniformBuffer<WebgpuMultipleRendertargetsReadbackUniforms> uniforms [[Binding4]])
    {
    }
};

/** Binds the 512-square MRT attachments used by explicit CPU readback. */
struct WebgpuMultipleRendertargetsReadbackQuantizeResources final : public IBindGroup
{
    /** Declares both readback attachments, nearest sampler, and selection. */
    constructor(
        Texture2D<float4> outputTexture [[Binding0]],
        Texture2D<float4> normalTexture [[Binding1]],
        Sampler attachmentSampler [[Binding2]],
        UniformBuffer<WebgpuMultipleRendertargetsReadbackUniforms> uniforms [[Binding3]])
    {
    }
};

/** Carries fullscreen coordinates to the split composite. */
struct WebgpuMultipleRendertargetsReadbackScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Converts one linear channel to the r185 output transfer. */
float webgpuMultipleRendertargetsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Returns the signed distance to a CSS-style rounded rectangle. */
float webgpuMultipleRendertargetsRoundedBoxDistance(
    float2 samplePosition,
    float2 minimumPoint,
    float2 maximumPoint,
    float leftRadius,
    float rightRadius)
{
    const float radius =
        samplePosition.x < (minimumPoint.x + maximumPoint.x) * 0.5f
            ? leftRadius
            : rightRadius;
    const float2 center = (minimumPoint + maximumPoint) * 0.5f;
    const float2 halfExtent = (maximumPoint - minimumPoint) * 0.5f;
    const float2 offset =
        abs(samplePosition - center) - (halfExtent - float2(radius));
    return length(max(offset, float2(0.0f))) +
        min(max(offset.x, offset.y), 0.0f) - radius;
}

/** Evaluates one-pixel analytic coverage for a browser-composited edge. */
float webgpuMultipleRendertargetsEdgeCoverage(float signedDistance)
{
    return clamp(0.5f - signedDistance, 0.0f, 1.0f);
}

/** Emits diffuse output and world normal in one true two-attachment pass. */
class WebgpuMultipleRendertargetsReadbackScenePass final : public IRenderClass
{
public:
    /** Binds the hardwood material and standard opaque depth state. */
    constructor(
        BindGroup<WebgpuMultipleRendertargetsReadbackSceneResources> resources [[Slot0]])
    {
        // The DSL render targets use an inverted framebuffer Y convention,
        // so Three.js front faces map to the generated front-cull state.
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the locked camera and animated Y rotation. */
    WebgpuMultipleRendertargetsReadbackVertexOutput vertex(
        WebgpuMultipleRendertargetsReadbackVertex inputValue [[VertexInput0]])
    {
        WebgpuMultipleRendertargetsReadbackVertexOutput outputValue;
        outputValue.position =
            mul(resources->uniforms->modelViewProjection, inputValue.position);
        outputValue.uv = inputValue.uv.xy * float2(10.0f, 4.0f);
        const float4 transformedNormal =
            mul(resources->uniforms->model, float4(inputValue.normal.xyz, 0.0f));
        outputValue.normalWorld = normalize(float3(
            transformedNormal.x,
            transformedNormal.y,
            transformedNormal.z));
        return outputValue;
    }

    /** Writes diffuse color and world normal to separate attachments. */
    WebgpuMultipleRendertargetsReadbackFrameBuffer fragment(
        WebgpuMultipleRendertargetsReadbackVertexOutput inputValue)
    {
        WebgpuMultipleRendertargetsReadbackFrameBuffer frameBuffer;
        frameBuffer.output =
            half4(resources->diffuse->sampleGrad(
                resources->diffuseSampler,
                inputValue.uv,
                ddx(inputValue.uv),
                ddy(inputValue.uv)));
        frameBuffer.normal =
            half4(half3(inputValue.normalWorld), half(1.0f));
        return frameBuffer;
    }
};

/** Renders the same Scene directly into the 512-square byte attachments. */
class WebgpuMultipleRendertargetsReadbackByteScenePass final : public IRenderClass
{
public:
    /** Binds the hardwood material and standard opaque depth state. */
    constructor(
        BindGroup<WebgpuMultipleRendertargetsReadbackSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the locked camera and animated Y rotation. */
    WebgpuMultipleRendertargetsReadbackVertexOutput vertex(
        WebgpuMultipleRendertargetsReadbackVertex inputValue [[VertexInput0]])
    {
        WebgpuMultipleRendertargetsReadbackVertexOutput outputValue;
        outputValue.position =
            mul(resources->uniforms->modelViewProjection, inputValue.position);
        outputValue.uv = inputValue.uv.xy * float2(10.0f, 4.0f);
        const float4 transformedNormal =
            mul(resources->uniforms->model, float4(inputValue.normal.xyz, 0.0f));
        outputValue.normalWorld = normalize(float3(
            transformedNormal.x,
            transformedNormal.y,
            transformedNormal.z));
        return outputValue;
    }

    /** Writes byte-quantized diffuse color and world normal simultaneously. */
    WebgpuMultipleRendertargetsReadbackByteFrameBuffer fragment(
        WebgpuMultipleRendertargetsReadbackVertexOutput inputValue)
    {
        WebgpuMultipleRendertargetsReadbackByteFrameBuffer frameBuffer;
        frameBuffer.output =
            half4(resources->diffuse->sampleGrad(
                resources->diffuseSampler,
                inputValue.uv,
                ddx(inputValue.uv),
                ddy(inputValue.uv)));
        frameBuffer.normal = half4(half3(inputValue.normalWorld), half(1.0f));
        return frameBuffer;
    }
};

/** Selects direct MRT split output or the uploaded readback texture. */
class WebgpuMultipleRendertargetsReadbackCompositePass final : public IRenderClass
{
public:
    /** Binds only the two MRT textures and disables culling. */
    constructor(
        BindGroup<WebgpuMultipleRendertargetsReadbackCompositeResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuMultipleRendertargetsReadbackScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuMultipleRendertargetsReadbackScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reads one direct MRT sample or one delayed GPU-CPU-GPU sample and composites the r185 inspector chrome. */
    WebgpuMultipleRendertargetsReadbackOutputFrameBuffer fragment(
        WebgpuMultipleRendertargetsReadbackScreenOutput inputValue)
    {
        float4 source;
        if (resources->uniforms->viewportSelection.z > 0.5f)
        {
            source = resources->uploadedReadbackTexture->sample(
                resources->attachmentSampler,
                inputValue.uv);
        }
        else if (inputValue.uv.x < 0.5f)
        {
            source = resources->outputTexture->sample(
                resources->attachmentSampler,
                inputValue.uv);
        }
        else
        {
            source = resources->normalTexture->sample(
                resources->attachmentSampler,
                inputValue.uv);
        }
        float3 display = float3(
            webgpuMultipleRendertargetsLinearToSrgb(source.x),
            webgpuMultipleRendertargetsLinearToSrgb(source.y),
            webgpuMultipleRendertargetsLinearToSrgb(source.z));

        const float2 pixel =
            inputValue.uv * resources->uniforms->viewportSelection.xy;
        const float outerCoverage =
            webgpuMultipleRendertargetsEdgeCoverage(
                webgpuMultipleRendertargetsRoundedBoxDistance(
                    pixel,
                    float2(614.0f, 15.0f),
                    float2(785.0f, 53.0f),
                    12.0f,
                    6.0f));
        const float innerCoverage =
            webgpuMultipleRendertargetsEdgeCoverage(
                webgpuMultipleRendertargetsRoundedBoxDistance(
                    pixel,
                    float2(615.0f, 16.0f),
                    float2(784.0f, 52.0f),
                    11.0f,
                    5.0f));
        const float activeCoverage =
            pixel.x < 663.0f ? innerCoverage : 0.0f;
        display = display * (1.0f - outerCoverage) +
            float3(42.0f, 42.0f, 50.0f) / 255.0f * outerCoverage;
        display = display * (1.0f - innerCoverage) +
            float3(26.0f, 26.0f, 31.0f) / 255.0f * innerCoverage;
        display = display * (1.0f - activeCoverage) +
            float3(21.0f, 55.0f, 76.0f) / 255.0f * activeCoverage;
        WebgpuMultipleRendertargetsReadbackOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(display), half(1.0f));
        return frameBuffer;
    }
};

/** Quantizes one selected float MRT attachment into an RGBA8 readback target. */
class WebgpuMultipleRendertargetsReadbackQuantizePass final : public IRenderClass
{
public:
    /** Binds the square MRT attachments and disables culling. */
    constructor(
        BindGroup<WebgpuMultipleRendertargetsReadbackQuantizeResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle for the 512-square conversion. */
    WebgpuMultipleRendertargetsReadbackScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuMultipleRendertargetsReadbackScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Selects diffuse or normal data without applying output transfer. */
    WebgpuMultipleRendertargetsReadbackOutputFrameBuffer fragment(
        WebgpuMultipleRendertargetsReadbackScreenOutput inputValue)
    {
        const float4 source =
            resources->uniforms->viewportSelection.z < 1.5f
                ? resources->outputTexture->sample(
                    resources->attachmentSampler,
                    inputValue.uv)
                : resources->normalTexture->sample(
                    resources->attachmentSampler,
                    inputValue.uv);
        WebgpuMultipleRendertargetsReadbackOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(source);
        return frameBuffer;
    }
};

/** Owns the dedicated TorusKnot Scene, true MRT, and split composite. */
class WebgpuMultipleRendertargetsReadbackRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebgpuMultipleRendertargetsReadbackVertex, BufferUsage<Vertex, CopyDst>>
        vertexBuffer;
    Buffer<WebgpuMultipleRendertargetsReadbackVertex, BufferUsage<Vertex, CopyDst>>
        byteVertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebgpuMultipleRendertargetsReadbackUniforms, BufferUsage<Uniform, CopyDst>>
        uniformBuffer;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> diffuseTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> outputAttachment;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> normalAttachment;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthAttachment;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> readbackOutputAttachment;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> readbackNormalAttachment;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> readbackDepthAttachment;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, CopySrc>,
            TextureDimension::e2D> readbackStagingTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> uploadedReadbackTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler diffuseSampler;
    Sampler attachmentSampler;
    BindGroup<WebgpuMultipleRendertargetsReadbackSceneResources> sceneResources;
    BindGroup<WebgpuMultipleRendertargetsReadbackCompositeResources> compositeResources;
    BindGroup<WebgpuMultipleRendertargetsReadbackQuantizeResources> quantizeResources;
    RenderClass<WebgpuMultipleRendertargetsReadbackScenePass> scenePass;
    RenderClass<WebgpuMultipleRendertargetsReadbackByteScenePass> byteScenePass;
    RenderClass<WebgpuMultipleRendertargetsReadbackCompositePass> compositePass;
    RenderClass<WebgpuMultipleRendertargetsReadbackQuantizePass> quantizePass;
    WebgpuMultipleRendertargetsReadbackUniforms uniforms;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;
    float orbitYaw = 0.0f;
    float orbitPitch = 0.0f;
    float outputSelection = 0.0f;

public:
    /** Creates all standalone geometry, texture, and control resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebgpuMultipleRendertargetsReadbackVertices", 5000u);
        byteVertexBuffer = device->createBuffer(
            "WebgpuMultipleRendertargetsReadbackByteVertices", 30000u);
        indexBuffer = device->createBuffer(
            "WebgpuMultipleRendertargetsReadbackIndices", 30000u);
        uniformBuffer = device->createBuffer(
            "WebgpuMultipleRendertargetsReadbackUniforms", 1u);
        diffuseSampler = device->createSampler({
            .label = "WebgpuMultipleRendertargetsReadbackDiffuseSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
        });
        attachmentSampler = device->createSampler({
            .label = "WebgpuMultipleRendertargetsReadbackAttachmentSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates single-sample MRT attachments and the final RGBA8 output. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputAttachment = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackOutputAttachment",
            width,
            height,
            1u);
        normalAttachment = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackNormalAttachment",
            width,
            height,
            1u);
        depthAttachment = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackDepth",
            width,
            height,
            1u);
        readbackOutputAttachment = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackSquareOutput",
            512u,
            512u,
            1u);
        readbackNormalAttachment = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackSquareNormal",
            512u,
            512u,
            1u);
        readbackDepthAttachment = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackSquareDepth",
            512u,
            512u,
            1u);
        readbackStagingTexture = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackStagingRGBA8",
            512u,
            512u,
            1u);
        uploadedReadbackTexture = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackUploadedRGBA8",
            512u,
            512u,
            1u);
        outputTexture = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackRGBA8",
            width,
            height,
            1u);
    }

    /** Uploads canonical TorusKnot geometry, diffuse texels, and orbit state. */
    void configureScene(
        const eastl::vector<float4> &positions,
        const eastl::vector<float4> &normals,
        const eastl::vector<float4> &textureCoordinates,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &diffuseMips,
        uint diffuseWidth,
        uint diffuseHeight,
        float inOrbitYaw,
        float inOrbitPitch,
        float inOutputSelection)
    {
        indexCount = uint(indices.size());
        eastl::vector<WebgpuMultipleRendertargetsReadbackVertex> vertices;
        vertices.resize(positions.size());
        for (uint index = 0u; index < uint(vertices.size()); ++index)
        {
            vertices[index].position = positions[index];
            vertices[index].normal = normals[index];
            vertices[index].uv = textureCoordinates[index];
        }
        eastl::vector<WebgpuMultipleRendertargetsReadbackVertex> byteVertices;
        byteVertices.resize(indices.size());
        for (uint index = 0u; index < uint(indices.size()); ++index)
        {
            byteVertices[index] = vertices[indices[index]];
        }
        orbitYaw = inOrbitYaw;
        orbitPitch = inOrbitPitch;
        outputSelection = inOutputSelection;
        uniforms.viewportSelection =
            float4(float(width), float(height), outputSelection, 0.0f);
        diffuseTexture = device->createTexture(
            "WebgpuMultipleRendertargetsReadbackHardwood",
            diffuseWidth,
            diffuseHeight,
            1u,
            uint(diffuseMips.size()));
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebgpuMultipleRendertargetsReadbackVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(byteVertexBuffer),
                byteVertices.data(),
                uint64_t(byteVertices.size()) *
                    sizeof(WebgpuMultipleRendertargetsReadbackVertex))
            ->submit();
        for (uint mipLevel = 0u;
             mipLevel < uint(diffuseMips.size());
             ++mipLevel)
        {
            graphicsQueue
                ->writeTexture(
                    diffuseTexture,
                    diffuseMips[mipLevel].data(),
                    uint64_t(diffuseMips[mipLevel].size()),
                    mipLevel);
        }
        graphicsQueue->submit();
        sceneResources =
            device->createBindGroup<WebgpuMultipleRendertargetsReadbackSceneResources>(
                diffuseTexture->createView(),
                diffuseSampler,
                uniformBuffer);
        compositeResources =
            device->createBindGroup<WebgpuMultipleRendertargetsReadbackCompositeResources>(
                outputAttachment->createView(),
                normalAttachment->createView(),
                uploadedReadbackTexture->createView(),
                attachmentSampler,
                uniformBuffer);
        quantizeResources =
            device->createBindGroup<WebgpuMultipleRendertargetsReadbackQuantizeResources>(
                readbackOutputAttachment->createView(),
                readbackNormalAttachment->createView(),
                attachmentSampler,
                uniformBuffer);
        scenePass =
            device->createRenderClass<WebgpuMultipleRendertargetsReadbackScenePass>(
                sceneResources);
        byteScenePass =
            device->createRenderClass<WebgpuMultipleRendertargetsReadbackByteScenePass>(
                sceneResources);
        compositePass =
            device->createRenderClass<WebgpuMultipleRendertargetsReadbackCompositePass>(
                compositeResources);
        quantizePass =
            device->createRenderClass<WebgpuMultipleRendertargetsReadbackQuantizePass>(
                quantizeResources);
    }

    /** Executes true MRT and the selected direct or explicit readback path. */
    void render() override
    {
        const float angle =
            float(frameIndex + (outputSelection > 0.5f ? 1u : 0u)) /
            60.0f * 0.4f;
        const float cosineY = cos(angle);
        const float sineY = sin(angle);
        const float4x4 model = float4x4(
            float4(cosineY, 0.0f, -sineY, 0.0f),
            float4(0.0f, 1.0f, 0.0f, 0.0f),
            float4(sineY, 0.0f, cosineY, 0.0f),
            float4(0.0f, 0.0f, 0.0f, 1.0f));
        const float phi = 1.57079632679489661923f + orbitPitch;
        const float3 cameraPosition = float3(
            sin(phi) * sin(orbitYaw) * 4.0f,
            cos(phi) * 4.0f,
            sin(phi) * cos(orbitYaw) * 4.0f);
        const float3 forward = normalize(-cameraPosition);
        const float3 side = normalize(cross(forward, float3(0.0f, 1.0f, 0.0f)));
        const float3 cameraUp = cross(side, forward);
        const float4x4 view = float4x4(
            float4(side.x, cameraUp.x, -forward.x, 0.0f),
            float4(side.y, cameraUp.y, -forward.y, 0.0f),
            float4(side.z, cameraUp.z, -forward.z, 0.0f),
            float4(
                -dot(side, cameraPosition),
                -dot(cameraUp, cameraPosition),
                dot(forward, cameraPosition),
                1.0f));
        const float aspect = float(width) / float(height);
        const float cotangent = 1.4281480067f;
        const float4x4 projection = float4x4(
            float4(cotangent / aspect, 0.0f, 0.0f, 0.0f),
            float4(0.0f, -cotangent, 0.0f, 0.0f),
            float4(0.0f, 0.0f, -50.0f / 49.9f, -1.0f),
            float4(0.0f, 0.0f, -5.0f / 49.9f, 0.0f));
        uniforms.model = model;
        uniforms.modelViewProjection =
            mul(projection, mul(view, model));
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();

        WebgpuMultipleRendertargetsReadbackFrameBuffer mrtFrameBuffer;
        mrtFrameBuffer.output = outputAttachment->createView();
        mrtFrameBuffer.output.loadOp = LoadOp::Clear;
        mrtFrameBuffer.output.storeOp = StoreOp::Store;
        mrtFrameBuffer.output.clearValue =
            {0.015996f, 0.015996f, 0.015996f, 1.0f};
        mrtFrameBuffer.normal = normalAttachment->createView();
        mrtFrameBuffer.normal.loadOp = LoadOp::Clear;
        mrtFrameBuffer.normal.storeOp = StoreOp::Store;
        mrtFrameBuffer.normal.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        mrtFrameBuffer.depth = depthAttachment->createView();
        mrtFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        mrtFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        mrtFrameBuffer.depth.depthClearValue = 1.0f;
        WebgpuMultipleRendertargetsReadbackOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        auto nextTexture = swapchain->queryNextTexture();
        if (outputSelection > 0.5f)
        {
            WebgpuMultipleRendertargetsReadbackByteFrameBuffer byteFrameBuffer;
            byteFrameBuffer.output = readbackOutputAttachment->createView();
            byteFrameBuffer.output.loadOp = LoadOp::Clear;
            byteFrameBuffer.output.storeOp = StoreOp::Store;
            byteFrameBuffer.output.clearValue =
                {0.015996f, 0.015996f, 0.015996f, 1.0f};
            byteFrameBuffer.normal = readbackNormalAttachment->createView();
            byteFrameBuffer.normal.loadOp = LoadOp::Clear;
            byteFrameBuffer.normal.storeOp = StoreOp::Store;
            byteFrameBuffer.normal.clearValue =
                {0.0f, 0.0f, 0.0f, 1.0f};
            byteFrameBuffer.depth = readbackDepthAttachment->createView();
            byteFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
            byteFrameBuffer.depth.depthStoreOp = StoreOp::Store;
            byteFrameBuffer.depth.depthClearValue = 1.0f;
            auto byteVertexTask =
                byteScenePass->setVertexBuffer(vertexBuffer);
            auto byteIndexTask =
                byteScenePass->setIndexBuffer(indexBuffer);
            auto byteDrawTask =
                byteScenePass(indexCount, 1u, 0u, 0, 0u);
            graphicsQueue
                ->renderPass(
                    "WebgpuMultipleRendertargetsReadbackMrt",
                    byteFrameBuffer,
                    byteVertexTask,
                    byteIndexTask,
                    byteDrawTask)
                ->renderPass(
                    "WebgpuMultipleRendertargetsReadbackComposite",
                    outputFrameBuffer,
                    compositePass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputTexture,
                    RenderToSwapchainDescriptor{})
                ->submit();
        }
        else
        {
            graphicsQueue
                ->renderPass(
                    "WebgpuMultipleRendertargetsReadbackMrt",
                    mrtFrameBuffer,
                    scenePass->setVertexBuffer(vertexBuffer),
                    scenePass->setIndexBuffer(indexBuffer),
                    scenePass(indexCount, 1u, 0u, 0, 0u))
                ->renderPass(
                    "WebgpuMultipleRendertargetsReadbackComposite",
                    outputFrameBuffer,
                    compositePass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputTexture,
                    RenderToSwapchainDescriptor{})
                ->submit();
        }
        swapchain->present();
        frameIndex += 1u;
    }

    /** Reads the completed 512-square RGBA8 attachment through the generated queue path. */
    void copySelectedAttachmentToCpu(eastl::vector<uint8_t> &bytes)
    {
        bytes.resize(512u * 512u * 4u);
        graphicsQueue
            ->readTexture(
                outputSelection < 1.5f
                    ? readbackOutputAttachment
                    : readbackNormalAttachment,
                bytes.data(),
                uint64_t(bytes.size()))
            ->submit();
    }

    /** Uploads opaque readback bytes for the next frame's fullscreen output. */
    void uploadSelectedAttachment(const eastl::vector<uint8_t> &bytes)
    {
        graphicsQueue
            ->writeTexture(
                uploadedReadbackTexture,
                bytes.data(),
                uint64_t(bytes.size()))
            ->submit();
    }

    /** Re-composites and presents the just-uploaded readback without advancing time. */
    void renderUploadedReadbackOutput()
    {
        WebgpuMultipleRendertargetsReadbackOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuMultipleRendertargetsReadbackUploadedOutput",
                outputFrameBuffer,
                compositePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the fixed output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the fixed output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases every standalone geometry and texture resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(byteVertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(diffuseTexture);
        device->freeTexture(outputAttachment);
        device->freeTexture(normalAttachment);
        device->freeTexture(depthAttachment);
        device->freeTexture(readbackOutputAttachment);
        device->freeTexture(readbackNormalAttachment);
        device->freeTexture(readbackDepthAttachment);
        device->freeTexture(readbackStagingTexture);
        device->freeTexture(uploadedReadbackTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
