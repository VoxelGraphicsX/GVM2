#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <EASTL/vector.h>

namespace GVM::RHI::Private
{
    /** Backend-created shader modules for the shared fullscreen texture draw; a backend may pass one module with two fragment entry points. */
    struct FullscreenTextureShaders
    {
        ShaderModule vertex;
        const char *vertexEntryPoint = nullptr;
        ShaderModule fragment;
        const char *fragmentEntryPoint = nullptr;
        ShaderModule flippedFragment;
        const char *flippedFragmentEntryPoint = nullptr;
    };

    /**
     * Draws a source texture as a fullscreen triangle into the current render pass using only public RHI
     * objects, so every backend shares one implementation.
     *
     * Shader contract:
     * - Bind group 0 has two bindings: binding 0 a sampled 2D float texture and binding 1 a filtering sampler.
     * - The vertex shader produces one fullscreen triangle from the vertex index alone (3 vertices, no vertex input).
     * - The fragment shader samples the texture and writes color attachment 0; the flipped variant samples
     *   with v = 1 - v.
     */
    class FullscreenTextureExecutor
    {
    public:
        /**
         * Builds the sampler, bind group layout, and pipeline layout for the shared draw. The shader modules
         * must implement the contract documented on this class.
         */
        void init(DeviceImpl &device, const FullscreenTextureShaders &shaders);

        /** Binds a cached bind group for the source's default view and the format-matched pipeline, then draws the triangle. */
        void encode(RenderPassEncoderImpl &encoder, Texture source, TextureFormat targetFormat, const RenderToSwapchainDescriptor &descriptor);

    private:
        struct CachedBindGroupEntry
        {
            TextureView sourceView = {};
            BindGroup bindGroup = {};
        };

        /** Returns a pipeline for the target format and flip flag; the device object cache deduplicates repeated creates. */
        RenderPipeline getOrCreatePipeline(TextureFormat targetFormat, const RenderToSwapchainDescriptor &descriptor);
        /** Returns a bind group sampling the source's default view, reusing one of three cached entries when possible. */
        BindGroup getOrCreateBindGroup(Texture source);

        DeviceImpl *mDevice = nullptr;
        FullscreenTextureShaders mShaders = {};
        Sampler mSampler = {};
        BindGroupLayout mBindGroupLayout = {};
        PipelineLayout mPipelineLayout = {};
        eastl::vector<CachedBindGroupEntry> mCachedBindGroups;
    };
} // namespace GVM::RHI::Private
