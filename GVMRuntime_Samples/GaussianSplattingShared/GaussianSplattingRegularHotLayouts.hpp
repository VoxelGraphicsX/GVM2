#pragma once

/**
 * Stores the hot Gaussian payload used by regular 3DGS samples.
 *
 * Use this layout for samples whose opacity is stored directly in positionOpacity.w and whose billboard color profile
 * does not require the Spark profile fields used by LoD and cluster samples.
 */
struct GaussianSplatHotGPU
{
    float4 positionOpacity;
    float4 axis0;
    float4 axis1;
    float4 axis2;
    float4 shDc;
};
