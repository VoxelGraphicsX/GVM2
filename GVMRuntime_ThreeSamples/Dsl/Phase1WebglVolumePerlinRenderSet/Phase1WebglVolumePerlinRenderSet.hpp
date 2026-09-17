#ifndef GVM_THREE_WEBGL_VOLUME_PERLIN_RENDER_SET_HPP
#define GVM_THREE_WEBGL_VOLUME_PERLIN_RENDER_SET_HPP

#include "Phase1VolumePerlinShared.hpp"

/** Defines the unique RenderSet for the WebGL volume Scene. */
struct WebglVolumePerlinSceneRenderSet : public IRenderSet
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

/** Binds the WebGL volume camera, controls, and atlas sampler. */
struct WebglVolumePerlinResources final : public IBindGroup
{
    /** Declares the immutable frame and sampler layout. */
    constructor(
        UniformBuffer<Phase1VolumePerlinFrameData> frame [[Binding0]],
        Sampler atlasSampler [[Binding1]])
    {
    }
};

/** Binds the Scene color for the deterministic screen resolve. */
struct WebglVolumePerlinResolveResources final : public IBindGroup
{
    /** Declares the screen-only source texture and sampler. */
    constructor(
        Texture2D<half4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Draws Three r185's WebGL volume through the Scene RenderSet. */
class WebglVolumePerlinScenePass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and its raymarch resources. */
    constructor(
        RenderSet<WebglVolumePerlinSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglVolumePerlinResources> resources [[Slot1]])
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

    /** Evaluates the exact fixed-count WebGL volume traversal and output transfer. */
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
        const float delta =
            (bounds.y - bounds.x) / stepCount;
        float3 position =
            inputValue.cameraPosition +
            bounds.x * rayDirection;
        float4 color = float4(0.0f);
        for (uint sampleIndex = 0u;
             sampleIndex < 300u;
             ++sampleIndex)
        {
            if (float(sampleIndex) >= stepCount)
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
                color = float4(linearColor, 1.0f);
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
struct WebglVolumePerlinResolveVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Resolves the Scene target without introducing standalone Scene geometry. */
class WebglVolumePerlinResolvePass final : public IRenderClass
{
public:
    /** Binds only screen-space resources. */
    constructor(
        BindGroup<WebglVolumePerlinResolveResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglVolumePerlinResolveVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglVolumePerlinResolveVertexOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Copies one resolved Scene pixel to the final deterministic target. */
    Phase1VolumePerlinFrameBuffer fragment(
        WebglVolumePerlinResolveVertexOutput inputValue)
    {
        Phase1VolumePerlinFrameBuffer frameBuffer;
        frameBuffer.color =
            resources->sceneColor->sample(
                resources->sceneSampler,
                inputValue.uv);
        return frameBuffer;
    }
};

/** Owns the dedicated WebGL volume RenderSet and output target. */
class Phase1WebglVolumePerlinRenderSetRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglVolumePerlinSceneRenderSet> sceneSet;
    Buffer<Phase1VolumePerlinFrameData, BufferUsage<Uniform, CopyDst>>
        frameBuffer;
    BindGroup<WebglVolumePerlinResources> resources;
    RenderClass<WebglVolumePerlinScenePass> scenePass;
    BindGroup<WebglVolumePerlinResolveResources> resolveResources;
    RenderClass<WebglVolumePerlinResolvePass> resolvePass;
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
    /** Creates the unique Scene RenderSet, atlas sampler, and Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebglVolumePerlinSceneRenderSet>();
        frameBuffer =
            device->createBuffer("WebglVolumePerlinFrameData", 1u);
        atlasSampler = device->createSampler({
            .label = "WebglVolumePerlinAtlasSampler",
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
            device->createBindGroup<WebglVolumePerlinResources>(
                frameBuffer,
                atlasSampler);
        scenePass =
            device->createRenderClass<WebglVolumePerlinScenePass>(
                sceneSet,
                resources);
    }

    /** Allocates the host-sized deterministic RGBA8 target. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor =
            device->createTexture(
                "WebglVolumePerlinOutput",
                width,
                height,
                1u);
        sceneColor =
            device->createTexture(
                "WebglVolumePerlinSceneColor",
                width,
                height,
                1u);
        resolveResources =
            device->createBindGroup<WebglVolumePerlinResolveResources>(
                sceneColor->createView(),
                atlasSampler);
        resolvePass =
            device->createRenderClass<WebglVolumePerlinResolvePass>(
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

    /** Draws the unique Scene RenderSet through indexed-indirect metadata. */
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
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglVolumePerlinScene",
                sceneTarget,
                scenePass())
            ->renderPass(
                "WebglVolumePerlinResolve",
                resolveTarget,
                resolvePass(3u, 1u, 0u, 0u))
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

    /** Releases every dedicated WebGL volume resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(frameBuffer);
        device->freeTexture(sceneColor);
        device->freeTexture(outputColor);
    }
};

#endif
