#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_SYMBOLIC_RESOURCES_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_SYMBOLIC_RESOURCES_HPP

#include "UGL.h"

using namespace UGL;

/** Stores uniform parameters used to keep UniformBuffer metadata and loads shader-reachable. */
struct ExperimentalUGLIRSymbolicResourcesParams
{
    uint addend;
    float scale;
};

/** Binds each resource category that should be represented by structured resource metadata in UGLIR. */
struct ExperimentalUGLIRSymbolicResourcesBindGroup final : public IBindGroup
{
    /** Declares buffers, textures, a sampler, and storage textures with stable set/binding metadata. */
    constructor(StructuredBuffer<uint> sourceValues [[Binding0]],
                RWStructuredBuffer<uint> outputValues [[Binding1]],
                UniformBuffer<ExperimentalUGLIRSymbolicResourcesParams> params [[Binding2]],
                Texture2D<TextureFormat::RGBA8Unorm> colorTexture [[Binding3]],
                Texture2DArray<TextureFormat::RGBA16Float> arrayTexture [[Binding4]],
                Texture3D<float4> volumeTexture [[Binding5]],
                Sampler sampler0 [[Binding6]],
                RWTexture2D<TextureFormat::RGBA8Unorm> storageColor [[Binding7]],
                RWTexture2DArray<TextureFormat::R16Float> storageArray [[Binding8]],
                RWTexture3D<TextureFormat::RGBA8Unorm> storageVolume [[Binding9]])
    {
    }
};

/** Exercises structured resource metadata without relying on runtime readback. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRSymbolicResourcesPass final : public IComputeClass
{
public:
    /** Stores the bind group used by the symbolic resource metadata pass. */
    constructor(BindGroup<ExperimentalUGLIRSymbolicResourcesBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Emits representative resource reads, writes, dimensions queries, and sampler use sites. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint width = 0u;
        uint height = 0u;
        uint layers = 0u;
        bindGroup->arrayTexture->getDimensions(width, height, layers);

        uint volumeWidth = 0u;
        uint volumeHeight = 0u;
        uint volumeDepth = 0u;
        bindGroup->volumeTexture->getDimensions(volumeWidth, volumeHeight, volumeDepth);

        const uint2 coord2D = uint2(threadID.x, 0u);
        const uint3 coord3D = uint3(threadID.x, 0u, 0u);
        const float4 colorValue = float4(bindGroup->colorTexture->sampleLevel(bindGroup->sampler0, float2(0.25f, 0.5f), 0.0f));
        const float4 arrayValue = float4(bindGroup->arrayTexture->read(coord2D, 0u, 0u));
        const float4 volumeValue = bindGroup->volumeTexture->read(coord3D, 0u);

        bindGroup->storageColor->write(coord2D, colorValue + arrayValue);
        bindGroup->storageArray->write(coord2D, 0u, half(0.5f));
        bindGroup->storageVolume->write(coord3D, volumeValue);

        bindGroup->outputValues[threadID.x] =
            bindGroup->sourceValues[threadID.x] + bindGroup->params->addend + width + height + layers + volumeWidth + volumeHeight + volumeDepth;
    }
};

#endif
