#ifndef GVM_THREE_WEBGPU_MULTIPLE_RENDERTARGETS_HPP
#define GVM_THREE_WEBGPU_MULTIPLE_RENDERTARGETS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one exact TorusKnotGeometry vertex. */
struct WebgpuMultipleRendertargetsVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Stores camera, animated model, viewport, and Inspector controls. */
struct WebgpuMultipleRendertargetsUniforms
{
    float4x4 modelViewProjection;
    float4x4 model;
    float4 viewportInspector;
};

/** Binds the hardwood texture and immutable Scene controls. */
struct WebgpuMultipleRendertargetsSceneResources final : public IBindGroup
{
    /** Declares the locked diffuse image, repeat sampler, and transform. */
    constructor(
        Texture2D<float4> diffuse [[Binding0]],
        Sampler diffuseSampler [[Binding1]],
        UniformBuffer<WebgpuMultipleRendertargetsUniforms> uniforms [[Binding2]])
    {
    }
};

/** Carries sampled UV and world normal to the true MRT fragment stage. */
struct WebgpuMultipleRendertargetsVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float3 normalWorld [[Attribute1]];
};

/** Defines two simultaneous float color attachments and one depth target. */
struct WebgpuMultipleRendertargetsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> output;
    ColorAttachment<TextureFormat::RGBA16Float> normal;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final RGBA8 composite attachment. */
struct WebgpuMultipleRendertargetsOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Binds both actual MRT attachments to the split composite. */
struct WebgpuMultipleRendertargetsCompositeResources final : public IBindGroup
{
    /** Declares both float attachments, nearest sampler, and controls. */
    constructor(
        Texture2D<float4> outputTexture [[Binding0]],
        Texture2D<float4> normalTexture [[Binding1]],
        Sampler attachmentSampler [[Binding2]],
        UniformBuffer<WebgpuMultipleRendertargetsUniforms> uniforms [[Binding3]])
    {
    }
};

/** Carries fullscreen coordinates to the split composite. */
struct WebgpuMultipleRendertargetsScreenOutput
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

/** Emits diffuse output and world normal in one true two-attachment pass. */
class WebgpuMultipleRendertargetsScenePass final : public IRenderClass
{
public:
    /** Binds the hardwood material and standard opaque depth state. */
    constructor(
        BindGroup<WebgpuMultipleRendertargetsSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the locked camera and animated Y rotation. */
    WebgpuMultipleRendertargetsVertexOutput vertex(
        WebgpuMultipleRendertargetsVertex inputValue [[VertexInput0]])
    {
        WebgpuMultipleRendertargetsVertexOutput outputValue;
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
    WebgpuMultipleRendertargetsFrameBuffer fragment(
        WebgpuMultipleRendertargetsVertexOutput inputValue)
    {
        WebgpuMultipleRendertargetsFrameBuffer frameBuffer;
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

/** Selects diffuse on the left and world-normal output on the right. */
class WebgpuMultipleRendertargetsCompositePass final : public IRenderClass
{
public:
    /** Binds only the two MRT textures and disables culling. */
    constructor(
        BindGroup<WebgpuMultipleRendertargetsCompositeResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuMultipleRendertargetsScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuMultipleRendertargetsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reads one centered sample from the selected MRT attachment. */
    WebgpuMultipleRendertargetsOutputFrameBuffer fragment(
        WebgpuMultipleRendertargetsScreenOutput inputValue)
    {
        float4 source;
        if (inputValue.uv.x < 0.5f)
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
        const float3 display = float3(
            webgpuMultipleRendertargetsLinearToSrgb(source.x),
            webgpuMultipleRendertargetsLinearToSrgb(source.y),
            webgpuMultipleRendertargetsLinearToSrgb(source.z));
        WebgpuMultipleRendertargetsOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(display), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated TorusKnot Scene, true MRT, and split composite. */
class WebgpuMultipleRendertargetsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebgpuMultipleRendertargetsVertex, BufferUsage<Vertex, CopyDst>>
        vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebgpuMultipleRendertargetsUniforms, BufferUsage<Uniform, CopyDst>>
        uniformBuffer;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst, CopySrc>,
            TextureDimension::e2D> diffuseTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputAttachment;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> normalAttachment;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthAttachment;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler diffuseSampler;
    Sampler attachmentSampler;
    BindGroup<WebgpuMultipleRendertargetsSceneResources> sceneResources;
    BindGroup<WebgpuMultipleRendertargetsCompositeResources> compositeResources;
    RenderClass<WebgpuMultipleRendertargetsScenePass> scenePass;
    RenderClass<WebgpuMultipleRendertargetsCompositePass> compositePass;
    WebgpuMultipleRendertargetsUniforms uniforms;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;
    float orbitYaw = 0.0f;
    float orbitPitch = 0.0f;

public:
    /** Creates all standalone geometry, texture, and control resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebgpuMultipleRendertargetsVertices", 5000u);
        indexBuffer = device->createBuffer(
            "WebgpuMultipleRendertargetsIndices", 30000u);
        uniformBuffer = device->createBuffer(
            "WebgpuMultipleRendertargetsUniforms", 1u);
        diffuseSampler = device->createSampler({
            .label = "WebgpuMultipleRendertargetsDiffuseSampler",
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
            .label = "WebgpuMultipleRendertargetsAttachmentSampler",
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
            "WebgpuMultipleRendertargetsOutputAttachment",
            width,
            height,
            1u);
        normalAttachment = device->createTexture(
            "WebgpuMultipleRendertargetsNormalAttachment",
            width,
            height,
            1u);
        depthAttachment = device->createTexture(
            "WebgpuMultipleRendertargetsDepth",
            width,
            height,
            1u);
        outputTexture = device->createTexture(
            "WebgpuMultipleRendertargetsRGBA8",
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
        float inspectorEnabled)
    {
        indexCount = uint(indices.size());
        eastl::vector<WebgpuMultipleRendertargetsVertex> vertices;
        vertices.resize(positions.size());
        for (uint index = 0u; index < uint(vertices.size()); ++index)
        {
            vertices[index].position = positions[index];
            vertices[index].normal = normals[index];
            vertices[index].uv = textureCoordinates[index];
        }
        orbitYaw = inOrbitYaw;
        orbitPitch = inOrbitPitch;
        uniforms.viewportInspector =
            float4(float(width), float(height), inspectorEnabled, 0.0f);
        diffuseTexture = device->createTexture(
            "WebgpuMultipleRendertargetsHardwood",
            diffuseWidth,
            diffuseHeight,
            1u,
            uint(diffuseMips.size()));
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebgpuMultipleRendertargetsVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
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
                    mipLevel)
                ->submit();
        }
        // Submit a tiny readback fence after the authored mip uploads.  Vulkan
        // keeps the internal copy command pending until submit; waiting on
        // this 1x1 readback makes the first sampled frame deterministic while
        // preserving the authored mip chain and ordinary DSL texture path.
        eastl::vector<uint8_t> uploadFence(4u);
        graphicsQueue
            ->readTexture(
                diffuseTexture,
                uploadFence.data(),
                uint64_t(uploadFence.size()),
                uint(diffuseMips.size() - 1u))
            ->submit();
        sceneResources =
            device->createBindGroup<WebgpuMultipleRendertargetsSceneResources>(
                diffuseTexture->createView(),
                diffuseSampler,
                uniformBuffer);
        compositeResources =
            device->createBindGroup<WebgpuMultipleRendertargetsCompositeResources>(
                outputAttachment->createView(),
                normalAttachment->createView(),
                attachmentSampler,
                uniformBuffer);
        scenePass =
            device->createRenderClass<WebgpuMultipleRendertargetsScenePass>(
                sceneResources);
        compositePass =
            device->createRenderClass<WebgpuMultipleRendertargetsCompositePass>(
                compositeResources);
    }

    /** Updates fixed-clock transforms and executes one true MRT submission. */
    void render() override
    {
        const float angle =
            float(frameIndex) / 60.0f * 0.4f;
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

        WebgpuMultipleRendertargetsFrameBuffer mrtFrameBuffer;
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
        WebgpuMultipleRendertargetsOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        graphicsQueue
            ->renderPass(
                "WebgpuMultipleRendertargetsMrt",
                mrtFrameBuffer,
                scenePass->setVertexBuffer(vertexBuffer),
                scenePass->setIndexBuffer(indexBuffer),
                scenePass(indexCount, 1u, 0u, 0, 0u))
            ->submit();
        // The scene writes two sampled attachments.  A tiny readback fence
        // forces the producer submission to complete before the composite
        // pass consumes those attachments on Vulkan.
        eastl::vector<uint8_t> mrtFence(
            size_t(width) * size_t(height) * 8u);
        graphicsQueue
            ->readTexture(
                outputAttachment,
                mrtFence.data(),
                uint64_t(mrtFence.size()))
            ->submit();
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuMultipleRendertargetsComposite",
                outputFrameBuffer,
                compositePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
        frameIndex += 1u;
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
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(diffuseTexture);
        device->freeTexture(outputAttachment);
        device->freeTexture(normalAttachment);
        device->freeTexture(depthAttachment);
        device->freeTexture(outputTexture);
    }
};

#endif
