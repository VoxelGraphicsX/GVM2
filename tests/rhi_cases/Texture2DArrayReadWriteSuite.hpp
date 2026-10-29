#ifndef GVM_TEST_TEXTURE2DARRAY_READ_WRITE_SUITE_HPP
#define GVM_TEST_TEXTURE2DARRAY_READ_WRITE_SUITE_HPP

#include "UGL.h"

using namespace UGL;

namespace Texture2DArrayReadWriteTest
{
    static const uint TextureWidth = 4u;

    /**
     * Binds a writable 2D texture array for runtime backend validation.
     * The texture uses R16Float so Metal lowering must preserve both the scalar half value and the array layer.
     */
    struct Texture2DArrayReadWriteBindGroup final : public IBindGroup
    {
        /** Declares the writable 2D array texture consumed by the compute pass. */
        constructor(RWTexture2DArray<TextureFormat::R16Float> outputArray [[Binding0]])
        {
        }
    };

    /**
     * Writes a deterministic half-float pattern into every texel of a 2D texture array.
     * The pass is intended for readback tests that verify coordinate, layer, and value lowering together.
     */
    class [[LocalWorkGroupSize(1, 1, 1)]] Texture2DArrayReadWritePass final : public IComputeClass
    {
    public:
        /** Stores the bind group used by the generated compute class. */
        constructor(BindGroup<Texture2DArrayReadWriteBindGroup> bindGroup [[Slot0]])
        {
        }

    private:
        /** Writes one texel when the dispatch thread falls inside the extent returned by getDimensions(). */
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const uint3 size = bindGroup->outputArray->getDimensions();
            if (threadID.x >= size.x || threadID.y >= size.y || threadID.z >= size.z)
            {
                return;
            }

            const uint texelOrdinal = threadID.y * TextureWidth + threadID.x;
            const float value = 0.125f + float(threadID.z) * 0.25f + float(texelOrdinal) * 0.0009765625f;
            bindGroup->outputArray->write(threadID.xy, threadID.z, half(value));
        }
    };
} // namespace Texture2DArrayReadWriteTest

#endif
