#pragma once

#include "UGL.h"
#include "WebgpuMaterialsDisplacementmap.hpp"

#include <EASTL/array.h>
#include <EASTL/vector.h>

using namespace UGL;

static const uint WebgpuMaterialsTransmissionCubeFaceSize = 1024u;
static const uint WebgpuMaterialsTransmissionCubeMipCount = 11u;
static const uint WebgpuMaterialsTransmissionAtlasWidth = 3078u;
static const uint WebgpuMaterialsTransmissionAtlasHeight = 4138u;
static const uint WebgpuMaterialsTransmissionEquirectangularWidth = 2048u;
static const uint WebgpuMaterialsTransmissionEquirectangularHeight = 1024u;
static const uint WebgpuMaterialsTransmissionEquirectangularMipCount = 12u;
static const uint WebgpuMaterialsTransmissionScreenMipCount = 7u;

/** Stores one non-index-shared r185 IcosahedronGeometry vertex. */
struct WebgpuMaterialsTransmissionVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
};

/** Stores one r185 background SphereGeometry vertex and its world direction. */
struct WebgpuMaterialsTransmissionBackgroundVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores the deterministic camera, environment mode, and synchronized rotations. */
struct WebgpuMaterialsTransmissionUniforms
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

/** Stores one explicit equirectangular mip build level and destination extent. */
struct WebgpuMaterialsTransmissionEquirectangularMipUniforms
{
    uint4 levelAndExtent;
};

/** Binds the decoded UltraHDR source to the PMREM cubeUV level-zero conversion. */
struct WebgpuMaterialsTransmissionPmremSourceResources final : public IBindGroup
{
    /** Declares the equirectangular source, sampler, and writable PMREM atlas. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        RWTexture2D<TextureFormat::RGBA16Float> destinationAtlas [[Binding2]])
    {
    }
};

/** Binds all six uploaded Bridge2 faces to the private atlas compute pass. */
struct WebgpuMaterialsTransmissionAtlasResources final : public IBindGroup
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

/** Binds the decoded base image and one ordered equirectangular mip pair. */
struct WebgpuMaterialsTransmissionEquirectangularMipResources final : public IBindGroup
{
    /** Declares the encoded base, previous linear mip, target mip, and level. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Texture2D<half4> previous [[Binding1]],
        RWTexture2D<TextureFormat::RGBA16Float> target [[Binding2]],
        UniformBuffer<WebgpuMaterialsTransmissionEquirectangularMipUniforms>
            uniforms [[Binding3]])
    {
    }
};

/** Binds the environment inputs used to render the immutable transmission backdrop. */
struct WebgpuMaterialsTransmissionBackgroundResources final : public IBindGroup
{
    /** Declares the decoded environment, sampler, and deterministic camera state. */
    constructor(
        Texture2D<half4> equirectangular [[Binding0]],
        Sampler environmentSampler [[Binding1]],
        UniformBuffer<WebgpuMaterialsTransmissionUniforms> uniforms [[Binding2]])
    {
    }
};

/** Binds one source level for the explicit single-sample transmission mip chain. */
struct WebgpuMaterialsTransmissionScreenMipResources final : public IBindGroup
{
    /** Declares one previous screen level and its linear downsample sampler. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]])
    {
    }
};

/** Binds the composed opaque/back-face Scene image for the front transmission chain. */
struct WebgpuMaterialsTransmissionScreenCaptureResources final : public IBindGroup
{
    /** Declares the Scene source and the clamp sampler used by the capture pass. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]])
    {
    }
};

/** Binds the generated cube atlas, equirectangular texture, and frame state. */
struct WebgpuMaterialsTransmissionSceneResources final : public IBindGroup
{
    /** Declares every resource shared by the background and sphere passes. */
    constructor(
        UniformBuffer<WebgpuMaterialsTransmissionUniforms> uniforms [[Binding0]],
        Texture2D<half4> pmremAtlas [[Binding1]],
        Texture2D<half4> dfgLut [[Binding2]],
        Sampler pmremSampler [[Binding3]],
        Sampler dfgSampler [[Binding4]],
        Texture2D<half4> transmissionMip0 [[Binding5]],
        Texture2D<half4> transmissionMip1 [[Binding6]],
        Texture2D<half4> transmissionMip2 [[Binding7]],
        Texture2D<half4> transmissionMip3 [[Binding8]],
        Texture2D<half4> transmissionMip4 [[Binding9]],
        Texture2D<half4> transmissionMip5 [[Binding10]],
        Texture2D<half4> transmissionMip6 [[Binding11]],
        Sampler transmissionSampler [[Binding12]],
        Texture2D<float4> cubeAtlas [[Binding13]],
        Texture2D<half4> equirectangular [[Binding14]],
        Sampler environmentSampler [[Binding15]],
        Texture2D<float4> positiveX [[Binding16]],
        Texture2D<float4> negativeX [[Binding17]],
        Texture2D<float4> positiveY [[Binding18]],
        Texture2D<float4> negativeY [[Binding19]],
        Texture2D<float4> positiveZ [[Binding20]],
        Texture2D<float4> negativeZ [[Binding21]],
        Texture2D<float4> alphaMap [[Binding22]],
        Sampler alphaSampler [[Binding23]])
    {
    }
};

/** Binds the linear Scene result to the final output transfer pass. */
struct WebgpuMaterialsTransmissionOutputResources final : public IBindGroup
{
    /** Declares the linear Scene texture, output sampler, and deterministic frame state. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler outputSampler [[Binding1]],
        UniformBuffer<WebgpuMaterialsTransmissionUniforms> uniforms [[Binding2]])
    {
    }
};

/** Carries a fullscreen direction reconstruction coordinate. */
struct WebgpuMaterialsTransmissionScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Carries projected sphere data and the vertex-computed reflection vector. */
struct WebgpuMaterialsTransmissionSphereOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float3 environmentDirection [[Attribute2]];
    float2 textureCoordinate [[Attribute3]];
};

/** Defines the linear Scene color and sphere depth attachments. */
struct WebgpuMaterialsTransmissionSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines one color-only linear backdrop or explicit mip attachment. */
struct WebgpuMaterialsTransmissionLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final single-sample RGBA8 attachment. */
struct WebgpuMaterialsTransmissionOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear working-space channel to the r185 sRGB transfer. */
float WebgpuMaterialsTransmissionLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Decodes one sRGB channel for explicit equirectangular mip filtering. */
float WebgpuMaterialsTransmissionSrgbToLinear(float value)
{
    const float clamped = clamp(value, 0.0f, 1.0f);
    return clamped <= 0.04045f
        ? clamped / 12.92f
        : pow((clamped + 0.055f) / 1.055f, 2.4f);
}

/** Encodes one linear channel for the explicit equirectangular mip atlas. */
float WebgpuMaterialsTransmissionLinearToMipSrgb(float value)
{
    const float clamped = clamp(value, 0.0f, 1.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Returns the signed distance to one asymmetric Inspector rounded rectangle. */
float WebgpuMaterialsTransmissionRoundedBoxDistance(
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

/** Evaluates the one-pixel analytic coverage used by Inspector chrome. */
float WebgpuMaterialsTransmissionEdgeCoverage(float signedDistance)
{
    return clamp(0.5f - signedDistance, 0.0f, 1.0f);
}

/** Returns the stacked-atlas Y offset for one explicit cube mip. */
uint WebgpuMaterialsTransmissionAtlasMipOffset(uint mipLevel)
{
    return 4096u - (4096u >> mipLevel) + mipLevel * 4u;
}

/** Rotates one direction by Three's transposed intrinsic XYZ matrix. */
float3 WebgpuMaterialsTransmissionRotate(
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
float3 WebgpuMaterialsTransmissionCubeFaceUv(float3 direction)
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
float2 WebgpuMaterialsTransmissionCubeUvForFace(
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
float3 WebgpuMaterialsTransmissionCubeDirection(uint face, float2 uv)
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
float4 WebgpuMaterialsTransmissionSampleSourceCube(
    IN BindGroup<WebgpuMaterialsTransmissionAtlasResources> resources,
    float3 direction,
    float mipLevel)
{
    const float3 faceUv = WebgpuMaterialsTransmissionCubeFaceUv(direction);
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
float3 WebgpuMaterialsTransmissionSampleSceneCube(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
    float3 direction)
{
    const float3 faceUv = WebgpuMaterialsTransmissionCubeFaceUv(direction);
    const float2 uv = clamp(
        float2(faceUv.x, faceUv.y),
        float2(0.0000001f),
        float2(0.9999999f));
    const uint face = uint(faceUv.z + 0.5f);
    const float3 directionDerivativeX = ddx(direction);
    const float3 directionDerivativeY = ddy(direction);
    const float2 gradientX =
        WebgpuMaterialsTransmissionCubeUvForFace(
            direction + directionDerivativeX,
            face) -
        uv;
    const float2 gradientY =
        WebgpuMaterialsTransmissionCubeUvForFace(
            direction + directionDerivativeY,
            face) -
        uv;
    const float footprint = max(
        max(
            length(gradientX * float(WebgpuMaterialsTransmissionCubeFaceSize)),
            length(gradientY * float(WebgpuMaterialsTransmissionCubeFaceSize))),
        1.0f);
    const float mipLevel = clamp(
        log2(footprint),
        0.0f,
        float(WebgpuMaterialsTransmissionCubeMipCount - 1u));
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
WebgpuMaterialsTransmissionCubeAtlasGutterBuild final : public IComputeClass
{
public:
    /** Binds the six source textures and the sole writable atlas. */
    constructor(
        BindGroup<WebgpuMaterialsTransmissionAtlasResources> resources [[Slot0]])
    {
    }

private:
    /** Converts one atlas texel into a seamless cube-direction sample. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= WebgpuMaterialsTransmissionAtlasWidth ||
            threadID.y >= WebgpuMaterialsTransmissionAtlasHeight)
        {
            return;
        }
        uint mipLevel = 0u;
        for (uint candidate = 1u;
             candidate < WebgpuMaterialsTransmissionCubeMipCount;
             ++candidate)
        {
            if (threadID.y >=
                WebgpuMaterialsTransmissionAtlasMipOffset(candidate))
            {
                mipLevel = candidate;
            }
        }
        const uint faceSize =
            max(1u, WebgpuMaterialsTransmissionCubeFaceSize >> mipLevel);
        const uint tileSize = faceSize + 2u;
        const uint mipOffset =
            WebgpuMaterialsTransmissionAtlasMipOffset(mipLevel);
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
            WebgpuMaterialsTransmissionCubeDirection(face, faceUv);
        const float4 value =
            WebgpuMaterialsTransmissionSampleSourceCube(
                resources,
                direction,
                float(mipLevel));
        resources->atlas->write(
            threadID.xy,
            half4(value));
    }
};

/** Samples one explicit guttered atlas mip without relying on cube descriptors. */
float3 WebgpuMaterialsTransmissionSampleAtlasLevel(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
    float3 direction,
    uint mipLevel)
{
    const uint clampedMip =
        min(mipLevel, WebgpuMaterialsTransmissionCubeMipCount - 1u);
    const uint faceSize =
        max(1u, WebgpuMaterialsTransmissionCubeFaceSize >> clampedMip);
    const uint tileSize = faceSize + 2u;
    const float3 faceUv =
        WebgpuMaterialsTransmissionCubeFaceUv(direction);
    const uint face = uint(faceUv.z + 0.5f);
    const uint faceColumn = face % 3u;
    const uint faceRow = face / 3u;
    const float2 atlasPixel = float2(
        float(faceColumn * tileSize + 1u) +
            faceUv.x * float(faceSize),
        float(WebgpuMaterialsTransmissionAtlasMipOffset(clampedMip) +
              faceRow * tileSize + 1u) +
            faceUv.y * float(faceSize));
    return resources->cubeAtlas->sampleLevel(
        resources->pmremSampler,
        atlasPixel /
            float2(
                float(WebgpuMaterialsTransmissionAtlasWidth),
                float(WebgpuMaterialsTransmissionAtlasHeight)),
        0.0f).xyz;
}

/** Samples the explicit cube atlas with a derivative-selected trilinear mip. */
float3 WebgpuMaterialsTransmissionSampleAtlas(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
    float3 direction)
{
    const float3 faceUv =
        WebgpuMaterialsTransmissionCubeFaceUv(direction);
    const float2 selectedUv = float2(faceUv.x, faceUv.y);
    const uint selectedFace = uint(faceUv.z + 0.5f);
    const float3 directionDerivativeX = ddx(direction);
    const float3 directionDerivativeY = ddy(direction);
    const float2 derivativeX =
        (WebgpuMaterialsTransmissionCubeUvForFace(
             direction + directionDerivativeX,
             selectedFace) -
         selectedUv) *
        float(WebgpuMaterialsTransmissionCubeFaceSize);
    const float2 derivativeY =
        (WebgpuMaterialsTransmissionCubeUvForFace(
             direction + directionDerivativeY,
             selectedFace) -
         selectedUv) *
        float(WebgpuMaterialsTransmissionCubeFaceSize);
    const float footprint = max(
        max(length(derivativeX), length(derivativeY)),
        1.0f);
    const float lod = clamp(
        log2(footprint),
        0.0f,
        float(WebgpuMaterialsTransmissionCubeMipCount - 1u));
    const uint lowerMip = uint(floor(lod));
    const uint upperMip =
        min(lowerMip + 1u, WebgpuMaterialsTransmissionCubeMipCount - 1u);
    const float3 lower =
        WebgpuMaterialsTransmissionSampleAtlasLevel(
            resources, direction, lowerMip);
    const float3 upper =
        WebgpuMaterialsTransmissionSampleAtlasLevel(
            resources, direction, upperMip);
    return lerp(lower, upper, frac(lod));
}

/** Builds one explicit linear equirectangular mip level per ordered dispatch. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebgpuMaterialsTransmissionEquirectangularMipBuild final : public IComputeClass
{
public:
    /** Binds the base image, previous mip, writable target mip, and level. */
    constructor(
        BindGroup<WebgpuMaterialsTransmissionEquirectangularMipResources>
            resources [[Slot0]])
    {
    }

private:
    /** Copies mip zero or box-filters the previous encoded mip in linear light. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint mipLevel = resources->uniforms->levelAndExtent.x;
        const uint width = resources->uniforms->levelAndExtent.y;
        const uint height = resources->uniforms->levelAndExtent.z;
        if (threadID.x >= width || threadID.y >= height)
        {
            return;
        }
        if (mipLevel == 0u)
        {
            const half4 encoded = resources->source->read(threadID.xy, 0u);
            resources->target->write(threadID.xy, encoded);
            return;
        }
        const uint sourceWidth = max(
            1u,
            WebgpuMaterialsTransmissionEquirectangularWidth >>
                (mipLevel - 1u));
        const uint sourceHeight = max(
            1u,
            WebgpuMaterialsTransmissionEquirectangularHeight >>
                (mipLevel - 1u));
        const uint2 source0 = uint2(
            min(threadID.x * 2u, sourceWidth - 1u),
            min(threadID.y * 2u, sourceHeight - 1u));
        const uint2 source1 = uint2(
            min(source0.x + 1u, sourceWidth - 1u),
            min(source0.y + 1u, sourceHeight - 1u));
        const half4 encoded0 = resources->previous->read(source0, 0u);
        const half4 encoded1 = resources->previous->read(
            uint2(source1.x, source0.y), 0u);
        const half4 encoded2 = resources->previous->read(
            uint2(source0.x, source1.y), 0u);
        const half4 encoded3 = resources->previous->read(source1, 0u);
        const half4 filtered =
            (encoded0 + encoded1 + encoded2 + encoded3) * half(0.25f);
        resources->target->write(threadID.xy, filtered);
    }
};

/** Converts the UltraHDR equirectangular source into the padded PMREM cubeUV base. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebgpuMaterialsTransmissionPmremSourcePass final : public IComputeClass
{
public:
    /** Binds the immutable equirectangular source and writable PMREM atlas. */
    constructor(
        BindGroup<WebgpuMaterialsTransmissionPmremSourceResources>
            sourceResources [[Slot0]])
    {
    }

private:
    /** Writes one level-zero cubeUV texel with Three's equirectangular mapping. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= WebgpuMaterialsDisplacementmapPmremAtlasWidth ||
            threadID.y >= WebgpuMaterialsDisplacementmapPmremCubeSize * 2u)
        {
            return;
        }
        const uint faceColumn =
            threadID.x / WebgpuMaterialsDisplacementmapPmremCubeSize;
        const uint faceRow =
            threadID.y / WebgpuMaterialsDisplacementmapPmremCubeSize;
        const uint face = faceColumn + faceRow * 3u;
        const uint2 localCoordinate = uint2(
            threadID.x -
                faceColumn * WebgpuMaterialsDisplacementmapPmremCubeSize,
            threadID.y -
                faceRow * WebgpuMaterialsDisplacementmapPmremCubeSize);
        const float2 faceUv =
            (float2(localCoordinate) - 0.5f) /
            float(WebgpuMaterialsDisplacementmapPmremCubeSize - 2u);
        const float3 direction = normalize(
            webgpuMaterialsDisplacementmapPmremDirection(faceUv, face));
        const float2 uv = float2(
            atan2(direction.z, direction.x) *
                    0.15915494309189535f +
                0.5f,
            asin(clamp(direction.y, -1.0f, 1.0f)) *
                    0.3183098861837907f +
                0.5f);
        const half4 color = sourceResources->source->sampleLevel(
            sourceResources->sourceSampler,
            uv,
            0.0f);
        sourceResources->destinationAtlas->write(threadID.xy, color);
    }
};

/** Samples the encoded level-zero environment in linear working space. */
float3 WebgpuMaterialsTransmissionSampleEquirectangular(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
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
    return float3(resources->equirectangular->sampleLevel(
        resources->environmentSampler,
        uv,
        0.0f).xyz);
}

/** Samples the scenario-selected environment using the exact mapping family. */
float3 WebgpuMaterialsTransmissionSampleEnvironment(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
    float3 direction)
{
    if (resources->uniforms->cameraPositionAndMode.w < 0.5f)
    {
        return WebgpuMaterialsTransmissionSampleSceneCube(
            resources,
            direction);
    }
    return WebgpuMaterialsTransmissionSampleEquirectangular(
        resources,
        direction);
}

/** Samples the exact packed PMREM levels used by the r185 physical material. */
float3 WebgpuMaterialsTransmissionSamplePmrem(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
    float3 direction,
    float roughness)
{
    direction = float3(direction.x, -direction.y, direction.z);
    const float mip = clamp(
        webgpuMaterialsDisplacementmapRoughnessToMip(roughness),
        -2.0f,
        9.0f);
    const float lowerMip = floor(mip);
    const float interpolation = mip - lowerMip;
    const float3 lowerColor = float4(resources->pmremAtlas->sampleGrad(
        resources->pmremSampler,
        webgpuMaterialsDisplacementmapPmremAtlasUv(
            direction,
            lowerMip),
        float2(0.0f),
        float2(0.0f))).xyz;
    if (interpolation == 0.0f)
    {
        return lowerColor;
    }
    const float3 upperColor = float4(resources->pmremAtlas->sampleGrad(
        resources->pmremSampler,
        webgpuMaterialsDisplacementmapPmremAtlasUv(
            direction,
            lowerMip + 1.0f),
        float2(0.0f),
        float2(0.0f))).xyz;
    return lerp(lowerColor, upperColor, interpolation);
}

/** Samples one explicitly generated screen level selected without descriptor arrays. */
float4 WebgpuMaterialsTransmissionSampleScreenLevel(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
    float2 uv,
    uint mipIndex)
{
    if (mipIndex == 0u)
        return float4(resources->transmissionMip0->sampleLevel(
            resources->transmissionSampler, uv, 0.0f));
    if (mipIndex == 1u)
        return float4(resources->transmissionMip1->sampleLevel(
            resources->transmissionSampler, uv, 0.0f));
    if (mipIndex == 2u)
        return float4(resources->transmissionMip2->sampleLevel(
            resources->transmissionSampler, uv, 0.0f));
    if (mipIndex == 3u)
        return float4(resources->transmissionMip3->sampleLevel(
            resources->transmissionSampler, uv, 0.0f));
    if (mipIndex == 4u)
        return float4(resources->transmissionMip4->sampleLevel(
            resources->transmissionSampler, uv, 0.0f));
    if (mipIndex == 5u)
        return float4(resources->transmissionMip5->sampleLevel(
            resources->transmissionSampler, uv, 0.0f));
    return float4(resources->transmissionMip6->sampleLevel(
        resources->transmissionSampler, uv, 0.0f));
}

/** Reproduces r185's four-tap bicubic filter for one explicit screen level. */
float4 WebgpuMaterialsTransmissionSampleScreenBicubicLevel(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
    float2 uv,
    uint mipIndex,
    float2 levelSize)
{
    const float2 coordinate = uv * levelSize + float2(0.5f);
    const float2 fraction = frac(coordinate);
    const float2 fractionSquared = fraction * fraction;
    const float2 fractionCubed = fractionSquared * fraction;
    const float2 weight0 =
        (-fractionCubed + 3.0f * fractionSquared -
         3.0f * fraction + 1.0f) / 6.0f;
    const float2 weight1 =
        (3.0f * fractionCubed - 6.0f * fractionSquared + 4.0f) / 6.0f;
    const float2 weight2 =
        (-3.0f * fractionCubed + 3.0f * fractionSquared +
         3.0f * fraction + 1.0f) / 6.0f;
    const float2 weight3 = fractionCubed / 6.0f;
    const float2 combined0 = weight0 + weight1;
    const float2 combined1 = weight2 + weight3;
    const float2 offset0 = -1.0f + weight1 / combined0;
    const float2 offset1 = 1.0f + weight3 / combined1;
    const float2 base = floor(coordinate);
    const float2 inverseSize = float2(1.0f) / levelSize;
    const float4 sample00 = WebgpuMaterialsTransmissionSampleScreenLevel(
        resources,
        (base + float2(offset0.x, offset0.y) - float2(0.5f)) * inverseSize,
        mipIndex);
    const float4 sample10 = WebgpuMaterialsTransmissionSampleScreenLevel(
        resources,
        (base + float2(offset1.x, offset0.y) - float2(0.5f)) * inverseSize,
        mipIndex);
    const float4 sample01 = WebgpuMaterialsTransmissionSampleScreenLevel(
        resources,
        (base + float2(offset0.x, offset1.y) - float2(0.5f)) * inverseSize,
        mipIndex);
    const float4 sample11 = WebgpuMaterialsTransmissionSampleScreenLevel(
        resources,
        (base + float2(offset1.x, offset1.y) - float2(0.5f)) * inverseSize,
        mipIndex);
    return combined0.y *
            (combined0.x * sample00 + combined1.x * sample10) +
        combined1.y *
            (combined0.x * sample01 + combined1.x * sample11);
}

/** Samples the explicit backdrop mip chain using r185's roughness interpolation. */
float4 WebgpuMaterialsTransmissionSampleScreenBicubic(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
    float2 uv,
    float roughness,
    float ior)
{
    const float appliedRoughness =
        roughness * clamp(ior * 2.0f - 2.0f, 0.0f, 1.0f);
    const float lod = clamp(
        log2(resources->uniforms->viewportAndReserved.x) * appliedRoughness,
        0.0f,
        float(WebgpuMaterialsTransmissionScreenMipCount - 1u));
    const uint lowerLevel = uint(floor(lod));
    const uint upperLevel = min(
        lowerLevel + 1u,
        WebgpuMaterialsTransmissionScreenMipCount - 1u);
    const float lowerScale = exp2(float(lowerLevel));
    const float upperScale = exp2(float(upperLevel));
    const float2 viewport = resources->uniforms->viewportAndReserved.xy;
    const float2 lowerSize = max(
        floor(viewport / lowerScale),
        float2(1.0f));
    const float2 upperSize = max(
        floor(viewport / upperScale),
        float2(1.0f));
    const float4 lowerColor =
        WebgpuMaterialsTransmissionSampleScreenBicubicLevel(
            resources, uv, lowerLevel, lowerSize);
    const float4 upperColor =
        WebgpuMaterialsTransmissionSampleScreenBicubicLevel(
            resources, uv, upperLevel, upperSize);
    return lerp(lowerColor, upperColor, frac(lod));
}

/** Evaluates the r185 physical environment and screen-space transmission terms. */
float4 WebgpuMaterialsTransmissionPhysicalColor(
    IN BindGroup<WebgpuMaterialsTransmissionSceneResources> resources,
    WebgpuMaterialsTransmissionSphereOutput inputValue,
    float normalSign)
{
    const float3 normal = normalize(inputValue.worldNormal) * normalSign;
    const float3 viewDirection = normalize(
        resources->uniforms->cameraPositionAndMode.xyz -
        inputValue.worldPosition);
    const float normalDotView = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float3 baseColor = resources->uniforms->rotationRow0.xyz;
    const float metalness = resources->uniforms->rotationRow0.w;
    // Consume the uploaded Three.js camera basis without renormalizing it.
    // The reference shader uses the float uniform values directly; an extra
    // normalization changes roughness derivatives at the transmission edge.
    const float3 cameraRight = float3(
        resources->uniforms->cameraRightAndRefraction.xyz);
    const float3 cameraUp = float3(
        resources->uniforms->cameraUpAndFrame.xyz);
    const float3 cameraForward = float3(
        resources->uniforms->cameraForwardAndTanHalfFov.xyz);
    const float3 viewGeometryNormal = normalize(float3(
        dot(normal, cameraRight),
        dot(normal, cameraUp),
        dot(normal, cameraForward)));
    const float3 normalDerivative = max(
        abs(ddx(viewGeometryNormal)),
        abs(ddy(viewGeometryNormal)));
    const float roughness = min(
        max(0.0f, 0.0525f) +
            max(
                max(normalDerivative.x, normalDerivative.y),
                normalDerivative.z),
        1.0f);
    const float ior = resources->uniforms->rotationRow2.y;
    const float thickness = resources->uniforms->rotationRow2.z;
    const float transmission =
        resources->uniforms->cameraRightAndRefraction.w;
    const float envMapIntensity = resources->uniforms->rotationRow2.x;
    const float specularIntensity =
        resources->uniforms->viewportAndReserved.w;
    const float dielectric = (ior - 1.0f) / (ior + 1.0f);
    const float3 specularColor = min(
        float3(dielectric * dielectric) *
            resources->uniforms->rotationRow1.xyz,
        float3(1.0f)) * specularIntensity;
    const float3 blendedSpecularColor =
        lerp(specularColor, baseColor, metalness);
    const float specularF90 = lerp(specularIntensity, 1.0f, metalness);

    const float2 dfg = float4(resources->dfgLut->sample(
        resources->dfgSampler,
        float2(roughness, normalDotView))).xy;
    const float3 environmentBrdf =
        blendedSpecularColor * dfg.x + float3(specularF90 * dfg.y);

    const float3 incident = -viewDirection;
    const float refractionRatio = 1.0f / ior;
    const float incidentNormalDot = dot(incident, normal);
    const float refractionDiscriminant =
        1.0f - refractionRatio * refractionRatio *
            (1.0f - incidentNormalDot * incidentNormalDot);
    const float3 refracted = refractionDiscriminant < 0.0f
        ? float3(0.0f)
        : refractionRatio * incident -
            (refractionRatio * incidentNormalDot +
             sqrt(refractionDiscriminant)) * normal;
    // Three r185 normalizes the refracted vector before applying local-space
    // thickness, matching getVolumeTransmissionRay for grazing fragments.
    const float3 transmissionRay = normalize(refracted) * thickness;
    const float3 transmittedPosition =
        inputValue.worldPosition + transmissionRay;
    const float3 transmittedRelative = transmittedPosition -
        resources->uniforms->cameraPositionAndMode.xyz;
    const float transmittedViewX =
        transmittedRelative.x *
            resources->uniforms->cameraRightAndRefraction.x +
        transmittedRelative.y *
            resources->uniforms->cameraRightAndRefraction.y +
        transmittedRelative.z *
            resources->uniforms->cameraRightAndRefraction.z;
    const float transmittedViewY =
        transmittedRelative.x * resources->uniforms->cameraUpAndFrame.x +
        transmittedRelative.y * resources->uniforms->cameraUpAndFrame.y +
        transmittedRelative.z * resources->uniforms->cameraUpAndFrame.z;
    const float transmittedViewDepth =
        transmittedRelative.x *
            resources->uniforms->cameraForwardAndTanHalfFov.x +
        transmittedRelative.y *
            resources->uniforms->cameraForwardAndTanHalfFov.y +
        transmittedRelative.z *
            resources->uniforms->cameraForwardAndTanHalfFov.z;
    const float tangentHalfFov =
        resources->uniforms->cameraForwardAndTanHalfFov.w;
    const float aspect = resources->uniforms->viewportAndReserved.x /
        resources->uniforms->viewportAndReserved.y;
    const float2 transmittedNdc = float2(
        transmittedViewX /
            (transmittedViewDepth * tangentHalfFov * aspect),
        transmittedViewY /
            (transmittedViewDepth * tangentHalfFov));
    const float2 transmittedUv = float2(
        transmittedNdc.x * 0.5f + 0.5f,
        transmittedNdc.y * 0.5f + 0.5f);
    const float4 backgroundSample =
        WebgpuMaterialsTransmissionSampleScreenBicubic(
            resources,
            transmittedUv,
            roughness,
            ior);
    const float3 backgroundColor = backgroundSample.xyz;
    const float3 diffuseContribution = baseColor * (1.0f - metalness);
    // r185 passes diffuseContribution (base color after metalness) into
    // getIBLVolumeRefraction.  Transmission therefore attenuates the
    // non-metallic component, rather than the unmodified base color.
    const float3 transmittedColor =
        (float3(1.0f) - environmentBrdf) *
        diffuseContribution * backgroundColor;

    const float3 reflected = normalize(lerp(
        reflect(incident, normal),
        normal,
        roughness * roughness * roughness * roughness));
    const float3 radiance =
        WebgpuMaterialsTransmissionSamplePmrem(
            resources,
            reflected,
            roughness) *
        envMapIntensity;
    const float3 dielectricSingle =
        specularColor * dfg.x + float3(specularF90 * dfg.y);
    const float3 dielectricAverage =
        specularColor +
        (float3(1.0f) - specularColor) * 0.047619f;
    const float energyRemainder = 1.0f - dfg.x - dfg.y;
    const float3 dielectricMulti =
        dielectricSingle * dielectricAverage /
        (float3(1.0f) -
         float3(energyRemainder) * dielectricAverage) *
        energyRemainder;
    const float3 metallicSingle =
        baseColor * dfg.x + float3(specularF90 * dfg.y);
    const float3 metallicAverage =
        baseColor + (float3(1.0f) - baseColor) * 0.047619f;
    const float3 metallicMulti =
        metallicSingle * metallicAverage /
        (float3(1.0f) -
         float3(energyRemainder) * metallicAverage) *
        energyRemainder;
    const float3 singleScattering = lerp(
        dielectricSingle,
        metallicSingle,
        metalness);
    const float3 multiScattering = lerp(
        dielectricMulti,
        metallicMulti,
        metalness);
    const float3 iblIrradianceOverPi =
        WebgpuMaterialsTransmissionSamplePmrem(
            resources,
            normal,
            1.0f) *
        envMapIntensity;
    const float3 reflectedColor =
        radiance * singleScattering +
        iblIrradianceOverPi * multiScattering;
    const float3 residualDiffuse =
        diffuseContribution *
        (float3(1.0f) - dielectricSingle - dielectricMulti) *
        iblIrradianceOverPi;
    const float3 surfaceColor =
        lerp(residualDiffuse, transmittedColor, transmission) +
        reflectedColor;
    // Sample the exact 2x2 CanvasTexture used by the upstream example.  The
    // nearest magnification, linear minification and repeat wrapping are
    // configured by alphaSampler, so the half-row transition and mip behavior
    // remain part of the DSL texture path instead of a hard-coded branch.
    const float2 alphaUv = float2(
        inputValue.textureCoordinate.x,
        inputValue.textureCoordinate.y * 3.5f);
    const float alphaMapValue = resources->alphaMap->sample(
        resources->alphaSampler, alphaUv).x;
    const float alpha = resources->uniforms->cameraUpAndFrame.w *
        alphaMapValue;
    return float4(surfaceColor, alpha);
}

/** Draws the environment background into the linear Scene target. */
class WebgpuMaterialsTransmissionEnvironmentBackgroundPass final : public IRenderClass
{
public:
    /** Binds the decoded equirectangular image and deterministic camera state. */
    constructor(
        BindGroup<WebgpuMaterialsTransmissionBackgroundResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle for the equirectangular environment. */
    WebgpuMaterialsTransmissionScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuMaterialsTransmissionScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reconstructs the camera ray and samples the equirectangular environment. */
    WebgpuMaterialsTransmissionLinearFrameBuffer fragment(
        WebgpuMaterialsTransmissionScreenOutput inputValue)
    {
        const float2 ndc = inputValue.uv * 2.0f - 1.0f;
        const float aspect =
            resources->uniforms->viewportAndReserved.x /
            resources->uniforms->viewportAndReserved.y;
        const float tangentHalfFov =
            resources->uniforms->cameraForwardAndTanHalfFov.w;
        const float3 ray = normalize(
            resources->uniforms->cameraForwardAndTanHalfFov.xyz +
            resources->uniforms->cameraRightAndRefraction.xyz *
                (ndc.x * aspect * tangentHalfFov) +
            resources->uniforms->cameraUpAndFrame.xyz *
                (-ndc.y * tangentHalfFov));
        const float3 normalizedRay = normalize(ray);
        const float2 environmentUv = float2(
            atan2(normalizedRay.z, normalizedRay.x) *
                    0.15915494309189535f +
                0.5f,
            0.5f + asin(clamp(normalizedRay.y, -1.0f, 1.0f)) *
                0.3183098861837907f);
        WebgpuMaterialsTransmissionLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            resources->equirectangular->sampleLevel(
                resources->environmentSampler,
                environmentUv,
                0.0f).xyz,
            half(1.0f));
        return frameBuffer;
    }
};

/** Downsamples one immutable backdrop level through the current DSL render path. */
class WebgpuMaterialsTransmissionScreenMipPass final : public IRenderClass
{
public:
    /** Binds one previous level and disables geometry depth state. */
    constructor(
        BindGroup<WebgpuMaterialsTransmissionScreenMipResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle for a single explicit mip level. */
    WebgpuMaterialsTransmissionScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuMaterialsTransmissionScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Uses linear center sampling to reproduce one ordered two-by-two mip reduction. */
    WebgpuMaterialsTransmissionLinearFrameBuffer fragment(
        WebgpuMaterialsTransmissionScreenOutput inputValue)
    {
        WebgpuMaterialsTransmissionLinearFrameBuffer frameBuffer;
        frameBuffer.color = resources->source->sampleLevel(
            resources->sourceSampler,
            inputValue.uv,
            0.0f);
        return frameBuffer;
    }
};

/** Copies the composed back-face Scene into the front transmission mip chain. */
class WebgpuMaterialsTransmissionScreenCapturePass final : public IRenderClass
{
public:
    /** Binds the Scene source and disables depth state for the fullscreen copy. */
    constructor(
        BindGroup<WebgpuMaterialsTransmissionScreenCaptureResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits a fullscreen triangle covering the capture target. */
    WebgpuMaterialsTransmissionScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuMaterialsTransmissionScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Copies the linear Scene color without applying a second transfer function. */
    WebgpuMaterialsTransmissionLinearFrameBuffer fragment(
        WebgpuMaterialsTransmissionScreenOutput inputValue)
    {
        WebgpuMaterialsTransmissionLinearFrameBuffer frameBuffer;
        frameBuffer.color = resources->source->sampleLevel(
            resources->sourceSampler,
            inputValue.uv,
            0.0f);
        return frameBuffer;
    }
};

/** Draws the single mutable MeshBasicMaterial sphere with explicit indexed geometry. */
class WebgpuMaterialsTransmissionFrontPass final : public IRenderClass
{
public:
    /** Binds one ordinary material resource group and opaque depth state. */
    constructor(
        BindGroup<WebgpuMaterialsTransmissionSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
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
    /** Projects the exact CPU-generated detail-15 IcosahedronGeometry. */
    WebgpuMaterialsTransmissionSphereOutput vertex(
        WebgpuMaterialsTransmissionVertex inputValue [[VertexInput0]])
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
            (2001.0f / 1999.0f) * viewDepth -
            (4000.0f / 1999.0f);
        WebgpuMaterialsTransmissionSphereOutput outputValue;
        outputValue.position = float4(
            viewX / (tangentHalfFov * aspect),
            viewY / tangentHalfFov,
            clipZ,
            viewDepth);
        outputValue.worldPosition = inputValue.position.xyz;
        outputValue.worldNormal = inputValue.normal.xyz;
        outputValue.textureCoordinate = inputValue.textureCoordinate;
        float3 shadingPosition = inputValue.position.xyz;
        float3 vertexNormal = normalize(float3(
            inputValue.normal.x,
            inputValue.normal.y,
            inputValue.normal.z));
        if (resources->uniforms->cameraPositionAndMode.w > 1.5f)
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
        const float refractionRatio =
            1.0f / resources->uniforms->rotationRow2.y;
        const float refractionDiscriminant =
            1.0f - refractionRatio * refractionRatio *
                (1.0f - incidentNormalDot * incidentNormalDot);
        outputValue.environmentDirection =
            refractionDiscriminant < 0.0f
                ? float3(0.0f)
                : refractionRatio * cameraToVertex -
                    (refractionRatio * incidentNormalDot +
                     sqrt(refractionDiscriminant)) * vertexNormal;
        return outputValue;
    }

    /** Evaluates reflection or refraction and the synchronized material rotation. */
    WebgpuMaterialsTransmissionSceneFrameBuffer fragment(
        WebgpuMaterialsTransmissionSphereOutput inputValue)
    {
        WebgpuMaterialsTransmissionSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            WebgpuMaterialsTransmissionPhysicalColor(
                resources,
                inputValue,
                1.0f));
        return frameBuffer;
    }
};

/** Draws the inner back faces before the front-face transmission phase. */
class WebgpuMaterialsTransmissionBackPass final : public IRenderClass
{
public:
    /** Binds one ordinary material resource group and opaque depth state. */
    constructor(
        BindGroup<WebgpuMaterialsTransmissionSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
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
    /** Projects the exact CPU-generated detail-15 IcosahedronGeometry. */
    WebgpuMaterialsTransmissionSphereOutput vertex(
        WebgpuMaterialsTransmissionVertex inputValue [[VertexInput0]])
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
            (2001.0f / 1999.0f) * viewDepth -
            (4000.0f / 1999.0f);
        WebgpuMaterialsTransmissionSphereOutput outputValue;
        outputValue.position = float4(
            viewX / (tangentHalfFov * aspect),
            viewY / tangentHalfFov,
            clipZ,
            viewDepth);
        outputValue.worldPosition = inputValue.position.xyz;
        outputValue.worldNormal = inputValue.normal.xyz;
        outputValue.textureCoordinate = inputValue.textureCoordinate;
        float3 shadingPosition = inputValue.position.xyz;
        float3 vertexNormal = normalize(float3(
            inputValue.normal.x,
            inputValue.normal.y,
            inputValue.normal.z));
        if (resources->uniforms->cameraPositionAndMode.w > 1.5f)
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
        const float refractionRatio =
            1.0f / resources->uniforms->rotationRow2.y;
        const float refractionDiscriminant =
            1.0f - refractionRatio * refractionRatio *
                (1.0f - incidentNormalDot * incidentNormalDot);
        outputValue.environmentDirection =
            refractionDiscriminant < 0.0f
                ? float3(0.0f)
                : refractionRatio * cameraToVertex -
                    (refractionRatio * incidentNormalDot +
                     sqrt(refractionDiscriminant)) * vertexNormal;
        return outputValue;
    }

    /** Evaluates reflection or refraction and the synchronized material rotation. */
    WebgpuMaterialsTransmissionSceneFrameBuffer fragment(
        WebgpuMaterialsTransmissionSphereOutput inputValue)
    {
        WebgpuMaterialsTransmissionSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            WebgpuMaterialsTransmissionPhysicalColor(
                resources,
                inputValue,
                -1.0f));
        return frameBuffer;
    }
};


/** Applies the sole r185 display transfer to the composed linear Scene. */
class WebgpuMaterialsTransmissionOutputToneMapPass final : public IRenderClass
{
public:
    /** Binds the composed Scene texture and disables geometry state. */
    constructor(
        BindGroup<WebgpuMaterialsTransmissionOutputResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen output triangle. */
    WebgpuMaterialsTransmissionScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuMaterialsTransmissionScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Encodes the linear environment result into the final RGBA8 target. */
    WebgpuMaterialsTransmissionOutputFrameBuffer fragment(
        WebgpuMaterialsTransmissionScreenOutput inputValue)
    {
        float3 linearColor = resources->sceneColor->sample(
            resources->outputSampler,
            inputValue.uv).xyz;
        // Match r185 acesFilmicToneMapping: exposure is multiplied and the
        // reference implementation normalizes by 0.6 before the ACES input
        // matrix (the 0.59719 coefficient belongs to that matrix, not the
        // exposure normalization).
        linearColor *= resources->uniforms->rotationRow2.w / 0.6f;
        const float3 acesInput = float3(
            0.59719f * linearColor.x + 0.35458f * linearColor.y +
                0.04823f * linearColor.z,
            0.07600f * linearColor.x + 0.90834f * linearColor.y +
                0.01566f * linearColor.z,
            0.02840f * linearColor.x + 0.13383f * linearColor.y +
                0.83777f * linearColor.z);
        const float3 acesNumerator =
            acesInput * (acesInput + 0.0245786f) - 0.000090537f;
        const float3 acesDenominator =
            acesInput * (0.983729f * acesInput + 0.4329510f) + 0.238081f;
        const float3 acesFit = acesNumerator / acesDenominator;
        linearColor = clamp(float3(
            1.60475f * acesFit.x - 0.53108f * acesFit.y -
                0.07367f * acesFit.z,
            -0.10208f * acesFit.x + 1.10813f * acesFit.y -
                0.00605f * acesFit.z,
            -0.00327f * acesFit.x - 0.07276f * acesFit.y +
                1.07602f * acesFit.z),
            float3(0.0f), float3(1.0f));
        float3 encoded = float3(
            WebgpuMaterialsTransmissionLinearToSrgb(linearColor.x),
            WebgpuMaterialsTransmissionLinearToSrgb(linearColor.y),
            WebgpuMaterialsTransmissionLinearToSrgb(linearColor.z));
        if (resources->uniforms->viewportAndReserved.z > 0.5f)
        {
            const float2 pixel =
                inputValue.uv * resources->uniforms->viewportAndReserved.xy;
            const float outerCoverage = WebgpuMaterialsTransmissionEdgeCoverage(
                WebgpuMaterialsTransmissionRoundedBoxDistance(
                    pixel,
                    float2(614.0f, 15.0f),
                    float2(785.0f, 53.0f),
                    12.0f,
                    6.0f));
            const float innerCoverage = WebgpuMaterialsTransmissionEdgeCoverage(
                WebgpuMaterialsTransmissionRoundedBoxDistance(
                    pixel,
                    float2(615.0f, 16.0f),
                    float2(784.0f, 52.0f),
                    11.0f,
                    5.0f));
            const float activeCoverage =
                pixel.x < 663.0f ? innerCoverage : 0.0f;
            const float inactiveCoverage =
                pixel.x >= 663.0f ? innerCoverage : 0.0f;
            const float outerAlpha =
                max(outerCoverage - innerCoverage, 0.0f) * 0.9f;
            const float innerAlpha = inactiveCoverage * 0.896f;
            const float activeAlpha = activeCoverage * 0.951f;
            encoded = encoded * (1.0f - outerAlpha) +
                float3(43.0f, 48.0f, 58.0f) / 255.0f * outerAlpha;
            encoded = encoded * (1.0f - innerAlpha) +
                float3(34.0f, 36.0f, 42.0f) / 255.0f * innerAlpha;
            encoded = encoded * (1.0f - activeAlpha) +
                float3(35.0f, 72.0f, 94.0f) / 255.0f * activeAlpha;
        }
        WebgpuMaterialsTransmissionOutputFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated environment-map Compute, Scene, and output DSL pipeline. */
class WebgpuMaterialsTransmissionRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebgpuMaterialsTransmissionVertex, BufferUsage<Vertex, CopyDst>>
        vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebgpuMaterialsTransmissionBackgroundVertex,
           BufferUsage<Vertex, CopyDst>> backgroundVertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> backgroundIndexBuffer;
    Buffer<WebgpuMaterialsTransmissionUniforms, BufferUsage<Uniform, CopyDst>>
        uniformBuffer;
    eastl::array<
        Buffer<WebgpuMaterialsTransmissionEquirectangularMipUniforms,
               BufferUsage<Uniform, CopyDst>>,
        WebgpuMaterialsTransmissionEquirectangularMipCount>
        equirectangularMipUniformBuffers;
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
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> alphaMap;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> equirectangularSource;
    eastl::array<
        Texture<TextureFormat::RGBA16Float,
                TextureUsage<StorageBinding, TextureBinding>,
                TextureDimension::e2D>,
        WebgpuMaterialsTransmissionEquirectangularMipCount>
        equirectangularMipTextures;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> cubeAtlas;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> pmremAtlas;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> pmremPingPongAtlas;
    Texture<TextureFormat::RG16Float,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> dfgLutTexture;
    eastl::array<
        Buffer<WebgpuMaterialsDisplacementmapPmremUniforms,
               BufferUsage<Uniform, CopyDst>>,
        WebgpuMaterialsDisplacementmapPmremLodCount - 1u>
        pmremUniformBuffers;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneColor;
    eastl::array<
        Texture<TextureFormat::RGBA16Float,
                TextureUsage<RenderAttachment, TextureBinding>,
                TextureDimension::e2D>,
        WebgpuMaterialsTransmissionScreenMipCount>
        transmissionMipTextures;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Sampler environmentSampler;
    Sampler pmremSampler;
    Sampler dfgSampler;
    Sampler transmissionSampler;
    Sampler alphaSampler;
    Sampler outputSampler;
    BindGroup<WebgpuMaterialsTransmissionAtlasResources> atlasResources;
    eastl::array<
        BindGroup<WebgpuMaterialsTransmissionEquirectangularMipResources>,
        WebgpuMaterialsTransmissionEquirectangularMipCount>
        equirectangularMipResources;
    BindGroup<WebgpuMaterialsTransmissionSceneResources> sceneResources;
    BindGroup<WebgpuMaterialsTransmissionBackgroundResources>
        backgroundResources;
    BindGroup<WebgpuMaterialsTransmissionScreenCaptureResources>
        transmissionCaptureResources;
    eastl::array<
        BindGroup<WebgpuMaterialsTransmissionScreenMipResources>,
        WebgpuMaterialsTransmissionScreenMipCount - 1u>
        transmissionMipResources;
    BindGroup<WebgpuMaterialsTransmissionOutputResources> outputResources;
    ComputeClass<WebgpuMaterialsTransmissionCubeAtlasGutterBuild> atlasBuild;
    eastl::array<
        ComputeClass<WebgpuMaterialsTransmissionEquirectangularMipBuild>,
        WebgpuMaterialsTransmissionEquirectangularMipCount>
        equirectangularMipBuilds;
    RenderClass<WebgpuMaterialsTransmissionEnvironmentBackgroundPass> backgroundPass;
    eastl::array<
        RenderClass<WebgpuMaterialsTransmissionScreenMipPass>,
        WebgpuMaterialsTransmissionScreenMipCount - 1u>
        transmissionMipPasses;
    RenderClass<WebgpuMaterialsTransmissionScreenCapturePass>
        transmissionCapturePass;
    RenderClass<WebgpuMaterialsTransmissionBackPass> backPass;
    RenderClass<WebgpuMaterialsTransmissionFrontPass> frontPass;
    RenderClass<WebgpuMaterialsTransmissionOutputToneMapPass> outputPass;
    WebgpuMaterialsTransmissionUniforms uniforms;
    uint indexCount = 0u;
    uint backgroundIndexCount = 0u;
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
            "WebgpuMaterialsTransmissionVertices",
            20000u);
        indexBuffer = device->createBuffer(
            "WebgpuMaterialsTransmissionIndices",
            20000u);
        backgroundVertexBuffer = device->createBuffer(
            "WebgpuMaterialsTransmissionBackgroundVertices",
            2000u);
        backgroundIndexBuffer = device->createBuffer(
            "WebgpuMaterialsTransmissionBackgroundIndices",
            7000u);
        uniformBuffer = device->createBuffer(
            "WebgpuMaterialsTransmissionUniforms",
            1u);
        for (uint buildIndex = 0u;
             buildIndex < WebgpuMaterialsTransmissionEquirectangularMipCount;
             ++buildIndex)
        {
            equirectangularMipUniformBuffers[buildIndex] =
                device->createBuffer(
                    "WebgpuMaterialsTransmissionEquirectangularMipUniforms",
                    1u);
        }
        environmentSampler = device->createSampler({
            .label = "WebgpuMaterialsTransmissionEnvironmentSampler",
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
        pmremSampler = device->createSampler({
            .label = "WebgpuMaterialsTransmissionPmremSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
        });
        dfgSampler = device->createSampler({
            .label = "WebgpuMaterialsTransmissionDfgSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
        });
        transmissionSampler = device->createSampler({
            .label = "WebgpuMaterialsTransmissionScreenSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
        });
        alphaSampler = device->createSampler({
            .label = "WebgpuMaterialsTransmissionAlphaSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 1.0f,
        });
        outputSampler = device->createSampler({
            .label = "WebgpuMaterialsTransmissionOutputSampler",
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
            "WebgpuMaterialsTransmissionSceneColor",
            width,
            height,
            1u);
        for (uint mipLevel = 0u;
             mipLevel < WebgpuMaterialsTransmissionScreenMipCount;
             ++mipLevel)
        {
            transmissionMipTextures[mipLevel] = device->createTexture(
                "WebgpuMaterialsTransmissionScreenMip",
                max(1u, width >> mipLevel),
                max(1u, height >> mipLevel),
                1u);
        }
        sceneDepth = device->createTexture(
            "WebgpuMaterialsTransmissionSceneDepth",
            width,
            height,
            1u);
        outputColor = device->createTexture(
            "WebgpuMaterialsTransmissionOutput",
            width,
            height,
            1u);
    }

    /** Uploads decoded inputs and builds the cube and equirectangular mip atlases. */
    void configureScene(
        const eastl::vector<float4> &positions,
        const eastl::vector<float4> &normals,
        const eastl::vector<float2> &textureCoordinates,
        const eastl::vector<uint> &indices,
        const eastl::vector<float4> &backgroundPositions,
        const eastl::vector<float4> &backgroundNormals,
        const eastl::vector<uint> &backgroundIndices,
        const eastl::vector<eastl::vector<uint8_t>> &positiveXMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeXMips,
        const eastl::vector<eastl::vector<uint8_t>> &positiveYMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeYMips,
        const eastl::vector<eastl::vector<uint8_t>> &positiveZMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeZMips,
        const eastl::vector<uint16_t> &equirectangularPixels,
        const eastl::vector<uint> &dfgLutPackedPixels,
        float4 cameraPositionAndMode,
        float4 cameraRightAndRefraction,
        float4 cameraUpAndFrame,
        float4 cameraForwardAndTanHalfFov,
        float4 rotationRow0,
        float4 rotationRow1,
        float4 rotationRow2)
    {
        indexCount = uint(indices.size());
        backgroundIndexCount = uint(backgroundIndices.size());
        eastl::vector<WebgpuMaterialsTransmissionVertex> vertices;
        vertices.resize(positions.size());
        for (uint index = 0u; index < uint(vertices.size()); ++index)
        {
            vertices[index].position = positions[index];
            vertices[index].normal = normals[index];
            vertices[index].textureCoordinate = textureCoordinates[index];
        }
        eastl::vector<WebgpuMaterialsTransmissionBackgroundVertex>
            backgroundVertices;
        backgroundVertices.resize(backgroundPositions.size());
        for (uint index = 0u;
             index < uint(backgroundVertices.size());
             ++index)
        {
            backgroundVertices[index].position =
                backgroundPositions[index];
            backgroundVertices[index].normal = backgroundNormals[index];
        }
        positiveX = device->createTexture(
            "WebgpuMaterialsTransmissionPositiveX", 1024u, 1024u, 1u, 11u);
        negativeX = device->createTexture(
            "WebgpuMaterialsTransmissionNegativeX", 1024u, 1024u, 1u, 11u);
        positiveY = device->createTexture(
            "WebgpuMaterialsTransmissionPositiveY", 1024u, 1024u, 1u, 11u);
        negativeY = device->createTexture(
            "WebgpuMaterialsTransmissionNegativeY", 1024u, 1024u, 1u, 11u);
        positiveZ = device->createTexture(
            "WebgpuMaterialsTransmissionPositiveZ", 1024u, 1024u, 1u, 11u);
        negativeZ = device->createTexture(
            "WebgpuMaterialsTransmissionNegativeZ", 1024u, 1024u, 1u, 11u);
        alphaMap = device->createTexture(
            "WebgpuMaterialsTransmissionAlphaMap", 2u, 2u, 1u, 2u);
        equirectangularSource = device->createTexture(
            "WebgpuMaterialsTransmissionEquirectangularSource",
            WebgpuMaterialsTransmissionEquirectangularWidth,
            WebgpuMaterialsTransmissionEquirectangularHeight,
            1u);
        for (uint mipLevel = 0u;
             mipLevel < WebgpuMaterialsTransmissionEquirectangularMipCount;
             ++mipLevel)
        {
            equirectangularMipTextures[mipLevel] =
                device->createTexture(
                    "WebgpuMaterialsTransmissionEquirectangularMip",
                    max(1u,
                        WebgpuMaterialsTransmissionEquirectangularWidth >>
                            mipLevel),
                    max(1u,
                        WebgpuMaterialsTransmissionEquirectangularHeight >>
                            mipLevel),
                    1u);
        }
        cubeAtlas = device->createTexture(
            "WebgpuMaterialsTransmissionCubeAtlas",
            WebgpuMaterialsTransmissionAtlasWidth,
            WebgpuMaterialsTransmissionAtlasHeight,
            1u);
        pmremAtlas = device->createTexture(
            "WebgpuMaterialsTransmissionPmremAtlas",
            WebgpuMaterialsDisplacementmapPmremAtlasWidth,
            WebgpuMaterialsDisplacementmapPmremAtlasHeight,
            1u);
        pmremPingPongAtlas = device->createTexture(
            "WebgpuMaterialsTransmissionPmremPingPongAtlas",
            WebgpuMaterialsDisplacementmapPmremAtlasWidth,
            WebgpuMaterialsDisplacementmapPmremAtlasHeight,
            1u);
        dfgLutTexture = device->createTexture(
            "WebgpuMaterialsTransmissionDfgLut",
            16u,
            16u,
            1u);
        for (uint index = 0u;
             index < WebgpuMaterialsDisplacementmapPmremLodCount - 1u;
             ++index)
        {
            pmremUniformBuffers[index] = device->createBuffer(
                "WebgpuMaterialsTransmissionPmremUniforms",
                1u);
        }
        uniforms.cameraPositionAndMode = cameraPositionAndMode;
        uniforms.cameraRightAndRefraction = cameraRightAndRefraction;
        uniforms.cameraUpAndFrame = cameraUpAndFrame;
        uniforms.cameraForwardAndTanHalfFov =
            cameraForwardAndTanHalfFov;
        uniforms.rotationRow0 = rotationRow0;
        uniforms.rotationRow1 = rotationRow1;
        uniforms.rotationRow2 = rotationRow2;
        // The upstream Inspector is DOM chrome and is excluded from the
        // renderer-surface oracle; keep the GPU output free of UI pixels.
        uniforms.viewportAndReserved =
            float4(
                float(width),
                float(height),
                0.0f,
                rotationRow0.w > 0.01f ? 0.7f : 1.0f);
        const eastl::array<uint8_t, 16u> alphaBasePixels = {
            255u, 255u, 255u, 255u,
            255u, 255u, 255u, 255u,
            0u, 0u, 0u, 0u,
            0u, 0u, 0u, 0u};
        const eastl::array<uint8_t, 4u> alphaMipPixels = {
            128u, 128u, 128u, 128u};
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebgpuMaterialsTransmissionVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(backgroundVertexBuffer),
                backgroundVertices.data(),
                uint64_t(backgroundVertices.size()) *
                    sizeof(WebgpuMaterialsTransmissionBackgroundVertex))
            ->writeBuffer(
                BufferRange(backgroundIndexBuffer),
                backgroundIndices.data(),
                uint64_t(backgroundIndices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->writeTexture(
                dfgLutTexture,
                dfgLutPackedPixels.data(),
                uint64_t(dfgLutPackedPixels.size()) * sizeof(uint))
            ->writeTexture(
                alphaMap,
                alphaBasePixels.data(),
                uint64_t(alphaBasePixels.size()) * sizeof(uint8_t),
                0u)
            ->writeTexture(
                alphaMap,
                alphaMipPixels.data(),
                uint64_t(alphaMipPixels.size()) * sizeof(uint8_t),
                1u)
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
        graphicsQueue
            ->writeTexture(
                equirectangularSource,
                equirectangularPixels.data(),
                uint64_t(equirectangularPixels.size()) * sizeof(uint16_t),
                0u)
            ->submit();
        atlasResources =
            device->createBindGroup<WebgpuMaterialsTransmissionAtlasResources>(
                positiveX->createView(),
                negativeX->createView(),
                positiveY->createView(),
                negativeY->createView(),
                positiveZ->createView(),
                negativeZ->createView(),
                environmentSampler,
                cubeAtlas->createView());
        for (uint mipLevel = 0u;
             mipLevel < WebgpuMaterialsTransmissionEquirectangularMipCount;
             ++mipLevel)
        {
            const uint buildIndex = mipLevel;
            const uint mipWidth = max(
                1u,
                WebgpuMaterialsTransmissionEquirectangularWidth >> mipLevel);
            const uint mipHeight = max(
                1u,
                WebgpuMaterialsTransmissionEquirectangularHeight >> mipLevel);
            WebgpuMaterialsTransmissionEquirectangularMipUniforms mipUniforms;
            mipUniforms.levelAndExtent =
                uint4(mipLevel, mipWidth, mipHeight, 0u);
            graphicsQueue
                ->writeBuffer(
                    BufferRange(
                        equirectangularMipUniformBuffers[buildIndex]),
                    &mipUniforms,
                    sizeof(mipUniforms))
                ->submit();
            if (mipLevel == 0u)
            {
                equirectangularMipResources[buildIndex] =
                    device->createBindGroup<
                        WebgpuMaterialsTransmissionEquirectangularMipResources>(
                            equirectangularSource->createView(),
                            equirectangularSource->createView(),
                            equirectangularMipTextures[mipLevel]
                                ->createView(),
                            equirectangularMipUniformBuffers[buildIndex]);
            }
            else
            {
                equirectangularMipResources[buildIndex] =
                    device->createBindGroup<
                        WebgpuMaterialsTransmissionEquirectangularMipResources>(
                            equirectangularSource->createView(),
                            equirectangularMipTextures[mipLevel - 1u]
                                ->createView(),
                            equirectangularMipTextures[mipLevel]
                                ->createView(),
                            equirectangularMipUniformBuffers[buildIndex]);
            }
            equirectangularMipBuilds[buildIndex] =
                device->createComputeClass<
                    WebgpuMaterialsTransmissionEquirectangularMipBuild>(
                        equirectangularMipResources[buildIndex]);
        }
        auto pmremSourceResources = device->createBindGroup<
            WebgpuMaterialsTransmissionPmremSourceResources>(
                equirectangularSource->createView(),
                environmentSampler,
                pmremAtlas->createView());
        auto pmremSourcePass = device->createComputeClass<
            WebgpuMaterialsTransmissionPmremSourcePass>(
                pmremSourceResources);
        graphicsQueue
            ->computePass(
                "WebgpuMaterialsTransmissionPmremLevelZero",
                pmremSourcePass(
                    WebgpuMaterialsDisplacementmapPmremAtlasWidth,
                    WebgpuMaterialsDisplacementmapPmremCubeSize * 2u,
                    1u))
            ->submit();
        for (uint lodIndex = 1u;
             lodIndex < WebgpuMaterialsDisplacementmapPmremLodCount;
             ++lodIndex)
        {
            const uint mipExponent = lodIndex <= 5u
                ? 9u - lodIndex
                : 4u;
            const uint faceSize = 1u << mipExponent;
            const uint outputX = lodIndex > 5u
                ? 3u * faceSize * (lodIndex - 5u)
                : 0u;
            const uint outputY = 4u *
                (WebgpuMaterialsDisplacementmapPmremCubeSize - faceSize);
            WebgpuMaterialsDisplacementmapPmremUniforms pmremUniforms;
            pmremUniforms.outputOffsetSize = uint4(
                outputX,
                outputY,
                faceSize * 3u,
                faceSize * 2u);
            pmremUniforms.roughnessSourceMipAndReserved = float4(
                float(lodIndex) /
                    float(WebgpuMaterialsDisplacementmapPmremLodCount - 1u),
                10.0f - float(lodIndex),
                0.0f,
                0.0f);
            auto filterResources = device->createBindGroup<
                WebgpuMaterialsDisplacementmapPmremFilterResources>(
                    pmremUniformBuffers[lodIndex - 1u],
                    pmremAtlas->createView(),
                    pmremSampler,
                    pmremPingPongAtlas->createView());
            auto filterPass = device->createComputeClass<
                WebgpuMaterialsDisplacementmapPmremFilterPass>(
                    filterResources);
            auto copyResources = device->createBindGroup<
                WebgpuMaterialsDisplacementmapPmremCopyResources>(
                    pmremUniformBuffers[lodIndex - 1u],
                    pmremPingPongAtlas->createView(),
                    pmremAtlas->createView());
            auto copyPass = device->createComputeClass<
                WebgpuMaterialsDisplacementmapPmremCopyPass>(
                    copyResources);
            graphicsQueue
                ->writeBuffer(
                    BufferRange(pmremUniformBuffers[lodIndex - 1u]),
                    &pmremUniforms,
                    sizeof(pmremUniforms))
                ->computePass(
                    "WebgpuMaterialsTransmissionPmremFilter",
                    filterPass(faceSize * 3u, faceSize * 2u, 1u))
                ->computePass(
                    "WebgpuMaterialsTransmissionPmremCopy",
                    copyPass(faceSize * 3u, faceSize * 2u, 1u));
        }
        graphicsQueue->submit();
        backgroundResources = device->createBindGroup<
            WebgpuMaterialsTransmissionBackgroundResources>(
                equirectangularSource->createView(),
                environmentSampler,
                uniformBuffer);
        transmissionCaptureResources = device->createBindGroup<
            WebgpuMaterialsTransmissionScreenCaptureResources>(
                sceneColor->createView(),
                transmissionSampler);
        for (uint mipLevel = 1u;
             mipLevel < WebgpuMaterialsTransmissionScreenMipCount;
             ++mipLevel)
        {
            transmissionMipResources[mipLevel - 1u] =
                device->createBindGroup<
                    WebgpuMaterialsTransmissionScreenMipResources>(
                        transmissionMipTextures[mipLevel - 1u]
                            ->createView(),
                        transmissionSampler);
            transmissionMipPasses[mipLevel - 1u] =
                device->createRenderClass<
                    WebgpuMaterialsTransmissionScreenMipPass>(
                        transmissionMipResources[mipLevel - 1u]);
        }
        sceneResources =
            device->createBindGroup<WebgpuMaterialsTransmissionSceneResources>(
                uniformBuffer,
                pmremAtlas->createView(),
                dfgLutTexture->createView(),
                pmremSampler,
                dfgSampler,
                transmissionMipTextures[0u]->createView(),
                transmissionMipTextures[1u]->createView(),
                transmissionMipTextures[2u]->createView(),
                transmissionMipTextures[3u]->createView(),
                transmissionMipTextures[4u]->createView(),
                transmissionMipTextures[5u]->createView(),
                transmissionMipTextures[6u]->createView(),
                transmissionSampler,
                cubeAtlas->createView(),
                equirectangularSource->createView(),
                environmentSampler,
                positiveX->createView(),
                negativeX->createView(),
                positiveY->createView(),
                negativeY->createView(),
                positiveZ->createView(),
                negativeZ->createView(),
                alphaMap->createView(),
                alphaSampler);
        outputResources =
            device->createBindGroup<WebgpuMaterialsTransmissionOutputResources>(
                sceneColor->createView(),
                outputSampler,
                uniformBuffer);
        atlasBuild =
            device->createComputeClass<
                WebgpuMaterialsTransmissionCubeAtlasGutterBuild>(
                    atlasResources);
        backgroundPass =
            device->createRenderClass<
                WebgpuMaterialsTransmissionEnvironmentBackgroundPass>(
                    backgroundResources);
        transmissionCapturePass =
            device->createRenderClass<
                WebgpuMaterialsTransmissionScreenCapturePass>(
                    transmissionCaptureResources);
        backPass =
            device->createRenderClass<
                WebgpuMaterialsTransmissionBackPass>(
                    sceneResources);
        frontPass =
            device->createRenderClass<
                WebgpuMaterialsTransmissionFrontPass>(
                    sceneResources);
        outputPass =
            device->createRenderClass<
                WebgpuMaterialsTransmissionOutputToneMapPass>(
                    outputResources);
        graphicsQueue
            ->computePass(
                "WebgpuMaterialsTransmissionCubeAtlasGutterBuild",
                atlasBuild(
                    WebgpuMaterialsTransmissionAtlasWidth,
                    WebgpuMaterialsTransmissionAtlasHeight,
                    1u))
            ->submit();
        for (uint mipLevel = 0u;
             mipLevel < WebgpuMaterialsTransmissionEquirectangularMipCount;
             ++mipLevel)
        {
            const uint buildIndex = mipLevel;
            const uint mipWidth = max(
                1u,
                WebgpuMaterialsTransmissionEquirectangularWidth >> mipLevel);
            const uint mipHeight = max(
                1u,
                WebgpuMaterialsTransmissionEquirectangularHeight >> mipLevel);
            graphicsQueue
                ->computePass(
                    "WebgpuMaterialsTransmissionEquirectangularMipBuild",
                    equirectangularMipBuilds[buildIndex](
                        mipWidth,
                        mipHeight,
                        1u))
                ->submit();
        }
    }

    /** Draws the environment, the one sphere, output transfer, and presentation. */
    void render() override
    {
        WebgpuMaterialsTransmissionLinearFrameBuffer transmissionFrameBuffer;
        transmissionFrameBuffer.color =
            transmissionMipTextures[0u]->createView();
        transmissionFrameBuffer.color.loadOp = LoadOp::Clear;
        transmissionFrameBuffer.color.storeOp = StoreOp::Store;
        transmissionFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        graphicsQueue
            ->renderPass(
                "WebgpuMaterialsTransmissionBackdropLevelZero",
                transmissionFrameBuffer,
                backgroundPass->setVertexBuffer(backgroundVertexBuffer),
                backgroundPass->setIndexBuffer(backgroundIndexBuffer),
                backgroundPass(3u, 1u, 0u, 0u))
            ->submit();
        for (uint mipLevel = 1u;
             mipLevel < WebgpuMaterialsTransmissionScreenMipCount;
             ++mipLevel)
        {
            WebgpuMaterialsTransmissionLinearFrameBuffer mipFrameBuffer;
            mipFrameBuffer.color =
                transmissionMipTextures[mipLevel]->createView();
            mipFrameBuffer.color.loadOp = LoadOp::Clear;
            mipFrameBuffer.color.storeOp = StoreOp::Store;
            mipFrameBuffer.color.clearValue =
                {0.0f, 0.0f, 0.0f, 1.0f};
            graphicsQueue
                ->renderPass(
                    "WebgpuMaterialsTransmissionBackdropMip",
                    mipFrameBuffer,
                    transmissionMipPasses[mipLevel - 1u](
                        3u, 1u, 0u, 0u))
                ->submit();
        }
        WebgpuMaterialsTransmissionLinearFrameBuffer backgroundFrameBuffer;
        backgroundFrameBuffer.color = sceneColor->createView();
        backgroundFrameBuffer.color.loadOp = LoadOp::Clear;
        backgroundFrameBuffer.color.storeOp = StoreOp::Store;
        backgroundFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuMaterialsTransmissionSceneFrameBuffer sphereFrameBuffer;
        sphereFrameBuffer.color = sceneColor->createView();
        sphereFrameBuffer.color.loadOp = LoadOp::Load;
        sphereFrameBuffer.color.storeOp = StoreOp::Store;
        sphereFrameBuffer.depth = sceneDepth->createView();
        sphereFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        sphereFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sphereFrameBuffer.depth.depthClearValue = 1.0f;
        WebgpuMaterialsTransmissionLinearFrameBuffer backCaptureFrame;
        backCaptureFrame.color = transmissionMipTextures[0u]->createView();
        backCaptureFrame.color.loadOp = LoadOp::Clear;
        backCaptureFrame.color.storeOp = StoreOp::Store;
        backCaptureFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuMaterialsTransmissionLinearFrameBuffer backMip1Frame;
        backMip1Frame.color = transmissionMipTextures[1u]->createView();
        backMip1Frame.color.loadOp = LoadOp::Clear;
        backMip1Frame.color.storeOp = StoreOp::Store;
        backMip1Frame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuMaterialsTransmissionLinearFrameBuffer backMip2Frame;
        backMip2Frame.color = transmissionMipTextures[2u]->createView();
        backMip2Frame.color.loadOp = LoadOp::Clear;
        backMip2Frame.color.storeOp = StoreOp::Store;
        backMip2Frame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuMaterialsTransmissionLinearFrameBuffer backMip3Frame;
        backMip3Frame.color = transmissionMipTextures[3u]->createView();
        backMip3Frame.color.loadOp = LoadOp::Clear;
        backMip3Frame.color.storeOp = StoreOp::Store;
        backMip3Frame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuMaterialsTransmissionLinearFrameBuffer backMip4Frame;
        backMip4Frame.color = transmissionMipTextures[4u]->createView();
        backMip4Frame.color.loadOp = LoadOp::Clear;
        backMip4Frame.color.storeOp = StoreOp::Store;
        backMip4Frame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuMaterialsTransmissionLinearFrameBuffer backMip5Frame;
        backMip5Frame.color = transmissionMipTextures[5u]->createView();
        backMip5Frame.color.loadOp = LoadOp::Clear;
        backMip5Frame.color.storeOp = StoreOp::Store;
        backMip5Frame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuMaterialsTransmissionLinearFrameBuffer backMip6Frame;
        backMip6Frame.color = transmissionMipTextures[6u]->createView();
        backMip6Frame.color.loadOp = LoadOp::Clear;
        backMip6Frame.color.storeOp = StoreOp::Store;
        backMip6Frame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuMaterialsTransmissionOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputColor->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuMaterialsTransmissionEnvironmentBackground",
                backgroundFrameBuffer,
                backgroundPass->setVertexBuffer(backgroundVertexBuffer),
                backgroundPass->setIndexBuffer(backgroundIndexBuffer),
                backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionBackCapture",
                backCaptureFrame,
                transmissionCapturePass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionBackMip1",
                backMip1Frame,
                transmissionMipPasses[0u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionBackMip2",
                backMip2Frame,
                transmissionMipPasses[1u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionBackMip3",
                backMip3Frame,
                transmissionMipPasses[2u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionBackMip4",
                backMip4Frame,
                transmissionMipPasses[3u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionBackMip5",
                backMip5Frame,
                transmissionMipPasses[4u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionBackMip6",
                backMip6Frame,
                transmissionMipPasses[5u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionBack",
                sphereFrameBuffer,
                backPass->setVertexBuffer(vertexBuffer),
                backPass->setIndexBuffer(indexBuffer),
                backPass(indexCount, 1u, 0u, 0, 0u))
            // r185 uses a separate viewport texture for the front-side
            // transmission node. Refresh it after the back-side pass so the
            // front pass observes the same render-order contents.
            ->renderPass(
                "WebgpuMaterialsTransmissionFrontCapture",
                backCaptureFrame,
                transmissionCapturePass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionFrontMip1",
                backMip1Frame,
                transmissionMipPasses[0u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionFrontMip2",
                backMip2Frame,
                transmissionMipPasses[1u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionFrontMip3",
                backMip3Frame,
                transmissionMipPasses[2u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionFrontMip4",
                backMip4Frame,
                transmissionMipPasses[3u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionFrontMip5",
                backMip5Frame,
                transmissionMipPasses[4u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionFrontMip6",
                backMip6Frame,
                transmissionMipPasses[5u](3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionFront",
                sphereFrameBuffer,
                frontPass->setVertexBuffer(vertexBuffer),
                frontPass->setIndexBuffer(indexBuffer),
                frontPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass(
                "WebgpuMaterialsTransmissionOutputToneMap",
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
        device->freeBuffer(backgroundVertexBuffer);
        device->freeBuffer(backgroundIndexBuffer);
        device->freeBuffer(uniformBuffer);
        for (uint mipLevel = 0u;
             mipLevel < WebgpuMaterialsTransmissionEquirectangularMipCount;
             ++mipLevel)
        {
            device->freeBuffer(
                equirectangularMipUniformBuffers[mipLevel]);
        }
        device->freeTexture(positiveX);
        device->freeTexture(negativeX);
        device->freeTexture(positiveY);
        device->freeTexture(negativeY);
        device->freeTexture(positiveZ);
        device->freeTexture(negativeZ);
        device->freeTexture(alphaMap);
        device->freeTexture(equirectangularSource);
        for (uint mipLevel = 0u;
             mipLevel < WebgpuMaterialsTransmissionEquirectangularMipCount;
             ++mipLevel)
        {
            device->freeTexture(
                equirectangularMipTextures[mipLevel]);
        }
        device->freeTexture(cubeAtlas);
        device->freeTexture(pmremAtlas);
        device->freeTexture(pmremPingPongAtlas);
        device->freeTexture(dfgLutTexture);
        for (uint mipLevel = 0u;
             mipLevel < WebgpuMaterialsTransmissionScreenMipCount;
             ++mipLevel)
        {
            device->freeTexture(transmissionMipTextures[mipLevel]);
        }
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};
