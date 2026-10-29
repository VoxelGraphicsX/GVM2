#ifndef GVM_THREE_PHASE1_SKY_SHARED_HPP
#define GVM_THREE_PHASE1_SKY_SHARED_HPP

#include "UGL.h"

using namespace UGL;

/** Stores clip-space geometry shared by the dedicated WebGL and WebGPU sky scenes. */
struct Phase1SkyVertex
{
    float4 position [[Attribute0]];
};

/** Stores the material phase selected for one sky-scene entity. */
struct Phase1SkyObjectData
{
    float4 phase;
};

/** Stores the mandatory per-instance record for one non-instanced sky entity. */
struct Phase1SkyInstanceData
{
    float4 value;
};

/** Stores the mandatory per-material record for one sky-scene entity. */
struct Phase1SkyMaterialData
{
    float4 value;
};

/** Stores atmospheric controls, sun direction, camera basis, viewport, and time. */
struct Phase1SkyFrameData
{
    float4 atmosphere;
    float4 clouds;
    float4 sunAndExposure;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraForward;
    float4 viewportAndTime;
};

/** Carries clip-space coordinates and entity metadata into the sky fragment stage. */
struct Phase1SkyVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Encodes one linear working-space channel with the Three.js sRGB output transfer. */
float phase1SkyLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : 1.055f * pow(clamped, 0.4166666667f) - 0.055f;
}

/** Evaluates the deterministic scalar hash used by the r185 sky cloud shader. */
float phase1SkyHash(float2 coordinate)
{
    return frac(
        sin(dot(coordinate, float2(127.1f, 311.7f))) *
        43758.5453123f);
}

/** Evaluates the smooth two-dimensional value noise used by the r185 sky cloud shader. */
float phase1SkyNoise(float2 coordinate)
{
    const float2 integerPoint = floor(coordinate);
    float2 fraction = frac(coordinate);
    fraction = fraction * fraction * (float2(3.0f) - 2.0f * fraction);
    const float a = phase1SkyHash(integerPoint);
    const float b = phase1SkyHash(integerPoint + float2(1.0f, 0.0f));
    const float c = phase1SkyHash(integerPoint + float2(0.0f, 1.0f));
    const float d = phase1SkyHash(integerPoint + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, fraction.x), lerp(c, d, fraction.x), fraction.y);
}

/** Evaluates the five-octave fractional Brownian motion used by the r185 sky cloud shader. */
float phase1SkyFbm(float2 coordinate)
{
    float value = 0.0f;
    float amplitude = 0.5f;
    for (uint octave = 0u; octave < 5u; ++octave)
    {
        value += amplitude * phase1SkyNoise(coordinate);
        coordinate *= 2.0f;
        amplitude *= 0.5f;
    }
    return value;
}

/** Applies the exact r185 ACES fitted operator before output color-space conversion. */
float3 phase1SkyAces(float3 color, float exposure)
{
    color *= exposure / 0.6f;
    const float3 acesInput = float3(
        0.59719f * color.x + 0.35458f * color.y + 0.04823f * color.z,
        0.07600f * color.x + 0.90834f * color.y + 0.01566f * color.z,
        0.02840f * color.x + 0.13383f * color.y + 0.83777f * color.z);
    const float3 fitA =
        acesInput * (acesInput + float3(0.0245786f)) -
        float3(0.000090537f);
    const float3 fitB =
        acesInput * (0.983729f * acesInput + float3(0.4329510f)) +
        float3(0.238081f);
    const float3 fitted = fitA / fitB;
    const float3 acesOutput = float3(
        1.60475f * fitted.x - 0.53108f * fitted.y - 0.07367f * fitted.z,
        -0.10208f * fitted.x + 1.10813f * fitted.y - 0.00605f * fitted.z,
        -0.00327f * fitted.x - 0.07276f * fitted.y + 1.07602f * fitted.z);
    return clamp(acesOutput, float3(0.0f), float3(1.0f));
}

/** Reconstructs the perspective-camera world ray for one normalized screen coordinate. */
float3 phase1SkyCameraRay(
    float2 uv,
    float4 cameraRight,
    float4 cameraUp,
    float4 cameraForward,
    float4 viewportAndTime)
{
    const float2 ndc =
        float2(
            uv.x * 2.0f - 1.0f,
            1.0f - uv.y * 2.0f);
    const float tangentHalfFov = 0.5773502691896258f;
    const float aspect =
        viewportAndTime.x / viewportAndTime.y;
    return normalize(
        float3(cameraForward.xyz) +
        float3(cameraRight.xyz) *
            (ndc.x * tangentHalfFov * aspect) +
        float3(cameraUp.xyz) * (ndc.y * tangentHalfFov));
}

/** Evaluates the r185 Preetham/Rayleigh/Mie atmosphere and five-octave cloud model. */
float3 phase1SkyAtmosphere(
    float3 direction,
    float4 atmosphere,
    float4 clouds,
    float4 sunAndDisc,
    float4 viewportAndTime)
{
    const float pi = 3.14159265358979323846f;
    const float3 sunDirection =
        normalize(float3(sunAndDisc.xyz));
    const float sunZenithCosine = clamp(sunDirection.y, -1.0f, 1.0f);
    const float sunEnergy =
        1000.0f * max(
            0.0f,
            1.0f -
                pow(
                    2.71828182845904523536f,
                    -((1.6110731556870734f - acos(sunZenithCosine)) /
                      1.5f)));
    const float sunFade =
        1.0f -
        clamp(1.0f - exp(sunDirection.y), 0.0f, 1.0f);
    const float rayleighCoefficient =
        atmosphere.y - (1.0f - sunFade);
    const float3 betaR =
        float3(
            5.804542996261093e-6f,
            1.3562911419845635e-5f,
            3.0265902468824876e-5f) *
        rayleighCoefficient;
    const float mieC = 0.2f * atmosphere.x * 1.0e-17f;
    const float3 betaM =
        0.434f * mieC *
        float3(
            1.8399918514433978e14f,
            2.7798023919660528e14f,
            4.0790479543861094e14f) *
        atmosphere.z;

    const float zenithAngle = acos(max(0.0f, direction.y));
    const float inverseOpticalLength =
        1.0f /
        (cos(zenithAngle) +
         0.15f *
             pow(
                 93.885f - zenithAngle * 180.0f / pi,
                 -1.253f));
    const float3 extinction =
        exp(
            -(betaR * (8400.0f * inverseOpticalLength) +
              betaM * (1250.0f * inverseOpticalLength)));
    const float cosineTheta = dot(direction, sunDirection);
    const float rayleighPhase =
        0.05968310365946075f *
        (1.0f + pow(cosineTheta * 0.5f + 0.5f, 2.0f));
    const float g = atmosphere.w;
    const float gSquared = g * g;
    const float miePhase =
        0.07957747154594767f *
        (1.0f - gSquared) /
        pow(
            1.0f - 2.0f * g * cosineTheta + gSquared,
            1.5f);
    const float3 scatteringRatio =
        (betaR * rayleighPhase + betaM * miePhase) /
        (betaR + betaM);
    float3 inScatter =
        pow(
            sunEnergy * scatteringRatio *
                (float3(1.0f) - extinction),
            float3(1.5f));
    inScatter *= lerp(
        float3(1.0f),
        pow(
            sunEnergy * scatteringRatio * extinction,
            float3(0.5f)),
        clamp(
            pow(1.0f - sunDirection.y, 5.0f),
            0.0f,
            1.0f));
    float3 night = 0.1f * extinction;
    const float sunDisc =
        smoothstep(
            0.9999566769464484f,
            0.9999766769464484f,
            cosineTheta) *
        sunAndDisc.w;
    night += sunEnergy * 19000.0f * extinction * sunDisc;
    float3 color =
        (inScatter + night) * 0.04f +
        float3(0.0f, 0.0003f, 0.00075f);

    if (direction.y > 0.0f && clouds.x > 0.0f)
    {
        const float elevation = lerp(1.0f, 0.1f, clouds.z);
        float2 cloudUv =
            float2(direction.xz) /
            (direction.y * elevation);
        cloudUv *= 0.0002f;
        cloudUv +=
            viewportAndTime.z * 0.0001f;
        float cloudNoise =
            phase1SkyFbm(cloudUv * 1000.0f) +
            0.5f *
                phase1SkyFbm(cloudUv * 2000.0f + float2(3.7f));
        cloudNoise = cloudNoise * 0.5f + 0.5f;
        float cloudMask =
            smoothstep(
                1.0f - clouds.x,
                1.3f - clouds.x,
                cloudNoise);
        cloudMask *=
            smoothstep(
                0.0f,
                0.1f + 0.2f * clouds.z,
                direction.y);
        const float sunInfluence =
            dot(direction, sunDirection) * 0.5f + 0.5f;
        const float daylight =
            max(0.0f, sunDirection.y * 2.0f);
        const float3 atmosphereColor = inScatter * 0.04f;
        float3 cloudColor =
            lerp(float3(0.3f), float3(1.0f), daylight);
        cloudColor =
            lerp(
                cloudColor,
                atmosphereColor + float3(1.0f),
                sunInfluence * 0.5f);
        cloudColor *= sunEnergy * 0.00002f;
        color =
            lerp(
                color,
                cloudColor,
                cloudMask * clouds.y);
    }
    return color;
}

/** Converts the linear sky result through r185 ACES and sRGB output stages. */
float3 phase1SkyOutput(float3 linearColor, float exposure)
{
    const float3 mapped = phase1SkyAces(linearColor, exposure);
    return float3(
        phase1SkyLinearToSrgb(mapped.x),
        phase1SkyLinearToSrgb(mapped.y),
        phase1SkyLinearToSrgb(mapped.z));
}

#endif
