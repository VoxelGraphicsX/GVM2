#ifndef GVM_THREE_PHASE1_VOLUME_PERLIN_SHARED_HPP
#define GVM_THREE_PHASE1_VOLUME_PERLIN_SHARED_HPP

#include "UGL.h"

using namespace UGL;

static const uint Phase1VolumePerlinMaxTextures = 8u;

/** Stores one BoxGeometry position in the volume Scene attribute union. */
struct Phase1VolumePerlinVertex
{
    float4 position [[Attribute0]];
};

/** Stores camera-independent data for the only volume entity. */
struct Phase1VolumePerlinObjectData
{
    float4 value;
};

/** Stores the mandatory record for the only non-instanced volume entity. */
struct Phase1VolumePerlinInstanceData
{
    float4 value;
};

/** Stores the mandatory record for the volume raymarch material. */
struct Phase1VolumePerlinMaterialData
{
    float4 value;
};

/** Stores the exact camera basis, threshold, step count, and atlas dimensions. */
struct Phase1VolumePerlinFrameData
{
    float4 cameraPosition;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraForward;
    float4 raymarch;
    float4 viewportAndOverlay;
};

/** Carries object-space ray inputs and entity identity into the volume fragment stage. */
struct Phase1VolumePerlinVertexOutput
{
    float4 position [[Position]];
    float3 objectPosition [[Attribute0]];
    float3 cameraPosition [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Stores the RGBA8 result of a volume or screen pass. */
struct Phase1VolumePerlinFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Encodes one linear channel with the Three r185 sRGB output transfer. */
float phase1VolumeLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : 1.055f * pow(clamped, 0.4166666667f) - 0.055f;
}

/** Returns the normalized atlas coordinate for one guttered 128-square slice. */
float2 phase1VolumeAtlasCoordinate(
    float2 xy,
    float slice)
{
    const float clampedSlice = clamp(slice, 0.0f, 127.0f);
    const float column = clampedSlice - floor(clampedSlice / 16.0f) * 16.0f;
    const float row = floor(clampedSlice / 16.0f);
    const float2 atlasPixel =
        float2(column * 130.0f, row * 130.0f) +
        float2(1.0f) +
        clamp(xy, float2(0.0f), float2(1.0f)) * 128.0f;
    return atlasPixel / float2(2080.0f, 1040.0f);
}

/** Stores the two atlas coordinates and interpolation weight for one 3D lookup. */
struct Phase1VolumeAtlasLookup
{
    float2 lowerCoordinate;
    float2 upperCoordinate;
    float fraction;
};

/** Resolves one normalized 3D coordinate into a resource-free atlas lookup. */
Phase1VolumeAtlasLookup phase1VolumeResolveAtlasLookup(
    float3 coordinate)
{
    const float zCoordinate =
        clamp(coordinate.z * 128.0f - 0.5f, 0.0f, 127.0f);
    const float lowerSlice = floor(zCoordinate);
    Phase1VolumeAtlasLookup lookup;
    lookup.lowerCoordinate =
        phase1VolumeAtlasCoordinate(
            coordinate.xy,
            lowerSlice);
    lookup.upperCoordinate =
        phase1VolumeAtlasCoordinate(
            coordinate.xy,
            min(lowerSlice + 1.0f, 127.0f));
    lookup.fraction = zCoordinate - lowerSlice;
    return lookup;
}

/** Intersects one normalized ray with the canonical unit BoxGeometry bounds. */
float2 phase1VolumeHitBox(float3 origin, float3 direction)
{
    const float3 inverseDirection = float3(1.0f) / direction;
    const float3 minimumCandidate =
        (float3(-0.5f) - origin) * inverseDirection;
    const float3 maximumCandidate =
        (float3(0.5f) - origin) * inverseDirection;
    const float3 minimumValues =
        min(minimumCandidate, maximumCandidate);
    const float3 maximumValues =
        max(minimumCandidate, maximumCandidate);
    return float2(
        max(minimumValues.x, max(minimumValues.y, minimumValues.z)),
        min(maximumValues.x, min(maximumValues.y, maximumValues.z)));
}

#endif
