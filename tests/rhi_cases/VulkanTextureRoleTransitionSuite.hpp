#ifndef GVM_TEST_VULKAN_TEXTURE_ROLE_TRANSITION_SUITE_HPP
#define GVM_TEST_VULKAN_TEXTURE_ROLE_TRANSITION_SUITE_HPP

#include "UGL.h"

using namespace UGL;

namespace VulkanTextureRoleTransitionTest
{
    /**
     * Binds one sampled 2D texture for compute passes that read from a texture binding.
     * The texture may be the same native image used by a different storage bind group in an ordered dispatch.
     */
    struct SampledTextureBindGroup final : public IBindGroup
    {
        /** Declares the sampled texture used by the generated compute class. */
        constructor(Texture2D<float4> sampledTexture [[Binding0]])
        {
        }
    };

    /**
     * Binds one writable 2D storage texture for compute passes that write through RWTexture2D.
     * This bind group is intentionally separate from SampledTextureBindGroup so dispatch order can define the hazard.
     */
    struct StorageTextureBindGroup final : public IBindGroup
    {
        /** Declares the storage texture used by the generated compute class. */
        constructor(RWTexture2D<TextureFormat::RGBA8Unorm> storageTexture [[Binding0]])
        {
        }
    };

    /**
     * Declares an invalid Vulkan bind group shape where one native texture can occupy sampled and storage roles together.
     * Runtime creation should reject this layout when both bindings resolve to the same texture.
     */
    struct DualRoleTextureBindGroup final : public IBindGroup
    {
        /** Declares sampled and storage bindings in the same bind group for conflict validation. */
        constructor(Texture2D<float4> sampledTexture [[Binding0]],
                    RWTexture2D<TextureFormat::RGBA8Unorm> storageTexture [[Binding1]])
        {
        }
    };

    /**
     * Binds a render-attachment result for a subsequent vertex-stage texture read.
     * The source image must not remain bound as an attachment in the consuming pass.
     */
    struct RenderAttachmentTransitionBindGroup final : public IBindGroup
    {
        /** Declares the sampled texture consumed by the vertex shader. */
        constructor(Texture2D<float4> sampledTexture [[Binding0]])
        {
        }
    };

    /** Carries one procedural clip-space position between the transition test stages. */
    struct RenderAttachmentTransitionVertexOutput
    {
        float4 position [[Position]];
    };

    /** Defines the RGBA8 attachment shared by the transition producer and consumer. */
    struct RenderAttachmentTransitionFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

    /** Writes an exact red value into every texel of one RGBA8 render attachment. */
    class RenderAttachmentWritePass final : public IRenderClass
    {
    public:
        /** Creates the stateless render-attachment producer. */
        constructor()
        {
        }

    private:
        /** Emits a full-screen triangle without a vertex buffer. */
        RenderAttachmentTransitionVertexOutput vertex(uint vertexID [[VertexID]])
        {
            RenderAttachmentTransitionVertexOutput output;
            if (vertexID == 0u)
            {
                output.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
            }
            else if (vertexID == 1u)
            {
                output.position = float4(3.0f, -1.0f, 0.0f, 1.0f);
            }
            else
            {
                output.position = float4(-1.0f, 3.0f, 0.0f, 1.0f);
            }
            return output;
        }

        /** Stores opaque red so stale black contents remain distinguishable. */
        RenderAttachmentTransitionFrameBuffer fragment(
            RenderAttachmentTransitionVertexOutput inputValue)
        {
            RenderAttachmentTransitionFrameBuffer output;
            output.color = half4(1.0f, 0.0f, 0.0f, 1.0f);
            return output;
        }
    };

    /**
     * Reads the producer attachment in the vertex stage and draws green only
     * when the current submission exposes the newly written red texel.
     */
    class VertexSampledTextureReadPass final : public IRenderClass
    {
    public:
        /** Creates the vertex-stage consumer with its sampled texture binding. */
        constructor(
            BindGroup<RenderAttachmentTransitionBindGroup> bindGroup [[Slot0]])
        {
        }

    private:
        /** Emits a full-screen triangle only when the sampled producer value is current. */
        RenderAttachmentTransitionVertexOutput vertex(uint vertexID [[VertexID]])
        {
            const float4 producerValue =
                bindGroup->sampledTexture->read(uint2(0u, 0u), 0u);
            const bool producerVisible = producerValue.x > 0.5f;
            RenderAttachmentTransitionVertexOutput output;
            output.position = float4(2.0f, 2.0f, 0.0f, 1.0f);
            if (producerVisible)
            {
                if (vertexID == 0u)
                {
                    output.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
                }
                else if (vertexID == 1u)
                {
                    output.position = float4(3.0f, -1.0f, 0.0f, 1.0f);
                }
                else
                {
                    output.position = float4(-1.0f, 3.0f, 0.0f, 1.0f);
                }
            }
            return output;
        }

        /** Writes opaque green for every visible consumer fragment. */
        RenderAttachmentTransitionFrameBuffer fragment(
            RenderAttachmentTransitionVertexOutput inputValue)
        {
            RenderAttachmentTransitionFrameBuffer output;
            output.color = half4(0.0f, 1.0f, 0.0f, 1.0f);
            return output;
        }
    };

    /**
     * Copies a sampled texture into a storage texture using one thread per texel.
     * Tests use a 2x2 image so dispatch bounds are hardcoded and independent of getDimensions lowering.
     */
    class [[LocalWorkGroupSize(1, 1, 1)]] SampledToStoragePass final : public IComputeClass
    {
    public:
        /** Stores the sampled source and storage destination bind groups. */
        constructor(BindGroup<SampledTextureBindGroup> sampledBindGroup [[Slot0]],
                    BindGroup<StorageTextureBindGroup> storageBindGroup [[Slot1]])
        {
        }

    private:
        /** Copies one texel when the dispatch thread is inside the fixed 2x2 image. */
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x >= 2u || threadID.y >= 2u)
            {
                return;
            }

            const half4 value = half4(sampledBindGroup->sampledTexture->read(threadID.xy, 0u));
            storageBindGroup->storageTexture->write(threadID.xy, value);
        }
    };

    /**
     * Writes a deterministic 2x2 RGBA8 pattern into a storage texture.
     * The pattern uses only 0 and 1 channel values so readback is exact after unorm conversion.
     */
    class [[LocalWorkGroupSize(1, 1, 1)]] WritePatternPass final : public IComputeClass
    {
    public:
        /** Stores the storage bind group that receives the deterministic texel pattern. */
        constructor(BindGroup<StorageTextureBindGroup> storageBindGroup [[Slot0]])
        {
        }

    private:
        /** Writes one deterministic texel when the dispatch thread is inside the fixed 2x2 image. */
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x >= 2u || threadID.y >= 2u)
            {
                return;
            }

            half4 value = half4(0.0f, 0.0f, 0.0f, 1.0f);
            if (threadID.x == 0u && threadID.y == 0u)
            {
                value = half4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            else if (threadID.x == 1u && threadID.y == 0u)
            {
                value = half4(0.0f, 1.0f, 0.0f, 1.0f);
            }
            else if (threadID.x == 0u && threadID.y == 1u)
            {
                value = half4(0.0f, 0.0f, 1.0f, 1.0f);
            }
            else
            {
                value = half4(1.0f, 1.0f, 1.0f, 1.0f);
            }
            storageBindGroup->storageTexture->write(threadID.xy, value);
        }
    };
} // namespace VulkanTextureRoleTransitionTest

#endif
