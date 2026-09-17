#ifndef GVM_THREE_WEBGPU_VOLUME_PERLIN_RENDER_SET_HPP
#define GVM_THREE_WEBGPU_VOLUME_PERLIN_RENDER_SET_HPP

#include "Phase1VolumePerlinShared.hpp"

/** Defines the unique RenderSet for the WebGPU volume Scene. */
struct WebgpuVolumePerlinSceneRenderSet : public IRenderSet
{
    /** Declares consolidated BoxGeometry and mandatory Scene components. */
    constructor(
        BufferComponent<Phase1VolumePerlinVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<Phase1VolumePerlinObjectData> objects,
        BufferComponent<Phase1VolumePerlinInstanceData> instances,
        BufferComponent<Phase1VolumePerlinMaterialData> materials,
        (TextureComponent<half4, Phase1VolumePerlinMaxTextures> textures))
    {
    }
};

/** Binds the WebGPU volume camera, controls, and atlas sampler. */
struct WebgpuVolumePerlinResources final : public IBindGroup
{
    /** Declares the immutable frame and sampler layout. */
    constructor(
        UniformBuffer<Phase1VolumePerlinFrameData> frame [[Binding0]],
        Sampler atlasSampler [[Binding1]])
    {
    }
};

/** Binds the Scene color for the deterministic screen resolve. */
struct WebgpuVolumePerlinResolveResources final : public IBindGroup
{
    /** Declares the screen-only source texture and sampler. */
    constructor(
        Texture2D<half4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Draws Three r185's WebGPU volume through the Scene RenderSet. */
class WebgpuVolumePerlinScenePass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and its raymarch resources. */
    constructor(
        RenderSet<WebgpuVolumePerlinSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuVolumePerlinResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(false);
    }

private:
    /** Projects BoxGeometry while resolving every mandatory component and entity builtin. */
    Phase1VolumePerlinVertexOutput vertex(
        Phase1VolumePerlinVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const Phase1VolumePerlinObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const Phase1VolumePerlinInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const Phase1VolumePerlinMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const float3 world = inputValue.position.xyz;
        const float3 relative =
            world - resources->frame->cameraPosition.xyz;
        const float depth =
            dot(
                relative,
                float3(resources->frame->cameraForward.xyz));
        const float tangentHalfFov = 0.5773502691896258f;
        const float aspect =
            resources->frame->viewportAndOverlay.x /
            resources->frame->viewportAndOverlay.y;
        Phase1VolumePerlinVertexOutput outputValue;
        outputValue.position = float4(
            dot(
                relative,
                float3(resources->frame->cameraRight.xyz)) /
                (tangentHalfFov * aspect),
            -dot(
                relative,
                float3(resources->frame->cameraUp.xyz)) /
                tangentHalfFov,
            depth,
            depth);
        outputValue.position.x +=
            objectData.value.x +
            instanceData.value.x +
            materialData.value.x;
        outputValue.objectPosition = world;
        outputValue.cameraPosition =
            resources->frame->cameraPosition.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates the exact axis-adaptive WebGPU volume traversal and output transfer. */
    Phase1VolumePerlinFrameBuffer fragment(
        Phase1VolumePerlinVertexOutput inputValue)
    {
        auto atlas =
            sceneSet->textures->get(inputValue.entityID, 0u);
        const float3 rayDirection =
            normalize(
                inputValue.objectPosition -
                inputValue.cameraPosition);
        float2 bounds =
            phase1VolumeHitBox(
                inputValue.cameraPosition,
                rayDirection);
        if (bounds.x > bounds.y)
        {
            discard_fragment();
        }
        bounds.x = max(bounds.x, 0.0f);
        const float stepCount =
            resources->frame->raymarch.y;
        const float3 inverseAxis =
            float3(1.0f) / abs(rayDirection);
        const float delta =
            min(
                inverseAxis.x,
                min(inverseAxis.y, inverseAxis.z)) /
            stepCount;
        float3 position =
            inputValue.cameraPosition +
            bounds.x * rayDirection;
        float4 color = float4(0.0f);
        for (uint sampleIndex = 0u;
             sampleIndex < 300u;
             ++sampleIndex)
        {
            if (bounds.x + float(sampleIndex) * delta >= bounds.y)
            {
                break;
            }
            const float3 coordinate =
                position + float3(0.5f);
            const Phase1VolumeAtlasLookup densityLookup =
                phase1VolumeResolveAtlasLookup(coordinate);
            const float density =
                lerp(
                    float(
                        atlas->sample(
                            resources->atlasSampler,
                            densityLookup.lowerCoordinate).x),
                    float(
                        atlas->sample(
                            resources->atlasSampler,
                            densityLookup.upperCoordinate).x),
                    densityLookup.fraction);
            if (density > resources->frame->raymarch.x)
            {
                float3 normal;
                const float epsilon = 0.0001f;
                if (coordinate.x < epsilon)
                {
                    normal = float3(1.0f, 0.0f, 0.0f);
                }
                else if (coordinate.y < epsilon)
                {
                    normal = float3(0.0f, 1.0f, 0.0f);
                }
                else if (coordinate.z < epsilon)
                {
                    normal = float3(0.0f, 0.0f, 1.0f);
                }
                else if (coordinate.x > 1.0f - epsilon)
                {
                    normal = float3(-1.0f, 0.0f, 0.0f);
                }
                else if (coordinate.y > 1.0f - epsilon)
                {
                    normal = float3(0.0f, -1.0f, 0.0f);
                }
                else if (coordinate.z > 1.0f - epsilon)
                {
                    normal = float3(0.0f, 0.0f, -1.0f);
                }
                else
                {
                    const float normalStep = 0.01f;
                    const Phase1VolumeAtlasLookup lookupX0 =
                        phase1VolumeResolveAtlasLookup(
                            coordinate +
                            float3(-normalStep, 0.0f, 0.0f));
                    const float sampleX0 =
                        lerp(
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupX0.lowerCoordinate).x),
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupX0.upperCoordinate).x),
                            lookupX0.fraction);
                    const Phase1VolumeAtlasLookup lookupX1 =
                        phase1VolumeResolveAtlasLookup(
                            coordinate +
                            float3(normalStep, 0.0f, 0.0f));
                    const float sampleX1 =
                        lerp(
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupX1.lowerCoordinate).x),
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupX1.upperCoordinate).x),
                            lookupX1.fraction);
                    const Phase1VolumeAtlasLookup lookupY0 =
                        phase1VolumeResolveAtlasLookup(
                            coordinate +
                            float3(0.0f, -normalStep, 0.0f));
                    const float sampleY0 =
                        lerp(
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupY0.lowerCoordinate).x),
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupY0.upperCoordinate).x),
                            lookupY0.fraction);
                    const Phase1VolumeAtlasLookup lookupY1 =
                        phase1VolumeResolveAtlasLookup(
                            coordinate +
                            float3(0.0f, normalStep, 0.0f));
                    const float sampleY1 =
                        lerp(
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupY1.lowerCoordinate).x),
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupY1.upperCoordinate).x),
                            lookupY1.fraction);
                    const Phase1VolumeAtlasLookup lookupZ0 =
                        phase1VolumeResolveAtlasLookup(
                            coordinate +
                            float3(0.0f, 0.0f, -normalStep));
                    const float sampleZ0 =
                        lerp(
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupZ0.lowerCoordinate).x),
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupZ0.upperCoordinate).x),
                            lookupZ0.fraction);
                    const Phase1VolumeAtlasLookup lookupZ1 =
                        phase1VolumeResolveAtlasLookup(
                            coordinate +
                            float3(0.0f, 0.0f, normalStep));
                    const float sampleZ1 =
                        lerp(
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupZ1.lowerCoordinate).x),
                            float(atlas->sample(
                                resources->atlasSampler,
                                lookupZ1.upperCoordinate).x),
                            lookupZ1.fraction);
                    normal = normalize(
                        float3(
                            sampleX0 - sampleX1,
                            sampleY0 - sampleY1,
                            sampleZ0 - sampleZ1));
                }
                const float3 linearColor =
                    normal * 0.5f +
                    position * 1.5f +
                    float3(0.25f);
                color = float4(
                    phase1VolumeLinearToSrgb(linearColor.x),
                    phase1VolumeLinearToSrgb(linearColor.y),
                    phase1VolumeLinearToSrgb(linearColor.z),
                    1.0f);
                break;
            }
            position += rayDirection * delta;
        }
        if (color.w == 0.0f)
        {
            discard_fragment();
        }
        Phase1VolumePerlinFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Carries fullscreen coordinates into the deterministic resolve pass. */
struct WebgpuVolumePerlinResolveVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Resolves the Scene target before Inspector composition. */
class WebgpuVolumePerlinResolvePass final : public IRenderClass
{
public:
    /** Binds only screen-space resources. */
    constructor(
        BindGroup<WebgpuVolumePerlinResolveResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuVolumePerlinResolveVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuVolumePerlinResolveVertexOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Copies one resolved Scene pixel to the final deterministic target. */
    Phase1VolumePerlinFrameBuffer fragment(
        WebgpuVolumePerlinResolveVertexOutput inputValue)
    {
        Phase1VolumePerlinFrameBuffer frameBuffer;
        frameBuffer.color =
            resources->sceneColor->sample(
                resources->sceneSampler,
                inputValue.uv);
        return frameBuffer;
    }
};

/** Carries fullscreen coordinates into the WebGPU Inspector overlay pass. */
struct WebgpuVolumePerlinInspectorVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Reproduces the minimized r185 Inspector bar as a screen-only pass. */
class WebgpuVolumePerlinInspectorPass final : public IRenderClass
{
public:
    /** Binds the frame flag controlling the initial-capture overlay. */
    constructor(BindGroup<WebgpuVolumePerlinResources> resources [[Slot0]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without introducing another Scene RenderSet. */
    WebgpuVolumePerlinInspectorVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuVolumePerlinInspectorVertexOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Draws the exact minimized-bar geometry and captured translucent colors. */
    Phase1VolumePerlinFrameBuffer fragment(
        WebgpuVolumePerlinInspectorVertexOutput inputValue)
    {
        if (resources->frame->viewportAndOverlay.z < 0.5f)
        {
            discard_fragment();
        }
        const float2 pixel =
            inputValue.uv *
            resources->frame->viewportAndOverlay.xy;
        const float2 center = float2(699.5f, 33.5f);
        const float2 halfExtent = float2(85.5f, 18.5f);
        const float cornerRadius =
            pixel.x < center.x ? 12.0f : 6.0f;
        const float2 delta =
            abs(pixel - center) -
            (halfExtent - float2(cornerRadius));
        const float roundedDistance =
            length(max(delta, float2(0.0f))) +
            min(max(delta.x, delta.y), 0.0f) -
            cornerRadius;
        if (roundedDistance > 0.5f)
        {
            const float2 shadowCenter =
                float2(699.5f, 37.5f);
            const float2 shadowDelta =
                abs(pixel - shadowCenter) -
                (halfExtent - float2(cornerRadius));
            const float shadowDistance =
                length(max(shadowDelta, float2(0.0f))) +
                min(max(shadowDelta.x, shadowDelta.y), 0.0f) -
                cornerRadius;
            const float shadowAlpha =
                0.13f *
                exp(
                    -max(shadowDistance, 0.0f) *
                    max(shadowDistance, 0.0f) /
                    72.0f);
            if (shadowAlpha < 0.004f)
            {
                discard_fragment();
            }
            Phase1VolumePerlinFrameBuffer shadow;
            shadow.color =
                half4(half3(float3(0.0f)), half(shadowAlpha));
            return shadow;
        }
        float3 color = float3(30.0f, 30.0f, 36.0f) / 255.0f;
        float alpha = 0.85f;
        if (pixel.x < 663.0f)
        {
            color =
                float3(23.1818f, 61.8182f, 85.7727f) /
                255.0f;
            alpha = 0.88f;
        }
        if (roundedDistance > -1.0f)
        {
            color = float3(46.1f, 46.1f, 55.8f) / 255.0f;
            alpha = 0.899f;
        }
        alpha *= clamp(0.5f - roundedDistance, 0.0f, 1.0f);
        Phase1VolumePerlinFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(alpha));
        return frameBuffer;
    }
};

/** Owns the dedicated WebGPU volume RenderSet, Scene pass, and Inspector pass. */
class Phase1WebgpuVolumePerlinRenderSetRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuVolumePerlinSceneRenderSet> sceneSet;
    Buffer<Phase1VolumePerlinFrameData, BufferUsage<Uniform, CopyDst>>
        frameBuffer;
    BindGroup<WebgpuVolumePerlinResources> resources;
    RenderClass<WebgpuVolumePerlinScenePass> scenePass;
    BindGroup<WebgpuVolumePerlinResolveResources> resolveResources;
    RenderClass<WebgpuVolumePerlinResolvePass> resolvePass;
    RenderClass<WebgpuVolumePerlinInspectorPass> inspectorPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        sceneColor;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputColor;
    Sampler atlasSampler;
    Phase1VolumePerlinFrameData frameData;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Scene RenderSet and dedicated WebGPU passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuVolumePerlinSceneRenderSet>();
        frameBuffer =
            device->createBuffer("WebgpuVolumePerlinFrameData", 1u);
        atlasSampler = device->createSampler({
            .label = "WebgpuVolumePerlinAtlasSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0,
            .lodMaxClamp = 0,
            .maxAnisotropy = 1,
        });
        resources =
            device->createBindGroup<WebgpuVolumePerlinResources>(
                frameBuffer,
                atlasSampler);
        scenePass =
            device->createRenderClass<WebgpuVolumePerlinScenePass>(
                sceneSet,
                resources);
        inspectorPass =
            device->createRenderClass<WebgpuVolumePerlinInspectorPass>(
                resources);
    }

    /** Allocates the host-sized deterministic RGBA8 target. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor =
            device->createTexture(
                "WebgpuVolumePerlinOutput",
                width,
                height,
                1u);
        sceneColor =
            device->createTexture(
                "WebgpuVolumePerlinSceneColor",
                width,
                height,
                1u);
        resolveResources =
            device->createBindGroup<WebgpuVolumePerlinResolveResources>(
                sceneColor->createView(),
                atlasSampler);
        resolvePass =
            device->createRenderClass<WebgpuVolumePerlinResolvePass>(
                resolveResources);
    }

    /** Uploads one canonical volume camera and raymarch configuration. */
    void configureVolume(
        float4 cameraPosition,
        float4 cameraRight,
        float4 cameraUp,
        float4 cameraForward,
        float4 raymarch,
        float4 viewportAndOverlay)
    {
        frameData.cameraPosition = cameraPosition;
        frameData.cameraRight = cameraRight;
        frameData.cameraUp = cameraUp;
        frameData.cameraForward = cameraForward;
        frameData.raymarch = raymarch;
        frameData.viewportAndOverlay = viewportAndOverlay;
        frameData.viewportAndOverlay.x = float(readbackWidth);
        frameData.viewportAndOverlay.y = float(readbackHeight);
        graphicsQueue
            ->writeBuffer(
                BufferRange(frameBuffer),
                &frameData,
                sizeof(frameData))
            ->submit();
    }

    /** Draws the unique Scene RenderSet and then the screen-only Inspector. */
    void render() override
    {
        sceneSet->update();
        Phase1VolumePerlinFrameBuffer sceneTarget;
        sceneTarget.color = sceneColor->createView();
        sceneTarget.color.loadOp = LoadOp::Clear;
        sceneTarget.color.storeOp = StoreOp::Store;
        sceneTarget.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        Phase1VolumePerlinFrameBuffer resolveTarget;
        resolveTarget.color = outputColor->createView();
        resolveTarget.color.loadOp = LoadOp::Clear;
        resolveTarget.color.storeOp = StoreOp::Store;
        resolveTarget.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        Phase1VolumePerlinFrameBuffer inspectorTarget;
        inspectorTarget.color = outputColor->createView();
        inspectorTarget.color.loadOp = LoadOp::Load;
        inspectorTarget.color.storeOp = StoreOp::Store;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuVolumePerlinScene",
                sceneTarget,
                scenePass())
            ->renderPass(
                "WebgpuVolumePerlinResolve",
                resolveTarget,
                resolvePass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuVolumePerlinInspector",
                inspectorTarget,
                inspectorPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 readback target. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases every dedicated WebGPU volume resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(frameBuffer);
        device->freeTexture(sceneColor);
        device->freeTexture(outputColor);
    }
};

#endif
