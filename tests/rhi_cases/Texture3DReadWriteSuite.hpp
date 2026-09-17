#ifndef GVM_TEST_TEXTURE3D_READ_WRITE_SUITE_HPP
#define GVM_TEST_TEXTURE3D_READ_WRITE_SUITE_HPP

#include "UGL.h"

using namespace UGL;

namespace Texture3DReadWriteTest
{
    /**
     * Binds sampled, storage-readable, and storage-writable 3D textures for runtime validation.
     * The compute pass keeps storage reads and writes on different textures so it does not rely on same-pass texture write visibility.
     */
    struct Texture3DReadWriteBindGroup final : public IBindGroup
    {
        /** Declares the 3D texture resources consumed by the compute pass. */
        constructor(Texture3D<float4> sourceVolume [[Binding0]],
                    RWTexture3D<TextureFormat::RGBA8Unorm> storageInputVolume [[Binding1]],
                    RWTexture3D<TextureFormat::RGBA8Unorm> outputVolume [[Binding2]])
        {
        }
    };

    /**
     * Copies each voxel through sampled Texture3D read, RWTexture3D read, and RWTexture3D write paths.
     * The pass intentionally uses exact one-thread-per-voxel dispatch and does not cover Texture3DArray or 3D attachments.
     */
    class [[LocalWorkGroupSize(1, 1, 1)]] Texture3DReadWritePass final : public IComputeClass
    {
    public:
        /** Stores the bind group used by the generated compute class. */
        constructor(BindGroup<Texture3DReadWriteBindGroup> bindGroup [[Slot0]])
        {
        }

    private:
        /** Copies one voxel when the dispatch thread falls inside the source volume dimensions. */
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            uint width = 0u;
            uint height = 0u;
            uint depth = 0u;
            bindGroup->sourceVolume->getDimensions(width, height, depth);
            if (threadID.x >= width || threadID.y >= height || threadID.z >= depth)
            {
                return;
            }

            const half4 sourceValue = half4(bindGroup->sourceVolume->read(threadID, 0u));
            const half4 storedValue = bindGroup->storageInputVolume->read(threadID);
            bindGroup->outputVolume->write(threadID, sourceValue * storedValue.a);
        }
    };
} // namespace Texture3DReadWriteTest

#endif
