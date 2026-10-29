#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>
#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKTesting.hpp>

#include <EASTL/shared_ptr.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <vector>

namespace GVM::RHI::Vulkan
{
    using VKSubmissionSerial = uint64_t;

    class VKQueue : public QueueImpl
    {
    public:
        [[nodiscard]] VKSubmissionSerial getMostRecentSubmissionSerial() const;
        void waitForSubmission(VKSubmissionSerial serial, const char *waitSource = "unspecified");
    };
} // namespace GVM::RHI::Vulkan

namespace
{
    constexpr const char *kRenderShaderCode = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut
{
    float4 position [[position]];
    float4 color;
};

vertex VertexOut vertex_main(uint vertexId [[vertex_id]])
{
    constexpr float2 positions[3] = {
        float2(-0.5, -0.5),
        float2(0.0, 0.5),
        float2(0.5, -0.5)
    };

    constexpr float4 colors[3] = {
        float4(1.0, 0.0, 0.0, 1.0),
        float4(0.0, 1.0, 0.0, 1.0),
        float4(0.0, 0.0, 1.0, 1.0)
    };

    VertexOut output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.color = colors[vertexId];
    return output;
}

    fragment float4 fragment_main(VertexOut input [[stage_in]])
    {
        return input.color;
    }
)";

    constexpr uint32_t kCacheStressVertexSpirv[] = {
        0x07230203, 0x00010000, 0x0008000a, 0x00000028,
        0x00000000, 0x00020011, 0x00000001, 0x0006000b,
        0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
        0x00000000, 0x0003000e, 0x00000000, 0x00000001,
        0x0007000f, 0x00000000, 0x00000004, 0x6e69616d,
        0x00000000, 0x00000018, 0x0000001c, 0x00030003,
        0x00000002, 0x000001c2, 0x00040005, 0x00000004,
        0x6e69616d, 0x00000000, 0x00050005, 0x0000000c,
        0x69736f70, 0x6e6f6974, 0x00000073, 0x00060005,
        0x00000016, 0x505f6c67, 0x65567265, 0x78657472,
        0x00000000, 0x00060006, 0x00000016, 0x00000000,
        0x505f6c67, 0x7469736f, 0x006e6f69, 0x00070006,
        0x00000016, 0x00000001, 0x505f6c67, 0x746e696f,
        0x657a6953, 0x00000000, 0x00070006, 0x00000016,
        0x00000002, 0x435f6c67, 0x4470696c, 0x61747369,
        0x0065636e, 0x00070006, 0x00000016, 0x00000003,
        0x435f6c67, 0x446c6c75, 0x61747369, 0x0065636e,
        0x00030005, 0x00000018, 0x00000000, 0x00060005,
        0x0000001c, 0x565f6c67, 0x65747265, 0x646e4978,
        0x00007865, 0x00050048, 0x00000016, 0x00000000,
        0x0000000b, 0x00000000, 0x00050048, 0x00000016,
        0x00000001, 0x0000000b, 0x00000001, 0x00050048,
        0x00000016, 0x00000002, 0x0000000b, 0x00000003,
        0x00050048, 0x00000016, 0x00000003, 0x0000000b,
        0x00000004, 0x00030047, 0x00000016, 0x00000002,
        0x00040047, 0x0000001c, 0x0000000b, 0x0000002a,
        0x00020013, 0x00000002, 0x00030021, 0x00000003,
        0x00000002, 0x00030016, 0x00000006, 0x00000020,
        0x00040017, 0x00000007, 0x00000006, 0x00000002,
        0x00040015, 0x00000008, 0x00000020, 0x00000000,
        0x0004002b, 0x00000008, 0x00000009, 0x00000003,
        0x0004001c, 0x0000000a, 0x00000007, 0x00000009,
        0x00040020, 0x0000000b, 0x00000007, 0x0000000a,
        0x0004002b, 0x00000006, 0x0000000d, 0xbf800000,
        0x0005002c, 0x00000007, 0x0000000e, 0x0000000d,
        0x0000000d, 0x0004002b, 0x00000006, 0x0000000f,
        0x40400000, 0x0005002c, 0x00000007, 0x00000010,
        0x0000000f, 0x0000000d, 0x0005002c, 0x00000007,
        0x00000011, 0x0000000d, 0x0000000f, 0x0006002c,
        0x0000000a, 0x00000012, 0x0000000e, 0x00000010,
        0x00000011, 0x00040017, 0x00000013, 0x00000006,
        0x00000004, 0x0004002b, 0x00000008, 0x00000014,
        0x00000001, 0x0004001c, 0x00000015, 0x00000006,
        0x00000014, 0x0006001e, 0x00000016, 0x00000013,
        0x00000006, 0x00000015, 0x00000015, 0x00040020,
        0x00000017, 0x00000003, 0x00000016, 0x0004003b,
        0x00000017, 0x00000018, 0x00000003, 0x00040015,
        0x00000019, 0x00000020, 0x00000001, 0x0004002b,
        0x00000019, 0x0000001a, 0x00000000, 0x00040020,
        0x0000001b, 0x00000001, 0x00000019, 0x0004003b,
        0x0000001b, 0x0000001c, 0x00000001, 0x00040020,
        0x0000001e, 0x00000007, 0x00000007, 0x0004002b,
        0x00000006, 0x00000021, 0x00000000, 0x0004002b,
        0x00000006, 0x00000022, 0x3f800000, 0x00040020,
        0x00000026, 0x00000003, 0x00000013, 0x00050036,
        0x00000002, 0x00000004, 0x00000000, 0x00000003,
        0x000200f8, 0x00000005, 0x0004003b, 0x0000000b,
        0x0000000c, 0x00000007, 0x0003003e, 0x0000000c,
        0x00000012, 0x0004003d, 0x00000019, 0x0000001d,
        0x0000001c, 0x00050041, 0x0000001e, 0x0000001f,
        0x0000000c, 0x0000001d, 0x0004003d, 0x00000007,
        0x00000020, 0x0000001f, 0x00050051, 0x00000006,
        0x00000023, 0x00000020, 0x00000000, 0x00050051,
        0x00000006, 0x00000024, 0x00000020, 0x00000001,
        0x00070050, 0x00000013, 0x00000025, 0x00000023,
        0x00000024, 0x00000021, 0x00000022, 0x00050041,
        0x00000026, 0x00000027, 0x00000018, 0x0000001a,
        0x0003003e, 0x00000027, 0x00000025, 0x000100fd,
        0x00010038,
    };

    constexpr uint32_t kCacheStressFragmentSpirv[] = {
        0x07230203, 0x00010000, 0x0008000a, 0x00000016,
        0x00000000, 0x00020011, 0x00000001, 0x0006000b,
        0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
        0x00000000, 0x0003000e, 0x00000000, 0x00000001,
        0x0009000f, 0x00000004, 0x00000004, 0x6e69616d,
        0x00000000, 0x00000009, 0x0000000f, 0x00000011,
        0x00000013, 0x00030010, 0x00000004, 0x00000007,
        0x00030003, 0x00000002, 0x000001c2, 0x00040005,
        0x00000004, 0x6e69616d, 0x00000000, 0x00050005,
        0x00000009, 0x4374756f, 0x726f6c6f, 0x00000030,
        0x00050005, 0x0000000f, 0x4374756f, 0x726f6c6f,
        0x00000031, 0x00050005, 0x00000011, 0x4374756f,
        0x726f6c6f, 0x00000032, 0x00050005, 0x00000013,
        0x4374756f, 0x726f6c6f, 0x00000033, 0x00040047,
        0x00000009, 0x0000001e, 0x00000000, 0x00040047,
        0x0000000f, 0x0000001e, 0x00000001, 0x00040047,
        0x00000011, 0x0000001e, 0x00000002, 0x00040047,
        0x00000013, 0x0000001e, 0x00000003, 0x00020013,
        0x00000002, 0x00030021, 0x00000003, 0x00000002,
        0x00030016, 0x00000006, 0x00000020, 0x00040017,
        0x00000007, 0x00000006, 0x00000004, 0x00040020,
        0x00000008, 0x00000003, 0x00000007, 0x0004003b,
        0x00000008, 0x00000009, 0x00000003, 0x0004002b,
        0x00000006, 0x0000000a, 0x3e000000, 0x0004002b,
        0x00000006, 0x0000000b, 0x3e800000, 0x0004002b,
        0x00000006, 0x0000000c, 0x3f000000, 0x0004002b,
        0x00000006, 0x0000000d, 0x3f800000, 0x0007002c,
        0x00000007, 0x0000000e, 0x0000000a, 0x0000000b,
        0x0000000c, 0x0000000d, 0x0004003b, 0x00000008,
        0x0000000f, 0x00000003, 0x0007002c, 0x00000007,
        0x00000010, 0x0000000b, 0x0000000c, 0x0000000a,
        0x0000000d, 0x0004003b, 0x00000008, 0x00000011,
        0x00000003, 0x0007002c, 0x00000007, 0x00000012,
        0x0000000c, 0x0000000a, 0x0000000b, 0x0000000d,
        0x0004003b, 0x00000008, 0x00000013, 0x00000003,
        0x0004002b, 0x00000006, 0x00000014, 0x3f400000,
        0x0007002c, 0x00000007, 0x00000015, 0x00000014,
        0x0000000c, 0x0000000b, 0x0000000d, 0x00050036,
        0x00000002, 0x00000004, 0x00000000, 0x00000003,
        0x000200f8, 0x00000005, 0x0003003e, 0x00000009,
        0x0000000e, 0x0003003e, 0x0000000f, 0x00000010,
        0x0003003e, 0x00000011, 0x00000012, 0x0003003e,
        0x00000013, 0x00000015, 0x000100fd, 0x00010038,
    };

    /** Minimal valid GLCompute module: void main() with LocalSize 1x1x1 and no descriptors. */
    constexpr uint32_t kDedupComputeSpirv[] = {
        0x07230203, 0x00010000, 0x0008000a, 0x00000006,
        0x00000000, 0x00020011, 0x00000001, 0x0003000e,
        0x00000000, 0x00000001, 0x0005000f, 0x00000005,
        0x00000004, 0x6e69616d, 0x00000000, 0x00060010,
        0x00000004, 0x00000011, 0x00000001, 0x00000001,
        0x00000001, 0x00040005, 0x00000004, 0x6e69616d,
        0x00000000, 0x00020013, 0x00000002, 0x00030021,
        0x00000003, 0x00000002, 0x00050036, 0x00000002,
        0x00000004, 0x00000000, 0x00000003, 0x000200f8,
        0x00000005, 0x000100fd, 0x00010038,
    };

    /** Minimal Metal kernel used where a valid compute entry point is required. */
    constexpr const char *kDedupComputeShaderCode = R"(
#include <metal_stdlib>
using namespace metal;

kernel void dedup_main()
{
}
)";

    class RhiPipelineTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);

            device = instance->createDevice();
            ASSERT_NE(device, nullptr);
            ASSERT_NE(device->getMainQueue(), nullptr);
        }

        void TearDown() override
        {
            if (instance != nullptr)
            {
                GVM::RHI::destroyInstance(instance);
                instance = nullptr;
            }
            device = nullptr;
        }

        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device device = nullptr;

        void submitAndWait(const eastl::vector<GVM::RHI::CommandEncoder> &encoders)
        {
            device->getMainQueue()->submit(encoders);
            if (instance == nullptr || instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
            {
                return;
            }

            auto *vulkanQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(device->getMainQueue());
            ASSERT_NE(vulkanQueue, nullptr);
            const auto serial = vulkanQueue->getMostRecentSubmissionSerial();
            if (serial != 0u)
            {
                vulkanQueue->waitForSubmission(serial, "pipeline_cache_pressure_test");
            }
        }
    };

    struct CachePressureFramebuffer
    {
        std::array<GVM::RHI::Texture, 4> textures;
        std::array<GVM::RHI::TextureView, 4> views;
        std::array<GVM::RHI::Texture, 4> transientTextures;
        std::array<GVM::RHI::TextureView, 4> transientViews;
    };

    struct CachePressureStats
    {
        size_t renderPassCacheSize = 0u;
        size_t framebufferCacheSize = 0u;
        size_t pipelineCompatibleRenderPassCacheSize = 0u;
        size_t nativePipelineCacheSize = 0u;
    };

    // Public Matrix Awakens / UE5 city stats are used as the scale seed here.
    // Public material does not expose exact VkPipeline/VkRenderPass/VkFramebuffer
    // counts, so the test maps those city-scale counts into deterministic cache-key
    // cardinalities that must warm once and stay stable on replay.
    constexpr uint32_t kAaaCitySamplePublishedBuildings = 7000u;
    constexpr uint32_t kAaaCitySamplePublishedVehicles = 45073u;
    constexpr uint32_t kAaaCitySamplePublishedPedestrians = 35000u;
    constexpr uint32_t kCachePressureFramebufferVariants = 96u;
    constexpr uint32_t kCachePressureRenderPassSignatures = 12u;
    constexpr uint32_t kCachePressureSubpasses = 4u;
    constexpr uint32_t kCachePressureWriterPipelineVariants = 15u;
    constexpr uint32_t kCachePressureNoopPipelineVariants = 16u;
    constexpr uint32_t kCachePressureNoopDrawsPerSubpass = 4u;
    constexpr uint32_t kCachePressurePipelineVariants =
        kCachePressureWriterPipelineVariants + kCachePressureNoopPipelineVariants;

    uint32_t cachePressureWriterMask(uint32_t renderPassSignatureIndex, uint32_t subpassIndex)
    {
        constexpr uint32_t masks[] = {
            0x1u, 0x2u, 0x4u, 0x8u,
            0x3u, 0x5u, 0x9u, 0x6u,
            0xau, 0xcu, 0x7u, 0xbu,
            0xdu, 0xeu, 0xfu,
        };
        return masks[(renderPassSignatureIndex * 5u + subpassIndex * 3u + renderPassSignatureIndex / 2u) % kCachePressureWriterPipelineVariants];
    }

    uint32_t cachePressureNoopPipelineIndex(uint32_t renderPassSignatureIndex, uint32_t subpassIndex, uint32_t drawIndex)
    {
        return kCachePressureWriterPipelineVariants +
            ((renderPassSignatureIndex * 7u + subpassIndex * 5u + drawIndex * 3u) % kCachePressureNoopPipelineVariants);
    }

    uint64_t cachePressureRenderPassSignatureKey(uint32_t renderPassSignatureIndex)
    {
        uint64_t key = 0u;
        for (uint32_t subpassIndex = 0u; subpassIndex < kCachePressureSubpasses; ++subpassIndex)
        {
            key |= uint64_t{cachePressureWriterMask(renderPassSignatureIndex, subpassIndex)} << (subpassIndex * 4u);
        }
        for (uint32_t colorIndex = 0u; colorIndex < 4u; ++colorIndex)
        {
            const uint64_t storeDiscard = ((renderPassSignatureIndex + colorIndex) % 3u) == 0u ? 1u : 0u;
            key |= storeDiscard << (32u + colorIndex);
        }
        return key;
    }

    GVM::RHI::BlendState makeCachePressureBlendState(uint32_t seed)
    {
        constexpr GVM::RHI::BlendFactor factors[] = {
            GVM::RHI::BlendFactor::One,
            GVM::RHI::BlendFactor::SrcAlpha,
            GVM::RHI::BlendFactor::OneMinusSrcAlpha,
            GVM::RHI::BlendFactor::DstAlpha,
        };

        GVM::RHI::BlendState blend = {};
        blend.color.operation = (seed & 1u) == 0u ? GVM::RHI::BlendOperation::Add : GVM::RHI::BlendOperation::ReverseSubtract;
        blend.color.srcFactor = factors[seed % 4u];
        blend.color.dstFactor = factors[(seed / 2u + 1u) % 4u];
        blend.alpha.operation = (seed & 2u) == 0u ? GVM::RHI::BlendOperation::Add : GVM::RHI::BlendOperation::Max;
        blend.alpha.srcFactor = factors[(seed / 3u + 2u) % 4u];
        blend.alpha.dstFactor = factors[(seed / 5u + 3u) % 4u];
        return blend;
    }

    GVM::RHI::RenderPipelineDescriptor makeCachePressurePipelineDescriptor(
        GVM::RHI::PipelineLayout pipelineLayout,
        GVM::RHI::ShaderModule vertexShader,
        GVM::RHI::ShaderModule fragmentShader,
        uint32_t colorAttachmentMask,
        uint32_t variantIndex)
    {
        GVM::RHI::RenderPipelineDescriptor descriptor = {};
        char label[96] = {};
        std::snprintf(label, sizeof(label), "CachePressurePipeline_%u", variantIndex);
        descriptor.label = label;
        descriptor.layout = pipelineLayout;
        descriptor.vertex.module = vertexShader;
        descriptor.vertex.entryPoint = "main";
        descriptor.primitive.topology = GVM::RHI::PrimitiveTopology::TriangleList;
        descriptor.primitive.stripIndexFormat = GVM::RHI::IndexFormat::Undefined;
        descriptor.primitive.frontFace = (variantIndex & 1u) == 0u ? GVM::RHI::FrontFace::CCW : GVM::RHI::FrontFace::CW;
        descriptor.primitive.cullMode = static_cast<GVM::RHI::CullMode>(variantIndex % 3u);
        descriptor.fragment.module = fragmentShader;
        descriptor.fragment.entryPoint = "main";
        descriptor.fragment.targets.reserve(4u);
        for (uint32_t colorIndex = 0u; colorIndex < 4u; ++colorIndex)
        {
            const bool writesAttachment = (colorAttachmentMask & (1u << colorIndex)) != 0u;
            descriptor.fragment.targets.push_back({
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .blend = makeCachePressureBlendState(variantIndex + colorIndex * 11u),
                .blendEnabled = ((variantIndex + colorIndex) & 1u) != 0u,
                .writeMask = writesAttachment ? GVM::RHI::ColorWriteMask::All : GVM::RHI::ColorWriteMask::None,
                .pixelLocal = true,
            });
        }
        return descriptor;
    }

    GVM::RHI::RenderPipelineDescriptor makePipelineCompatibleCacheKeyDescriptor(
        GVM::RHI::PipelineLayout pipelineLayout,
        GVM::RHI::ShaderModule vertexShader,
        GVM::RHI::ShaderModule fragmentShader,
        const char *label,
        bool pixelLocalAttachment,
        bool useExplicitAccess,
        GVM::RHI::PixelLocalPassAttachmentAccess explicitAccess)
    {
        GVM::RHI::RenderPipelineDescriptor descriptor = {};
        descriptor.label = label;
        descriptor.layout = pipelineLayout;
        descriptor.vertex.module = vertexShader;
        descriptor.vertex.entryPoint = "main";
        descriptor.primitive.topology = GVM::RHI::PrimitiveTopology::TriangleList;
        descriptor.primitive.stripIndexFormat = GVM::RHI::IndexFormat::Undefined;
        descriptor.primitive.frontFace = GVM::RHI::FrontFace::CCW;
        descriptor.primitive.cullMode = GVM::RHI::CullMode::None;
        descriptor.fragment.module = fragmentShader;
        descriptor.fragment.entryPoint = "main";
        descriptor.fragment.targets.reserve(4u);
        for (uint32_t colorIndex = 0u; colorIndex < 4u; ++colorIndex)
        {
            descriptor.fragment.targets.push_back({
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .blendEnabled = false,
                .writeMask = GVM::RHI::ColorWriteMask::All,
                .pixelLocal = pixelLocalAttachment,
            });
        }
        if (useExplicitAccess)
        {
            descriptor.pixelLocalAttachmentAccess = explicitAccess;
        }
        return descriptor;
    }

    GVM::RHI::RenderPassDescriptor makeCachePressureRenderPassDescriptor(
        const CachePressureFramebuffer &framebuffer,
        uint32_t renderPassSignatureIndex)
    {
        GVM::RHI::RenderPassDescriptor descriptor = {};
        char label[96] = {};
        std::snprintf(label, sizeof(label), "CachePressureRenderPass_%u", renderPassSignatureIndex);
        descriptor.label = label;
        descriptor.colorAttachments.reserve(4u);
        for (uint32_t colorIndex = 0u; colorIndex < 4u; ++colorIndex)
        {
            descriptor.colorAttachments.push_back({
                .view = ((renderPassSignatureIndex + colorIndex) % 3u) == 0u
                    ? framebuffer.transientViews[colorIndex] : framebuffer.views[colorIndex],
                .loadOp = GVM::RHI::LoadOp::Clear,
                .storeOp = ((renderPassSignatureIndex + colorIndex) % 3u) == 0u
                    ? GVM::RHI::StoreOp::Discard
                    : GVM::RHI::StoreOp::Store,
                .clearValue = {
                    0.02f * static_cast<float>(colorIndex + 1u),
                    0.01f * static_cast<float>(renderPassSignatureIndex + 1u),
                    0.0f,
                    1.0f,
                },
                .pixelLocal = true,
            });
        }
        descriptor.pixelLocal.enabled = true;
        descriptor.pixelLocal.passCount = kCachePressureSubpasses;
        return descriptor;
    }

    CachePressureStats readCachePressureStats(
        GVM::RHI::Device device,
        const std::vector<GVM::RHI::RenderPipeline> &pipelines)
    {
        CachePressureStats stats = {};
        stats.renderPassCacheSize = GVM::RHI::Vulkan::Testing::getRenderPassCacheSize(device);
        stats.framebufferCacheSize = GVM::RHI::Vulkan::Testing::getFramebufferCacheSize(device);
        stats.pipelineCompatibleRenderPassCacheSize =
            GVM::RHI::Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize(device);
        for (const GVM::RHI::RenderPipeline &pipeline : pipelines)
        {
            stats.nativePipelineCacheSize += GVM::RHI::Vulkan::Testing::getNativePipelineCacheSize(pipeline);
        }
        return stats;
    }

    const GVM::RHI::Vulkan::Testing::PixelLocalSubpassDependencySnapshot *findPixelLocalDependency(
        const eastl::vector<GVM::RHI::Vulkan::Testing::PixelLocalSubpassDependencySnapshot> &dependencies,
        uint32_t sourceSubpass,
        uint32_t destinationSubpass)
    {
        for (const GVM::RHI::Vulkan::Testing::PixelLocalSubpassDependencySnapshot &dependency : dependencies)
        {
            if (dependency.sourceSubpass == sourceSubpass &&
                dependency.destinationSubpass == destinationSubpass)
            {
                return &dependency;
            }
        }
        return nullptr;
    }

    eastl::vector<GVM::RHI::Vulkan::Testing::PixelLocalDependencyColorAttachment> makePixelLocalDependencyColors(
        uint32_t count,
        GVM::RHI::LoadOp loadOp)
    {
        eastl::vector<GVM::RHI::Vulkan::Testing::PixelLocalDependencyColorAttachment> colorAttachments(count);
        for (auto &colorAttachment : colorAttachments)
        {
            colorAttachment.loadOp = loadOp;
            colorAttachment.storeOp = GVM::RHI::StoreOp::Store;
            colorAttachment.pixelLocal = true;
        }
        return colorAttachments;
    }

    TEST_F(RhiPipelineTest, CreatesDefaultAndCustomTextureViews)
    {
        auto texture = device->createTexture({
            .label = "ViewTexture",
            .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {16, 16, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 2,
            .arrayLayerCount = 1,
        });
        ASSERT_FALSE(texture.isNull());

        auto defaultView = texture->createView();
        ASSERT_FALSE(defaultView.isNull());

        auto customView = texture->createView({
            .label = "CustomTextureView",
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .dimension = GVM::RHI::TextureViewDimension::e2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
        });
        ASSERT_FALSE(customView.isNull());

        device->freeTexture(texture);
    }

    TEST_F(RhiPipelineTest, DISABLED_CreatesMinimalRenderPipeline)
    {
        auto shaderModule = device->createShaderModule({
            .label = "TriangleRenderShader",
            .code = kRenderShaderCode,
        });
        ASSERT_TRUE(static_cast<bool>(shaderModule));

        auto pipelineLayout = device->createPipelineLayout({
            .label = "TriangleRenderPipelineLayout",
            .bindGroupLayouts = {},
        });
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));

        GVM::RHI::RenderPipelineDescriptor descriptor = {};
        descriptor.label = "TriangleRenderPipeline";
        descriptor.layout = pipelineLayout;
        descriptor.vertex.module = shaderModule;
        descriptor.vertex.entryPoint = "vertex_main";
        descriptor.primitive.topology = GVM::RHI::PrimitiveTopology::TriangleList;
        descriptor.primitive.stripIndexFormat = GVM::RHI::IndexFormat::Undefined;
        descriptor.primitive.frontFace = GVM::RHI::FrontFace::CCW;
        descriptor.primitive.cullMode = GVM::RHI::CullMode::None;
        descriptor.fragment.module = shaderModule;
        descriptor.fragment.entryPoint = "fragment_main";
        descriptor.fragment.targets.push_back({
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .blendEnabled = false,
        });

        auto renderPipeline = device->createRenderPipeline(descriptor);
        ASSERT_TRUE(static_cast<bool>(renderPipeline));
    }

    TEST_F(RhiPipelineTest, MetalRejectsInvalidLibraryWithoutSourceFallback)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Metal)
            GTEST_SKIP() << "Tests Metal binary validation.";
        GVM::RHI::ShaderModuleDescriptor descriptor = {};
        descriptor.label = "InvalidLibraryWithValidSource";
        descriptor.code = kRenderShaderCode;
        descriptor.metallib = {0u, 1u, 2u, 3u};
        EXPECT_THROW(device->createShaderModule(descriptor), std::runtime_error);
    }

    /** Rejects a Metal render pipeline that declares more vertex buffers than fit above the fixed Args root index. */
    TEST_F(RhiPipelineTest, MetalRejectsMoreVertexBuffersThanTheBufferTableAllows)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Metal)
        {
            GTEST_SKIP() << "Tests Metal buffer-table vertex buffer limit validation.";
        }
        auto shaderModule = device->createShaderModule({.label = "BufferTableLimitShader", .code = kRenderShaderCode});
        auto pipelineLayout = device->createPipelineLayout({.label = "BufferTableLimitLayout"});

        GVM::RHI::RenderPipelineDescriptor descriptor = {};
        descriptor.label = "BufferTableLimit";
        descriptor.layout = pipelineLayout;
        descriptor.vertex.module = shaderModule;
        descriptor.vertex.entryPoint = "vertex_main";
        descriptor.vertex.buffers.resize(23);
        descriptor.fragment.module = shaderModule;
        descriptor.fragment.entryPoint = "fragment_main";
        descriptor.fragment.targets.push_back({.format = GVM::RHI::TextureFormat::RGBA8Unorm});

        try
        {
            (void)device->createRenderPipeline(descriptor);
            FAIL() << "Expected the vertex buffer count beyond the Metal buffer table to be rejected.";
        }
        catch (const std::invalid_argument &error)
        {
            EXPECT_NE(std::string(error.what()).find("at most 22 vertex buffers"), std::string::npos);
        }
    }

    /// Verifies Metal records reject vertex buffer slots that would collide with the fixed Args root index.
    TEST_F(RhiPipelineTest, MetalRejectsVertexBufferSlotBeyondBufferTableAtRecordTime)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Metal)
        {
            GTEST_SKIP() << "Tests Metal buffer-table vertex slot validation.";
        }
        auto texture = device->createTexture({
            .label = "VertexSlotLimitColor",
            .usage = GVM::RHI::TextureUsage::RenderAttachment,
            .size = {8, 8, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        auto buffer = device->createBuffer({.label = "VertexSlotLimitData", .usage = GVM::RHI::BufferUsage::Vertex, .size = 16});
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass({.label = "VertexSlotLimitPass",
            .colorAttachments = {{.view = texture->createView(),
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store}}});
        EXPECT_THROW(render->setVertexBuffer(GVM::RHI::BufferRange(buffer), 22), std::invalid_argument);
        render->end();
        encoder->end();
        device->freeBuffer(buffer);
        device->freeTexture(texture);
    }

    /// Verifies Metal records reject render bind-group indices beyond the fixed eight-entry group table.
    TEST_F(RhiPipelineTest, MetalRejectsRenderBindGroupIndexBeyondBufferTableAtRecordTime)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Metal)
        {
            GTEST_SKIP() << "Tests Metal buffer-table bind-group validation.";
        }
        auto texture = device->createTexture({
            .label = "RenderGroupLimitColor",
            .usage = GVM::RHI::TextureUsage::RenderAttachment,
            .size = {8, 8, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        auto buffer = device->createBuffer({.label = "RenderGroupLimitData", .usage = GVM::RHI::BufferUsage::Storage, .size = 16});
        GVM::RHI::BindGroupLayoutDescriptor layoutDescriptor = {};
        layoutDescriptor.entries.resize(1);
        layoutDescriptor.entries[0].binding = 0;
        layoutDescriptor.entries[0].visibility = GVM::RHI::ShaderStage::Fragment;
        layoutDescriptor.entries[0].buffer.type = GVM::RHI::BufferBindingType::Storage;
        layoutDescriptor.entries[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadWrite;
        auto bindGroupLayout = device->createBindGroupLayout(layoutDescriptor);
        auto bindGroup = device->createBindGroup({.layout = bindGroupLayout,
            .entries = GVM::RHI::BindGroupEntry{.binding = 0, .buffer = GVM::RHI::BufferRange(buffer)}});
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass({.label = "RenderGroupLimitPass",
            .colorAttachments = {{.view = texture->createView(),
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store}}});
        EXPECT_THROW(render->setBindGroup(bindGroup, 8), std::invalid_argument);
        render->end();
        encoder->end();
        device->freeBuffer(buffer);
        device->freeTexture(texture);
    }

    /// Verifies Metal records reject compute bind-group indices beyond the fixed eight-entry group table.
    TEST_F(RhiPipelineTest, MetalRejectsComputeBindGroupIndexBeyondBufferTableAtRecordTime)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Metal)
        {
            GTEST_SKIP() << "Tests Metal buffer-table bind-group validation.";
        }
        auto buffer = device->createBuffer({.label = "ComputeGroupLimitData", .usage = GVM::RHI::BufferUsage::Storage, .size = 16});
        GVM::RHI::BindGroupLayoutDescriptor layoutDescriptor = {};
        layoutDescriptor.entries.resize(1);
        layoutDescriptor.entries[0].binding = 0;
        layoutDescriptor.entries[0].visibility = GVM::RHI::ShaderStage::Compute;
        layoutDescriptor.entries[0].buffer.type = GVM::RHI::BufferBindingType::Storage;
        layoutDescriptor.entries[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadWrite;
        auto bindGroupLayout = device->createBindGroupLayout(layoutDescriptor);
        auto bindGroup = device->createBindGroup({.layout = bindGroupLayout,
            .entries = GVM::RHI::BindGroupEntry{.binding = 0, .buffer = GVM::RHI::BufferRange(buffer)}});
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto computePass = encoder->beginComputePass({.label = "ComputeGroupLimitPass"});
        EXPECT_THROW(computePass->setBindGroup(bindGroup, 8), std::invalid_argument);
        computePass->end();
        encoder->end();
        device->freeBuffer(buffer);
    }

    /// Verifies Vulkan rejects bind-group indices beyond the fixed 32-slot descriptor table at record and pipeline-layout time.
    TEST_F(RhiPipelineTest, VulkanRejectsBindGroupIndicesBeyondDescriptorTable)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
        {
            GTEST_SKIP() << "Tests Vulkan bind-group index validation.";
        }
        constexpr uint32_t kVulkanBindGroupTableCapacity = 32;
        auto buffer = device->createBuffer({.label = "VulkanGroupLimitData", .usage = GVM::RHI::BufferUsage::Storage, .size = 16});
        auto texture = device->createTexture({
            .label = "VulkanGroupLimitColor",
            .usage = GVM::RHI::TextureUsage::RenderAttachment,
            .size = {8, 8, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        GVM::RHI::BindGroupLayoutDescriptor layoutDescriptor = {};
        layoutDescriptor.entries.resize(1);
        layoutDescriptor.entries[0].binding = 0;
        layoutDescriptor.entries[0].visibility = GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment | GVM::RHI::ShaderStage::Compute;
        layoutDescriptor.entries[0].buffer.type = GVM::RHI::BufferBindingType::Storage;
        layoutDescriptor.entries[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadWrite;
        auto bindGroupLayout = device->createBindGroupLayout(layoutDescriptor);
        auto bindGroup = device->createBindGroup({.layout = bindGroupLayout,
            .entries = GVM::RHI::BindGroupEntry{.binding = 0, .buffer = GVM::RHI::BufferRange(buffer)}});

        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass({.label = "VulkanGroupLimitRenderPass",
            .colorAttachments = {{.view = texture->createView(),
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store}}});
        EXPECT_THROW(render->setBindGroup(bindGroup, kVulkanBindGroupTableCapacity), std::invalid_argument);
        render->end();
        auto computePass = encoder->beginComputePass({.label = "VulkanGroupLimitComputePass"});
        EXPECT_THROW(computePass->setBindGroup(bindGroup, kVulkanBindGroupTableCapacity), std::invalid_argument);
        computePass->end();
        encoder->end();

        GVM::RHI::PipelineLayoutDescriptor pipelineLayoutDescriptor = {};
        pipelineLayoutDescriptor.bindGroupLayouts.assign(kVulkanBindGroupTableCapacity + 1u, bindGroupLayout);
        EXPECT_THROW(device->createPipelineLayout(pipelineLayoutDescriptor), std::invalid_argument);

        device->freeBuffer(buffer);
        device->freeTexture(texture);
    }

    TEST_F(RhiPipelineTest, CreatesBindGroupLayoutWithMixedBindings)
    {
        GVM::RHI::BindGroupLayoutDescriptor descriptor = {};
        descriptor.label = "MixedBindingLayout";
        descriptor.entries.resize(3);
        descriptor.entries[0].binding = 0;
        descriptor.entries[0].visibility = GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
        descriptor.entries[0].buffer.type = GVM::RHI::BufferBindingType::Storage;
        descriptor.entries[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
        descriptor.entries[1].binding = 1;
        descriptor.entries[1].visibility = GVM::RHI::ShaderStage::Fragment;
        descriptor.entries[1].texture.sampleType = GVM::RHI::TextureSampleType::Float;
        descriptor.entries[1].texture.viewDimension = GVM::RHI::TextureViewDimension::e2D;
        descriptor.entries[2].binding = 2;
        descriptor.entries[2].visibility = GVM::RHI::ShaderStage::Fragment;
        descriptor.entries[2].sampler.type = GVM::RHI::SamplerBindingType::Filtering;

        auto bindGroupLayout = device->createBindGroupLayout(descriptor);
        ASSERT_TRUE(static_cast<bool>(bindGroupLayout));
    }

    TEST_F(RhiPipelineTest, CreatesPipelineLayoutWithMultipleBindGroupLayouts)
    {
        GVM::RHI::BindGroupLayoutDescriptor storageLayoutDescriptor = {};
        storageLayoutDescriptor.label = "StorageOnlyLayout";
        storageLayoutDescriptor.entries.resize(1);
        storageLayoutDescriptor.entries[0].binding = 0;
        storageLayoutDescriptor.entries[0].visibility = GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
        storageLayoutDescriptor.entries[0].buffer.type = GVM::RHI::BufferBindingType::Storage;
        storageLayoutDescriptor.entries[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
        auto storageLayout = device->createBindGroupLayout(storageLayoutDescriptor);
        ASSERT_TRUE(static_cast<bool>(storageLayout));

        GVM::RHI::BindGroupLayoutDescriptor samplerLayoutDescriptor = {};
        samplerLayoutDescriptor.label = "SamplerOnlyLayout";
        samplerLayoutDescriptor.entries.resize(1);
        samplerLayoutDescriptor.entries[0].binding = 0;
        samplerLayoutDescriptor.entries[0].visibility = GVM::RHI::ShaderStage::Fragment;
        samplerLayoutDescriptor.entries[0].sampler.type = GVM::RHI::SamplerBindingType::Filtering;
        auto samplerLayout = device->createBindGroupLayout(samplerLayoutDescriptor);
        ASSERT_TRUE(static_cast<bool>(samplerLayout));

        GVM::RHI::PipelineLayoutDescriptor pipelineLayoutDescriptor = {};
        pipelineLayoutDescriptor.label = "MultiLayoutPipelineLayout";
        pipelineLayoutDescriptor.bindGroupLayouts.resize(2);
        pipelineLayoutDescriptor.bindGroupLayouts[0] = storageLayout;
        pipelineLayoutDescriptor.bindGroupLayouts[1] = samplerLayout;

        auto pipelineLayout = device->createPipelineLayout(pipelineLayoutDescriptor);
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));
    }

    TEST_F(RhiPipelineTest, DISABLED_CreatesRenderPipelineWithBlendStateEnabled)
    {
        auto shaderModule = device->createShaderModule({
            .label = "TriangleBlendRenderShader",
            .code = kRenderShaderCode,
        });
        ASSERT_TRUE(static_cast<bool>(shaderModule));

        auto pipelineLayout = device->createPipelineLayout({
            .label = "BlendPipelineLayout",
            .bindGroupLayouts = {},
        });
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));

        GVM::RHI::RenderPipelineDescriptor descriptor = {};
        descriptor.label = "BlendEnabledPipeline";
        descriptor.layout = pipelineLayout;
        descriptor.vertex.module = shaderModule;
        descriptor.vertex.entryPoint = "vertex_main";
        descriptor.primitive.topology = GVM::RHI::PrimitiveTopology::TriangleList;
        descriptor.primitive.stripIndexFormat = GVM::RHI::IndexFormat::Undefined;
        descriptor.primitive.frontFace = GVM::RHI::FrontFace::CCW;
        descriptor.primitive.cullMode = GVM::RHI::CullMode::None;
        descriptor.fragment.module = shaderModule;
        descriptor.fragment.entryPoint = "fragment_main";
        descriptor.fragment.targets.push_back({
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .blend = {
                .color = {
                    .operation = GVM::RHI::BlendOperation::Add,
                    .srcFactor = GVM::RHI::BlendFactor::SrcAlpha,
                    .dstFactor = GVM::RHI::BlendFactor::OneMinusSrcAlpha,
                },
                .alpha = {
                    .operation = GVM::RHI::BlendOperation::Add,
                    .srcFactor = GVM::RHI::BlendFactor::One,
                    .dstFactor = GVM::RHI::BlendFactor::OneMinusSrcAlpha,
                },
            },
            .blendEnabled = true,
        });

        auto renderPipeline = device->createRenderPipeline(descriptor);
        ASSERT_TRUE(static_cast<bool>(renderPipeline));
    }

    /** Keeps Args layouts separate from ordinary cached layouts and rejects invalid roots before cache lookup. */
    TEST_F(RhiPipelineTest, VulkanSeparatesArgumentsFromOrdinaryLayoutCache)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
        {
            GTEST_SKIP() << "Tests Vulkan Args layout isolation.";
        }
        const GVM::RHI::PipelineLayoutDescriptor invalid{.arguments = {.byteSize = 1024u * 1024u + 8}};
        EXPECT_THROW(device->createPipelineLayout(invalid), std::invalid_argument);
        auto ordinary = device->createPipelineLayout({});
        auto arguments = device->createPipelineLayout({.arguments = {.byteSize = 8}});
        ASSERT_NE(ordinary, nullptr);
        ASSERT_NE(arguments, nullptr);
        EXPECT_NE(arguments, ordinary);
        EXPECT_EQ(arguments, device->createPipelineLayout({.arguments = {.byteSize = 8}}));
        EXPECT_THROW(device->createPipelineLayout(invalid), std::invalid_argument);
    }

    /** Rejects an ordinary shader paired with an Args layout instead of silently sharing an incompatible pipeline cache entry. */
    TEST_F(RhiPipelineTest, VulkanRejectsLegacyShaderWithArgumentsLayout)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
        {
            GTEST_SKIP() << "Tests Vulkan Args shader compatibility.";
        }
        auto vertex = device->createShaderModule({.spirv = eastl::vector<uint32_t>(
            kCacheStressVertexSpirv, kCacheStressVertexSpirv + sizeof(kCacheStressVertexSpirv) / sizeof(uint32_t))});
        auto fragment = device->createShaderModule({.spirv = eastl::vector<uint32_t>(
            kCacheStressFragmentSpirv, kCacheStressFragmentSpirv + sizeof(kCacheStressFragmentSpirv) / sizeof(uint32_t))});
        auto layout = device->createPipelineLayout({.arguments = {.byteSize = 8}});
        const auto descriptor = makePipelineCompatibleCacheKeyDescriptor(layout, vertex, fragment,
            "InvalidLegacyShaderArgsLayout", false, false, {});
        EXPECT_THROW(device->createRenderPipeline(descriptor), std::invalid_argument);
    }

    /** Detects duplicate Metal layout bindings during actual descriptor matching without a temporary table. */
    TEST_F(RhiPipelineTest, MetalRejectsDuplicateLayoutBindingsWhenCreatingBindGroup)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Metal)
        {
            GTEST_SKIP() << "Tests Metal descriptor matching.";
        }
        auto buffer = device->createBuffer({.usage = GVM::RHI::BufferUsage::Storage, .size = 16});
        auto layout = device->createBindGroupLayout({.entries = {
            {.binding = 2, .visibility = GVM::RHI::ShaderStage::Compute, .buffer = {.type = GVM::RHI::BufferBindingType::Storage, .access = GVM::RHI::StorageBufferAccess::ReadWrite}},
            {.binding = 2, .visibility = GVM::RHI::ShaderStage::Compute, .buffer = {.type = GVM::RHI::BufferBindingType::Storage, .access = GVM::RHI::StorageBufferAccess::ReadWrite}},
        }});
        const GVM::RHI::BindGroupDescriptor descriptor{.layout = layout, .entries = {
            {.binding = 2, .buffer = GVM::RHI::BufferRange(buffer)},
            {.binding = 7, .buffer = GVM::RHI::BufferRange(buffer)},
        }};
        EXPECT_THROW(device->createBindGroup(descriptor), std::invalid_argument);
    }

    TEST_F(RhiPipelineTest, VulkanReusesPrebuiltPipelineAcrossCompatibleLoadStorePasses)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
            GTEST_SKIP() << "Tests Vulkan native pipeline reuse.";
        auto vertexShader = device->createShaderModule({
            .label = "PipelineCacheKeyVertexShader",
            .spirv = eastl::vector<uint32_t>(
                kCacheStressVertexSpirv,
                kCacheStressVertexSpirv + sizeof(kCacheStressVertexSpirv) / sizeof(kCacheStressVertexSpirv[0])),
        });
        ASSERT_TRUE(static_cast<bool>(vertexShader));

        auto fragmentShader = device->createShaderModule({
            .label = "PipelineCacheKeyFragmentShader",
            .spirv = eastl::vector<uint32_t>(
                kCacheStressFragmentSpirv,
                kCacheStressFragmentSpirv + sizeof(kCacheStressFragmentSpirv) / sizeof(kCacheStressFragmentSpirv[0])),
        });
        ASSERT_TRUE(static_cast<bool>(fragmentShader));

        auto pipelineLayout = device->createPipelineLayout({
            .label = "PipelineCacheKeyPipelineLayout",
            .bindGroupLayouts = {},
        });
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));

        auto descriptor = makePipelineCompatibleCacheKeyDescriptor(pipelineLayout, vertexShader, fragmentShader,
                                                                  "CompatiblePipeline", false, false, {});
        auto pipeline = device->createRenderPipeline(descriptor);
        ASSERT_EQ(GVM::RHI::Vulkan::Testing::getNativePipelineCount(pipeline), 1u);
        eastl::vector<GVM::RHI::Texture> textures;
        GVM::RHI::RenderPassDescriptor pass = {};
        pass.label = "CompatibleLoadStorePass";
        for (uint32_t index = 0; index < 4; ++index)
        {
            auto texture = device->createTexture({.label = "CompatiblePassColor",
                .usage = GVM::RHI::TextureUsage::RenderAttachment,
                .dimension = GVM::RHI::TextureDimension::e2D,
                .size = {16, 16, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .mipLevelCount = 1, .arrayLayerCount = 1});
            textures.push_back(texture);
            pass.colorAttachments.push_back({.view = texture->createView(), .loadOp = GVM::RHI::LoadOp::Clear,
                                              .storeOp = GVM::RHI::StoreOp::Store});
        }
        auto encoder = device->getMainQueue()->createCommandEncoder();
        for (uint32_t iteration = 0; iteration < 3; ++iteration)
        {
            for (auto &attachment : pass.colorAttachments)
            {
                attachment.loadOp = iteration == 0 ? GVM::RHI::LoadOp::Clear : GVM::RHI::LoadOp::Load;
                attachment.storeOp = iteration == 2 ? GVM::RHI::StoreOp::Discard : GVM::RHI::StoreOp::Store;
            }
            auto render = encoder->beginRenderPass(pass);
            render->setPipeline(pipeline);
            render->draw(3u, 1u, 0u, 0u);
            render->end();
        }
        EXPECT_EQ(GVM::RHI::Vulkan::Testing::getNativePipelineCount(pipeline), 1u);
        encoder->end();
        submitAndWait({encoder});
        for (auto texture : textures)
            device->freeTexture(texture);
    }

    TEST_F(RhiPipelineTest, VulkanPipelineCompatibleRenderPassCacheSeparatesPixelLocalSignatures)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
        {
            GTEST_SKIP() << "This cache key test targets Vulkan pipeline-compatible render pass signatures.";
        }
        ASSERT_TRUE(GVM::RHI::Vulkan::Testing::isVulkanDevice(device));

        auto vertexShader = device->createShaderModule({
            .label = "PipelineCacheKeyVertexShader",
            .spirv = eastl::vector<uint32_t>(
                kCacheStressVertexSpirv,
                kCacheStressVertexSpirv + sizeof(kCacheStressVertexSpirv) / sizeof(kCacheStressVertexSpirv[0])),
        });
        ASSERT_TRUE(static_cast<bool>(vertexShader));

        auto fragmentShader = device->createShaderModule({
            .label = "PipelineCacheKeyFragmentShader",
            .spirv = eastl::vector<uint32_t>(
                kCacheStressFragmentSpirv,
                kCacheStressFragmentSpirv + sizeof(kCacheStressFragmentSpirv) / sizeof(kCacheStressFragmentSpirv[0])),
        });
        ASSERT_TRUE(static_cast<bool>(fragmentShader));

        auto pipelineLayout = device->createPipelineLayout({
            .label = "PipelineCacheKeyPipelineLayout",
            .bindGroupLayouts = {},
        });
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));

        const size_t baselineCacheSize =
            GVM::RHI::Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize(device);

        auto ordinaryA = device->createRenderPipeline(makePipelineCompatibleCacheKeyDescriptor(
            pipelineLayout,
            vertexShader,
            fragmentShader,
            "PipelineCacheKey_Ordinary_A",
            false,
            false,
            {}));
        ASSERT_TRUE(static_cast<bool>(ordinaryA));
        EXPECT_EQ(
            GVM::RHI::Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize(device),
            baselineCacheSize + 1u);

        auto ordinaryB = device->createRenderPipeline(makePipelineCompatibleCacheKeyDescriptor(
            pipelineLayout,
            vertexShader,
            fragmentShader,
            "PipelineCacheKey_Ordinary_B",
            false,
            false,
            {}));
        ASSERT_TRUE(static_cast<bool>(ordinaryB));
        EXPECT_EQ(
            GVM::RHI::Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize(device),
            baselineCacheSize + 1u);

        auto pixelLocalRasterA = device->createRenderPipeline(makePipelineCompatibleCacheKeyDescriptor(
            pipelineLayout,
            vertexShader,
            fragmentShader,
            "PipelineCacheKey_PixelLocalRaster_A",
            true,
            false,
            {}));
        ASSERT_TRUE(static_cast<bool>(pixelLocalRasterA));
        EXPECT_EQ(
            GVM::RHI::Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize(device),
            baselineCacheSize + 2u);

        auto pixelLocalRasterB = device->createRenderPipeline(makePipelineCompatibleCacheKeyDescriptor(
            pipelineLayout,
            vertexShader,
            fragmentShader,
            "PipelineCacheKey_PixelLocalRaster_B",
            true,
            false,
            {}));
        ASSERT_TRUE(static_cast<bool>(pixelLocalRasterB));
        EXPECT_EQ(
            GVM::RHI::Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize(device),
            baselineCacheSize + 2u);

        GVM::RHI::PixelLocalPassAttachmentAccess explicitWriteOne = {};
        explicitWriteOne.colorWriteMask = 0x1u;
        auto pixelLocalExplicitOneA = device->createRenderPipeline(makePipelineCompatibleCacheKeyDescriptor(
            pipelineLayout,
            vertexShader,
            fragmentShader,
            "PipelineCacheKey_PixelLocalExplicitOne_A",
            true,
            true,
            explicitWriteOne));
        ASSERT_TRUE(static_cast<bool>(pixelLocalExplicitOneA));
        EXPECT_EQ(
            GVM::RHI::Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize(device),
            baselineCacheSize + 3u);

        auto pixelLocalExplicitOneB = device->createRenderPipeline(makePipelineCompatibleCacheKeyDescriptor(
            pipelineLayout,
            vertexShader,
            fragmentShader,
            "PipelineCacheKey_PixelLocalExplicitOne_B",
            true,
            true,
            explicitWriteOne));
        ASSERT_TRUE(static_cast<bool>(pixelLocalExplicitOneB));
        EXPECT_EQ(
            GVM::RHI::Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize(device),
            baselineCacheSize + 3u);

        GVM::RHI::PixelLocalPassAttachmentAccess explicitWriteTwo = {};
        explicitWriteTwo.colorWriteMask = 0x3u;
        auto pixelLocalExplicitTwo = device->createRenderPipeline(makePipelineCompatibleCacheKeyDescriptor(
            pipelineLayout,
            vertexShader,
            fragmentShader,
            "PipelineCacheKey_PixelLocalExplicitTwo",
            true,
            true,
            explicitWriteTwo));
        ASSERT_TRUE(static_cast<bool>(pixelLocalExplicitTwo));
        EXPECT_EQ(
            GVM::RHI::Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize(device),
            baselineCacheSize + 4u);
    }

    TEST_F(RhiPipelineTest, VulkanPixelLocalDependenciesCoverClearReadAndReadWriteHazards)
    {
        constexpr uint32_t ExternalSubpass = GVM::RHI::Vulkan::Testing::ExternalSubpass;
        const eastl::vector<GVM::RHI::Vulkan::Testing::PixelLocalDependencyColorAttachment> colors =
            makePixelLocalDependencyColors(1u, GVM::RHI::LoadOp::Clear);
        GVM::RHI::Vulkan::Testing::PixelLocalDependencyDepthAttachment depth = {};

        eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> accesses(3u);
        accesses[1].colorReadMask = 0x1u;
        accesses[2].colorWriteMask = 0x1u;

        const eastl::vector<GVM::RHI::Vulkan::Testing::PixelLocalSubpassDependencySnapshot> dependencies =
            GVM::RHI::Vulkan::Testing::buildPixelLocalSubpassDependenciesForTesting(
                colors,
                depth,
                3u,
                accesses);

        const auto *externalToClear = findPixelLocalDependency(dependencies, ExternalSubpass, 0u);
        ASSERT_NE(externalToClear, nullptr);
        EXPECT_TRUE(externalToClear->destinationColorAttachmentOutputStage);
        EXPECT_TRUE(externalToClear->destinationColorAttachmentWriteAccess);

        const auto *clearToRead = findPixelLocalDependency(dependencies, 0u, 1u);
        ASSERT_NE(clearToRead, nullptr);
        EXPECT_TRUE(clearToRead->sourceColorAttachmentOutputStage);
        EXPECT_TRUE(clearToRead->sourceColorAttachmentWriteAccess);
        EXPECT_TRUE(clearToRead->destinationFragmentShaderStage);
        EXPECT_TRUE(clearToRead->destinationInputAttachmentReadAccess);

        const auto *readToWrite = findPixelLocalDependency(dependencies, 1u, 2u);
        ASSERT_NE(readToWrite, nullptr);
        EXPECT_TRUE(readToWrite->sourceFragmentShaderStage);
        EXPECT_TRUE(readToWrite->sourceInputAttachmentReadAccess);
        EXPECT_TRUE(readToWrite->destinationColorAttachmentOutputStage);
        EXPECT_TRUE(readToWrite->destinationColorAttachmentReadAccess);
        EXPECT_TRUE(readToWrite->destinationColorAttachmentWriteAccess);
    }

    TEST_F(RhiPipelineTest, VulkanPixelLocalDependenciesCoverPreserveGapAndReadOnlyFinalUse)
    {
        constexpr uint32_t ExternalSubpass = GVM::RHI::Vulkan::Testing::ExternalSubpass;
        const eastl::vector<GVM::RHI::Vulkan::Testing::PixelLocalDependencyColorAttachment> colors =
            makePixelLocalDependencyColors(1u, GVM::RHI::LoadOp::Undefined);
        GVM::RHI::Vulkan::Testing::PixelLocalDependencyDepthAttachment depth = {};

        eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> accesses(3u);
        accesses[0].colorWriteMask = 0x1u;
        accesses[2].colorReadMask = 0x1u;

        const eastl::vector<GVM::RHI::Vulkan::Testing::PixelLocalSubpassDependencySnapshot> dependencies =
            GVM::RHI::Vulkan::Testing::buildPixelLocalSubpassDependenciesForTesting(
                colors,
                depth,
                3u,
                accesses);

        const auto *writeToLaterRead = findPixelLocalDependency(dependencies, 0u, 2u);
        ASSERT_NE(writeToLaterRead, nullptr);
        EXPECT_TRUE(writeToLaterRead->sourceColorAttachmentOutputStage);
        EXPECT_TRUE(writeToLaterRead->sourceColorAttachmentWriteAccess);
        EXPECT_TRUE(writeToLaterRead->destinationFragmentShaderStage);
        EXPECT_TRUE(writeToLaterRead->destinationInputAttachmentReadAccess);
        EXPECT_TRUE(writeToLaterRead->destinationColorAttachmentOutputStage);
        EXPECT_TRUE(writeToLaterRead->destinationColorAttachmentWriteAccess);
        EXPECT_EQ(findPixelLocalDependency(dependencies, 1u, 2u), nullptr);

        const auto *readOnlyFinal = findPixelLocalDependency(dependencies, 2u, ExternalSubpass);
        ASSERT_NE(readOnlyFinal, nullptr);
        EXPECT_TRUE(readOnlyFinal->sourceFragmentShaderStage);
        EXPECT_TRUE(readOnlyFinal->sourceInputAttachmentReadAccess);
        EXPECT_TRUE(readOnlyFinal->sourceColorAttachmentOutputStage);
        EXPECT_TRUE(readOnlyFinal->sourceColorAttachmentWriteAccess);
    }

    TEST_F(RhiPipelineTest, VulkanPixelLocalDependenciesMergeIndependentAttachmentLastUsesBySubpass)
    {
        constexpr uint32_t ExternalSubpass = GVM::RHI::Vulkan::Testing::ExternalSubpass;
        const eastl::vector<GVM::RHI::Vulkan::Testing::PixelLocalDependencyColorAttachment> colors =
            makePixelLocalDependencyColors(3u, GVM::RHI::LoadOp::Undefined);
        GVM::RHI::Vulkan::Testing::PixelLocalDependencyDepthAttachment depth = {};

        eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> accesses(3u);
        accesses[0].colorWriteMask = 0x5u;
        accesses[1].colorReadMask = 0x1u;
        accesses[1].colorWriteMask = 0x2u;
        accesses[2].colorReadMask = 0x4u;

        const eastl::vector<GVM::RHI::Vulkan::Testing::PixelLocalSubpassDependencySnapshot> dependencies =
            GVM::RHI::Vulkan::Testing::buildPixelLocalSubpassDependenciesForTesting(
                colors,
                depth,
                3u,
                accesses);

        const auto *aWriteToRead = findPixelLocalDependency(dependencies, 0u, 1u);
        ASSERT_NE(aWriteToRead, nullptr);
        EXPECT_TRUE(aWriteToRead->sourceColorAttachmentWriteAccess);
        EXPECT_TRUE(aWriteToRead->destinationInputAttachmentReadAccess);
        EXPECT_TRUE(aWriteToRead->destinationColorAttachmentWriteAccess);

        const auto *cWriteToLaterRead = findPixelLocalDependency(dependencies, 0u, 2u);
        ASSERT_NE(cWriteToLaterRead, nullptr);
        EXPECT_TRUE(cWriteToLaterRead->sourceColorAttachmentWriteAccess);
        EXPECT_TRUE(cWriteToLaterRead->destinationInputAttachmentReadAccess);
        EXPECT_TRUE(cWriteToLaterRead->destinationColorAttachmentWriteAccess);

        const auto *mixedFinal = findPixelLocalDependency(dependencies, 1u, ExternalSubpass);
        ASSERT_NE(mixedFinal, nullptr);
        EXPECT_TRUE(mixedFinal->sourceInputAttachmentReadAccess);
        EXPECT_TRUE(mixedFinal->sourceColorAttachmentWriteAccess);

        const auto *readOnlyFinal = findPixelLocalDependency(dependencies, 2u, ExternalSubpass);
        ASSERT_NE(readOnlyFinal, nullptr);
        EXPECT_TRUE(readOnlyFinal->sourceInputAttachmentReadAccess);
        EXPECT_TRUE(readOnlyFinal->sourceColorAttachmentOutputStage);
        EXPECT_TRUE(readOnlyFinal->sourceColorAttachmentWriteAccess);
    }

    TEST_F(RhiPipelineTest, VulkanAaaCityScaleRenderGraphCachesDoNotGrowAfterWarmup)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
        {
            GTEST_SKIP() << "This cache pressure test targets Vulkan renderpass/framebuffer/native-pipeline caches.";
        }
        ASSERT_TRUE(GVM::RHI::Vulkan::Testing::isVulkanDevice(device));

        std::set<uint64_t> renderPassRecipeKeys;
        for (uint32_t signatureIndex = 0u; signatureIndex < kCachePressureRenderPassSignatures; ++signatureIndex)
        {
            renderPassRecipeKeys.insert(cachePressureRenderPassSignatureKey(signatureIndex));
        }
        ASSERT_EQ(renderPassRecipeKeys.size(), kCachePressureRenderPassSignatures);

        auto vertexShader = device->createShaderModule({
            .label = "CachePressureVertexShader",
            .spirv = eastl::vector<uint32_t>(
                kCacheStressVertexSpirv,
                kCacheStressVertexSpirv + sizeof(kCacheStressVertexSpirv) / sizeof(kCacheStressVertexSpirv[0])),
        });
        ASSERT_TRUE(static_cast<bool>(vertexShader));

        auto fragmentShader = device->createShaderModule({
            .label = "CachePressureFragmentShader",
            .spirv = eastl::vector<uint32_t>(
                kCacheStressFragmentSpirv,
                kCacheStressFragmentSpirv + sizeof(kCacheStressFragmentSpirv) / sizeof(kCacheStressFragmentSpirv[0])),
        });
        ASSERT_TRUE(static_cast<bool>(fragmentShader));

        auto pipelineLayout = device->createPipelineLayout({
            .label = "CachePressurePipelineLayout",
            .bindGroupLayouts = {},
        });
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));

        std::vector<GVM::RHI::RenderPipeline> pipelines;
        pipelines.reserve(kCachePressurePipelineVariants);
        for (uint32_t mask = 1u; mask <= kCachePressureWriterPipelineVariants; ++mask)
        {
            pipelines.push_back(device->createRenderPipeline(
                makeCachePressurePipelineDescriptor(pipelineLayout, vertexShader, fragmentShader, mask, mask - 1u)));
            ASSERT_TRUE(static_cast<bool>(pipelines.back()));
        }
        for (uint32_t noopIndex = 0u; noopIndex < kCachePressureNoopPipelineVariants; ++noopIndex)
        {
            pipelines.push_back(device->createRenderPipeline(
                makeCachePressurePipelineDescriptor(
                    pipelineLayout,
                    vertexShader,
                    fragmentShader,
                    0u,
                    kCachePressureWriterPipelineVariants + noopIndex)));
            ASSERT_TRUE(static_cast<bool>(pipelines.back()));
        }
        ASSERT_EQ(pipelines.size(), kCachePressurePipelineVariants);

        std::vector<CachePressureFramebuffer> framebuffers(kCachePressureFramebufferVariants);
        for (uint32_t framebufferIndex = 0u; framebufferIndex < kCachePressureFramebufferVariants; ++framebufferIndex)
        {
            for (uint32_t colorIndex = 0u; colorIndex < 4u; ++colorIndex)
            {
                char textureLabel[128] = {};
                std::snprintf(textureLabel, sizeof(textureLabel), "CachePressureFramebuffer_%u_Color_%u", framebufferIndex, colorIndex);
                framebuffers[framebufferIndex].textures[colorIndex] = device->createTexture({
                    .label = textureLabel,
                    .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::PixelLocalAttachment,
                    .dimension = GVM::RHI::TextureDimension::e2D,
                    .size = {32u, 32u, 1u},
                    .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                    .mipLevelCount = 1u,
                    .arrayLayerCount = 1u,
                });
                ASSERT_FALSE(framebuffers[framebufferIndex].textures[colorIndex].isNull());
                framebuffers[framebufferIndex].views[colorIndex] =
                    framebuffers[framebufferIndex].textures[colorIndex]->createView();
                ASSERT_FALSE(framebuffers[framebufferIndex].views[colorIndex].isNull());
                // Discard signatures exercise actual transient attachments; stored signatures
                // retain persistent backing. Both retain the same cache-key pressure coverage.
                framebuffers[framebufferIndex].transientTextures[colorIndex] = device->createTexture({
                    .label = textureLabel,
                    .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::PixelLocalAttachment,
                    .dimension = GVM::RHI::TextureDimension::e2D,
                    .size = {32u, 32u, 1u},
                    .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                    .mipLevelCount = 1u,
                    .arrayLayerCount = 1u,
                    .storageMode = GVM::RHI::TextureStorageMode::TransientAttachment,
                });
                framebuffers[framebufferIndex].transientViews[colorIndex] =
                    framebuffers[framebufferIndex].transientTextures[colorIndex]->createView();
            }
        }

        const auto encodeSweep = [&](const char *sweepLabel)
        {
            std::set<uint64_t> nativePipelineCombos;
            auto commandEncoder = device->getMainQueue()->createCommandEncoder();
            if (!commandEncoder)
            {
                ADD_FAILURE() << "Failed to create command encoder for cache pressure sweep.";
                return size_t{0u};
            }

            for (uint32_t framebufferIndex = 0u; framebufferIndex < kCachePressureFramebufferVariants; ++framebufferIndex)
            {
                const CachePressureFramebuffer &framebuffer = framebuffers[framebufferIndex];
                for (uint32_t signatureIndex = 0u; signatureIndex < kCachePressureRenderPassSignatures; ++signatureIndex)
                {
                    auto renderPass = commandEncoder->beginRenderPass(
                        makeCachePressureRenderPassDescriptor(framebuffer, signatureIndex));
                    if (!renderPass)
                    {
                        ADD_FAILURE() << "Failed to begin cache pressure render pass.";
                        return size_t{0u};
                    }

                    for (uint32_t subpassIndex = 0u; subpassIndex < kCachePressureSubpasses; ++subpassIndex)
                    {
                        const uint32_t writerPipelineIndex =
                            cachePressureWriterMask(signatureIndex, subpassIndex) - 1u;
                        renderPass->setPipeline(pipelines[writerPipelineIndex]);
                        renderPass->draw(3u, 1u, 0u, 0u);
                        nativePipelineCombos.insert(
                            (uint64_t{writerPipelineIndex} << 32u) |
                            (uint64_t{signatureIndex} << 16u) |
                            uint64_t{subpassIndex});

                        for (uint32_t drawIndex = 0u; drawIndex < kCachePressureNoopDrawsPerSubpass; ++drawIndex)
                        {
                            const uint32_t noopPipelineIndex =
                                cachePressureNoopPipelineIndex(signatureIndex, subpassIndex, drawIndex);
                            renderPass->setPipeline(pipelines[noopPipelineIndex]);
                            renderPass->draw(3u, 1u, 0u, 0u);
                            nativePipelineCombos.insert(
                                (uint64_t{noopPipelineIndex} << 32u) |
                                (uint64_t{signatureIndex} << 16u) |
                                uint64_t{subpassIndex});
                        }

                        if (subpassIndex + 1u < kCachePressureSubpasses)
                        {
                            renderPass->nextPixelLocalPass();
                        }
                    }
                    renderPass->end();
                }
            }

            commandEncoder->end();
            eastl::vector<GVM::RHI::CommandEncoder> encoders(1u);
            encoders[0] = commandEncoder;
            submitAndWait(encoders);
            std::printf(
                "[gvm-cache-pressure] sweep=%s framebufferVariants=%u renderPassSignatures=%u subpasses=%u pipelines=%u nativeCombos=%zu publishedBuildings=%u publishedVehicles=%u publishedPedestrians=%u\n",
                sweepLabel,
                kCachePressureFramebufferVariants,
                kCachePressureRenderPassSignatures,
                kCachePressureSubpasses,
                kCachePressurePipelineVariants,
                nativePipelineCombos.size(),
                kAaaCitySamplePublishedBuildings,
                kAaaCitySamplePublishedVehicles,
                kAaaCitySamplePublishedPedestrians);
            return nativePipelineCombos.size();
        };

        const size_t expectedNativePipelineCacheSize = encodeSweep("warmup");
        const CachePressureStats warmupStats = readCachePressureStats(device, pipelines);

        EXPECT_EQ(warmupStats.renderPassCacheSize, kCachePressureRenderPassSignatures);
        EXPECT_EQ(
            warmupStats.framebufferCacheSize,
            static_cast<size_t>(kCachePressureFramebufferVariants) * kCachePressureRenderPassSignatures);
        EXPECT_EQ(warmupStats.nativePipelineCacheSize, expectedNativePipelineCacheSize);
        EXPECT_GT(warmupStats.nativePipelineCacheSize, 0u);

        const size_t replayNativePipelineCombos = encodeSweep("replay");
        const CachePressureStats replayStats = readCachePressureStats(device, pipelines);

        EXPECT_EQ(replayNativePipelineCombos, expectedNativePipelineCacheSize);
        EXPECT_EQ(replayStats.renderPassCacheSize, warmupStats.renderPassCacheSize);
        EXPECT_EQ(replayStats.framebufferCacheSize, warmupStats.framebufferCacheSize);
        EXPECT_EQ(
            replayStats.pipelineCompatibleRenderPassCacheSize,
            warmupStats.pipelineCompatibleRenderPassCacheSize);
        EXPECT_EQ(replayStats.nativePipelineCacheSize, warmupStats.nativePipelineCacheSize);

        std::printf(
            "[gvm-cache-pressure] warmup renderPassCache=%zu framebufferCache=%zu pipelineCompatibleRenderPassCache=%zu nativePipelineCache=%zu\n",
            warmupStats.renderPassCacheSize,
            warmupStats.framebufferCacheSize,
            warmupStats.pipelineCompatibleRenderPassCacheSize,
            warmupStats.nativePipelineCacheSize);
        std::printf(
            "[gvm-cache-pressure] replay renderPassCache=%zu framebufferCache=%zu pipelineCompatibleRenderPassCache=%zu nativePipelineCache=%zu\n",
            replayStats.renderPassCacheSize,
            replayStats.framebufferCacheSize,
            replayStats.pipelineCompatibleRenderPassCacheSize,
            replayStats.nativePipelineCacheSize);
    }

    /** Fills descriptor with valid module content for the active backend. */
    void fillDedupShaderContent(GVM::RHI::ShaderModuleDescriptor &descriptor, GVM::RHI::GraphicsBackend backend, bool variant)
    {
        if (backend == GVM::RHI::GraphicsBackend::Vulkan)
        {
            const uint32_t *begin = variant ? kCacheStressFragmentSpirv : kCacheStressVertexSpirv;
            const uint32_t *end = variant
                ? kCacheStressFragmentSpirv + sizeof(kCacheStressFragmentSpirv) / sizeof(uint32_t)
                : kCacheStressVertexSpirv + sizeof(kCacheStressVertexSpirv) / sizeof(uint32_t);
            descriptor.spirv.assign(begin, end);
        }
        else
        {
            descriptor.code = kRenderShaderCode;
            if (variant)
            {
                descriptor.code += "\n// dedup variant\n";
            }
        }
    }

    /** Returns a compute shader module with valid content for the active backend. */
    GVM::RHI::ShaderModule createDedupComputeModule(GVM::RHI::Device device, GVM::RHI::GraphicsBackend backend, const char *label)
    {
        GVM::RHI::ShaderModuleDescriptor descriptor = {};
        descriptor.label = label;
        if (backend == GVM::RHI::GraphicsBackend::Vulkan)
        {
            descriptor.spirv.assign(kDedupComputeSpirv, kDedupComputeSpirv + sizeof(kDedupComputeSpirv) / sizeof(uint32_t));
        }
        else
        {
            descriptor.code = kDedupComputeShaderCode;
        }
        return device->createShaderModule(descriptor);
    }

    /** Returns the compute stage entry point name valid for the active backend. */
    const char *dedupComputeEntryPoint(GVM::RHI::GraphicsBackend backend)
    {
        return backend == GVM::RHI::GraphicsBackend::Vulkan ? "main" : "dedup_main";
    }

    /** Builds a fragment-visible storage-buffer (binding 0) or sampled-texture layout entry for dedup tests. */
    GVM::RHI::BindGroupLayoutEntry makeDedupLayoutEntry(uint32_t binding)
    {
        GVM::RHI::BindGroupLayoutEntry entry = {};
        entry.binding = binding;
        entry.visibility = GVM::RHI::ShaderStage::Fragment;
        if (binding == 0u)
        {
            entry.buffer.type = GVM::RHI::BufferBindingType::Storage;
            entry.buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
        }
        else
        {
            entry.texture.sampleType = GVM::RHI::TextureSampleType::Float;
            entry.texture.viewDimension = GVM::RHI::TextureViewDimension::e2D;
        }
        return entry;
    }

    TEST_F(RhiPipelineTest, DeduplicatesShaderModulesIgnoringLabel)
    {
        const GVM::RHI::GraphicsBackend backend = instance->getBackend();
        GVM::RHI::ShaderModuleDescriptor descriptor = {};
        descriptor.label = "DedupShaderModuleFirst";
        fillDedupShaderContent(descriptor, backend, false);
        auto first = device->createShaderModule(descriptor);
        ASSERT_TRUE(static_cast<bool>(first));

        descriptor.label = "DedupShaderModuleSecond";
        auto second = device->createShaderModule(descriptor);
        ASSERT_TRUE(static_cast<bool>(second));
        EXPECT_EQ(first.get(), second.get());

        descriptor.label = "DedupShaderModuleDifferent";
        fillDedupShaderContent(descriptor, backend, true);
        auto different = device->createShaderModule(descriptor);
        ASSERT_TRUE(static_cast<bool>(different));
        EXPECT_NE(first.get(), different.get());
    }

    TEST_F(RhiPipelineTest, DeduplicatesBindGroupLayoutsBySortedEntries)
    {
        GVM::RHI::BindGroupLayoutDescriptor descriptor = {};
        descriptor.label = "DedupBindGroupLayoutFirst";
        descriptor.entries = {makeDedupLayoutEntry(0u), makeDedupLayoutEntry(1u)};
        auto first = device->createBindGroupLayout(descriptor);
        ASSERT_TRUE(static_cast<bool>(first));

        descriptor.label = "DedupBindGroupLayoutReordered";
        descriptor.entries = {makeDedupLayoutEntry(1u), makeDedupLayoutEntry(0u)};
        auto reordered = device->createBindGroupLayout(descriptor);
        ASSERT_TRUE(static_cast<bool>(reordered));
        EXPECT_EQ(first.get(), reordered.get());

        descriptor.label = "DedupBindGroupLayoutDifferentVisibility";
        auto vertexOnly = makeDedupLayoutEntry(0u);
        vertexOnly.visibility = GVM::RHI::ShaderStage::Vertex;
        descriptor.entries = {vertexOnly, makeDedupLayoutEntry(1u)};
        auto different = device->createBindGroupLayout(descriptor);
        ASSERT_TRUE(static_cast<bool>(different));
        EXPECT_NE(first.get(), different.get());
    }

    TEST_F(RhiPipelineTest, DeduplicatesPipelineLayoutsFromCanonicalChildren)
    {
        GVM::RHI::BindGroupLayoutDescriptor layoutDescriptor = {};
        layoutDescriptor.entries = {{
            .binding = 0u,
            .visibility = GVM::RHI::ShaderStage::Fragment,
            .texture = {
                .sampleType = GVM::RHI::TextureSampleType::Float,
                .viewDimension = GVM::RHI::TextureViewDimension::e2D,
            },
        }};
        auto firstLayout = device->createBindGroupLayout(layoutDescriptor);
        layoutDescriptor.label = "EquivalentSecondBindGroupLayout";
        auto secondLayout = device->createBindGroupLayout(layoutDescriptor);
        ASSERT_TRUE(static_cast<bool>(firstLayout));
        ASSERT_TRUE(static_cast<bool>(secondLayout));

        GVM::RHI::PipelineLayoutDescriptor pipelineDescriptor = {};
        pipelineDescriptor.label = "DedupPipelineLayoutFirst";
        pipelineDescriptor.bindGroupLayouts = {firstLayout};
        auto first = device->createPipelineLayout(pipelineDescriptor);
        ASSERT_TRUE(static_cast<bool>(first));

        pipelineDescriptor.label = "DedupPipelineLayoutSecond";
        pipelineDescriptor.bindGroupLayouts = {secondLayout};
        auto second = device->createPipelineLayout(pipelineDescriptor);
        ASSERT_TRUE(static_cast<bool>(second));
        EXPECT_EQ(first.get(), second.get());
    }

    TEST_F(RhiPipelineTest, DeduplicatesRenderAndComputePipelinesIgnoringLabel)
    {
        const GVM::RHI::GraphicsBackend backend = instance->getBackend();
        auto pipelineLayout = device->createPipelineLayout({.label = "DedupSharedLayout"});
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));

        GVM::RHI::ShaderModuleDescriptor vertexShaderDescriptor = {};
        fillDedupShaderContent(vertexShaderDescriptor, backend, false);
        auto vertexModule = device->createShaderModule(vertexShaderDescriptor);
        ASSERT_TRUE(static_cast<bool>(vertexModule));

        GVM::RHI::ShaderModuleDescriptor fragmentShaderDescriptor = {};
        fillDedupShaderContent(fragmentShaderDescriptor, backend, backend == GVM::RHI::GraphicsBackend::Vulkan);
        auto fragmentModule = device->createShaderModule(fragmentShaderDescriptor);
        ASSERT_TRUE(static_cast<bool>(fragmentModule));

        const char *vertexEntryPoint = backend == GVM::RHI::GraphicsBackend::Vulkan ? "main" : "vertex_main";
        const char *fragmentEntryPoint = backend == GVM::RHI::GraphicsBackend::Vulkan ? "main" : "fragment_main";
        GVM::RHI::RenderPipelineDescriptor renderDescriptor =
            makePipelineCompatibleCacheKeyDescriptor(pipelineLayout, vertexModule, fragmentModule,
                "DedupRenderPipelineFirst", false, false, {});
        renderDescriptor.vertex.entryPoint = vertexEntryPoint;
        renderDescriptor.fragment.entryPoint = fragmentEntryPoint;

        auto firstRender = device->createRenderPipeline(renderDescriptor);
        ASSERT_TRUE(static_cast<bool>(firstRender));

        renderDescriptor.label = "DedupRenderPipelineSecond";
        auto secondRender = device->createRenderPipeline(renderDescriptor);
        ASSERT_TRUE(static_cast<bool>(secondRender));
        EXPECT_EQ(firstRender.get(), secondRender.get());

        renderDescriptor.label = "DedupRenderPipelineCulled";
        renderDescriptor.primitive.cullMode = GVM::RHI::CullMode::Back;
        auto culledRender = device->createRenderPipeline(renderDescriptor);
        ASSERT_TRUE(static_cast<bool>(culledRender));
        EXPECT_NE(firstRender.get(), culledRender.get());

        auto computeModule = createDedupComputeModule(device, backend, "DedupComputeModule");
        ASSERT_TRUE(static_cast<bool>(computeModule));

        GVM::RHI::ComputePipelineDescriptor computeDescriptor = {};
        computeDescriptor.label = "DedupComputePipelineFirst";
        computeDescriptor.layout = pipelineLayout;
        computeDescriptor.compute.module = computeModule;
        computeDescriptor.compute.entryPoint = dedupComputeEntryPoint(backend);
        computeDescriptor.compute.workgroupX = 1u;
        computeDescriptor.compute.workgroupY = 1u;
        computeDescriptor.compute.workgroupZ = 1u;

        auto firstCompute = device->createComputePipeline(computeDescriptor);
        ASSERT_TRUE(static_cast<bool>(firstCompute));

        computeDescriptor.label = "DedupComputePipelineSecond";
        auto secondCompute = device->createComputePipeline(computeDescriptor);
        ASSERT_TRUE(static_cast<bool>(secondCompute));
        EXPECT_EQ(firstCompute.get(), secondCompute.get());
    }

    TEST_F(RhiPipelineTest, ReleasesUnreferencedDeviceObjectsAfterSubmit)
    {
        const GVM::RHI::GraphicsBackend backend = instance->getBackend();
        auto pipelineLayout = device->createPipelineLayout({.label = "ReleaseTestLayout"});
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));

        auto module = createDedupComputeModule(device, backend, "ReleaseTestModule");
        ASSERT_TRUE(static_cast<bool>(module));

        {
            auto pipeline = device->createComputePipeline({
                .label = "ReleaseTestPipeline",
                .layout = pipelineLayout,
                .compute = {
                    .module = module,
                    .entryPoint = dedupComputeEntryPoint(backend),
                    .workgroupX = 1u,
                    .workgroupY = 1u,
                    .workgroupZ = 1u,
                },
            });
            ASSERT_TRUE(static_cast<bool>(pipeline));
            EXPECT_GT(module->getReferenceCount(), 2);
        }

        for (int submitIndex = 0; submitIndex < 4; ++submitIndex)
        {
            device->getMainQueue()->submit({});
        }
        EXPECT_EQ(module->getReferenceCount(), 2);
    }

    /** Creates a small RGBA8 render-target texture usable on either backend for render-pass validation tests. */
    GVM::RHI::Texture createValidationAttachment(GVM::RHI::Device device, uint32_t width, uint32_t height, GVM::RHI::TextureUsageFlags extraUsage = GVM::RHI::TextureUsage::None)
    {
        return device->createTexture({
            .label = "RenderPassValidationAttachment",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | extraUsage,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {width, height, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
    }

    /** Creates four RGBA8 render-target textures and their views for multi-target render-pass validation tests. */
    void createValidationAttachments(GVM::RHI::Device device, GVM::RHI::TextureUsageFlags extraUsage,
        eastl::vector<GVM::RHI::Texture> &textures, eastl::vector<GVM::RHI::TextureView> &views)
    {
        for (uint32_t index = 0u; index < 4u; ++index)
        {
            auto texture = createValidationAttachment(device, 8u, 8u, extraUsage);
            views.push_back(texture->createView());
            textures.push_back(texture);
        }
    }

    /** Builds a clear/store render-pass descriptor over the given views, optionally pixel-local. */
    GVM::RHI::RenderPassDescriptor makeValidationPassDescriptor(
        const char *label, const eastl::vector<GVM::RHI::TextureView> &views, bool pixelLocal, uint32_t passCount)
    {
        GVM::RHI::RenderPassDescriptor descriptor = {};
        descriptor.label = label;
        for (const auto &view : views)
        {
            descriptor.colorAttachments.push_back({.view = view,
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store,
                .clearValue = {0.0, 0.0, 0.0, 0.0}, .pixelLocal = pixelLocal});
        }
        descriptor.pixelLocal.enabled = pixelLocal;
        descriptor.pixelLocal.passCount = passCount;
        return descriptor;
    }

    /** Releases every validation attachment texture created by createValidationAttachments. */
    void freeValidationAttachments(GVM::RHI::Device device, eastl::vector<GVM::RHI::Texture> &textures)
    {
        for (auto &texture : textures)
        {
            device->freeTexture(texture);
        }
    }

    /** Creates a four-target render pipeline valid on the active backend for render-pass validation tests. */
    GVM::RHI::RenderPipeline createValidationRenderPipeline(
        GVM::RHI::Device device,
        GVM::RHI::GraphicsBackend backend,
        GVM::RHI::PipelineLayout pipelineLayout,
        const char *label)
    {
        GVM::RHI::ShaderModuleDescriptor vertexShaderDescriptor = {};
        fillDedupShaderContent(vertexShaderDescriptor, backend, false);
        auto vertexModule = device->createShaderModule(vertexShaderDescriptor);
        GVM::RHI::ShaderModuleDescriptor fragmentShaderDescriptor = {};
        fillDedupShaderContent(fragmentShaderDescriptor, backend, backend == GVM::RHI::GraphicsBackend::Vulkan);
        auto fragmentModule = device->createShaderModule(fragmentShaderDescriptor);
        if (vertexModule == nullptr || fragmentModule == nullptr)
        {
            return nullptr;
        }

        GVM::RHI::RenderPipelineDescriptor descriptor = {};
        descriptor.label = label;
        descriptor.layout = pipelineLayout;
        descriptor.vertex.module = vertexModule;
        descriptor.vertex.entryPoint = backend == GVM::RHI::GraphicsBackend::Vulkan ? "main" : "vertex_main";
        descriptor.primitive.topology = GVM::RHI::PrimitiveTopology::TriangleList;
        descriptor.primitive.stripIndexFormat = GVM::RHI::IndexFormat::Undefined;
        descriptor.primitive.frontFace = GVM::RHI::FrontFace::CCW;
        descriptor.primitive.cullMode = GVM::RHI::CullMode::None;
        descriptor.fragment.module = fragmentModule;
        descriptor.fragment.entryPoint = backend == GVM::RHI::GraphicsBackend::Vulkan ? "main" : "fragment_main";
        descriptor.fragment.targets.reserve(4u);
        for (uint32_t colorIndex = 0u; colorIndex < 4u; ++colorIndex)
        {
            descriptor.fragment.targets.push_back({
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .blendEnabled = false,
                .writeMask = GVM::RHI::ColorWriteMask::All,
            });
        }
        return device->createRenderPipeline(descriptor);
    }

    /// Rejects a draw recorded without a bound pipeline on either backend.
    TEST_F(RhiPipelineTest, RejectsDrawWithoutPipelineAtRecordTime)
    {
        auto texture = createValidationAttachment(device, 8u, 8u);
        ASSERT_FALSE(texture.isNull());
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass({.label = "DrawWithoutPipeline",
            .colorAttachments = {{.view = texture->createView(),
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store}}});
        EXPECT_THROW(render->draw(3u, 1u, 0u, 0u), std::logic_error);
        render->end();
        encoder->end();
        device->freeTexture(texture);
    }

    /// Rejects a draw whose pipeline layout declares a bind group that was never set.
    TEST_F(RhiPipelineTest, RejectsDrawWithMissingBindGroupAtRecordTime)
    {
        const GVM::RHI::GraphicsBackend backend = instance->getBackend();
        auto bindGroupLayout = device->createBindGroupLayout({.label = "MissingGroupLayout",
            .entries = {makeDedupLayoutEntry(0u)}});
        ASSERT_TRUE(static_cast<bool>(bindGroupLayout));
        auto pipelineLayout = device->createPipelineLayout({.label = "MissingGroupPipelineLayout",
            .bindGroupLayouts = {bindGroupLayout}});
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));
        auto pipeline = createValidationRenderPipeline(device, backend, pipelineLayout, "MissingGroupPipeline");
        ASSERT_TRUE(static_cast<bool>(pipeline));

        eastl::vector<GVM::RHI::Texture> textures;
        eastl::vector<GVM::RHI::TextureView> views;
        createValidationAttachments(device, GVM::RHI::TextureUsage::None, textures, views);
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass(makeValidationPassDescriptor("DrawMissingBindGroup", views, false, 1u));
        render->setPipeline(pipeline);
        EXPECT_THROW(render->draw(3u, 1u, 0u, 0u), std::logic_error);
        render->end();
        encoder->end();
        freeValidationAttachments(device, textures);
    }

    /// Rejects a draw in a later pixel-local phase when no pipeline was selected for that phase.
    TEST_F(RhiPipelineTest, RejectsDrawWithoutPipelineInSecondPixelLocalPass)
    {
        const GVM::RHI::GraphicsBackend backend = instance->getBackend();
        auto pipelineLayout = device->createPipelineLayout({.label = "PixelLocalPhaseLayout"});
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));
        auto pipeline = createValidationRenderPipeline(device, backend, pipelineLayout, "PixelLocalPhasePipeline");
        ASSERT_TRUE(static_cast<bool>(pipeline));

        eastl::vector<GVM::RHI::Texture> textures;
        eastl::vector<GVM::RHI::TextureView> views;
        createValidationAttachments(device, GVM::RHI::TextureUsage::PixelLocalAttachment, textures, views);
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass(makeValidationPassDescriptor("PixelLocalPhasePass", views, true, 2u));
        render->setPipeline(pipeline);
        render->draw(3u, 1u, 0u, 0u);
        render->nextPixelLocalPass();
        EXPECT_THROW(render->draw(3u, 1u, 0u, 0u), std::logic_error);
        render->end();
        encoder->end();
        freeValidationAttachments(device, textures);
    }

    /// Rejects a storage-texture bind group when an earlier group already sampled the same texture.
    TEST_F(RhiPipelineTest, RejectsConflictingBindGroupTextureRolesAtRecordTime)
    {
        auto texture = device->createTexture({
            .label = "RoleConflictTexture",
            .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::StorageBinding,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {8u, 8u, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        ASSERT_FALSE(texture.isNull());
        auto view = texture->createView();
        ASSERT_FALSE(view.isNull());

        auto sampledLayout = device->createBindGroupLayout({.label = "SampledConflictLayout",
            .entries = {{
                .binding = 0u,
                .visibility = GVM::RHI::ShaderStage::Fragment,
                .texture = {.sampleType = GVM::RHI::TextureSampleType::Float,
                    .viewDimension = GVM::RHI::TextureViewDimension::e2D},
            }}});
        auto storageLayout = device->createBindGroupLayout({.label = "StorageConflictLayout",
            .entries = {{
                .binding = 0u,
                .visibility = GVM::RHI::ShaderStage::Fragment,
                .storageTexture = {.access = GVM::RHI::StorageTextureAccess::WriteOnly,
                    .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                    .viewDimension = GVM::RHI::TextureViewDimension::e2D},
            }}});
        ASSERT_TRUE(static_cast<bool>(sampledLayout));
        ASSERT_TRUE(static_cast<bool>(storageLayout));
        auto sampledGroup = device->createBindGroup({.label = "SampledConflictGroup",
            .layout = sampledLayout,
            .entries = {GVM::RHI::BindGroupEntry{.binding = 0u, .textureView = view}}});
        auto storageGroup = device->createBindGroup({.label = "StorageConflictGroup",
            .layout = storageLayout,
            .entries = {GVM::RHI::BindGroupEntry{.binding = 0u, .textureView = view}}});
        ASSERT_TRUE(static_cast<bool>(sampledGroup));
        ASSERT_TRUE(static_cast<bool>(storageGroup));

        auto target = createValidationAttachment(device, 8u, 8u);
        ASSERT_FALSE(target.isNull());
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass({.label = "RoleConflictPass",
            .colorAttachments = {{.view = target->createView(),
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store}}});
        render->setBindGroup(sampledGroup, 0u);
        EXPECT_THROW(render->setBindGroup(storageGroup, 1u), std::logic_error);
        EXPECT_THROW(render->setBindGroup(storageGroup, 1u), std::logic_error);
        render->end();
        encoder->end();
        device->freeTexture(target);
        device->freeTexture(texture);
    }

    /// Rejects a bind group that samples a texture already used as a pass attachment.
    TEST_F(RhiPipelineTest, RejectsBindGroupSamplingAnActiveAttachment)
    {
        auto texture = device->createTexture({
            .label = "AttachmentSampledTexture",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::TextureBinding,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {8u, 8u, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        ASSERT_FALSE(texture.isNull());
        auto sampledLayout = device->createBindGroupLayout({.label = "AttachmentSampledLayout",
            .entries = {{
                .binding = 0u,
                .visibility = GVM::RHI::ShaderStage::Fragment,
                .texture = {.sampleType = GVM::RHI::TextureSampleType::Float,
                    .viewDimension = GVM::RHI::TextureViewDimension::e2D},
            }}});
        ASSERT_TRUE(static_cast<bool>(sampledLayout));
        auto sampledGroup = device->createBindGroup({.label = "AttachmentSampledGroup",
            .layout = sampledLayout,
            .entries = {GVM::RHI::BindGroupEntry{.binding = 0u, .textureView = texture->createView()}}});
        ASSERT_TRUE(static_cast<bool>(sampledGroup));

        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass({.label = "AttachmentSampledPass",
            .colorAttachments = {{.view = texture->createView(),
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store}}});
        EXPECT_THROW(render->setBindGroup(sampledGroup, 0u), std::logic_error);
        render->end();
        encoder->end();
        device->freeTexture(texture);
    }

    /// Rejects indexed draws and indexed indirect draws recorded without an index buffer.
    TEST_F(RhiPipelineTest, RejectsIndexedDrawsWithoutIndexBufferAtRecordTime)
    {
        const GVM::RHI::GraphicsBackend backend = instance->getBackend();
        auto pipelineLayout = device->createPipelineLayout({.label = "MissingIndexLayout"});
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));
        auto pipeline = createValidationRenderPipeline(device, backend, pipelineLayout, "MissingIndexPipeline");
        ASSERT_TRUE(static_cast<bool>(pipeline));
        auto commands = device->createBuffer({.label = "MissingIndexCommands",
            .usage = GVM::RHI::BufferUsage::Indirect, .size = 16u});
        ASSERT_FALSE(commands.isNull());

        eastl::vector<GVM::RHI::Texture> textures;
        eastl::vector<GVM::RHI::TextureView> views;
        createValidationAttachments(device, GVM::RHI::TextureUsage::None, textures, views);
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass(makeValidationPassDescriptor("MissingIndexPass", views, false, 1u));
        render->setPipeline(pipeline);
        EXPECT_THROW(render->drawIndexed(3u, 1u, 0u, 0, 0u), std::logic_error);
        EXPECT_THROW(render->drawIndexedIndirect(GVM::RHI::BufferRange(commands), 1u, 0u), std::logic_error);
        render->end();
        encoder->end();
        device->freeBuffer(commands);
        freeValidationAttachments(device, textures);
    }

    /// Accepts attachments whose VIEW extents match even though parent texture extents differ.
    TEST_F(RhiPipelineTest, AcceptsAttachmentsMatchedByViewExtent)
    {
        auto colorTexture = device->createTexture({
            .label = "ViewExtentColor",
            .usage = GVM::RHI::TextureUsage::RenderAttachment,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {4u, 4u, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 2u,
        });
        ASSERT_FALSE(colorTexture.isNull());
        auto depthTexture = device->createTexture({
            .label = "ViewExtentDepth",
            .usage = GVM::RHI::TextureUsage::RenderAttachment,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {2u, 2u, 1u},
            .format = GVM::RHI::TextureFormat::Depth32Float,
        });
        ASSERT_FALSE(depthTexture.isNull());
        auto colorView = colorTexture->createView({
            .label = "ViewExtentColorMip1",
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .dimension = GVM::RHI::TextureViewDimension::e2D,
            .baseMipLevel = 1u,
            .mipLevelCount = 1u,
            .baseArrayLayer = 0u,
            .arrayLayerCount = 1u,
        });
        ASSERT_FALSE(colorView.isNull());

        auto queue = device->getMainQueue();
        auto encoder = queue->createCommandEncoder();
        auto render = encoder->beginRenderPass({.label = "ViewExtentPass",
            .colorAttachments = {{.view = colorView,
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store}},
            .depthStencilAttachment = {.view = depthTexture->createView(),
                .depthLoadOp = GVM::RHI::LoadOp::Clear, .depthStoreOp = GVM::RHI::StoreOp::Store,
                .depthClearValue = 1.0f}});
        render->end();
        encoder->end();
        queue->submit({encoder});
        device->freeTexture(colorTexture);
        device->freeTexture(depthTexture);
    }

    /// Rejects a render pass whose attachments do not share one extent.
    TEST_F(RhiPipelineTest, RejectsMismatchedAttachmentExtentsAtBeginRenderPass)
    {
        auto first = createValidationAttachment(device, 8u, 8u);
        auto second = createValidationAttachment(device, 16u, 16u);
        ASSERT_FALSE(first.isNull());
        ASSERT_FALSE(second.isNull());
        auto encoder = device->getMainQueue()->createCommandEncoder();
        EXPECT_THROW(encoder->beginRenderPass({.label = "MismatchedExtentPass",
            .colorAttachments = {
                {.view = first->createView(), .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store},
                {.view = second->createView(), .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store},
            }}), std::invalid_argument);
        encoder->end();
        device->freeTexture(first);
        device->freeTexture(second);
    }

    /// Proves a rejected draw leaves encoder state untouched: a valid draw still records, submits, and reads back.
    TEST_F(RhiPipelineTest, RejectedDrawLeavesEncoderStateUntouched)
    {
        const GVM::RHI::GraphicsBackend backend = instance->getBackend();
        auto pipelineLayout = device->createPipelineLayout({.label = "RejectedDrawLayout"});
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));
        auto pipeline = createValidationRenderPipeline(device, backend, pipelineLayout, "RejectedDrawPipeline");
        ASSERT_TRUE(static_cast<bool>(pipeline));

        eastl::vector<GVM::RHI::Texture> textures;
        eastl::vector<GVM::RHI::TextureView> views;
        createValidationAttachments(device, GVM::RHI::TextureUsage::CopySrc, textures, views);
        auto queue = device->getMainQueue();
        auto encoder = queue->createCommandEncoder();
        auto render = encoder->beginRenderPass(makeValidationPassDescriptor("RejectedDrawPass", views, false, 1u));
        render->setPipeline(pipeline);
        EXPECT_THROW(render->drawIndexed(3u, 1u, 0u, 0, 0u), std::logic_error);
        render->draw(3u, 1u, 0u, 0u);
        render->end();
        encoder->end();
        queue->submit({encoder});

        std::array<uint8_t, 8u * 8u * 4u> pixels = {};
        queue->readTexture({.texture = textures[0]}, pixels.data(), pixels.size(),
            {.bytesPerRow = 8u * 4u, .rowsPerImage = 8u}, {8u, 8u, 1u});
        queue->submit({});
        bool drewAnyPixel = false;
        for (const uint8_t channel : pixels)
        {
            drewAnyPixel = drewAnyPixel || channel != 0u;
        }
        EXPECT_TRUE(drewAnyPixel);
        freeValidationAttachments(device, textures);
    }

    /// Verifies both backends reject one dispatch whose bound groups assign sampled and storage roles to a single texture.
    TEST_F(RhiPipelineTest, RejectsComputeTextureRoleConflictAtDispatchTime)
    {
        auto texture = device->createTexture({
            .label = "ComputeRoleConflictTexture",
            .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::StorageBinding,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {8u, 8u, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        auto otherTexture = device->createTexture({
            .label = "ComputeRoleOtherTexture",
            .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::StorageBinding,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {8u, 8u, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        ASSERT_FALSE(texture.isNull());
        ASSERT_FALSE(otherTexture.isNull());
        auto view = texture->createView();
        auto otherView = otherTexture->createView();
        ASSERT_FALSE(view.isNull());
        ASSERT_FALSE(otherView.isNull());

        auto sampledLayout = device->createBindGroupLayout({.label = "ComputeRoleSampledLayout",
            .entries = {{
                .binding = 0u,
                .visibility = GVM::RHI::ShaderStage::Compute,
                .texture = {.sampleType = GVM::RHI::TextureSampleType::Float,
                    .viewDimension = GVM::RHI::TextureViewDimension::e2D},
            }}});
        auto storageLayout = device->createBindGroupLayout({.label = "ComputeRoleStorageLayout",
            .entries = {{
                .binding = 0u,
                .visibility = GVM::RHI::ShaderStage::Compute,
                .storageTexture = {.access = GVM::RHI::StorageTextureAccess::WriteOnly,
                    .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                    .viewDimension = GVM::RHI::TextureViewDimension::e2D},
            }}});
        ASSERT_TRUE(static_cast<bool>(sampledLayout));
        ASSERT_TRUE(static_cast<bool>(storageLayout));
        auto sampledGroup = device->createBindGroup({.label = "ComputeRoleSampledGroup",
            .layout = sampledLayout,
            .entries = {GVM::RHI::BindGroupEntry{.binding = 0u, .textureView = view}}});
        auto storageGroup = device->createBindGroup({.label = "ComputeRoleStorageGroup",
            .layout = storageLayout,
            .entries = {GVM::RHI::BindGroupEntry{.binding = 0u, .textureView = view}}});
        auto otherStorageGroup = device->createBindGroup({.label = "ComputeRoleOtherStorageGroup",
            .layout = storageLayout,
            .entries = {GVM::RHI::BindGroupEntry{.binding = 0u, .textureView = otherView}}});
        ASSERT_TRUE(static_cast<bool>(sampledGroup));
        ASSERT_TRUE(static_cast<bool>(storageGroup));
        ASSERT_TRUE(static_cast<bool>(otherStorageGroup));

        auto pipelineLayout = device->createPipelineLayout({.label = "ComputeRoleLayout",
            .bindGroupLayouts = {sampledLayout, storageLayout}});
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));
        const GVM::RHI::GraphicsBackend backend = instance->getBackend();
        auto module = createDedupComputeModule(device, backend, "ComputeRoleModule");
        ASSERT_TRUE(static_cast<bool>(module));
        auto pipeline = device->createComputePipeline({
            .label = "ComputeRolePipeline",
            .layout = pipelineLayout,
            .compute = {
                .module = module,
                .entryPoint = dedupComputeEntryPoint(backend),
                .workgroupX = 1u,
                .workgroupY = 1u,
                .workgroupZ = 1u,
            },
        });
        ASSERT_TRUE(static_cast<bool>(pipeline));

        auto queue = device->getMainQueue();
        auto encoder = queue->createCommandEncoder();
        auto computePass = encoder->beginComputePass({.label = "ComputeRoleConflictPass"});
        computePass->setPipeline(pipeline);
        computePass->setBindGroup(sampledGroup, 0u);
        computePass->setBindGroup(storageGroup, 1u);
        EXPECT_THROW(computePass->dispatchWorkgroups(1u, 1u, 1u), std::logic_error);
        // The rejected dispatch recorded nothing: rebinding a non-conflicting group lets a retry succeed.
        computePass->setBindGroup(otherStorageGroup, 1u);
        computePass->dispatchWorkgroups(1u, 1u, 1u);
        computePass->end();
        encoder->end();
        queue->submit({encoder});

        // A dispatch whose pipeline layout still lacks a required bind group is rejected the same way on both backends.
        auto missingGroupEncoder = queue->createCommandEncoder();
        auto missingGroupPass = missingGroupEncoder->beginComputePass({.label = "ComputeMissingGroupPass"});
        missingGroupPass->setPipeline(pipeline);
        missingGroupPass->setBindGroup(sampledGroup, 0u);
        EXPECT_THROW(missingGroupPass->dispatchWorkgroups(1u, 1u, 1u), std::logic_error);
        missingGroupPass->setBindGroup(otherStorageGroup, 1u);
        missingGroupPass->dispatchWorkgroups(1u, 1u, 1u);
        missingGroupPass->end();
        missingGroupEncoder->end();
        queue->submit({missingGroupEncoder});
        device->freeTexture(otherTexture);
        device->freeTexture(texture);
    }

    /** Creates the small compute pipeline the pipeline cache round-trip test compiles on each fresh device. */
    GVM::RHI::ComputePipeline createPipelineCacheProbe(GVM::RHI::Device device, GVM::RHI::GraphicsBackend backend)
    {
        auto layout = device->createPipelineLayout({.label = "PipelineCacheProbeLayout"});
        auto module = createDedupComputeModule(device, backend, "PipelineCacheProbeModule");
        return device->createComputePipeline({
            .label = "PipelineCacheProbe",
            .layout = layout,
            .compute = {
                .module = module,
                .entryPoint = dedupComputeEntryPoint(backend),
                .workgroupX = 1u,
                .workgroupY = 1u,
                .workgroupZ = 1u,
            },
        });
    }

    /// Verifies the pipeline cache contract: Vulkan exports data a new instance accepts, corrupt data is ignored without failing device or pipeline creation, and Metal exports nothing.
    TEST(RhiPipelineCacheTests, PipelineCacheDataRoundTripsAndIgnoresCorruptData)
    {
        GVM::RHI::Instance firstInstance = GVM::RHI::createInstance(GVM::Tests::makeTestInstanceDescriptor());
        ASSERT_NE(firstInstance, nullptr);
        const GVM::RHI::GraphicsBackend backend = firstInstance->getBackend();
        GVM::RHI::Device firstDevice = firstInstance->createDevice();
        ASSERT_NE(firstDevice, nullptr);
        ASSERT_TRUE(static_cast<bool>(createPipelineCacheProbe(firstDevice, backend)));
        const eastl::vector<uint8_t> cacheData = firstDevice->getPipelineCacheData();
        GVM::RHI::destroyInstance(firstInstance);
        if (backend != GVM::RHI::GraphicsBackend::Vulkan)
        {
            EXPECT_TRUE(cacheData.empty());
            return;
        }
        ASSERT_FALSE(cacheData.empty());

        GVM::RHI::InstanceDescriptor reloadDescriptor = GVM::Tests::makeTestInstanceDescriptor();
        reloadDescriptor.pipelineCache.initialData = cacheData;
        GVM::RHI::Instance reloadInstance = GVM::RHI::createInstance(reloadDescriptor);
        ASSERT_NE(reloadInstance, nullptr);
        GVM::RHI::Device reloadDevice = reloadInstance->createDevice();
        ASSERT_NE(reloadDevice, nullptr);
        EXPECT_TRUE(static_cast<bool>(createPipelineCacheProbe(reloadDevice, backend)));
        EXPECT_FALSE(reloadDevice->getPipelineCacheData().empty());
        GVM::RHI::destroyInstance(reloadInstance);

        GVM::RHI::InstanceDescriptor corruptDescriptor = GVM::Tests::makeTestInstanceDescriptor();
        corruptDescriptor.pipelineCache.initialData = cacheData;
        corruptDescriptor.pipelineCache.initialData[0] ^= 0xFFu;
        GVM::RHI::Instance corruptInstance = GVM::RHI::createInstance(corruptDescriptor);
        ASSERT_NE(corruptInstance, nullptr);
        GVM::RHI::Device corruptDevice = corruptInstance->createDevice();
        ASSERT_NE(corruptDevice, nullptr);
        EXPECT_TRUE(static_cast<bool>(createPipelineCacheProbe(corruptDevice, backend)));
        GVM::RHI::destroyInstance(corruptInstance);
    }

    /// Verifies both backends apply the shared blit validation: a fill into a buffer without copy-destination usage, a fill past the end, and a copy past the end are rejected.
    TEST_F(RhiPipelineTest, RejectsInvalidBlitBufferRangesOnBothBackends)
    {
        auto storageOnly = device->createBuffer({
            .label = "BlitStorageOnlyBuffer",
            .usage = GVM::RHI::BufferUsage::Storage,
            .size = 64u,
        });
        auto copyTarget = device->createBuffer({
            .label = "BlitCopyTargetBuffer",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = 64u,
        });
        ASSERT_FALSE(storageOnly.isNull());
        ASSERT_FALSE(copyTarget.isNull());

        auto queue = device->getMainQueue();
        auto encoder = queue->createCommandEncoder();
        auto blitPass = encoder->beginBlitPass({.label = "InvalidBlitRanges"});
        EXPECT_THROW(blitPass->fillBuffer(GVM::RHI::BufferRange(storageOnly, 0u, 64u), 0u), std::invalid_argument);
        // BufferRange validates its own bounds, so shift an already valid range past the end to reach the blit checks.
        GVM::RHI::BufferRange pastEnd(copyTarget, 0u, 64u);
        pastEnd.offset = 32u;
        EXPECT_THROW(blitPass->fillBuffer(pastEnd, 0u), std::out_of_range);
        EXPECT_THROW(blitPass->copyBufferToBuffer(GVM::RHI::BufferRange(copyTarget, 0u, 64u), pastEnd), std::out_of_range);
        blitPass->fillBuffer(GVM::RHI::BufferRange(copyTarget, 0u, 64u), 0u);
        blitPass->end();
        encoder->end();
        queue->submit({encoder});
        device->freeBuffer(copyTarget);
        device->freeBuffer(storageOnly);
    }

    /// Verifies the queue-level fillBuffer fills each byte with the given value.
    TEST_F(RhiPipelineTest, FillsBufferAtQueueLevelWithByteValueOnBothBackends)
    {
        auto buffer = device->createBuffer({
            .label = "QueueFillTargetBuffer",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
            .size = 32u,
        });
        ASSERT_FALSE(buffer.isNull());

        auto queue = device->getMainQueue();
        queue->fillBuffer(GVM::RHI::BufferRange(buffer, 0u, 32u), 0xABu);
        queue->submit({});

        std::array<uint32_t, 8u> mapped = {};
        queue->readBuffer(GVM::RHI::BufferRange(buffer), mapped.data(), sizeof(mapped));
        queue->submit({});
        for (uint32_t index = 0u; index < 8u; ++index)
        {
            EXPECT_EQ(mapped[index], 0xABABABABu);
        }
        device->freeBuffer(buffer);
    }


    /// Verifies an empty buffer range in a bind group is rejected at creation on both backends.
    TEST_F(RhiPipelineTest, RejectsEmptyBufferRangeInBindGroupAtCreation)
    {
        auto buffer = device->createBuffer({
            .label = "EmptyBindGroupRangeBuffer",
            .usage = GVM::RHI::BufferUsage::Storage,
            .size = 16u,
        });
        ASSERT_FALSE(buffer.isNull());
        auto layout = device->createBindGroupLayout({.label = "EmptyRangeLayout",
            .entries = {{
                .binding = 0u,
                .visibility = GVM::RHI::ShaderStage::Compute,
                .buffer = {.type = GVM::RHI::BufferBindingType::Storage,
                    .access = GVM::RHI::StorageBufferAccess::ReadWrite},
            }}});
        ASSERT_TRUE(static_cast<bool>(layout));

        const auto expectCreationThrow = [this, &layout](GVM::RHI::BufferRange range)
        {
            if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan)
            {
                EXPECT_THROW(device->createBindGroup({.layout = layout,
                    .entries = {GVM::RHI::BindGroupEntry{.binding = 0u, .buffer = {range}}}}), std::invalid_argument);
            }
            else
            {
                EXPECT_THROW(device->createBindGroup({.layout = layout,
                    .entries = {GVM::RHI::BindGroupEntry{.binding = 0u, .buffer = {range}}}}), std::out_of_range);
            }
        };
        expectCreationThrow(GVM::RHI::BufferRange(buffer, 0u, 0u));
        expectCreationThrow(GVM::RHI::BufferRange(buffer, 16u, GVM::RHI::WholeSize));
        device->freeBuffer(buffer);
    }
} // namespace

    /// Verifies calling a pass-scoped API after end() throws std::runtime_error on both backends.
    TEST_F(RhiPipelineTest, RejectsCallsOnEndedPassEncodersOnBothBackends)
    {
        auto buffer = device->createBuffer({
            .label = "EndedPassTargetBuffer",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
            .size = 32u,
        });
        ASSERT_FALSE(buffer.isNull());
        auto queue = device->getMainQueue();
        auto encoder = queue->createCommandEncoder();
        auto blitPass = encoder->beginBlitPass({.label = "EndedBlitPass"});
        blitPass->end();
        EXPECT_THROW(blitPass->fillBuffer(GVM::RHI::BufferRange(buffer, 0u, 16u), 0x1u), std::runtime_error);
        blitPass->end();
        auto computePass = encoder->beginComputePass({.label = "EndedComputePass"});
        computePass->end();
        EXPECT_THROW(computePass->dispatchWorkgroups(1u, 1u, 1u), std::runtime_error);
        computePass->end();
        encoder->end();
        queue->submit({encoder});
        device->freeBuffer(buffer);
    }
