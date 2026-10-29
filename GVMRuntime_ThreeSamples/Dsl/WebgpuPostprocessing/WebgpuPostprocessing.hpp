#ifndef GVM_THREE_WEBGPU_POSTPROCESSING_HPP
#define GVM_THREE_WEBGPU_POSTPROCESSING_HPP

#include "UGL.h"
#include "WebgpuPostprocessingData.hpp"

using namespace UGL;

/** Stores one ordinary child Mesh transform in view and clip spaces. */
struct WebgpuPostprocessingObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalModelView;
};

/** Stores the mandatory identity instance payload. */
struct WebgpuPostprocessingInstanceData
{
    float4 reserved;
};

/** Stores the shared white Phong material constants. */
struct WebgpuPostprocessingMaterialData
{
    float4 diffuseAndShininess;
    float4 specular;
};

/** Stores the runtime DotScreen uniforms to preserve WebGPU shader evaluation. */
struct WebgpuPostprocessingEffectParameters
{
    float4 angleScaleAndTextureSize;
};

/** Defines the unique Scene RenderSet containing 100 ordinary Mesh entities. */
struct WebgpuPostprocessingSceneRenderSet : public IRenderSet
{
    /** Declares the frozen five-component Scene ABI. */
    constructor(
        BufferComponent<WebgpuPostprocessingVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuPostprocessingObjectData> objects,
        BufferComponent<WebgpuPostprocessingInstanceData> instances,
        BufferComponent<WebgpuPostprocessingMaterialData> materials)
    {
    }
};

/** Binds one fullscreen half-float source texture. */
struct WebgpuPostprocessingScreenResources final : public IBindGroup
{
    /** Declares the source texture and linear clamp sampler. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        UniformBuffer<WebgpuPostprocessingEffectParameters>
            parameters [[Binding2]])
    {
    }
};

/** Carries view-space Phong and fog inputs. */
struct WebgpuPostprocessingSceneOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
};

/** Carries fullscreen coordinates. */
struct WebgpuPostprocessingScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear Scene and postprocess target. */
struct WebgpuPostprocessingLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a color-only linear postprocess target. */
struct WebgpuPostprocessingLinearColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final ordinary single-sample RGBA8 output. */
struct WebgpuPostprocessingOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear channel to Three r185 output sRGB. */
float webgpuPostprocessingLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped < 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666667f) * 1.055f - 0.055f;
}

/** Emits the shared fullscreen triangle. */
WebgpuPostprocessingScreenOutput webgpuPostprocessingFullscreen(uint vertexID)
{
    const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebgpuPostprocessingScreenOutput outputValue;
    outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Evaluates Three r185's optimized Schlick Fresnel term. */
float3 webgpuPostprocessingFresnel(float3 f0, float dotViewHalf)
{
    const float fresnel =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(fresnel);
}

/** Draws all 100 ordinary child Mesh entities through the unique Scene Set. */
class WebgpuPostprocessingMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebgpuPostprocessingSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebgpuPostprocessingSceneOutput vertex(
        WebgpuPostprocessingVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        (void)instanceData;
        const float4 viewPosition =
            mul(objectData.modelView, inputValue.position);
        float4 clipPosition = mul(
            objectData.modelViewProjection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuPostprocessingSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebgpuPostprocessingLinearFrameBuffer fragment(
        WebgpuPostprocessingSceneOutput inputValue)
    {
        // MeshPhong's flat shading path is reconstructed from the fragment
        // position derivatives, matching the r185 TSL normalFlat evaluation.
        float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        // The RenderSet flips clip-space Y before rasterization.  Reorient
        // the derivative normal against the transported face normal so both
        // backends preserve Three's front-face lighting after that flip.
        if (dot(normal, inputValue.viewNormal) < 0.0f)
            normal = -normal;
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(1.0f, 1.0f, 1.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float inversePi = 0.3183098861837907f;
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(inversePi);
        const float3 indirectDiffuse =
            float3(0.60382736f) * float3(inversePi);
        const float distribution =
            inversePi * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webgpuPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        float3 linearColor =
            directDiffuse + directSpecular + indirectDiffuse;
        // This scene uses THREE.Fog(0x000000, 1, 1000).  TSL's
        // rangeFogFactor is smoothstep(near, far, -viewZ), so preserve the
        // exact cubic Hermite interpolation before mixing with black fog.
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebgpuPostprocessingLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Applies the legacy EffectComposer DotScreenShader at scale four. */
class WebgpuPostprocessingDotScreenPass final : public IRenderClass
{
public:
    /** Binds the Scene texture without depth or blending. */
    constructor(
        BindGroup<WebgpuPostprocessingScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Reproduces DotScreenShader's rotated sine-product pattern. */
    WebgpuPostprocessingLinearColorFrameBuffer fragment(
        WebgpuPostprocessingScreenOutput inputValue)
    {
        const float4 sourceColor = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        const float angle =
            resources->parameters->angleScaleAndTextureSize.x;
        const float sineValue = sin(angle);
        const float cosineValue = cos(angle);
        const float2 textureCoordinate = float2(
            inputValue.uv.x *
                resources->parameters->angleScaleAndTextureSize.z,
            inputValue.uv.y *
                resources->parameters->angleScaleAndTextureSize.w);
        const float2 patternCoordinate = float2(
            cosineValue * textureCoordinate.x -
                sineValue * textureCoordinate.y,
                sineValue * textureCoordinate.x +
                cosineValue * textureCoordinate.y) *
            resources->parameters->angleScaleAndTextureSize.y;
        const float pattern =
            sin(patternCoordinate.x) * sin(patternCoordinate.y) * 4.0f;
        const float average =
            (sourceColor.r + sourceColor.g + sourceColor.b) / 3.0f;
        WebgpuPostprocessingLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(float3(average * 10.0f - 5.0f + pattern)),
            half(sourceColor.a));
        return frameBuffer;
    }
};

/** Applies the legacy RGBShiftShader with its locked horizontal amount. */
class WebgpuPostprocessingRgbShiftPass final : public IRenderClass
{
public:
    /** Binds the dot-screen result without depth or blending. */
    constructor(
        BindGroup<WebgpuPostprocessingScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Selects red and blue from opposite horizontal offsets. */
    WebgpuPostprocessingLinearColorFrameBuffer fragment(
        WebgpuPostprocessingScreenOutput inputValue)
    {
        // The r185 example explicitly overrides RGBShiftNode.amount to 0.001.
        const float2 offset = float2(0.001f, 0.0f);
        const float red = float(resources->source->sample(
            resources->sourceSampler, inputValue.uv + offset).r);
        const float green = float(resources->source->sample(
            resources->sourceSampler, inputValue.uv).g);
        const float blue = float(resources->source->sample(
            resources->sourceSampler, inputValue.uv - offset).b);
        const float alpha = float(resources->source->sample(
            resources->sourceSampler, inputValue.uv).a);
        WebgpuPostprocessingLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(red), half(green), half(blue), half(alpha));
        return frameBuffer;
    }
};

/** Performs the explicit r185 output color conversion. */
class WebgpuPostprocessingOutputPass final : public IRenderClass
{
public:
    /** Binds the shifted linear result without depth or blending. */
    constructor(
        BindGroup<WebgpuPostprocessingScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Converts each linear channel to display sRGB. */
    WebgpuPostprocessingOutputFrameBuffer fragment(
        WebgpuPostprocessingScreenOutput inputValue)
    {
        const float4 sourceColor = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        WebgpuPostprocessingOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuPostprocessingLinearToSrgb(sourceColor.r)),
            half(webgpuPostprocessingLinearToSrgb(sourceColor.g)),
            half(webgpuPostprocessingLinearToSrgb(sourceColor.b)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set and the three ordered fullscreen effects. */
class WebgpuPostprocessingRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuPostprocessingSceneRenderSet> sceneSet;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> dotTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> shiftedTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler linearSampler;
    Buffer<WebgpuPostprocessingEffectParameters,
           BufferUsage<Uniform, CopyDst>> effectParameterBuffer;
    BindGroup<WebgpuPostprocessingScreenResources> sceneScreenResources;
    BindGroup<WebgpuPostprocessingScreenResources> dotScreenResources;
    BindGroup<WebgpuPostprocessingScreenResources> shiftedScreenResources;
    RenderClass<WebgpuPostprocessingMainPass> mainPass;
    RenderClass<WebgpuPostprocessingDotScreenPass> dotPass;
    RenderClass<WebgpuPostprocessingRgbShiftPass> rgbShiftPass;
    RenderClass<WebgpuPostprocessingOutputPass> outputPass;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene RenderSet and its Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuPostprocessingSceneRenderSet>();
        mainPass = device->createRenderClass<WebgpuPostprocessingMainPass>(
            sceneSet);
        linearSampler = device->createSampler({
            .label = "WebgpuPostprocessingLinearSampler",
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
        effectParameterBuffer = device->createBuffer(
            "WebgpuPostprocessingEffectParameters", 1u);
    }

    /** Allocates all ordinary single-sample intermediate and output textures. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebgpuPostprocessingScene", width, height, 1u);
        dotTexture = device->createTexture(
            "WebgpuPostprocessingDot", width, height, 1u);
        shiftedTexture = device->createTexture(
            "WebgpuPostprocessingShifted", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebgpuPostprocessingDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebgpuPostprocessingOutput", width, height, 1u);
        sceneScreenResources = device->createBindGroup<
            WebgpuPostprocessingScreenResources>(
                sceneTexture->createView(), linearSampler,
                effectParameterBuffer);
        dotScreenResources = device->createBindGroup<
            WebgpuPostprocessingScreenResources>(
                dotTexture->createView(), linearSampler,
                effectParameterBuffer);
        shiftedScreenResources = device->createBindGroup<
            WebgpuPostprocessingScreenResources>(
                shiftedTexture->createView(), linearSampler,
                effectParameterBuffer);
        WebgpuPostprocessingEffectParameters parameters;
        parameters.angleScaleAndTextureSize =
            float4(1.57f, 0.3f, float(width), float(height));
        graphicsQueue->writeBuffer(
            BufferRange(effectParameterBuffer),
            &parameters, sizeof(parameters))->submit();
        dotPass = device->createRenderClass<
            WebgpuPostprocessingDotScreenPass>(sceneScreenResources);
        rgbShiftPass = device->createRenderClass<
            WebgpuPostprocessingRgbShiftPass>(dotScreenResources);
        outputPass = device->createRenderClass<
            WebgpuPostprocessingOutputPass>(shiftedScreenResources);
    }

    /** Renders the Set once, then DotScreen, RGBShift, output, and Present. */
    void render() override
    {
        sceneSet->update();
        WebgpuPostprocessingLinearFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebgpuPostprocessingLinearColorFrameBuffer dotFrame;
        dotFrame.color = dotTexture->createView();
        dotFrame.color.loadOp = LoadOp::Clear;
        dotFrame.color.storeOp = StoreOp::Store;
        WebgpuPostprocessingLinearColorFrameBuffer shiftedFrame;
        shiftedFrame.color = shiftedTexture->createView();
        shiftedFrame.color.loadOp = LoadOp::Clear;
        shiftedFrame.color.storeOp = StoreOp::Store;
        WebgpuPostprocessingOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuPostprocessingMain", sceneFrame, mainPass())
            ->renderPass(
                "WebgpuPostprocessingDotScreen", dotFrame,
                dotPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuPostprocessingRgbShift", shiftedFrame,
                rgbShiftPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuPostprocessingOutput", outputFrame,
                outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the Scene Set and every postprocessing attachment. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(dotTexture);
        device->freeTexture(shiftedTexture);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputTexture);
        device->freeBuffer(effectParameterBuffer);
    }
};

#endif
