#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>
#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/VulkanTestHooks.hpp>

#include <EASTL/shared_ptr.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <vector>

namespace GVM::RHI::Vulkan
{
    struct VKSubmissionCompletionState;
    using VKSubmissionCompletion = eastl::shared_ptr<VKSubmissionCompletionState>;

    class VKQueue : public QueueImpl
    {
    public:
        [[nodiscard]] VKSubmissionCompletion getMostRecentSubmissionCompletion() const;
        void waitForSubmission(const VKSubmissionCompletion &completion, const char *waitSource = "unspecified");
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
            const auto completion = vulkanQueue->getMostRecentSubmissionCompletion();
            if (completion)
            {
                vulkanQueue->waitForSubmission(completion, "pipeline_cache_pressure_test");
            }
        }
    };

    struct CachePressureFramebuffer
    {
        std::array<GVM::RHI::Texture, 4> textures;
        std::array<GVM::RHI::TextureView, 4> views;
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
                .view = framebuffer.views[colorIndex],
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
        EXPECT_EQ(findPixelLocalDependency(dependencies, 1u, 2u), nullptr);

        const auto *readOnlyFinal = findPixelLocalDependency(dependencies, 2u, ExternalSubpass);
        ASSERT_NE(readOnlyFinal, nullptr);
        EXPECT_TRUE(readOnlyFinal->sourceFragmentShaderStage);
        EXPECT_TRUE(readOnlyFinal->sourceInputAttachmentReadAccess);
        EXPECT_FALSE(readOnlyFinal->sourceColorAttachmentWriteAccess);
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

        const auto *cWriteToLaterRead = findPixelLocalDependency(dependencies, 0u, 2u);
        ASSERT_NE(cWriteToLaterRead, nullptr);
        EXPECT_TRUE(cWriteToLaterRead->sourceColorAttachmentWriteAccess);
        EXPECT_TRUE(cWriteToLaterRead->destinationInputAttachmentReadAccess);

        const auto *mixedFinal = findPixelLocalDependency(dependencies, 1u, ExternalSubpass);
        ASSERT_NE(mixedFinal, nullptr);
        EXPECT_TRUE(mixedFinal->sourceInputAttachmentReadAccess);
        EXPECT_TRUE(mixedFinal->sourceColorAttachmentWriteAccess);

        const auto *readOnlyFinal = findPixelLocalDependency(dependencies, 2u, ExternalSubpass);
        ASSERT_NE(readOnlyFinal, nullptr);
        EXPECT_TRUE(readOnlyFinal->sourceInputAttachmentReadAccess);
        EXPECT_FALSE(readOnlyFinal->sourceColorAttachmentWriteAccess);
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
} // namespace
