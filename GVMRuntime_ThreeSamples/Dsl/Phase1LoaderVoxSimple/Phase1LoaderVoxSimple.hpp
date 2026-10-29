#ifndef GVM_THREE_PHASE1_LOADER_VOX_SIMPLE_HPP
#define GVM_THREE_PHASE1_LOADER_VOX_SIMPLE_HPP

#include "UGL.h"
#include "Phase1LoaderVoxSimpleData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the fixed camera and three-light state for the decoded VOX mesh. */
struct WebglLoaderVoxResources final : public IBindGroup
{
    /** Declares the only uniform buffer consumed by the VOX material. */
    constructor(UniformBuffer<WebglLoaderVoxUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries flat normal, world position, and linear palette color to fragments. */
struct WebglLoaderVoxVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float3 color [[Attribute2]];
};

/** Defines the final color and depth attachments for the simple VOX Scene. */
struct WebglLoaderVoxFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one non-negative linear channel through Three's output transfer. */
float webglLoaderVoxLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f) return clamped * 12.92f;
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three's optimized Schlick Fresnel approximation. */
float3 webglLoaderVoxFresnel(float3 f0, float dotViewHalf)
{
    const float factor = exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - factor) + float3(factor);
}

/** Returns one DFG LUT row used by roughness-one MeshStandardMaterial. */
float2 webglLoaderVoxDfgRow(uint row)
{
    if (row == 0u) return float2(0.85986328125f, 0.039031982421875f);
    if (row == 1u) return float2(0.7548828125f, 0.0269775390625f);
    if (row == 2u) return float2(0.68359375f, 0.019439697265625f);
    if (row == 3u) return float2(0.62841796875f, 0.01419830322265625f);
    if (row == 4u) return float2(0.583984375f, 0.01041412353515625f);
    if (row == 5u) return float2(0.54638671875f, 0.007633209228515625f);
    if (row == 6u) return float2(0.51416015625f, 0.005565643310546875f);
    if (row == 7u) return float2(0.486083984375f, 0.00402069091796875f);
    if (row == 8u) return float2(0.46142578125f, 0.0028629302978515625f);
    if (row == 9u) return float2(0.439453125f, 0.0020046234130859375f);
    if (row == 10u) return float2(0.419677734375f, 0.00136566162109375f);
    if (row == 11u) return float2(0.402099609375f, 0.0008993148803710938f);
    if (row == 12u) return float2(0.385986328125f, 0.0005650520324707031f);
    if (row == 13u) return float2(0.37109375f, 0.0003311634063720703f);
    if (row == 14u) return float2(0.357666015625f, 0.00017201900482177734f);
    return float2(0.34521484375f, 0.00007051229476928711f);
}

/** Samples the roughness-one edge column of Three's DFG LUT. */
float2 webglLoaderVoxDfg(float normalDotDirection)
{
    const float coordinate = clamp(normalDotDirection, 0.0f, 1.0f) * 16.0f - 0.5f;
    if (coordinate <= 0.0f) return webglLoaderVoxDfgRow(0u);
    if (coordinate >= 15.0f) return webglLoaderVoxDfgRow(15u);
    const uint lower = uint(floor(coordinate));
    return lerp(webglLoaderVoxDfgRow(lower), webglLoaderVoxDfgRow(lower + 1u),
                coordinate - floor(coordinate));
}

/** Evaluates Three r185's roughness-one direct GGX multiscatter BRDF. */
float3 webglLoaderVoxPhysicalSpecular(float3 lightDirection,
                                      float3 viewDirection,
                                      float3 normal)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float normalDotLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float normalDotView = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float viewDotHalf = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float3 f0 = float3(0.04f);
    const float3 fresnel = webglLoaderVoxFresnel(f0, viewDotHalf);
    const float visibility = 0.5f / max(normalDotLight + normalDotView, 0.000001f);
    const float3 singleScatter = fresnel * (visibility * 0.3183098861837907f);
    const float2 dfgView = webglLoaderVoxDfg(normalDotView);
    const float2 dfgLight = webglLoaderVoxDfg(normalDotLight);
    const float3 viewEnergy = f0 * dfgView.x + float3(dfgView.y);
    const float3 lightEnergy = f0 * dfgLight.x + float3(dfgLight.y);
    const float viewMissing = 1.0f - dfgView.x - dfgView.y;
    const float lightMissing = 1.0f - dfgLight.x - dfgLight.y;
    const float3 averageFresnel = f0 + (float3(1.0f) - f0) * 0.047619f;
    const float3 multiple = viewEnergy * lightEnergy * averageFresnel /
                            (float3(1.0f) - averageFresnel * (viewMissing * lightMissing) +
                             float3(0.000001f));
    return singleScatter + multiple * (viewMissing * lightMissing);
}

/** Reproduces the one vertex-colored MeshStandardMaterial renderable. */
class WebglLoaderVoxMeshPass final : public IRenderClass
{
public:
    /** Enables opaque depth testing with the default back-face culling. */
    constructor(BindGroup<WebglLoaderVoxResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the locked scale and camera transform to one greedy-mesh vertex. */
    WebglLoaderVoxVertexOutput vertex(WebglLoaderVoxVertex inputValue [[VertexInput0]])
    {
        const float scale = resources->uniforms->cameraPositionAndScale.w;
        const float3 worldPosition = inputValue.position * scale;
        WebglLoaderVoxVertexOutput outputValue;
        outputValue.position =
            resources->uniforms->modelViewProjectionColumn0 * inputValue.position.x +
            resources->uniforms->modelViewProjectionColumn1 * inputValue.position.y +
            resources->uniforms->modelViewProjectionColumn2 * inputValue.position.z +
            resources->uniforms->modelViewProjectionColumn3;
        outputValue.position.x -=
            resources->uniforms->sampleOffsetAndPadding.x * 2.0f / 800.0f *
            outputValue.position.w;
        outputValue.position.y -=
            resources->uniforms->sampleOffsetAndPadding.y * 2.0f / 500.0f *
            outputValue.position.w;
        outputValue.worldPosition = worldPosition;
        outputValue.worldNormal = inputValue.normal;
        outputValue.color = inputValue.color;
        return outputValue;
    }

    /** Evaluates hemisphere and two-direction MeshStandard lighting plus output transfer. */
    WebglLoaderVoxFrameBuffer fragment(WebglLoaderVoxVertexOutput inputValue)
    {
        const float3 normal = normalize(inputValue.worldNormal);
        const float3 viewDirection = normalize(
            resources->uniforms->cameraPositionAndScale.xyz - inputValue.worldPosition);
        const float3 lightDirection0 =
            normalize(float3(resources->uniforms->lightDirection0.xyz));
        const float3 lightDirection1 =
            normalize(float3(resources->uniforms->lightDirection1.xyz));
        const float dotLight0 = clamp(dot(normal, lightDirection0), 0.0f, 1.0f);
        const float dotLight1 = clamp(dot(normal, lightDirection1), 0.0f, 1.0f);
        const float3 direct0 = float3(2.5f * dotLight0);
        const float3 direct1 = float3(1.5f * dotLight1);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 ground = float3(0.0578054302f);
        const float3 sky = float3(0.6038273389f);
        const float3 hemisphere = lerp(ground, sky, hemisphereWeight) * 3.0f;
        const float inversePi = 0.3183098861837907f;
        float3 linearColor =
            (direct0 + direct1 + hemisphere) * inputValue.color * inversePi;
        linearColor +=
            direct0 * webglLoaderVoxPhysicalSpecular(lightDirection0, viewDirection, normal) +
            direct1 * webglLoaderVoxPhysicalSpecular(lightDirection1, viewDirection, normal);
        WebglLoaderVoxFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            webglLoaderVoxLinearToSrgb(linearColor.x),
            webglLoaderVoxLinearToSrgb(linearColor.y),
            webglLoaderVoxLinearToSrgb(linearColor.z),
            1.0f);
        return frameBuffer;
    }
};

/** Owns one ordinary RenderClass and the decoded single VOX mesh. */
class Phase1LoaderVoxSimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglLoaderVoxVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglLoaderVoxUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer0;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    BindGroup<WebglLoaderVoxResources> resources0;
    RenderClass<WebglLoaderVoxMeshPass> meshPass0;
    uint indexCount = 0u;

public:
    /** Creates only the fixed uniform buffer before decoded geometry is available. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer0 = device->createBuffer("WebglLoaderVoxUniforms0", 1u);
    }

    /** Allocates the canonical color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        sceneDepth = device->createTexture("WebglLoaderVoxDepth", width, height, 1u);
        outputColor = device->createTexture("WebglLoaderVoxOutput", width, height, 1u);
    }

    /** Uploads deterministic greedy geometry and the selected Orbit camera. */
    void configureScene(const eastl::vector<WebglLoaderVoxVertex> &vertices,
                        const eastl::vector<uint> &indices,
                        WebglLoaderVoxUniforms uniforms)
    {
        vertexBuffer = device->createBuffer("WebglLoaderVoxVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer("WebglLoaderVoxIndices", uint(indices.size()));
        indexCount = uint(indices.size());
        uniforms.sampleOffsetAndPadding = float4(0.0f);
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                          uint64_t(vertices.size()) * sizeof(WebglLoaderVoxVertex))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(),
                          uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(BufferRange(uniformBuffer0), &uniforms, sizeof(uniforms))
            ->submit();
        resources0 = device->createBindGroup<WebglLoaderVoxResources>(uniformBuffer0);
        meshPass0 = device->createRenderClass<WebglLoaderVoxMeshPass>(resources0);
    }

    /** Draws and presents the only decoded VOX mesh. */
    void render() override
    {
        WebglLoaderVoxFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = sceneDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglLoaderVoxScene", frameBuffer,
                         meshPass0->setVertexBuffer(vertexBuffer),
                         meshPass0->setIndexBuffer(indexBuffer),
                         meshPass0(indexCount, 1u, 0u, 0, 0u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 target for readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the canonical output width. */
    uint getReadbackWidth() const { return 800u; }

    /** Returns the canonical output height. */
    uint getReadbackHeight() const { return 500u; }

    /** Releases the decoded mesh and output resources. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
