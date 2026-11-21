#pragma once
#include "UGL.h"
#include <EASTL/vector.h>

namespace TaskGraphSwapchainSample
{
    using namespace UGL;

    /// Self-contained deterministic sine approximation on [-pi, pi] wrapped for arbitrary radians.
    inline float prismSin(float radians)
    {
        constexpr float kPi = 3.14159265359f;
        constexpr float kTwoPi = 6.28318530718f;
        int turns = int(radians / kTwoPi);
        float x = radians - float(turns) * kTwoPi;
        if (x > kPi)
        {
            x -= kTwoPi;
        }
        else if (x < -kPi)
        {
            x += kTwoPi;
        }
        const float x2 = x * x;
        return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f))));
    }

    /// Self-contained deterministic cosine approximation using a quarter-turn phase shift.
    inline float prismCos(float radians)
    {
        return prismSin(radians + 1.57079632679f);
    }

    /// Owned CPU animation parameters validated independently of swapchain presentation state.
    struct SceneParams
    {
        uint2 extent = uint2(800u, 600u);
        float timeSeconds = 0.0f;
        float aspectRatio = 4.0f / 3.0f;
    };

    /// Pairs the queried swapchain image with owned CPU scene parameters for the TaskGraph entry.
    struct FrameRequest
    {
        SwapchainQueryResult swapchainTarget;
        SceneParams params;
    };

    /// Interleaved clip-space position and linear RGBA color generated on a CPU worker task.
    struct PrismVertex
    {
        float4 position;
        float4 color;
    };

    using PrismVertexBuffer = Buffer<PrismVertex, BufferUsage<Storage, CopyDst>>;
    using SceneColorTexture = Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D>;

    /// Synthesizes three animated overlapping triangles on a CPU TaskGraph worker without touching GPU state.
    inline eastl::vector<PrismVertex> buildAnimatedPrism(IN SceneParams params)
    {
        constexpr uint kTriangleCount = 3u;
        constexpr float kTwoPi = 6.28318530718f;
        eastl::vector<PrismVertex> vertices(kTriangleCount * 3u);
        const float invAspect = params.aspectRatio > 0.01f ? (1.0f / params.aspectRatio) : 1.0f;

        for (uint tri = 0u; tri < kTriangleCount; ++tri)
        {
            const float layerPhase = float(tri) * 0.85f;
            const float baseAngle = params.timeSeconds * (0.65f + 0.2f * float(tri)) + layerPhase;
            const float radius = 0.68f - 0.14f * float(tri);

            for (uint corner = 0u; corner < 3u; ++corner)
            {
                const float theta = baseAngle + float(corner) * (kTwoPi / 3.0f);
                const float x = prismCos(theta) * radius * invAspect;
                const float y = prismSin(theta) * radius;

                PrismVertex v;
                v.position = float4(x, y, 0.0f, 1.0f);
                v.color = float4(
                    0.35f + 0.55f * (0.5f + 0.5f * prismSin(theta + params.timeSeconds)),
                    0.30f + 0.55f * (0.5f + 0.5f * prismCos(theta * 1.3f - layerPhase)),
                    0.50f + 0.45f * (0.5f + 0.5f * prismSin(params.timeSeconds * 0.9f + float(tri))),
                    1.0f);
                vertices[tri * 3u + corner] = v;
            }
        }
        return vertices;
    }

    /// Uploads CPU-generated vertices into an execution-scoped GPU storage buffer.
    class UploadPrismVerticesNode final : public IRenderNode<eastl::vector<PrismVertex>, PrismVertexBuffer>
    {
    public:
        /// Allocates and populates an isolated GPU vertex payload buffer for the current frame execution.
        PrismVertexBuffer build(RenderNode &node, IN eastl::vector<PrismVertex> vertices) const
        {
            const uint count = uint(vertices.size());
            auto buffer = node.createBuffer<PrismVertexBuffer>("TaskGraphPrismVertices", count > 0u ? count : 1u);
            if (count > 0u)
            {
                node.writeBuffer(buffer, vertices);
            }
            return buffer;
        }
    };

    /// Declares the offscreen color target rendered before swapchain presentation.
    struct PrismFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

    /// Passes interpolated vertex color from the procedural vertex pull shader to the fragment stage.
    struct PrismVarying
    {
        float4 position [[Position]];
        float4 color [[Attribute0]];
    };

    /// Pulls animated prism vertices from an immutable Args structured buffer and shades them.
    class PrismRasterShader final : public IRenderClass
    {
    public:
        /// Binds the frame's uploaded storage buffer without mutable BindGroup state.
        constructor(StructuredBuffer<PrismVertex> vertices [[Args0]])
        {
        }

    private:
        /// Fetches clip-space position and color by VertexID.
        PrismVarying vertex(uint id [[VertexID]])
        {
            PrismVarying out;
            PrismVertex v = vertices[id];
            out.position = v.position;
            out.color = v.color;
            return out;
        }

        /// Writes linear RGBA color to the offscreen scene attachment.
        PrismFrameBuffer fragment(PrismVarying input)
        {
            PrismFrameBuffer out;
            out.color = half4(half(input.color.x), half(input.color.y), half(input.color.z), half(1.0f));
            return out;
        }
    };

    /// Renders the uploaded prism geometry into an execution-managed offscreen RGBA8 texture.
    class RenderPrismSceneNode final : public IRenderNode<PrismVertexBuffer, SceneColorTexture>
    {
    public:
        /// Clears the offscreen target and records the procedural prism draw pass.
        SceneColorTexture build(RenderNode &node, IN SceneParams params, IN PrismVertexBuffer vertices) const
        {
            const uint2 safeExtent = uint2(
                params.extent.x > 0u ? params.extent.x : 1u,
                params.extent.y > 0u ? params.extent.y : 1u);
            auto sceneColor = node.createTexture<SceneColorTexture>("TaskGraphSceneColor", safeExtent);
            PrismFrameBuffer fb;
            fb.color = node.view(sceneColor);
            fb.color.loadOp = LoadOp::Clear;
            fb.color.clearValue = {0.06, 0.08, 0.14, 1.0};
            fb.color.storeOp = StoreOp::Store;
            node.renderPass("PrismScenePass", fb, node.draw<PrismRasterShader>(vertices, 9u, 1u, 0u, 0u));
            return sceneColor;
        }
    };

    /// Blits the rendered scene texture to the swapchain image and composites the ImGui overlay on the main queue.
    class PresentSwapchainNode final : public IRenderNode<SwapchainQueryResult, uint>
    {
    public:
        /// Records main-queue swapchain presentation + ImGui overlay draw.
        uint build(RenderNode &node, IN SwapchainQueryResult swapchainTarget, IN SceneColorTexture sceneColor) const
        {
            node.renderToSwapchain(swapchainTarget, sceneColor, RenderToSwapchainDescriptor{}, RenderImGui());
            return 1u;
        }
    };

    /// Minimal @entry TaskGraph template: CPU generation -> GPU upload -> GPU raster -> Swapchain + ImGui present.
    [[Export]] inline auto renderSwapchainFrame(IN FrameRequest request)
    {
        TaskGraph graph;
        auto cpuVertices = graph.add(buildAnimatedPrism, request.params);
        auto gpuVertices = graph.add(UploadPrismVerticesNode{}, cpuVertices);
        auto sceneColor = graph.add(RenderPrismSceneNode{}, request.params, gpuVertices);
        return graph.add(PresentSwapchainNode{}, request.swapchainTarget, sceneColor);
    }
} // namespace TaskGraphSwapchainSample
