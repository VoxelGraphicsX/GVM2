#ifndef GVM_THREE_WEBGPU_POSTPROCESSING_RADIAL_BLUR_HPP
#define GVM_THREE_WEBGPU_POSTPROCESSING_RADIAL_BLUR_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one flat-shaded tetrahedron vertex. */
struct WebgpuPostprocessingRadialBlurVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores the one parent Group transform and camera projection. */
struct WebgpuPostprocessingRadialBlurObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
};

/** Stores one canonical tetrahedron transform and HSL color. */
struct WebgpuPostprocessingRadialBlurInstanceData
{
    float4 transformColumn0;
    float4 transformColumn1;
    float4 transformColumn2;
    float4 transformColumn3;
    float4 color;
};

/** Stores the standard-material scalar controls for the Scene entity. */
struct WebgpuPostprocessingRadialBlurMaterialData
{
    float4 roughnessMetalnessPadding;
};

/** Defines the only RenderSet owned by the radial-blur Scene. */
struct WebgpuPostprocessingRadialBlurSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry and all entity/instance components. */
    constructor(
        BufferComponent<WebgpuPostprocessingRadialBlurVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuPostprocessingRadialBlurObjectData> objects,
        BufferComponent<WebgpuPostprocessingRadialBlurInstanceData> instances,
        BufferComponent<WebgpuPostprocessingRadialBlurMaterialData> materials)
    {
    }
};

/** Stores viewport, blur, output, and deterministic inspector controls. */
struct WebgpuPostprocessingRadialBlurFrameData
{
    float4 viewportEnabledAnimated;
    float4 weightDecayExposureCount;
    float4 inspectorAndPadding;
};

/** Binds the immutable frame controls to Scene and screen passes. */
struct WebgpuPostprocessingRadialBlurFrameResources final : public IBindGroup
{
    /** Declares one uniform buffer shared by all dedicated passes. */
    constructor(
        UniformBuffer<WebgpuPostprocessingRadialBlurFrameData>
            frame [[Binding0]])
    {
    }
};

/** Binds the linear Scene image to the radial blur pass. */
struct WebgpuPostprocessingRadialBlurScreenResources final : public IBindGroup
{
    /** Declares the Scene texture, linear sampler, and blur controls. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]],
        UniformBuffer<WebgpuPostprocessingRadialBlurFrameData>
            frame [[Binding2]])
    {
    }
};

/** Carries lit surface inputs from one RenderSet entity instance. */
struct WebgpuPostprocessingRadialBlurVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float4 color [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Carries fullscreen coordinates into the radial blur graph. */
struct WebgpuPostprocessingRadialBlurScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear HDR Scene color and depth targets. */
struct WebgpuPostprocessingRadialBlurSceneFrameBuffer final
    : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the canonical final RGBA8 target. */
struct WebgpuPostprocessingRadialBlurOutputFrameBuffer final
    : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Encodes one linear output channel with the r185 sRGB transfer. */
float webgpuPostprocessingRadialBlurLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Applies the exact r185 NeutralToneMapping compression curve. */
float3 webgpuPostprocessingRadialBlurNeutralToneMap(float3 color)
{
    const float startCompression = 0.76f;
    const float desaturation = 0.15f;
    const float minimumChannel = min(color.x, min(color.y, color.z));
    const float offset = minimumChannel < 0.08f
        ? minimumChannel - 6.25f * minimumChannel * minimumChannel
        : 0.04f;
    color -= float3(offset);
    const float peak = max(color.x, max(color.y, color.z));
    if (peak < startCompression) return color;
    const float compressionRange = 1.0f - startCompression;
    const float newPeak =
        1.0f -
        compressionRange * compressionRange /
            (peak + compressionRange - startCompression);
    color *= newPeak / peak;
    const float desaturationWeight =
        1.0f -
        1.0f /
            (desaturation * (peak - newPeak) + 1.0f);
    return lerp(color, float3(newPeak), desaturationWeight);
}

/** Evaluates Three's optimized Schlick Fresnel approximation. */
float3 webgpuPostprocessingRadialBlurFresnel(
    float3 f0,
    float dotViewHalf)
{
    const float factor =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - factor) + float3(factor);
}

/** Returns one half-float DFG LUT row for roughness-one standard shading. */
float2 webgpuPostprocessingRadialBlurDfgRow(uint row)
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
float2 webgpuPostprocessingRadialBlurDfg(float normalDotDirection)
{
    const float coordinate =
        clamp(normalDotDirection, 0.0f, 1.0f) * 16.0f - 0.5f;
    if (coordinate <= 0.0f)
        return webgpuPostprocessingRadialBlurDfgRow(0u);
    if (coordinate >= 15.0f)
        return webgpuPostprocessingRadialBlurDfgRow(15u);
    const uint lower = uint(floor(coordinate));
    return lerp(
        webgpuPostprocessingRadialBlurDfgRow(lower),
        webgpuPostprocessingRadialBlurDfgRow(lower + 1u),
        coordinate - floor(coordinate));
}

/** Evaluates Three r185's roughness-one direct GGX multiscatter BRDF. */
float3 webgpuPostprocessingRadialBlurPhysicalSpecular(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal)
{
    const float3 halfDirection =
        normalize(lightDirection + viewDirection);
    const float normalDotLight =
        clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float normalDotView =
        clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float viewDotHalf =
        clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float3 f0 = float3(0.04f);
    const float3 fresnel =
        webgpuPostprocessingRadialBlurFresnel(f0, viewDotHalf);
    const float visibility =
        0.5f /
        max(normalDotLight + normalDotView, 0.000001f);
    const float3 singleScatter =
        fresnel * (visibility * 0.3183098861837907f);
    const float2 dfgView =
        webgpuPostprocessingRadialBlurDfg(normalDotView);
    const float2 dfgLight =
        webgpuPostprocessingRadialBlurDfg(normalDotLight);
    const float3 viewEnergy =
        f0 * dfgView.x + float3(dfgView.y);
    const float3 lightEnergy =
        f0 * dfgLight.x + float3(dfgLight.y);
    const float viewMissing =
        1.0f - dfgView.x - dfgView.y;
    const float lightMissing =
        1.0f - dfgLight.x - dfgLight.y;
    const float3 averageFresnel =
        f0 + (float3(1.0f) - f0) * 0.047619f;
    const float3 multiple =
        viewEnergy * lightEnergy * averageFresnel /
        (float3(1.0f) -
         averageFresnel * averageFresnel *
             (viewMissing * lightMissing) +
         float3(0.000001f));
    return singleScatter +
        multiple * (viewMissing * lightMissing);
}

/** Evaluates the r185 interleaved-gradient noise seed for one output pixel. */
float webgpuPostprocessingRadialBlurNoise(float2 pixel)
{
    return frac(
        52.9829189f *
        frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

/** Draws one hundred instances through the RenderSet-only indirect path. */
class WebgpuPostprocessingRadialBlurMainPass final : public IRenderClass
{
public:
    /** Binds the Scene's unique RenderSet and opaque standard state. */
    constructor(
        RenderSet<WebgpuPostprocessingRadialBlurSceneRenderSet>
            sceneSet [[Slot0]],
        BindGroup<WebgpuPostprocessingRadialBlurFrameResources>
            frameResources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Reads both entity and instance IDs and applies the parent hierarchy. */
    WebgpuPostprocessingRadialBlurVertexOutput vertex(
        WebgpuPostprocessingRadialBlurVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingRadialBlurObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingRadialBlurInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float4 localPosition =
            instanceData.transformColumn0 * inputValue.position.x +
            instanceData.transformColumn1 * inputValue.position.y +
            instanceData.transformColumn2 * inputValue.position.z +
            instanceData.transformColumn3 * inputValue.position.w;
        const float3 localNormal = normalize(
            instanceData.transformColumn0.xyz * inputValue.normal.x +
            instanceData.transformColumn1.xyz * inputValue.normal.y +
            instanceData.transformColumn2.xyz * inputValue.normal.z);
        const float4 viewPosition =
            mul(objectData.modelView, localPosition);
        const float4 viewNormal =
            mul(objectData.modelView, float4(localNormal, 0.0f));
        WebgpuPostprocessingRadialBlurVertexOutput outputValue;
        outputValue.position =
            mul(objectData.modelViewProjection, localPosition);
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal =
            normalize(float3(
                viewNormal.x,
                viewNormal.y,
                viewNormal.z));
        outputValue.color = instanceData.color;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates the locked hemisphere and central point-light response. */
    WebgpuPostprocessingRadialBlurSceneFrameBuffer fragment(
        WebgpuPostprocessingRadialBlurVertexOutput inputValue)
    {
        const float3 normal = normalize(inputValue.viewNormal);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere =
            lerp(float3(0.26635560f), float3(1.0f), hemisphereWeight);
        const float3 pointVector =
            float3(0.0f, 0.0f, -50.0f) -
            inputValue.viewPosition;
        const float pointDistanceSquared =
            max(dot(pointVector, pointVector), 0.0001f);
        const float pointDiffuse =
            max(dot(normal, normalize(pointVector)), 0.0f) *
            1000.0f / pointDistanceSquared;
        const float3 viewDirection =
            normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(pointVector);
        const float3 specular =
            pointDiffuse *
            webgpuPostprocessingRadialBlurPhysicalSpecular(
                lightDirection,
                viewDirection,
                normal);
        const float3 linearColor =
            inputValue.color.xyz *
                (hemisphere * 0.3183098862f + pointDiffuse * 0.3183098862f) +
            specular;
        WebgpuPostprocessingRadialBlurSceneFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Executes exact bounded radial accumulation or the disabled bypass. */
class WebgpuPostprocessingRadialBlurCompositePass final
    : public IRenderClass
{
public:
    /** Binds the Scene image and disables fullscreen culling. */
    constructor(
        BindGroup<WebgpuPostprocessingRadialBlurScreenResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingRadialBlurScreenOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuPostprocessingRadialBlurScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Applies noise-jittered sampling, Neutral tone mapping, and sRGB output. */
    WebgpuPostprocessingRadialBlurOutputFrameBuffer fragment(
        WebgpuPostprocessingRadialBlurScreenOutput inputValue)
    {
        const float4 frame0 =
            resources->frame->viewportEnabledAnimated;
        const float4 frame1 =
            resources->frame->weightDecayExposureCount;
        const float3 base =
            resources->sceneColor->sample(
                resources->sceneSampler,
                inputValue.uv).xyz;
        float3 color = base;
        if (frame0.z > 0.5f)
        {
            const uint sampleCount = uint(frame1.w);
            const float2 offset =
                (float2(0.5f) - inputValue.uv) /
                float(sampleCount);
            float2 sampleUv =
                inputValue.uv +
                offset *
                    webgpuPostprocessingRadialBlurNoise(
                        inputValue.uv * frame0.xy);
            float weight = frame1.x;
            float3 blur = float3(0.0f);
            for (uint sampleIndex = 0u;
                 sampleIndex < 64u;
                 ++sampleIndex)
            {
                if (sampleIndex < sampleCount)
                {
                    sampleUv += offset;
                    blur +=
                        resources->sceneColor->sample(
                            resources->sceneSampler,
                            sampleUv).xyz *
                        weight;
                    weight *= frame1.y;
                }
            }
            blur = blur / float(sampleCount) * frame1.z;
            color = lerp(blur, base * 2.0f, 0.5f);
        }
        color =
            webgpuPostprocessingRadialBlurNeutralToneMap(color);
        float3 encoded = float3(
            webgpuPostprocessingRadialBlurLinearToSrgb(color.x),
            webgpuPostprocessingRadialBlurLinearToSrgb(color.y),
            webgpuPostprocessingRadialBlurLinearToSrgb(color.z));
        WebgpuPostprocessingRadialBlurOutputFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated one-Set Scene and radial postprocessing chain. */
class WebgpuPostprocessingRadialBlurRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]]
    RenderSet<WebgpuPostprocessingRadialBlurSceneRenderSet> sceneSet;
    Buffer<WebgpuPostprocessingRadialBlurFrameData,
           BufferUsage<Uniform, CopyDst>> frameBuffer;
    BindGroup<WebgpuPostprocessingRadialBlurFrameResources> frameResources;
    BindGroup<WebgpuPostprocessingRadialBlurScreenResources> screenResources;
    RenderClass<WebgpuPostprocessingRadialBlurMainPass> mainPass;
    RenderClass<WebgpuPostprocessingRadialBlurCompositePass> compositePass;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Sampler sceneSampler;
    WebgpuPostprocessingRadialBlurFrameData frameData;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique RenderSet and immutable pass resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<
                WebgpuPostprocessingRadialBlurSceneRenderSet>();
        frameBuffer = device->createBuffer(
            "WebgpuPostprocessingRadialBlurFrame",
            1u);
        frameResources =
            device->createBindGroup<
                WebgpuPostprocessingRadialBlurFrameResources>(
                frameBuffer);
        mainPass =
            device->createRenderClass<
                WebgpuPostprocessingRadialBlurMainPass>(
                sceneSet,
                frameResources);
        sceneSampler = device->createSampler({
            .label = "WebgpuPostprocessingRadialBlurSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates the fixed Scene, depth, and final readback attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneColor = device->createTexture(
            "WebgpuPostprocessingRadialBlurScene",
            width,
            height,
            1u);
        sceneDepth = device->createTexture(
            "WebgpuPostprocessingRadialBlurDepth",
            width,
            height,
            1u);
        outputColor = device->createTexture(
            "WebgpuPostprocessingRadialBlurOutput",
            width,
            height,
            1u);
        screenResources =
            device->createBindGroup<
                WebgpuPostprocessingRadialBlurScreenResources>(
                sceneColor->createView(),
                sceneSampler,
                frameBuffer);
        compositePass =
            device->createRenderClass<
                WebgpuPostprocessingRadialBlurCompositePass>(
                screenResources);
    }

    /** Uploads the scenario-specific blur and animation controls. */
    void configureCase(
        float weight,
        float decay,
        float exposure,
        float count,
        float enabled,
        float animated,
        float inspectorEnabled)
    {
        frameData.viewportEnabledAnimated =
            float4(float(width), float(height), enabled, animated);
        frameData.weightDecayExposureCount =
            float4(weight, decay, exposure, count);
        frameData.inspectorAndPadding =
            float4(inspectorEnabled, 0.0f, 0.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(frameBuffer),
                &frameData,
                sizeof(frameData))
            ->submit();
    }

    /** Draws the Set once and executes the dedicated radial screen pass. */
    void render() override
    {
        sceneSet->update();
        WebgpuPostprocessingRadialBlurSceneFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = sceneColor->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        sceneFrameBuffer.depth = sceneDepth->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer.depth.depthClearValue = 1.0f;
        WebgpuPostprocessingRadialBlurOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputColor->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuPostprocessingRadialBlurScene",
                sceneFrameBuffer,
                mainPass())
            ->renderPass(
                "WebgpuPostprocessingRadialBlurComposite",
                outputFrameBuffer,
                compositePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the fixed readback width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the fixed readback height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the unique Set and every dedicated attachment. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(frameBuffer);
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
