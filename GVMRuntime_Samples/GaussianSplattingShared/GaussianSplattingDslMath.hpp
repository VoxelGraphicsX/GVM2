#pragma once

/**
 * Returns the linear background color stored in shared 3DGS frame globals.
 *
 * Include this header inside namespace GsViewer after ViewerGlobals has been declared.
 */
inline float3 backgroundColor(const ViewerGlobals &globals)
{
    return globals.backgroundAndScale.xyz;
}

/**
 * Reads one scalar component from a packed float4 value.
 *
 * Component indices above 2 return the w component to match existing sample shader behavior.
 */
inline float packedFloat4Component(float4 value, uint componentIndex)
{
    if (componentIndex == 0u)
    {
        return value.x;
    }
    if (componentIndex == 1u)
    {
        return value.y;
    }
    if (componentIndex == 2u)
    {
        return value.z;
    }
    return value.w;
}

/**
 * Reads one SH rest coefficient from the packed 12-float4 GPU layout.
 *
 * Out-of-range coefficient indices return zero so callers can safely clamp higher SH degrees.
 */
inline float packedShRestCoefficient(const GaussianShRestGPU &shRest, uint coefficientIndex)
{
    if (coefficientIndex < 4u)
        return packedFloat4Component(shRest.shCoeff0, coefficientIndex);
    if (coefficientIndex < 8u)
        return packedFloat4Component(shRest.shCoeff1, coefficientIndex - 4u);
    if (coefficientIndex < 12u)
        return packedFloat4Component(shRest.shCoeff2, coefficientIndex - 8u);
    if (coefficientIndex < 16u)
        return packedFloat4Component(shRest.shCoeff3, coefficientIndex - 12u);
    if (coefficientIndex < 20u)
        return packedFloat4Component(shRest.shCoeff4, coefficientIndex - 16u);
    if (coefficientIndex < 24u)
        return packedFloat4Component(shRest.shCoeff5, coefficientIndex - 20u);
    if (coefficientIndex < 28u)
        return packedFloat4Component(shRest.shCoeff6, coefficientIndex - 24u);
    if (coefficientIndex < 32u)
        return packedFloat4Component(shRest.shCoeff7, coefficientIndex - 28u);
    if (coefficientIndex < 36u)
        return packedFloat4Component(shRest.shCoeff8, coefficientIndex - 32u);
    if (coefficientIndex < 40u)
        return packedFloat4Component(shRest.shCoeff9, coefficientIndex - 36u);
    if (coefficientIndex < 44u)
        return packedFloat4Component(shRest.shCoeff10, coefficientIndex - 40u);
    if (coefficientIndex < 48u)
        return packedFloat4Component(shRest.shCoeff11, coefficientIndex - 44u);
    return 0.0f;
}

/**
 * Reads one channel-major SH rest coefficient for a Graphdeco-style PLY payload.
 *
 * The persisted layout is r[1..15], g[1..15], b[1..15]; invalid channel or basis indices return zero.
 */
inline float shRestChannelCoefficient(const GaussianShRestGPU &shRest, uint channelIndex, uint restTermIndex)
{
    if (channelIndex >= 3u || restTermIndex >= 15u)
    {
        return 0.0f;
    }

    const uint packedIndex = channelIndex * 15u + restTermIndex;
    return packedShRestCoefficient(shRest, packedIndex);
}

/**
 * Returns the RGB triplet for one SH rest basis term.
 *
 * This keeps channel-major storage hidden from projection and raster paths.
 */
inline float3 shRestTriplet(const GaussianShRestGPU &shRest, uint restTermIndex)
{
    return float3(
        shRestChannelCoefficient(shRest, 0u, restTermIndex),
        shRestChannelCoefficient(shRest, 1u, restTermIndex),
        shRestChannelCoefficient(shRest, 2u, restTermIndex));
}

/**
 * Selects the SH degree used for one projected Gaussian based on screen-space support.
 *
 * The caller must provide the scene maximum degree and the already computed support radius in pixels.
 */
inline uint chooseShLodDegree(uint maxSceneDegree, float supportRadiusPx)
{
    const uint clampedMaxDegree = min(maxSceneDegree, ShMaxDegree);
    if (clampedMaxDegree == 0u)
    {
        return 0u;
    }
    if (supportRadiusPx <= ShLodDegree0MaxRadiusPx)
    {
        return 0u;
    }
    if (clampedMaxDegree == 1u || supportRadiusPx <= ShLodDegree1MaxRadiusPx)
    {
        return min(clampedMaxDegree, 1u);
    }
    if (clampedMaxDegree == 2u || supportRadiusPx <= ShLodDegree2MaxRadiusPx)
    {
        return min(clampedMaxDegree, 2u);
    }
    return clampedMaxDegree;
}

/**
 * Evaluates only the clamped DC SH color for one Gaussian.
 *
 * Use this when the selected SH degree is zero or when a CPU/GPU proxy stores only DC color.
 */
inline float3 evaluateShColorDc(const GaussianSplatHotGPU &splat)
{
    return max(float3(0.5f) + splat.shDc.xyz * ShC0, float3(0.0f));
}

/**
 * Evaluates only the unclamped DC SH color for one Gaussian.
 *
 * Use this as the base term for higher-order SH evaluation before the final non-negative clamp.
 */
inline float3 evaluateShColorDcUnclamped(const GaussianSplatHotGPU &splat)
{
    return float3(0.5f) + splat.shDc.xyz * ShC0;
}

/**
 * Evaluates Graphdeco SH color up to the requested degree for one view direction.
 *
 * shDegree must be no larger than ShMaxDegree; callers normally pass chooseShLodDegree output.
 */
inline float3 evaluateShColor(const GaussianSplatHotGPU &splat, const GaussianShRestGPU &shRest, float3 viewDirection, uint shDegree)
{
    float3 color = evaluateShColorDcUnclamped(splat);
    if (shDegree == 0u)
    {
        return max(color, float3(0.0f));
    }

    const float x = viewDirection.x;
    const float y = viewDirection.y;
    const float z = viewDirection.z;
    color = color
        - ShC1 * y * shRestTriplet(shRest, 0u)
        + ShC1 * z * shRestTriplet(shRest, 1u)
        - ShC1 * x * shRestTriplet(shRest, 2u);
    if (shDegree == 1u)
    {
        return max(color, float3(0.0f));
    }

    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float yz = y * z;
    const float xz = x * z;

    color = color
        + ShC2_0 * xy * shRestTriplet(shRest, 3u)
        + ShC2_1 * yz * shRestTriplet(shRest, 4u)
        + ShC2_2 * (2.0f * zz - xx - yy) * shRestTriplet(shRest, 5u)
        + ShC2_3 * xz * shRestTriplet(shRest, 6u)
        + ShC2_4 * (xx - yy) * shRestTriplet(shRest, 7u);
    if (shDegree == 2u)
    {
        return max(color, float3(0.0f));
    }

    color = color
        + ShC3_0 * y * (3.0f * xx - yy) * shRestTriplet(shRest, 8u)
        + ShC3_1 * xy * z * shRestTriplet(shRest, 9u)
        + ShC3_2 * y * (4.0f * zz - xx - yy) * shRestTriplet(shRest, 10u)
        + ShC3_3 * z * (2.0f * zz - 3.0f * xx - 3.0f * yy) * shRestTriplet(shRest, 11u)
        + ShC3_4 * x * (4.0f * zz - xx - yy) * shRestTriplet(shRest, 12u)
        + ShC3_5 * z * (xx - yy) * shRestTriplet(shRest, 13u)
        + ShC3_6 * x * (xx - 3.0f * yy) * shRestTriplet(shRest, 14u);

    return max(color, float3(0.0f));
}

/**
 * Converts one normalized-device coordinate to the sample's pixel-center convention.
 *
 * The returned coordinate is continuous and is not clamped to the render target.
 */
inline float ndcToPixel(float ndcCoordinate, uint resolution)
{
    return ((ndcCoordinate + 1.0f) * float(resolution) - 1.0f) * 0.5f;
}

/**
 * Projects one clip-space position to continuous pixel coordinates.
 *
 * The function clamps the reciprocal w denominator to preserve existing near-plane robustness.
 */
inline float2 projectClipToPixel(float4 clip, const ViewerGlobals &globals)
{
    const float inverseClipW = 1.0f / max(clip.w, 0.00001f);
    const float2 projectedNdc = clip.xy * inverseClipW;
    return float2(ndcToPixel(projectedNdc.x, globals.imageInfo.x), ndcToPixel(projectedNdc.y, globals.imageInfo.y));
}

/**
 * Projects one camera-space position to continuous pixel coordinates.
 *
 * The camera projection matrix must match the current ViewerGlobals render target dimensions.
 */
inline float2 projectCameraToPixel(float3 cameraPosition, const Camera &camera, const ViewerGlobals &globals)
{
    return projectClipToPixel(mul(camera.proj, float4(cameraPosition, 1.0f)), globals);
}

/**
 * Projects one camera-space axis vector into a local pixel-space delta.
 *
 * The caller supplies focal lengths already expressed in pixel units.
 */
inline float2 projectCameraAxisToPixelDelta(float3 cameraPosition, float3 cameraAxis, float focalX, float focalY)
{
    const float inverseZ = 1.0f / max(cameraPosition.z, 0.00001f);
    const float inverseZSquared = inverseZ * inverseZ;
    return float2(focalX * (cameraAxis.x * inverseZ - cameraPosition.x * cameraAxis.z * inverseZSquared), focalY * (cameraAxis.y * inverseZ - cameraPosition.y * cameraAxis.z * inverseZSquared));
}

/**
 * Returns the maximum eigenvalue of a symmetric 2x2 covariance matrix.
 *
 * Negative numerical residue is clamped away because the covariance is expected to be positive semidefinite.
 */
inline float maxEigenvalueSymmetric2x2(float xx, float xy, float yy)
{
    const float trace = xx + yy;
    const float determinant = xx * yy - xy * xy;
    const float discriminant = max(trace * trace - 4.0f * determinant, 0.0f);
    return max(0.5f * (trace + sqrt(discriminant)), 0.0f);
}

/**
 * Returns the principal eigenvector of a symmetric 2x2 covariance matrix.
 *
 * Degenerate matrices fall back to the axis associated with the larger diagonal entry.
 */
inline float2 principalEigenvectorSymmetric2x2(float xx, float xy, float yy, float eigenvalue)
{
    (void)yy;
    float2 eigenvector = float2(xy, eigenvalue - xx);
    const float lengthSquared = dot(eigenvector, eigenvector);
    if (lengthSquared <= 1.0e-12f)
    {
        return xx >= yy ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f);
    }

    return eigenvector * rsqrt(lengthSquared);
}

/**
 * Computes a standard Gaussian support scale from a logarithmic alpha threshold.
 *
 * This is used by non-Spark splat profiles where opacity is represented as a direct alpha multiplier.
 */
inline float computeGaussianSupportScale(float alphaThresholdPower)
{
    return sqrt(max(-2.0f * alphaThresholdPower, 0.0f));
}

/**
 * Computes a Spark-style support scale from one opacity profile value.
 *
 * Values above one represent shifted profiles and must be evaluated by evaluateSparkOpacity.
 */
inline float computeSparkOpacitySupportScale(float opacityProfileD)
{
    if (opacityProfileD <= MinVisibleAlpha)
    {
        return 0.0f;
    }

    if (opacityProfileD <= 1.0f)
    {
        const float alphaThresholdPower = log(clamp(MinVisibleAlpha / opacityProfileD, 0.000001f, 0.999999f));
        return sqrt(max(-2.0f * alphaThresholdPower, 0.0f));
    }

    return (opacityProfileD - 1.0f) + sqrt(max((-2.0f * log(MinVisibleAlpha)) / opacityProfileD, 0.0f));
}

/**
 * Evaluates one Spark-style opacity profile at a squared local distance.
 *
 * Values above one use the shifted-distance profile while direct alpha profiles use the standard Gaussian falloff.
 */
inline float evaluateSparkOpacity(float opacityProfileD, float localDistanceSquared)
{
    if (opacityProfileD <= MinVisibleAlpha)
    {
        return 0.0f;
    }

    if (opacityProfileD <= 1.0f)
    {
        return min(0.99f, opacityProfileD * exp(-0.5f * localDistanceSquared));
    }

    const float localDistance = sqrt(max(localDistanceSquared, 0.0f));
    const float shiftedDistance = max(localDistance - (opacityProfileD - 1.0f), 0.0f);
    return min(0.99f, exp(-0.5f * opacityProfileD * shiftedDistance * shiftedDistance));
}

/**
 * Computes the conservative support radius for a projected Gaussian in pixels.
 *
 * globals.frameMathInfo.x provides the sample-specific maximum radius clamp.
 */
inline float computeGaussianSupportRadius(float maxEigenvalue, const ViewerGlobals &globals)
{
    if (maxEigenvalue <= 0.0f)
    {
        return 0.0f;
    }

    return min(ceil(3.0f * sqrt(maxEigenvalue)), globals.frameMathInfo.x);
}

/**
 * Packs one camera-space depth into the sortable integer depth key used by 3DGS samples.
 *
 * The bit shift and mask are supplied through ViewerGlobals::sortKeyInfo.
 */
inline uint packDepthKey(float cameraDepth, const ViewerGlobals &globals)
{
    return (asuint(max(cameraDepth, 0.000001f)) >> globals.sortKeyInfo.x) & globals.sortKeyInfo.z;
}

/**
 * Converts a packed depth key into the reverse order required by the graphics billboard pass.
 *
 * The result keeps nearer splats later for back-to-front blending with indirect draw order.
 */
inline uint reversePackedDepthKeyForGraphics(uint packedDepthKey, const ViewerGlobals &globals)
{
    return globals.sortKeyInfo.z - min(packedDepthKey, globals.sortKeyInfo.z);
}

/**
 * Returns the radix bucket for one sort pass.
 *
 * passIndex must be less than Amd8RadixPassCount in the calling sort loop.
 */
inline uint radixBucketForPass(const SortEntry &entry, uint passIndex)
{
    return (entry.sortKey >> (passIndex * Amd8RadixPassBits)) & 0xffu;
}

/**
 * Clamps a raw entry count to the active sort budget stored in ViewerGlobals.
 *
 * This prevents later indirect dispatches from reading beyond the current sort buffers.
 */
inline uint clampActiveEntryCount(const ViewerGlobals &globals, uint rawActiveCount)
{
    return min(rawActiveCount, globals.sceneInfo.y);
}

/**
 * Returns the number of AMD8 radix partitions needed for an active entry count.
 *
 * The result is at least one because the dispatch setup path expects a non-zero partition count.
 */
inline uint computeAmd8PartitionCount(uint activeCount)
{
    return max((activeCount + Amd8PartitionElementCount - 1u) / Amd8PartitionElementCount, 1u);
}

/**
 * Converts one pixel-edge coordinate to normalized-device coordinates.
 *
 * This is used by billboard raster bounds and debug overlay conversion paths.
 */
inline float pixelEdgeToNdc(float pixelEdge, uint resolution)
{
    return ((pixelEdge + 0.5f) * 2.0f / max(float(resolution), 1.0f)) - 1.0f;
}
