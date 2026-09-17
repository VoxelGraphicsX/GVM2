#ifndef GVM_THREE_PHASE1_MODIFIER_TESSELLATION_SIMPLE_HPP
#define GVM_THREE_PHASE1_MODIFIER_TESSELLATION_SIMPLE_HPP

#include "UGL.h"
#include "Phase1ModifierTessellationSimpleData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglModifierTessellationVertexCount = 32442u;
static const uint WebglModifierTessellationOutputWidth = 800u;
static const uint WebglModifierTessellationOutputHeight = 500u;

/** Binds the private tessellation shader's camera and amplitude state. */
struct WebglModifierTessellationSceneResources final : public IBindGroup
{
    /** Declares the immutable per-capture uniform buffer. */
    constructor(UniformBuffer<WebglModifierTessellationUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries the authored normal and per-face color into the fragment stage. */
struct WebglModifierTessellationVertexOutput
{
    float4 position [[Position]];
    float3 normal [[Attribute0]];
    float3 customColor [[Attribute1]];
};

/** Defines the custom shader's single-sample color and depth attachments. */
struct WebglModifierTessellationFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Reproduces the authored displacement and directional-light ShaderMaterial. */
class WebglModifierTessellationMainPass final : public IRenderClass
{
public:
    /** Uses one ordinary non-indexed opaque triangle-list draw. */
    constructor(BindGroup<WebglModifierTessellationSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the face-constant animated normal displacement and camera transform. */
    WebglModifierTessellationVertexOutput vertex(WebglModifierTessellationVertex inputValue [[VertexInput0]])
    {
        const float3 displaced = inputValue.position.xyz + inputValue.normal.xyz *
                                 resources->uniforms->amplitudeAndReserved.x * inputValue.displacement.xyz;
        const float4 modelPosition = float4(displaced, 1.0f);
        WebglModifierTessellationVertexOutput outputValue;
        outputValue.position = resources->uniforms->modelViewProjectionColumn0 * modelPosition.x +
                               resources->uniforms->modelViewProjectionColumn1 * modelPosition.y +
                               resources->uniforms->modelViewProjectionColumn2 * modelPosition.z +
                               resources->uniforms->modelViewProjectionColumn3 * modelPosition.w;
        outputValue.normal = inputValue.normal.xyz;
        outputValue.customColor = inputValue.customColor.xyz;
        return outputValue;
    }

    /** Evaluates the exact ambient-plus-directional authored fragment equation. */
    WebglModifierTessellationFrameBuffer fragment(WebglModifierTessellationVertexOutput inputValue)
    {
        const float directional = max(dot(inputValue.normal, normalize(float3(1.0f, 1.0f, 1.0f))), 0.0f);
        const float3 linearColor = (directional + 0.4f) * inputValue.customColor;
        WebglModifierTessellationFrameBuffer frameBuffer;
        frameBuffer.color = half4(linearColor.x, linearColor.y, linearColor.z, 1.0f);
        return frameBuffer;
    }
};

/** Owns the one simple Scene RenderClass and its single-sample output. */
class Phase1ModifierTessellationSimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglModifierTessellationVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<WebglModifierTessellationUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    BindGroup<WebglModifierTessellationSceneResources> sceneResources;
    RenderClass<WebglModifierTessellationMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;

public:
    /** Creates all fixed standalone buffers through the current DSL resource interface. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer("WebglModifierTessellationVertices", WebglModifierTessellationVertexCount);
        uniformBuffer = device->createBuffer("WebglModifierTessellationUniforms", 1u);
    }

    /** Allocates the fixed single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        if (width != WebglModifierTessellationOutputWidth || height != WebglModifierTessellationOutputHeight)
        {
            return;
        }
        outputColor = device->createTexture("WebglModifierTessellationOutput", width, height, 1u);
        outputDepth = device->createTexture("WebglModifierTessellationOutputDepth", width, height, 1u);
    }

    /** Uploads the locked CPU geometry and canonical camera/amplitude state. */
    void configureScene(const eastl::vector<WebglModifierTessellationVertex> &vertices,
                        float m00, float m01, float m02, float m03,
                        float m10, float m11, float m12, float m13,
                        float m20, float m21, float m22, float m23,
                        float m30, float m31, float m32, float m33,
                        float amplitude)
    {
        WebglModifierTessellationUniforms uniforms;
        uniforms.modelViewProjectionColumn0 = float4(m00, m01, m02, m03);
        uniforms.modelViewProjectionColumn1 = float4(m10, m11, m12, m13);
        uniforms.modelViewProjectionColumn2 = float4(m20, m21, m22, m23);
        uniforms.modelViewProjectionColumn3 = float4(m30, m31, m32, m33);
        uniforms.amplitudeAndReserved = float4(amplitude, 0.0f, 0.0f, 0.0f);
        graphicsQueue->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                                   uint64_t(vertices.size()) * sizeof(WebglModifierTessellationVertex))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))->submit();
        sceneResources = device->createBindGroup<WebglModifierTessellationSceneResources>(uniformBuffer);
        mainPass = device->createRenderClass<WebglModifierTessellationMainPass>(sceneResources);
    }

    /** Draws the one Scene object directly into the single-sample output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglModifierTessellationFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {5.0 / 255.0, 5.0 / 255.0, 5.0 / 255.0, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue->renderPass("WebglModifierTessellationMain", frameBuffer,
                                  mainPass->setVertexBuffer(vertexBuffer),
                                  mainPass(WebglModifierTessellationVertexCount, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})->submit();
        swapchain->present();
    }

    /** Returns the DSL-created final comparison texture for readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the immutable output width. */
    uint getReadbackWidth() const { return WebglModifierTessellationOutputWidth; }

    /** Returns the immutable output height. */
    uint getReadbackHeight() const { return WebglModifierTessellationOutputHeight; }

    /** Releases every private buffer and texture owned by this sample. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
