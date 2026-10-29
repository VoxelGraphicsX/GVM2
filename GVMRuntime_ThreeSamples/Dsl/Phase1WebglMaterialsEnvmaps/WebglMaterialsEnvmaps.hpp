#pragma once

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglMaterialsEnvmapsCubeFaceSize = 1024u;
static const uint WebglMaterialsEnvmapsCubeMipCount = 11u;
static const uint WebglMaterialsEnvmapsAtlasWidth = 3078u;
static const uint WebglMaterialsEnvmapsAtlasHeight = 4138u;

/** Stores one non-index-shared r185 IcosahedronGeometry vertex. */
struct WebglMaterialsEnvmapsVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores the deterministic camera, environment mode, and synchronized rotations. */
struct WebglMaterialsEnvmapsUniforms
{
    float4 cameraPositionAndMode;
    float4 cameraRightAndRefraction;
    float4 cameraUpAndFrame;
    float4 cameraForwardAndTanHalfFov;
    float4 rotationRow0;
    float4 rotationRow1;
    float4 rotationRow2;
    float4 viewportAndReserved;
};

/** Binds all six uploaded Bridge2 faces to the private atlas compute pass. */
struct WebglMaterialsEnvmapsAtlasResources final : public IBindGroup
{
    /** Declares the face textures, mip sampler, and writable guttered atlas. */
    constructor(
        Texture2D<float4> positiveX [[Binding0]],
        Texture2D<float4> negativeX [[Binding1]],
        Texture2D<float4> positiveY [[Binding2]],
        Texture2D<float4> negativeY [[Binding3]],
        Texture2D<float4> positiveZ [[Binding4]],
        Texture2D<float4> negativeZ [[Binding5]],
        Sampler cubeSampler [[Binding6]],
        RWTexture2D<TextureFormat::RGBA16Float> atlas [[Binding7]])
    {
    }
};

/** Binds the generated cube atlas, equirectangular texture, and frame state. */
struct WebglMaterialsEnvmapsSceneResources final : public IBindGroup
{
    /** Declares every resource shared by the background and sphere passes. */
    constructor(
        Texture2D<float4> cubeAtlas [[Binding0]],
        Texture2D<float4> equirectangular [[Binding1]],
        Sampler environmentSampler [[Binding2]],
        UniformBuffer<WebglMaterialsEnvmapsUniforms> uniforms [[Binding3]],
        Texture2D<float4> positiveX [[Binding4]],
        Texture2D<float4> negativeX [[Binding5]],
        Texture2D<float4> positiveY [[Binding6]],
        Texture2D<float4> negativeY [[Binding7]],
        Texture2D<float4> positiveZ [[Binding8]],
        Texture2D<float4> negativeZ [[Binding9]])
    {
    }
};

/** Binds the linear Scene result to the final output transfer pass. */
struct WebglMaterialsEnvmapsOutputResources final : public IBindGroup
{
    /** Declares the linear Scene texture and exact nearest output sampler. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler outputSampler [[Binding1]])
    {
    }
};

/** Carries a fullscreen direction reconstruction coordinate. */
struct WebglMaterialsEnvmapsScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Carries projected sphere data and the vertex-computed reflection vector. */
struct WebglMaterialsEnvmapsSphereOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float3 environmentDirection [[Attribute2]];
};

/** Defines the linear Scene color and sphere depth attachments. */
struct WebglMaterialsEnvmapsSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final single-sample RGBA8 attachment. */
struct WebglMaterialsEnvmapsOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear working-space channel to the r185 sRGB transfer. */
float WebglMaterialsEnvmapsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Returns the stacked-atlas Y offset for one explicit cube mip. */
uint WebglMaterialsEnvmapsAtlasMipOffset(uint mipLevel)
{
    return 4096u - (4096u >> mipLevel) + mipLevel * 4u;
}

/** Rotates one direction by Three's transposed intrinsic XYZ matrix. */
float3 WebglMaterialsEnvmapsRotate(
    float4 rotationRow0,
    float4 rotationRow1,
    float4 rotationRow2,
    float3 direction)
{
    const float rotatedX =
        rotationRow0.x * direction.x +
        rotationRow0.y * direction.y +
        rotationRow0.z * direction.z;
    const float rotatedY =
        rotationRow1.x * direction.x +
        rotationRow1.y * direction.y +
        rotationRow1.z * direction.z;
    const float rotatedZ =
        rotationRow2.x * direction.x +
        rotationRow2.y * direction.y +
        rotationRow2.z * direction.z;
    return float3(rotatedX, rotatedY, rotatedZ);
}

/** Selects a right-handed cube face and returns its normalized face UV. */
float3 WebglMaterialsEnvmapsCubeFaceUv(float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    float face = 0.0f;
    float2 uv = float2(0.0f);
    if (absoluteDirection.x > absoluteDirection.z)
    {
        if (absoluteDirection.x > absoluteDirection.y)
        {
            if (direction.x > 0.0f)
            {
                face = 3.0f;
                uv = float2(direction.z, -direction.y) /
                    absoluteDirection.x;
            }
            else
            {
                face = 0.0f;
                uv = float2(-direction.z, -direction.y) /
                    absoluteDirection.x;
            }
        }
        else if (direction.y > 0.0f)
        {
            face = 1.0f;
            uv = float2(-direction.x, direction.z) /
                absoluteDirection.y;
        }
        else
        {
            face = 4.0f;
            uv = float2(-direction.x, -direction.z) /
                absoluteDirection.y;
        }
    }
    else if (absoluteDirection.z > absoluteDirection.y)
    {
        if (direction.z > 0.0f)
        {
            face = 2.0f;
            uv = float2(-direction.x, -direction.y) /
                absoluteDirection.z;
        }
        else
        {
            face = 5.0f;
            uv = float2(direction.x, -direction.y) /
                absoluteDirection.z;
        }
    }
    else if (direction.y > 0.0f)
    {
        face = 1.0f;
        uv = float2(-direction.x, direction.z) /
            absoluteDirection.y;
    }
    else
    {
        face = 4.0f;
        uv = float2(-direction.x, -direction.z) /
            absoluteDirection.y;
    }
    return float3(uv * 0.5f + 0.5f, face);
}

/** Projects one direction onto an explicitly selected cube face for stable gradients. */
float2 WebglMaterialsEnvmapsCubeUvForFace(
    float3 direction,
    uint face)
{
    float2 coordinate;
    float divisor;
    if (face == 0u)
    {
        divisor = max(abs(direction.x), 0.000001f);
        coordinate = float2(-direction.z, -direction.y) / divisor;
    }
    else if (face == 1u)
    {
        divisor = max(abs(direction.y), 0.000001f);
        coordinate = float2(-direction.x, direction.z) / divisor;
    }
    else if (face == 2u)
    {
        divisor = max(abs(direction.z), 0.000001f);
        coordinate = float2(-direction.x, -direction.y) / divisor;
    }
    else if (face == 3u)
    {
        divisor = max(abs(direction.x), 0.000001f);
        coordinate = float2(direction.z, -direction.y) / divisor;
    }
    else if (face == 4u)
    {
        divisor = max(abs(direction.y), 0.000001f);
        coordinate = float2(-direction.x, -direction.z) / divisor;
    }
    else
    {
        divisor = max(abs(direction.z), 0.000001f);
        coordinate = float2(direction.x, -direction.y) / divisor;
    }
    return coordinate * 0.5f + 0.5f;
}

/** Reconstructs a direction from one possibly gutter-exterior face UV. */
float3 WebglMaterialsEnvmapsCubeDirection(uint face, float2 uv)
{
    const float2 coordinate = uv * 2.0f - 1.0f;
    if (face == 0u)
    {
        return float3(1.0f, -coordinate.y, -coordinate.x);
    }
    if (face == 1u)
    {
        return float3(-coordinate.x, 1.0f, coordinate.y);
    }
    if (face == 2u)
    {
        return float3(-coordinate.x, -coordinate.y, 1.0f);
    }
    if (face == 3u)
    {
        return float3(-1.0f, -coordinate.y, coordinate.x);
    }
    if (face == 4u)
    {
        return float3(-coordinate.x, -1.0f, -coordinate.y);
    }
    return float3(coordinate.x, -coordinate.y, -1.0f);
}

/** Samples one selected source face at an explicit mip for atlas construction. */
float4 WebglMaterialsEnvmapsSampleSourceCube(
    IN BindGroup<WebglMaterialsEnvmapsAtlasResources> resources,
    float3 direction,
    float mipLevel)
{
    const float3 faceUv = WebglMaterialsEnvmapsCubeFaceUv(direction);
    const float2 uv = clamp(
        float2(faceUv.x, faceUv.y),
        float2(0.0000001f),
        float2(0.9999999f));
    const uint face = uint(faceUv.z + 0.5f);
    if (face == 0u)
    {
        return resources->positiveX->sampleLevel(
            resources->cubeSampler, uv, mipLevel);
    }
    if (face == 1u)
    {
        return resources->negativeY->sampleLevel(
            resources->cubeSampler,
            uv,
            mipLevel);
    }
    if (face == 2u)
    {
        return resources->positiveZ->sampleLevel(
            resources->cubeSampler, uv, mipLevel);
    }
    if (face == 3u)
    {
        return resources->negativeX->sampleLevel(
            resources->cubeSampler, uv, mipLevel);
    }
    if (face == 4u)
    {
        return resources->positiveY->sampleLevel(
            resources->cubeSampler,
            uv,
            mipLevel);
    }
    return resources->negativeZ->sampleLevel(
        resources->cubeSampler, uv, mipLevel);
}

/** Samples one original Bridge2 face with cube-projected implicit gradients. */
float3 WebglMaterialsEnvmapsSampleSceneCube(
    IN BindGroup<WebglMaterialsEnvmapsSceneResources> resources,
    float3 direction)
{
    const float3 faceUv = WebglMaterialsEnvmapsCubeFaceUv(direction);
    const float2 uv = clamp(
        float2(faceUv.x, faceUv.y),
        float2(0.0000001f),
        float2(0.9999999f));
    const uint face = uint(faceUv.z + 0.5f);
    const float3 directionDerivativeX = ddx(direction);
    const float3 directionDerivativeY = ddy(direction);
    const float2 gradientX =
        WebglMaterialsEnvmapsCubeUvForFace(
            direction + directionDerivativeX,
            face) -
        uv;
    const float2 gradientY =
        WebglMaterialsEnvmapsCubeUvForFace(
            direction + directionDerivativeY,
            face) -
        uv;
    const float footprint = max(
        max(
            length(gradientX * float(WebglMaterialsEnvmapsCubeFaceSize)),
            length(gradientY * float(WebglMaterialsEnvmapsCubeFaceSize))),
        1.0f);
    const float mipLevel = clamp(
        log2(footprint),
        0.0f,
        float(WebglMaterialsEnvmapsCubeMipCount - 1u));
    if (face == 0u)
    {
        return resources->positiveX->sampleLevel(
            resources->environmentSampler,
            uv,
            mipLevel).xyz;
    }
    if (face == 1u)
    {
        return resources->negativeY->sampleLevel(
            resources->environmentSampler,
            uv,
            mipLevel).xyz;
    }
    if (face == 2u)
    {
        return resources->positiveZ->sampleLevel(
            resources->environmentSampler,
            uv,
            mipLevel).xyz;
    }
    if (face == 3u)
    {
        return resources->negativeX->sampleLevel(
            resources->environmentSampler,
            uv,
            mipLevel).xyz;
    }
    if (face == 4u)
    {
        return resources->positiveY->sampleLevel(
            resources->environmentSampler,
            uv,
            mipLevel).xyz;
    }
    return resources->negativeZ->sampleLevel(
        resources->environmentSampler,
        uv,
        mipLevel).xyz;
}

/** Builds all explicit cube mip tiles and their cross-face edge/corner gutters. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebglMaterialsEnvmapsCubeAtlasGutterBuild final : public IComputeClass
{
public:
    /** Binds the six source textures and the sole writable atlas. */
    constructor(
        BindGroup<WebglMaterialsEnvmapsAtlasResources> resources [[Slot0]])
    {
    }

private:
    /** Converts one atlas texel into a seamless cube-direction sample. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= WebglMaterialsEnvmapsAtlasWidth ||
            threadID.y >= WebglMaterialsEnvmapsAtlasHeight)
        {
            return;
        }
        uint mipLevel = 0u;
        for (uint candidate = 1u;
             candidate < WebglMaterialsEnvmapsCubeMipCount;
             ++candidate)
        {
            if (threadID.y >=
                WebglMaterialsEnvmapsAtlasMipOffset(candidate))
            {
                mipLevel = candidate;
            }
        }
        const uint faceSize =
            max(1u, WebglMaterialsEnvmapsCubeFaceSize >> mipLevel);
        const uint tileSize = faceSize + 2u;
        const uint mipOffset =
            WebglMaterialsEnvmapsAtlasMipOffset(mipLevel);
        if (threadID.x >= tileSize * 3u ||
            threadID.y >= mipOffset + tileSize * 2u)
        {
            resources->atlas->write(
                threadID.xy,
                half4(half3(0.0f), half(1.0f)));
            return;
        }
        const uint faceColumn = threadID.x / tileSize;
        const uint faceRow = (threadID.y - mipOffset) / tileSize;
        const uint face = faceColumn + faceRow * 3u;
        const float2 facePixel = float2(
            float(threadID.x - faceColumn * tileSize) - 1.0f,
            float(threadID.y - mipOffset - faceRow * tileSize) - 1.0f);
        const float2 faceUv =
            (facePixel + 0.5f) / float(faceSize);
        const float3 direction =
            WebglMaterialsEnvmapsCubeDirection(face, faceUv);
        const float4 value =
            WebglMaterialsEnvmapsSampleSourceCube(
                resources,
                direction,
                float(mipLevel));
        resources->atlas->write(
            threadID.xy,
            half4(value));
    }
};

/** Samples one explicit guttered atlas mip without relying on cube descriptors. */
float3 WebglMaterialsEnvmapsSampleAtlasLevel(
    IN BindGroup<WebglMaterialsEnvmapsSceneResources> resources,
    float3 direction,
    uint mipLevel)
{
    const uint clampedMip =
        min(mipLevel, WebglMaterialsEnvmapsCubeMipCount - 1u);
    const uint faceSize =
        max(1u, WebglMaterialsEnvmapsCubeFaceSize >> clampedMip);
    const uint tileSize = faceSize + 2u;
    const float3 faceUv =
        WebglMaterialsEnvmapsCubeFaceUv(direction);
    const uint face = uint(faceUv.z + 0.5f);
    const uint faceColumn = face % 3u;
    const uint faceRow = face / 3u;
    const float2 atlasPixel = float2(
        float(faceColumn * tileSize + 1u) +
            faceUv.x * float(faceSize),
        float(WebglMaterialsEnvmapsAtlasMipOffset(clampedMip) +
              faceRow * tileSize + 1u) +
            faceUv.y * float(faceSize));
    return resources->cubeAtlas->sampleLevel(
        resources->environmentSampler,
        atlasPixel /
            float2(
                float(WebglMaterialsEnvmapsAtlasWidth),
                float(WebglMaterialsEnvmapsAtlasHeight)),
        0.0f).xyz;
}

/** Samples the explicit cube atlas with a derivative-selected trilinear mip. */
float3 WebglMaterialsEnvmapsSampleAtlas(
    IN BindGroup<WebglMaterialsEnvmapsSceneResources> resources,
    float3 direction)
{
    const float3 faceUv =
        WebglMaterialsEnvmapsCubeFaceUv(direction);
    const float2 selectedUv = float2(faceUv.x, faceUv.y);
    const uint selectedFace = uint(faceUv.z + 0.5f);
    const float3 directionDerivativeX = ddx(direction);
    const float3 directionDerivativeY = ddy(direction);
    const float2 derivativeX =
        (WebglMaterialsEnvmapsCubeUvForFace(
             direction + directionDerivativeX,
             selectedFace) -
         selectedUv) *
        float(WebglMaterialsEnvmapsCubeFaceSize);
    const float2 derivativeY =
        (WebglMaterialsEnvmapsCubeUvForFace(
             direction + directionDerivativeY,
             selectedFace) -
         selectedUv) *
        float(WebglMaterialsEnvmapsCubeFaceSize);
    const float footprint = max(
        max(length(derivativeX), length(derivativeY)),
        1.0f);
    const float lod = clamp(
        log2(footprint),
        0.0f,
        float(WebglMaterialsEnvmapsCubeMipCount - 1u));
    const uint lowerMip = uint(floor(lod));
    const uint upperMip =
        min(lowerMip + 1u, WebglMaterialsEnvmapsCubeMipCount - 1u);
    const float3 lower =
        WebglMaterialsEnvmapsSampleAtlasLevel(
            resources, direction, lowerMip);
    const float3 upper =
        WebglMaterialsEnvmapsSampleAtlasLevel(
            resources, direction, upperMip);
    return lerp(lower, upper, frac(lod));
}

/** Samples the locked equirectangular sRGB texture in linear working space. */
float3 WebglMaterialsEnvmapsSampleEquirectangular(
    IN BindGroup<WebglMaterialsEnvmapsSceneResources> resources,
    float3 direction)
{
    const float3 normalizedDirection = normalize(direction);
    const float2 uv = float2(
        atan2(normalizedDirection.z, normalizedDirection.x) *
                0.15915494309189535f +
            0.5f,
        0.5f +
            asin(clamp(normalizedDirection.y, -1.0f, 1.0f)) *
                0.3183098861837907f);
    return resources->equirectangular->sampleGrad(
        resources->environmentSampler,
        uv,
        ddx(uv),
        ddy(uv)).xyz;
}

/** Samples the scenario-selected environment using the exact mapping family. */
float3 WebglMaterialsEnvmapsSampleEnvironment(
    IN BindGroup<WebglMaterialsEnvmapsSceneResources> resources,
    float3 direction)
{
    if (resources->uniforms->cameraPositionAndMode.w < 0.5f)
    {
        return WebglMaterialsEnvmapsSampleSceneCube(
            resources,
            direction);
    }
    return WebglMaterialsEnvmapsSampleEquirectangular(
        resources,
        direction);
}

/** Draws the environment background into the linear Scene target. */
class WebglMaterialsEnvmapsEnvironmentBackgroundPass final : public IRenderClass
{
public:
    /** Binds the generated atlas and decoded equirectangular image. */
    constructor(
        BindGroup<WebglMaterialsEnvmapsSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglMaterialsEnvmapsScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglMaterialsEnvmapsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reconstructs the camera ray, applies background rotation, and samples the environment. */
    WebglMaterialsEnvmapsSceneFrameBuffer fragment(
        WebglMaterialsEnvmapsScreenOutput inputValue)
    {
        const float2 ndc = inputValue.uv * 2.0f - 1.0f;
        const float aspect =
            resources->uniforms->viewportAndReserved.x /
            resources->uniforms->viewportAndReserved.y;
        const float tangentHalfFov =
            resources->uniforms->cameraForwardAndTanHalfFov.w;
        const float verticalNdc =
            resources->uniforms->cameraPositionAndMode.w > 0.5f
                ? -ndc.y
                : ndc.y;
        const float3 ray = normalize(
            resources->uniforms->cameraForwardAndTanHalfFov.xyz +
            resources->uniforms->cameraRightAndRefraction.xyz *
                (ndc.x * aspect * tangentHalfFov) +
            resources->uniforms->cameraUpAndFrame.xyz *
                (verticalNdc * tangentHalfFov));
        const float3 rotated =
            WebglMaterialsEnvmapsRotate(
                resources->uniforms->rotationRow0,
                resources->uniforms->rotationRow1,
                resources->uniforms->rotationRow2,
                ray);
        WebglMaterialsEnvmapsSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(WebglMaterialsEnvmapsSampleEnvironment(
                resources,
                rotated)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the single mutable MeshBasicMaterial sphere with explicit indexed geometry. */
class WebglMaterialsEnvmapsMainPass final : public IRenderClass
{
public:
    /** Binds one ordinary material resource group and opaque depth state. */
    constructor(
        BindGroup<WebglMaterialsEnvmapsSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects the exact CPU-generated detail-15 IcosahedronGeometry. */
    WebglMaterialsEnvmapsSphereOutput vertex(
        WebglMaterialsEnvmapsVertex inputValue [[VertexInput0]])
    {
        const float3 relative =
            inputValue.position.xyz -
            resources->uniforms->cameraPositionAndMode.xyz;
        const float viewX =
            relative.x *
                resources->uniforms->cameraRightAndRefraction.x +
            relative.y *
                resources->uniforms->cameraRightAndRefraction.y +
            relative.z *
                resources->uniforms->cameraRightAndRefraction.z;
        const float viewY =
            relative.x * resources->uniforms->cameraUpAndFrame.x +
            relative.y * resources->uniforms->cameraUpAndFrame.y +
            relative.z * resources->uniforms->cameraUpAndFrame.z;
        const float viewDepth =
            relative.x *
                resources->uniforms->cameraForwardAndTanHalfFov.x +
            relative.y *
                resources->uniforms->cameraForwardAndTanHalfFov.y +
            relative.z *
                resources->uniforms->cameraForwardAndTanHalfFov.z;
        const float tangentHalfFov =
            resources->uniforms->cameraForwardAndTanHalfFov.w;
        const float aspect =
            resources->uniforms->viewportAndReserved.x /
            resources->uniforms->viewportAndReserved.y;
        const float clipZ =
            (100.0f / 99.9f) * viewDepth -
            (10.0f / 99.9f);
        WebglMaterialsEnvmapsSphereOutput outputValue;
        outputValue.position = float4(
            viewX / (tangentHalfFov * aspect),
            viewY / tangentHalfFov,
            clipZ,
            viewDepth);
        outputValue.worldPosition = inputValue.position.xyz;
        outputValue.worldNormal = inputValue.normal.xyz;
        float3 shadingPosition = inputValue.position.xyz;
        float3 vertexNormal = normalize(float3(
            inputValue.normal.x,
            inputValue.normal.y,
            inputValue.normal.z));
        if (resources->uniforms->cameraPositionAndMode.w > 0.5f)
        {
            const float3 cameraUp =
                resources->uniforms->cameraUpAndFrame.xyz;
            shadingPosition -=
                2.0f *
                (shadingPosition.x * cameraUp.x +
                 shadingPosition.y * cameraUp.y +
                 shadingPosition.z * cameraUp.z) *
                cameraUp;
            vertexNormal -=
                2.0f *
                (vertexNormal.x * cameraUp.x +
                 vertexNormal.y * cameraUp.y +
                 vertexNormal.z * cameraUp.z) *
                cameraUp;
        }
        const float3 cameraToVertex = normalize(
            shadingPosition -
            resources->uniforms->cameraPositionAndMode.xyz);
        const float incidentNormalDot =
            cameraToVertex.x * vertexNormal.x +
            cameraToVertex.y * vertexNormal.y +
            cameraToVertex.z * vertexNormal.z;
        if (resources->uniforms->cameraRightAndRefraction.w > 0.5f)
        {
            const float refractionRatio = 0.98f;
            const float refractionDiscriminant =
                1.0f -
                refractionRatio * refractionRatio *
                    (1.0f -
                     incidentNormalDot * incidentNormalDot);
            outputValue.environmentDirection =
                refractionDiscriminant < 0.0f
                    ? float3(0.0f)
                    : refractionRatio * cameraToVertex -
                        (refractionRatio * incidentNormalDot +
                         sqrt(refractionDiscriminant)) *
                            vertexNormal;
        }
        else
        {
            outputValue.environmentDirection =
                cameraToVertex -
                2.0f * incidentNormalDot * vertexNormal;
        }
        return outputValue;
    }

    /** Evaluates reflection or refraction and the synchronized material rotation. */
    WebglMaterialsEnvmapsSceneFrameBuffer fragment(
        WebglMaterialsEnvmapsSphereOutput inputValue)
    {
        float3 direction = inputValue.environmentDirection;
        direction =
            WebglMaterialsEnvmapsRotate(
                resources->uniforms->rotationRow0,
                resources->uniforms->rotationRow1,
                resources->uniforms->rotationRow2,
                direction);
        WebglMaterialsEnvmapsSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(WebglMaterialsEnvmapsSampleEnvironment(
                resources,
                direction)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Applies the sole r185 display transfer to the composed linear Scene. */
class WebglMaterialsEnvmapsOutputToneMapPass final : public IRenderClass
{
public:
    /** Binds the composed Scene texture and disables geometry state. */
    constructor(
        BindGroup<WebglMaterialsEnvmapsOutputResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen output triangle. */
    WebglMaterialsEnvmapsScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglMaterialsEnvmapsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Encodes the linear environment result into the final RGBA8 target. */
    WebglMaterialsEnvmapsOutputFrameBuffer fragment(
        WebglMaterialsEnvmapsScreenOutput inputValue)
    {
        const float3 linearColor = resources->sceneColor->sample(
            resources->outputSampler,
            inputValue.uv).xyz;
        const float3 encoded = float3(
            WebglMaterialsEnvmapsLinearToSrgb(linearColor.x),
            WebglMaterialsEnvmapsLinearToSrgb(linearColor.y),
            WebglMaterialsEnvmapsLinearToSrgb(linearColor.z));
        WebglMaterialsEnvmapsOutputFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated environment-map Compute, Scene, and output DSL pipeline. */
class WebglMaterialsEnvmapsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglMaterialsEnvmapsVertex, BufferUsage<Vertex, CopyDst>>
        vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglMaterialsEnvmapsUniforms, BufferUsage<Uniform, CopyDst>>
        uniformBuffer;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> positiveX;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> negativeX;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> positiveY;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> negativeY;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> positiveZ;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> negativeZ;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> equirectangular;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> cubeAtlas;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Sampler environmentSampler;
    Sampler outputSampler;
    BindGroup<WebglMaterialsEnvmapsAtlasResources> atlasResources;
    BindGroup<WebglMaterialsEnvmapsSceneResources> sceneResources;
    BindGroup<WebglMaterialsEnvmapsOutputResources> outputResources;
    ComputeClass<WebglMaterialsEnvmapsCubeAtlasGutterBuild> atlasBuild;
    RenderClass<WebglMaterialsEnvmapsEnvironmentBackgroundPass> backgroundPass;
    RenderClass<WebglMaterialsEnvmapsMainPass> mainPass;
    RenderClass<WebglMaterialsEnvmapsOutputToneMapPass> outputPass;
    WebglMaterialsEnvmapsUniforms uniforms;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates standalone geometry, uniform, sampler, and single-sample resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglMaterialsEnvmapsVertices",
            20000u);
        indexBuffer = device->createBuffer(
            "WebglMaterialsEnvmapsIndices",
            20000u);
        uniformBuffer = device->createBuffer(
            "WebglMaterialsEnvmapsUniforms",
            1u);
        environmentSampler = device->createSampler({
            .label = "WebglMaterialsEnvmapsEnvironmentSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 12.0f,
            .maxAnisotropy = 1u,
        });
        outputSampler = device->createSampler({
            .label = "WebglMaterialsEnvmapsOutputSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates linear Scene attachments and the final ordinary RGBA8 target. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneColor = device->createTexture(
            "WebglMaterialsEnvmapsSceneColor",
            width,
            height,
            1u);
        sceneDepth = device->createTexture(
            "WebglMaterialsEnvmapsSceneDepth",
            width,
            height,
            1u);
        outputColor = device->createTexture(
            "WebglMaterialsEnvmapsOutput",
            width,
            height,
            1u);
    }

    /** Uploads exact geometry and all explicit texture mips, then builds the cube atlas. */
    void configureScene(
        const eastl::vector<float4> &positions,
        const eastl::vector<float4> &normals,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &positiveXMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeXMips,
        const eastl::vector<eastl::vector<uint8_t>> &positiveYMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeYMips,
        const eastl::vector<eastl::vector<uint8_t>> &positiveZMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeZMips,
        const eastl::vector<eastl::vector<uint8_t>> &equirectangularMips,
        float4 cameraPositionAndMode,
        float4 cameraRightAndRefraction,
        float4 cameraUpAndFrame,
        float4 cameraForwardAndTanHalfFov,
        float4 rotationRow0,
        float4 rotationRow1,
        float4 rotationRow2)
    {
        indexCount = uint(indices.size());
        eastl::vector<WebglMaterialsEnvmapsVertex> vertices;
        vertices.resize(positions.size());
        for (uint index = 0u; index < uint(vertices.size()); ++index)
        {
            vertices[index].position = positions[index];
            vertices[index].normal = normals[index];
        }
        positiveX = device->createTexture(
            "WebglMaterialsEnvmapsPositiveX", 1024u, 1024u, 1u, 11u);
        negativeX = device->createTexture(
            "WebglMaterialsEnvmapsNegativeX", 1024u, 1024u, 1u, 11u);
        positiveY = device->createTexture(
            "WebglMaterialsEnvmapsPositiveY", 1024u, 1024u, 1u, 11u);
        negativeY = device->createTexture(
            "WebglMaterialsEnvmapsNegativeY", 1024u, 1024u, 1u, 11u);
        positiveZ = device->createTexture(
            "WebglMaterialsEnvmapsPositiveZ", 1024u, 1024u, 1u, 11u);
        negativeZ = device->createTexture(
            "WebglMaterialsEnvmapsNegativeZ", 1024u, 1024u, 1u, 11u);
        equirectangular = device->createTexture(
            "WebglMaterialsEnvmapsEquirectangular",
            4096u,
            2048u,
            1u,
            13u);
        cubeAtlas = device->createTexture(
            "WebglMaterialsEnvmapsCubeAtlas",
            WebglMaterialsEnvmapsAtlasWidth,
            WebglMaterialsEnvmapsAtlasHeight,
            1u);
        uniforms.cameraPositionAndMode = cameraPositionAndMode;
        uniforms.cameraRightAndRefraction = cameraRightAndRefraction;
        uniforms.cameraUpAndFrame = cameraUpAndFrame;
        uniforms.cameraForwardAndTanHalfFov =
            cameraForwardAndTanHalfFov;
        uniforms.rotationRow0 = rotationRow0;
        uniforms.rotationRow1 = rotationRow1;
        uniforms.rotationRow2 = rotationRow2;
        uniforms.viewportAndReserved =
            float4(float(width), float(height), 0.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebglMaterialsEnvmapsVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        for (uint mipLevel = 0u; mipLevel < 11u; ++mipLevel)
        {
            graphicsQueue
                ->writeTexture(positiveX, positiveXMips[mipLevel].data(),
                    uint64_t(positiveXMips[mipLevel].size()), mipLevel)
                ->writeTexture(negativeX, negativeXMips[mipLevel].data(),
                    uint64_t(negativeXMips[mipLevel].size()), mipLevel)
                ->writeTexture(positiveY, positiveYMips[mipLevel].data(),
                    uint64_t(positiveYMips[mipLevel].size()), mipLevel)
                ->writeTexture(negativeY, negativeYMips[mipLevel].data(),
                    uint64_t(negativeYMips[mipLevel].size()), mipLevel)
                ->writeTexture(positiveZ, positiveZMips[mipLevel].data(),
                    uint64_t(positiveZMips[mipLevel].size()), mipLevel)
                ->writeTexture(negativeZ, negativeZMips[mipLevel].data(),
                    uint64_t(negativeZMips[mipLevel].size()), mipLevel)
                ->submit();
        }
        for (uint mipLevel = 0u; mipLevel < 13u; ++mipLevel)
        {
            graphicsQueue
                ->writeTexture(
                    equirectangular,
                    equirectangularMips[mipLevel].data(),
                    uint64_t(equirectangularMips[mipLevel].size()),
                    mipLevel)
                ->submit();
        }
        atlasResources =
            device->createBindGroup<WebglMaterialsEnvmapsAtlasResources>(
                positiveX->createView(),
                negativeX->createView(),
                positiveY->createView(),
                negativeY->createView(),
                positiveZ->createView(),
                negativeZ->createView(),
                environmentSampler,
                cubeAtlas->createView());
        sceneResources =
            device->createBindGroup<WebglMaterialsEnvmapsSceneResources>(
                cubeAtlas->createView(),
                equirectangular->createView(),
                environmentSampler,
                uniformBuffer,
                positiveX->createView(),
                negativeX->createView(),
                positiveY->createView(),
                negativeY->createView(),
                positiveZ->createView(),
                negativeZ->createView());
        outputResources =
            device->createBindGroup<WebglMaterialsEnvmapsOutputResources>(
                sceneColor->createView(),
                outputSampler);
        atlasBuild =
            device->createComputeClass<
                WebglMaterialsEnvmapsCubeAtlasGutterBuild>(
                    atlasResources);
        backgroundPass =
            device->createRenderClass<
                WebglMaterialsEnvmapsEnvironmentBackgroundPass>(
                    sceneResources);
        mainPass =
            device->createRenderClass<
                WebglMaterialsEnvmapsMainPass>(
                    sceneResources);
        outputPass =
            device->createRenderClass<
                WebglMaterialsEnvmapsOutputToneMapPass>(
                    outputResources);
        graphicsQueue
            ->computePass(
                "WebglMaterialsEnvmapsCubeAtlasGutterBuild",
                atlasBuild(
                    WebglMaterialsEnvmapsAtlasWidth,
                    WebglMaterialsEnvmapsAtlasHeight,
                    1u))
            ->submit();
    }

    /** Draws the environment, the one sphere, output transfer, and presentation. */
    void render() override
    {
        WebglMaterialsEnvmapsSceneFrameBuffer backgroundFrameBuffer;
        backgroundFrameBuffer.color = sceneColor->createView();
        backgroundFrameBuffer.color.loadOp = LoadOp::Clear;
        backgroundFrameBuffer.color.storeOp = StoreOp::Store;
        backgroundFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        backgroundFrameBuffer.depth = sceneDepth->createView();
        backgroundFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        backgroundFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        backgroundFrameBuffer.depth.depthClearValue = 1.0f;
        WebglMaterialsEnvmapsSceneFrameBuffer sphereFrameBuffer;
        sphereFrameBuffer.color = sceneColor->createView();
        sphereFrameBuffer.color.loadOp = LoadOp::Load;
        sphereFrameBuffer.color.storeOp = StoreOp::Store;
        sphereFrameBuffer.depth = sceneDepth->createView();
        sphereFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        sphereFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        WebglMaterialsEnvmapsOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputColor->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglMaterialsEnvmapsEnvironmentBackground",
                backgroundFrameBuffer,
                backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebglMaterialsEnvmapsMain",
                sphereFrameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass(
                "WebglMaterialsEnvmapsOutputToneMap",
                outputFrameBuffer,
                outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured readback height. */
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
        device->freeTexture(positiveX);
        device->freeTexture(negativeX);
        device->freeTexture(positiveY);
        device->freeTexture(negativeY);
        device->freeTexture(positiveZ);
        device->freeTexture(negativeZ);
        device->freeTexture(equirectangular);
        device->freeTexture(cubeAtlas);
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};
