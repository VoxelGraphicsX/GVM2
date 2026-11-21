#pragma once
#include "UGL.h"
#include <EASTL/vector.h>

namespace GpuGraphFixture
{
    using namespace UGL;

    using SampleBuffer = Buffer<float, BufferUsage<Storage, CopySrc, CopyDst>>;
    using SampleRange = BufferRange<float, BufferUsage<Storage, CopySrc, CopyDst>>;

    /// Describes CPU-owned inputs to the mixed CPU/GPU graph.
    struct Input
    {
        uint count = 256u;
        float scale = 2.0f;
        float bias = 0.25f;
    };

    /// Generates ordinary CPU values without GPU resources or borrowed storage.
    eastl::vector<float> generate(IN Input input)
    {
        eastl::vector<float> values(input.count);
        for (uint index = 0; index < input.count; ++index)
        {
            values[index] = float(index) / 256.0f;
        }
        return values;
    }

    /// Scales one immutable input range into a separate GPU allocation.
    class [[LocalWorkGroupSize(64, 1, 1)]] Scale final : public IComputeClass
    {
    public:
        /// Captures the complete resource and scalar arguments for one dispatch.
        constructor(StructuredBuffer<float> source [[Args0]], RWStructuredBuffer<float> target [[Args1]],
            uint count [[Args2]], float scale [[Args3]])
        {
        }
    private:
        /// Writes one scaled element for each valid invocation.
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                target[id.x] = source[id.x] * scale;
            }
        }
    };

    /// Biases an independent branch of the input dataset.
    class [[LocalWorkGroupSize(64, 1, 1)]] Bias final : public IComputeClass
    {
    public:
        /// Captures independent output storage and the bias value.
        constructor(StructuredBuffer<float> source [[Args0]], RWStructuredBuffer<float> target [[Args1]],
            uint count [[Args2]], float bias [[Args3]])
        {
        }
    private:
        /// Writes one biased element for each valid invocation.
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                target[id.x] = source[id.x] + bias;
            }
        }
    };

    /// Combines two independently produced GPU ranges.
    class [[LocalWorkGroupSize(64, 1, 1)]] Add final : public IComputeClass
    {
    public:
        /// Captures both producers and the destination allocation.
        constructor(StructuredBuffer<float> left [[Args0]], StructuredBuffer<float> right [[Args1]],
            RWStructuredBuffer<float> target [[Args2]], uint count [[Args3]])
        {
        }
    private:
        /// Writes the sum after the resource dependency solver orders both producers.
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                target[id.x] = left[id.x] + right[id.x];
            }
        }
    };

    /// Uploads CPU values into an existing Buffer managed internally by the execution.
    class Upload final : public IRenderNode<eastl::vector<float>, SampleBuffer>
    {
    public:
        /// Returns an existing buffer handle while GPU execution remains deferred.
        SampleBuffer build(IN eastl::vector<float> values) const
        {
            auto output = createBuffer<SampleBuffer>("GraphInput", uint(values.size()));
            writeBuffer(SampleRange(output, 0u, uint(values.size()) * sizeof(float)), values);
            return output;
        }
    };

    /// Expands one configuration object into three independently recordable compute passes.
    class Filter final : public IRenderNode<SampleBuffer, SampleBuffer>
    {
    public:
        float scale = 2.0f;
        float bias = 0.25f;
        uint elementCount = 256u;
        /// Builds two independent branches and a resource-dependent combine pass.
        SampleBuffer build(IN SampleBuffer input) const
        {
            uint count = elementCount;
            auto scaled = scaleInput(input, count);
            auto biased = createBuffer<SampleBuffer>("Biased", count);
            auto combined = createBuffer<SampleBuffer>("Combined", count);
            computePass("Bias", dispatchThreads<Bias>(input, biased, count, bias, count, 1u, 1u));
            computePass("Combine", dispatchThreads<Add>(scaled, biased, combined, count, count, 1u, 1u));
            return combined;
        }
    private:
        /// Verifies synchronous helper methods receive the same hidden invocation state.
        SampleBuffer scaleInput(IN SampleBuffer input, uint count) const
        {
            auto output = createBuffer<SampleBuffer>("Scaled", count);
            computePass("Scale", dispatchThreads<Scale>(input, output, count, scale, count, 1u, 1u));
            return output;
        }
    };

    /// Reduces the completed readback using ordinary CPU code.
    float sum(IN eastl::vector<float> values)
    {
        float result = 0.0f;
        for (uint index = 0; index < values.size(); ++index)
        {
            result += values[index];
        }
        return result;
    }

    /// Exports a renderer-independent CPU/upload/compute/readback/CPU graph using one add API.
    [[Export]] auto buildGraph(IN Input input)
    {
        TaskGraph graph;
        Filter filter;
        filter.elementCount = input.count;
        filter.scale = input.scale;
        filter.bias = input.bias;
        auto source = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, source);
        auto filtered = graph.add(filter, uploaded);
        auto copied = graph.add(ReadbackNode<float>{}, filtered);
        return graph.add(sum, copied);
    }

    /// Reuses an earlier launch group's buffer after a CPU readback dependency has completed.
    class ReuseBuffer final : public IRenderNode<SampleBuffer, SampleBuffer>
    {
    public:
        /// Reads the retained original allocation while deriving dispatch size from ordinary CPU values.
        SampleBuffer build(IN SampleBuffer input, IN eastl::vector<float> values) const
        {
            uint count = uint(values.size());
            auto output = createBuffer<SampleBuffer>("Reused", count);
            computePass("Reuse", dispatchThreads<Scale>(input, output, count, 1.0f, count, 1u, 1u));
            return output;
        }
    };

    /// Builds a second GPU launch group from the first launch group's completed owned CPU readback.
    [[Export]] auto buildTwoLaunchGroups(IN Input input)
    {
        TaskGraph graph;
        auto source = graph.add(generate, input);
        auto first = graph.add(Upload{}, source);
        auto intermediate = graph.add(ReadbackNode<float>{}, first);
        auto second = graph.add(ReuseBuffer{}, first, intermediate);
        auto result = graph.add(ReadbackNode<float>{}, second);
        return graph.add(sum, result);
    }

    /// Exposes independently produced buffers through ordinary named output fields.
    struct BranchOutputs
    {
        SampleBuffer scaled;
        SampleBuffer biased;
    };

    /// Builds two outputs without imposing a RenderNode-wide GPU completion fence.
    class Branches final : public IRenderNode<SampleBuffer, BranchOutputs>
    {
    public:
        uint count = 256u;
        /// Produces distinct resource versions that can be projected independently.
        BranchOutputs build(IN SampleBuffer input) const
        {
            BranchOutputs output;
            output.scaled = createBuffer<SampleBuffer>("ProjectedScale", count);
            output.biased = createBuffer<SampleBuffer>("ProjectedBias", count);
            computePass("ProjectedScale", dispatchThreads<Scale>(input, output.scaled, count, 2.0f, count, 1u, 1u));
            computePass("ProjectedBias", dispatchThreads<Bias>(input, output.biased, count, 0.25f, count, 1u, 1u));
            return output;
        }
    };

    /// Connects only one projected GPU output to a typed readback adapter.
    [[Export]] auto buildProjectedGpuGraph(IN Input input)
    {
        TaskGraph graph;
        Branches branches;
        branches.count = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        auto outputs = graph.add(branches, uploaded);
        auto copied = graph.add(ReadbackNode<float>{}, outputs.scaled);
        return graph.add(sum, copied);
    }

    using PreviewTexture = Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>;

    /// Reuses the ordinary framebuffer declaration for graph raster passes.
    struct PreviewFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

    /// Carries procedural clip-space positions between raster stages.
    struct PreviewVertex
    {
        float4 position [[Position]];
    };

    /// Writes a GPU-computed input into an ordinary RGBA8 attachment.
    class Preview final : public IRenderClass
    {
    public:
        /// Captures the compute result through immutable draw arguments.
        constructor(StructuredBuffer<float> source [[Args0]])
        {
        }
    private:
        /// Emits a full-screen triangle without vertex buffers.
        PreviewVertex vertex(uint id [[VertexID]])
        {
            PreviewVertex output;
            if (id == 0u)
            {
                output.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
            }
            else if (id == 1u)
            {
                output.position = float4(3.0f, -1.0f, 0.0f, 1.0f);
            }
            else
            {
                output.position = float4(-1.0f, 3.0f, 0.0f, 1.0f);
            }
            return output;
        }
        /// Stores exact green when the upstream filter produced its expected first element.
        PreviewFrameBuffer fragment(PreviewVertex input)
        {
            PreviewFrameBuffer output;
            output.color = half4(0.0f, source[0] * 4.0f, 0.0f, 1.0f);
            return output;
        }
    };

    /// Converts a raster attachment into an ordinary buffer for the existing readback adapter.
    class [[LocalWorkGroupSize(1, 1, 1)]] ReadPreview final : public IComputeClass
    {
    public:
        /// Captures a sampled view and the one-element destination.
        constructor(Texture2D<float4> source [[Args0]], RWStructuredBuffer<float> target [[Args1]])
        {
        }
    private:
        /// Reads the attachment through its declared sampled-texture role.
        void compute()
        {
            target[0] = source->read(uint2(0u, 0u), 0u).y;
        }
    };

    /// Uses existing texture, view, framebuffer and shader types in a render subgraph.
    class PreviewNode final : public IRenderNode<SampleBuffer, SampleBuffer>
    {
    public:
        /// Records a raster pass followed by a dependent texture-reading compute pass.
        SampleBuffer build(IN SampleBuffer input) const
        {
            auto texture = createTexture<PreviewTexture>("GraphPreview", uint2(1u, 1u));
            auto textureView = view(texture);
            PreviewFrameBuffer framebuffer;
            framebuffer.color = textureView;
            framebuffer.color.loadOp = LoadOp::Clear;
            framebuffer.color.storeOp = StoreOp::Store;
            renderPass("Preview", framebuffer, draw<Preview>(input, 3u, 1u, 0u, 0u));
            auto output = createBuffer<SampleBuffer>("PreviewValue", 1u);
            computePass("ReadPreview", dispatchThreads<ReadPreview>(textureView, output, 1u, 1u, 1u));
            return output;
        }
    };

    /// Exercises upload, compute, raster, sampled-texture compute and CPU readback in one graph.
    [[Export]] auto buildRasterGraph(IN Input input)
    {
        TaskGraph graph;
        Filter filter;
        filter.elementCount = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        auto filtered = graph.add(filter, uploaded);
        auto preview = graph.add(PreviewNode{}, filtered);
        auto copied = graph.add(ReadbackNode<float>{}, preview);
        return graph.add(sum, copied);
    }

    /// Exports an existing buffer handle whose allocation is explicitly destroyed by the host.
    [[Export]] auto buildBuffer(IN Input input)
    {
        TaskGraph graph;
        auto values = graph.add(generate, input);
        return graph.add(Upload{}, values);
    }

    /// Exports named GPU allocations using ordinary output fields and existing resource types.
    [[Export]] auto buildBufferOutputs(IN Input input)
    {
        TaskGraph graph;
        Branches branches;
        branches.count = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        return graph.add(branches, uploaded);
    }

    /// Builds an exported image without adding a graph-specific resource owner type.
    class ImageOutput final : public IRenderNode<SampleBuffer, PreviewTexture>
    {
    public:
        /// Records the image producer while leaving explicit destruction to its successful recipient.
        PreviewTexture build(IN SampleBuffer input) const
        {
            auto texture = createTexture<PreviewTexture>("ExportedPreview", uint2(1u, 1u));
            PreviewFrameBuffer framebuffer;
            framebuffer.color = view(texture);
            renderPass("ExportedPreview", framebuffer, draw<Preview>(input, 3u, 1u, 0u, 0u));
            return texture;
        }
    };

    /// Exports a raster result that remains valid after its execution and runtime are released.
    [[Export]] auto buildImage(IN Input input)
    {
        TaskGraph graph;
        Filter filter;
        filter.elementCount = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        auto filtered = graph.add(filter, uploaded);
        return graph.add(ImageOutput{}, filtered);
    }

    /// Returns two handles to one allocation after its final in-place write.
    class AliasedOutputs final : public IRenderNode<SampleBuffer, BranchOutputs>
    {
    public:
        uint count = 256u;
        /// Copies resource descriptions without claiming to preserve old GPU contents.
        BranchOutputs build(IN SampleBuffer input) const
        {
            BranchOutputs output;
            output.scaled = input;
            computePass("InPlaceScale", dispatchThreads<Scale>(input, input, count, 2.0f, count, 1u, 1u));
            output.biased = input;
            return output;
        }
    };

    /// Reads an aliased output only after the allocation's final producing write.
    [[Export]] auto buildAliasedOutputGraph(IN Input input)
    {
        TaskGraph graph;
        AliasedOutputs aliases;
        aliases.count = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        auto outputs = graph.add(aliases, uploaded);
        auto copied = graph.add(ReadbackNode<float>{}, outputs.scaled);
        return graph.add(sum, copied);
    }

    using Texture1D = Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<CopySrc, CopyDst, TextureBinding>, TextureDimension::e1D>;
    using Texture3D = Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<CopySrc, CopyDst, TextureBinding>, TextureDimension::e3D>;
    using TransientPixelLocalTexture = Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, PixelLocalAttachment>, TextureDimension::e2D>;

    /// Exercises explicit RenderNode& authoring, 1D texture creation, writeTexture, and copyTexture.
    class Texture1DNode final : public IRenderNode<eastl::vector<uint8_t>, Texture1D>
    {
    public:
        /// Uploads 1D texels into a staging texture and copies to a fresh 1D destination.
        Texture1D build(RenderNode &node, IN eastl::vector<uint8_t> texels) const
        {
            uint width = uint(texels.size() / 4u);
            auto src = node.createTexture<Texture1D>("GraphTex1DSrc", width);
            auto dst = node.createTexture<Texture1D>("GraphTex1DDst", width);
            uploadImplicit(src, texels);
            copyExplicit(node, src, dst);
            return dst;
        }
    private:
        /// Verifies implicit helper methods remain callable inside explicit-node build methods.
        void uploadImplicit(IN Texture1D destination, IN eastl::vector<uint8_t> texels) const
        {
            writeTexture(destination, texels);
        }
        /// Verifies explicit RenderNode& helper methods receive the forwarded builder reference.
        void copyExplicit(RenderNode &node, IN Texture1D source, IN Texture1D destination) const
        {
            node.copyTexture(source, destination);
        }
    };

    /// Uploads, copies, and reads back a 1D texture through ReadbackTextureNode.
    [[Export]] auto buildTexture1DReadbackGraph(IN eastl::vector<uint8_t> texels)
    {
        TaskGraph graph;
        auto copied = graph.add(Texture1DNode{}, texels);
        return graph.add(ReadbackTextureNode<uint8_t>{}, copied);
    }

    /// Exercises 3D texture creation, writeTexture, and copyTexture with explicit RenderNode&.
    class Texture3DNode final : public IRenderNode<eastl::vector<uint8_t>, Texture3D>
    {
    public:
        uint width = 2u;
        uint height = 2u;
        uint depth = 2u;
        /// Uploads 3D texels and copies them into a second 3D allocation.
        Texture3D build(RenderNode &node, IN eastl::vector<uint8_t> texels) const
        {
            auto src = node.createTexture<Texture3D>("GraphTex3DSrc", uint3(width, height, depth));
            auto dst = node.createTexture<Texture3D>("GraphTex3DDst", uint3(width, height, depth));
            node.writeTexture(src, texels);
            node.copyTexture(src, dst);
            return dst;
        }
    };

    /// Uploads, copies, and reads back a 3D volume texture through ReadbackTextureNode.
    [[Export]] auto buildTexture3DReadbackGraph(IN eastl::vector<uint8_t> texels)
    {
        TaskGraph graph;
        auto copied = graph.add(Texture3DNode{}, texels);
        return graph.add(ReadbackTextureNode<uint8_t>{}, copied);
    }

    /// Shares one transient pixel-local color attachment with a readable color output.
    struct GraphPixelFrame final : public IFrameBuffer
    {
        PixelLocalColorAttachment<TextureFormat::RGBA8Unorm, PixelLocalAccess::ReadWrite,
            PixelLocalStorage::Transient, PixelLocalLoad::Clear, PixelLocalStore::Discard> albedo;
        ColorAttachment<TextureFormat::RGBA8Unorm> present;
    };

    /// Populates the transient pixel-local albedo attachment during the first subpass.
    class GraphPixelRaster final : public IRenderClass
    {
    public:
        /// Captures the constant color written into the pixel-local albedo slot.
        constructor(float4 color [[Args0]])
        {
        }
    private:
        /// Emits a full-screen triangle without vertex buffers.
        PreviewVertex vertex(uint id [[VertexID]])
        {
            PreviewVertex output;
            if (id == 0u)
            {
                output.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
            }
            else if (id == 1u)
            {
                output.position = float4(3.0f, -1.0f, 0.0f, 1.0f);
            }
            else
            {
                output.position = float4(-1.0f, 3.0f, 0.0f, 1.0f);
            }
            return output;
        }
        /// Writes the captured color into the transient pixel-local attachment.
        GraphPixelFrame fragment(PreviewVertex input)
        {
            GraphPixelFrame output;
            output.albedo = half4(color);
            return output;
        }
    };

    /// Reads the pixel-local albedo attachment and emits the final color attachment.
    class GraphPixelResolve final : public IPixelLocalRenderClass
    {
    public:
        /// Captures a color bias added on top of the transient pixel-local albedo.
        constructor(float4 bias [[Args0]])
        {
        }
    private:
        /// Combines the transient pixel-local color with the bias into the persistent color attachment.
        GraphPixelFrame pixel(GraphPixelFrame input [[PixelLocalInput]])
        {
            GraphPixelFrame output;
            output.present = input.albedo.read() + half4(bias);
            return output;
        }
    };

    /// Executes a multi-subpass pixelLocalPass inside RenderNode::renderPass.
    class GraphPixelLocalNode final : public IRenderNode<uint, PreviewTexture>
    {
    public:
        /// Allocates transient pixel-local and output attachments and runs two subpasses.
        PreviewTexture build(RenderNode &node, IN uint extent) const
        {
            auto albedo = node.createTexture<TransientPixelLocalTexture>("GraphPixelAlbedo", uint2(extent, extent));
            auto present = node.createTexture<PreviewTexture>("GraphPixelPresent", uint2(extent, extent));
            GraphPixelFrame framebuffer;
            framebuffer.albedo = node.view(albedo);
            framebuffer.present = node.view(present);
            framebuffer.present.loadOp = LoadOp::Clear;
            framebuffer.present.storeOp = StoreOp::Store;
            node.renderPass("GraphPixelLocalPass", framebuffer,
                pixelLocalPass(
                    node.draw<GraphPixelRaster>(float4(0.5f, 0.25f, 0.0f, 1.0f), 3u, 1u, 0u, 0u),
                    nextPixelLocalPass(),
                    node.draw<GraphPixelResolve>(float4(0.25f, 0.5f, 0.25f, 0.0f))));
            return present;
        }
    };

    /// Executes pixelLocalPass inside a TaskGraph RenderNode and reads back the resolved pixels.
    [[Export]] auto buildPixelLocalGraph(IN uint extent)
    {
        TaskGraph graph;
        auto rendered = graph.add(GraphPixelLocalNode{}, extent);
        return graph.add(ReadbackTextureNode<uint8_t>{}, rendered);
    }

    /// Blits a rendered preview texture to a queried swapchain image inside a TaskGraph RenderNode.
    class SwapchainPresentNode final : public IRenderNode<SwapchainQueryResult, uint>
    {
    public:
        /// Records a main-queue swapchain blit from source into swapchainTarget.
        uint build(RenderNode &node, IN SwapchainQueryResult swapchainTarget, IN PreviewTexture source) const
        {
            node.renderToSwapchain(swapchainTarget, source);
            return 1u;
        }
    };

    /// Captures a queried swapchain image and graph parameters for a swapchain presentation entry.
    struct SwapchainPresentInput
    {
        SwapchainQueryResult swapchainTarget;
        Input input;
    };

    /// Exports a graph entry taking a SwapchainPresentInput and presenting a rendered preview texture.
    [[Export]] auto buildSwapchainPresentGraph(IN SwapchainPresentInput request)
    {
        TaskGraph graph;
        Filter filter;
        filter.elementCount = request.input.count;
        auto values = graph.add(generate, request.input);
        auto uploaded = graph.add(Upload{}, values);
        auto filtered = graph.add(filter, uploaded);
        auto rendered = graph.add(ImageOutput{}, filtered);
        return graph.add(SwapchainPresentNode{}, request.swapchainTarget, rendered);
    }

    /// Runs a configurable ALU loop inside a small 64-thread workgroup to exercise profile-guided pass weighting.
    class [[LocalWorkGroupSize(64, 1, 1)]] HeavyIterate final : public IComputeClass
    {
    public:
        /// Captures source, destination, element count, and ALU iteration count.
        constructor(StructuredBuffer<float> source [[Args0]], RWStructuredBuffer<float> target [[Args1]],
            uint count [[Args2]], uint iterations [[Args3]])
        {
        }
    private:
        /// Executes bounded arithmetic iterations per thread so a tiny dispatch produces measurable GPU duration.
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                float acc = source[id.x] + 0.125f;
                for (uint step = 0u; step < iterations; ++step)
                {
                    acc = acc * 1.00006103515625f + 0.000244140625f;
                    if (acc > 4.0f)
                    {
                        acc = acc - 2.0f;
                    }
                }
                target[id.x] = acc;
            }
        }
    };

    /// Exercises split multi-dispatch compute passes, heavy small-dispatch compute, and raster passes for GPU timestamp feedback.
    class ProfiledGraphNode final : public IRenderNode<SampleBuffer, SampleBuffer>
    {
    public:
        uint iterations = 16384u;
        /// Emits labeled split compute passes, a heavy 64-thread compute pass, and a raster pass.
        SampleBuffer build(RenderNode &node, IN SampleBuffer input) const
        {
            uint count = 64u;
            auto branchA = node.createBuffer<SampleBuffer>("ProfiledBranchA", count);
            auto branchB = node.createBuffer<SampleBuffer>("ProfiledBranchB", count);
            auto heavyOut = node.createBuffer<SampleBuffer>("ProfiledHeavyOut", count);
            node.computePass("SplitProfiled",
                node.dispatchThreads<Scale>(input, branchA, count, 2.0f, count, 1u, 1u),
                node.dispatchThreads<Bias>(input, branchB, count, 0.5f, count, 1u, 1u));
            node.computePass("HeavySmallDispatch",
                node.dispatchThreads<HeavyIterate>(branchA, heavyOut, count, iterations, count, 1u, 1u));
            auto preview = node.createTexture<PreviewTexture>("ProfiledPreviewTex", uint2(4u, 4u));
            PreviewFrameBuffer fb;
            fb.color = node.view(preview);
            fb.color.loadOp = LoadOp::Clear;
            fb.color.storeOp = StoreOp::Store;
            node.renderPass("PreviewProfiled", fb, node.draw<Preview>(branchB, 3u, 1u, 0u, 0u));
            auto combined = node.createBuffer<SampleBuffer>("ProfiledCombined", count);
            node.computePass("CombineProfiled",
                node.dispatchThreads<Add>(heavyOut, branchB, combined, count, count, 1u, 1u));
            return combined;
        }
    };

    /// Exports a mixed compute+raster graph parameterized by ALU loop iterations for timestamp feedback verification.
    [[Export]] auto buildProfiledGraph(IN uint iterations)
    {
        TaskGraph graph;
        Input input;
        input.count = 64u;
        ProfiledGraphNode profiled;
        profiled.iterations = iterations;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        auto out = graph.add(profiled, uploaded);
        auto copied = graph.add(ReadbackNode<float>{}, out);
        return graph.add(sum, copied);
    }

    /// Exports a child GPU subgraph that generates and uploads a GPU buffer from a single owned Input parameter.
    [[Export]] auto buildNestedUploadSubgraph(IN Input input)
    {
        TaskGraph graph;
        auto source = graph.add(generate, input);
        return graph.add(Upload{}, source);
    }

    /// Executes an explicit TaskGraph& child GPU subgraph that filters a parent-promoted GPU buffer.
    class NestedFilterSubgraph
    {
    public:
        Task<SampleBuffer> operator()(TaskGraph &sub, IN SampleBuffer input, uint count, float scale, float bias) const
        {
            Filter filter;
            filter.elementCount = count;
            filter.scale = scale;
            filter.bias = bias;
            return sub.add(filter, input);
        }
    };

    /// Invokes both an [[Export]] GPU subgraph and an explicit TaskGraph& GPU subgraph inside a parent GPU TaskGraph.
    [[Export]] auto buildParentNestedGpuGraph(IN Input input)
    {
        TaskGraph graph;
        auto uploaded = graph.add(buildNestedUploadSubgraph, input);
        auto filtered = graph.add(NestedFilterSubgraph{}, uploaded, input.count, input.scale, input.bias);
        auto copied = graph.add(ReadbackNode<float>{}, filtered);
        return graph.add(sum, copied);
    }

    /// Generic user-defined class template wrapping a value or exported GPU allocation handle.
    template <class T>
    struct ResourceBox
    {
        T payload;
        uint count = 0u;
    };

    /// Wraps an allocated GPU buffer inside a generic class template for ownership-transfer verification.
    class BoxedUploadNode final : public IRenderNode<eastl::vector<float>, ResourceBox<SampleBuffer>>
    {
    public:
        /// Allocates and populates a SampleBuffer, returning it inside ResourceBox<SampleBuffer>.
        ResourceBox<SampleBuffer> build(RenderNode &node, IN eastl::vector<float> values) const
        {
            uint count = uint(values.size());
            auto buf = node.createBuffer<SampleBuffer>("BoxedExportBuffer", count);
            node.writeBuffer(SampleRange(buf, 0u, count * sizeof(float)), values);
            ResourceBox<SampleBuffer> boxed;
            boxed.payload = buf;
            boxed.count = count;
            return boxed;
        }
    };

    /// Exports a generic class template containing a graph-created GPU buffer allocation.
    [[Export]] auto buildTemplatedBoxExport(IN Input input)
    {
        TaskGraph graph;
        auto values = graph.add(generate, input);
        return graph.add(BoxedUploadNode{}, values);
    }

    /// Tracks how many times a texture extent expression is evaluated within a render node.
    struct ExtentEvalCounter
    {
        uint calls = 0u;

        /// Advances the invocation counter once and returns a 2D texture extent.
        uint2 next2D()
        {
            calls += 1u;
            return uint2(calls * 2u, calls * 4u);
        }
    };

    /// Verifies 2D createTexture evaluates its extent argument expression exactly once.
    class SingleEvalTextureExtentNode final : public IRenderNode<uint, uint>
    {
    public:
        /// Allocates a 2D PreviewTexture using a stateful extent expression and returns the observed call count.
        uint build(RenderNode &node, uint seed) const
        {
            ExtentEvalCounter counter{seed};
            auto tex = node.createTexture<PreviewTexture>("SingleEvalTex", counter.next2D());
            (void)tex;
            return counter.calls;
        }
    };

    /// Exports single-evaluation verification for 2D createTexture extent lowering.
    [[Export]] auto buildSingleEvalTextureExtentGraph(uint seed)
    {
        TaskGraph graph;
        return graph.add(SingleEvalTextureExtentNode{}, seed);
    }

    /// Carries a borrowed external GPU buffer into an exported graph entry submitted from DSL host code.
    struct BorrowedGpuSubmitInput
    {
        SampleBuffer source;
    };

    /// Reads back a borrowed GPU buffer passed as a root graph entry input.
    [[Export]] auto buildBorrowedBufferReadbackEntry(IN BorrowedGpuSubmitInput input)
    {
        TaskGraph graph;
        auto copied = graph.add(ReadbackNode<float>{}, SampleRange(input.source));
        return graph.add(sum, copied);
    }

    /// Submits an exported graph taking a GPU buffer handle directly from a DSL host function.
    float submitBorrowedBufferInDsl(DeviceImpl &device, BorrowedGpuSubmitInput input)
    {
        TaskRuntime runtime(device);
        auto execution = runtime.submit(buildBorrowedBufferReadbackEntry, input);
        execution.wait();
        return execution.get();
    }

}
