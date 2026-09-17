#include <gtest/gtest.h>

#include <PixelLocalPassAccess.hpp>
#include <stdexcept>

namespace
{
    GVM::RHI::PixelLocalPassAttachmentAccess makeColorAccess(uint64_t readMask, uint64_t writeMask)
    {
        GVM::RHI::PixelLocalPassAttachmentAccess access = {};
        access.colorReadMask = readMask;
        access.colorWriteMask = writeMask;
        return access;
    }
} // namespace

TEST(PixelLocalPassAccessBuilderTests, AllowsReadAfterExplicitNextPass)
{
    GVM::RHI::Private::PixelLocalPassAccessBuilder builder;

    builder.appendTask(makeColorAccess(0u, 1u));
    builder.nextPass();
    builder.appendTask(makeColorAccess(1u, 0u));

    const eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> passes = builder.finish();
    ASSERT_EQ(passes.size(), 2u);
    EXPECT_EQ(passes[0].colorReadMask, 0u);
    EXPECT_EQ(passes[0].colorWriteMask, 1u);
    EXPECT_EQ(passes[1].colorReadMask, 1u);
    EXPECT_EQ(passes[1].colorWriteMask, 0u);
}

TEST(PixelLocalPassAccessBuilderTests, ReturnsOneEmptyPassWithoutDrawTasks)
{
    GVM::RHI::Private::PixelLocalPassAccessBuilder builder;

    const eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> passes = builder.finish();
    ASSERT_EQ(passes.size(), 1u);
    EXPECT_EQ(passes[0].colorReadMask, 0u);
    EXPECT_EQ(passes[0].colorWriteMask, 0u);
    EXPECT_FALSE(passes[0].depthWrite);
}

TEST(PixelLocalPassAccessBuilderTests, PreservesTrailingEmptyPassAfterExplicitBoundary)
{
    GVM::RHI::Private::PixelLocalPassAccessBuilder builder;

    builder.appendTask(makeColorAccess(0u, 1u));
    builder.nextPass();

    const eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> passes = builder.finish();
    ASSERT_EQ(passes.size(), 2u);
    EXPECT_EQ(passes[0].colorReadMask, 0u);
    EXPECT_EQ(passes[0].colorWriteMask, 1u);
    EXPECT_EQ(passes[1].colorReadMask, 0u);
    EXPECT_EQ(passes[1].colorWriteMask, 0u);
    EXPECT_FALSE(passes[1].depthWrite);
}

TEST(PixelLocalPassAccessBuilderTests, PreservesEmptyPassBetweenExplicitBoundaries)
{
    GVM::RHI::Private::PixelLocalPassAccessBuilder builder;

    builder.appendTask(makeColorAccess(0u, 1u));
    builder.nextPass();
    builder.nextPass();
    builder.appendTask(makeColorAccess(2u, 0u));

    const eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> passes = builder.finish();
    ASSERT_EQ(passes.size(), 3u);
    EXPECT_EQ(passes[0].colorWriteMask, 1u);
    EXPECT_EQ(passes[1].colorReadMask, 0u);
    EXPECT_EQ(passes[1].colorWriteMask, 0u);
    EXPECT_FALSE(passes[1].depthWrite);
    EXPECT_EQ(passes[2].colorReadMask, 2u);
}

TEST(PixelLocalPassAccessBuilderTests, RejectsSingleTaskColorReadWriteOverlap)
{
    GVM::RHI::Private::PixelLocalPassAccessBuilder builder;

    EXPECT_THROW(builder.appendTask(makeColorAccess(1u, 1u)), std::invalid_argument);
}

TEST(PixelLocalPassAccessBuilderTests, RejectsMergedSamePhaseColorReadWriteOverlap)
{
    GVM::RHI::Private::PixelLocalPassAccessBuilder builder;

    builder.appendTask(makeColorAccess(0u, 1u));

    EXPECT_THROW(builder.appendTask(makeColorAccess(1u, 0u)), std::invalid_argument);
}

TEST(PixelLocalPassAccessNormalizationTests, RejectsMissingMetadataForPixelLocalRenderPass)
{
    const eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> emptyAccesses;

    EXPECT_THROW(
        GVM::RHI::Private::normalizePixelLocalPassAttachmentAccesses(
            2u,
            false,
            true,
            2u,
            emptyAccesses),
        std::invalid_argument);
}

TEST(PixelLocalPassAccessNormalizationTests, RejectsMismatchedMetadataForPixelLocalRenderPass)
{
    eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> accesses;
    accesses.push_back(makeColorAccess(0u, 1u));

    EXPECT_THROW(
        GVM::RHI::Private::normalizePixelLocalPassAttachmentAccesses(
            2u,
            false,
            true,
            2u,
            accesses),
        std::invalid_argument);
}

TEST(PixelLocalPassAccessNormalizationTests, KeepsOrdinaryCompatibilityAccessWhenPixelLocalIsDisabled)
{
    const eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> emptyAccesses;

    const eastl::vector<GVM::RHI::PixelLocalPassAttachmentAccess> normalized =
        GVM::RHI::Private::normalizePixelLocalPassAttachmentAccesses(
            3u,
            true,
            false,
            1u,
            emptyAccesses);

    ASSERT_EQ(normalized.size(), 1u);
    EXPECT_EQ(normalized[0].colorReadMask, 0u);
    EXPECT_EQ(normalized[0].colorWriteMask, 0b111u);
    EXPECT_TRUE(normalized[0].depthWrite);
}
