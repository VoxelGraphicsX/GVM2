#include "MiscUvTestsRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "MiscUvGeometryData.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <CoreText/CoreText.h>

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr float CanvasInternalExtent = 1024.0f;
        constexpr float CanvasCoordinateExtent = 1022.0f;
        constexpr uint32_t GlyphAtlasPhaseCount = 32u;
        constexpr uint32_t GlyphAtlasPhaseColumns = 8u;
        constexpr uint32_t GlyphAtlasWidth = 3328u;
        constexpr uint32_t GlyphAtlasHeight = 8192u;
        constexpr char GlyphCharacters[] = "0123456789abc";

        /** Returns the locked SHA-256 digest for one deterministic scroll replay. */
        const char *miscUvExpectedReplaySha256(const eastl::string &scenarioId)
        {
            if (scenarioId == "sphere-scroll")
                return "abe92c407c550821696a8c293e3a7a3ac4440ebee0c0faeefd3ea5e544478847";
            if (scenarioId == "icosahedron-scroll")
                return "689628cac13a60a4f89de711e98edb51959f7629eab552f783e53fdb0c321378";
            if (scenarioId == "octahedron-scroll")
                return "e15451558496c63a898c0d6ced5b0f25c8d5d3379d67a048d49422967ba5c106";
            if (scenarioId == "cylinder-scroll")
                return "f5a13b18eba510f4985192edb02278602a34a743c92b92f66f572123023b65b3";
            if (scenarioId == "box-scroll")
                return "cfde01429c9835f762cafe2cc820ad5e9f7c7e20e586ac6c8caccc3786236953";
            if (scenarioId == "lathe-scroll")
                return "8302a391e568bf8b6837134835315e8e16bf0670b1836c4f89d1ef0643bb5c91";
            if (scenarioId == "torus-scroll")
                return "b85571754d8a71865669cb9041a8ec3e6e7f546dc1d7c1e51859949c32a5dce2";
            if (scenarioId == "torus-knot-scroll")
                return "02ebb2171ccfaeb5ba1e6c7e9cbb1fa698ba436107837bd9c57a6da943f34c88";
            return nullptr;
        }

        /** Calculates the SHA-256 digest of one explicitly requested replay file. */
        eastl::string calculateMiscUvReplaySha256(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open the misc_uv_tests input replay.");
            }
            CC_SHA256_CTX context;
            CC_SHA256_Init(&context);
            eastl::array<char, 4096u> bytes = {};
            while (input)
            {
                input.read(bytes.data(), bytes.size());
                const std::streamsize count = input.gcount();
                if (count > 0)
                {
                    CC_SHA256_Update(
                        &context,
                        bytes.data(),
                        static_cast<CC_LONG>(count));
                }
            }
            eastl::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256_Final(digest.data(), &context);
            std::ostringstream text;
            text << std::hex << std::setfill('0');
            for (unsigned char byte : digest)
            {
                text << std::setw(2) << static_cast<uint32_t>(byte);
            }
            return eastl::string(text.str().c_str());
        }

        /** Returns one generated array's compile-time face count. */
        template <size_t Count>
        constexpr uint32_t miscUvFaceCount(const MiscUvFace (&)[Count])
        {
            return static_cast<uint32_t>(Count);
        }

        /** Maps one supported label character into the immutable atlas column. */
        uint32_t miscUvGlyphIndex(char character)
        {
            if (character >= '0' && character <= '9')
            {
                return static_cast<uint32_t>(character - '0');
            }
            if (character >= 'a' && character <= 'c')
            {
                return 10u + static_cast<uint32_t>(character - 'a');
            }
            throw std::invalid_argument("misc_uv_tests encountered an unsupported label glyph.");
        }

        /** Returns the single Arial pair adjustment used by Chrome's shaped labels. */
        float miscUvGlyphKerning(char left, char right, bool large)
        {
            if (left == '1' && right == '1')
            {
                return large ? -1.3359375f : -0.890625f;
            }
            return 0.0f;
        }

        /** Converts one Canvas internal coordinate into the captured CSS coordinate. */
        glm::vec2 miscUvCanvasPoint(const glm::vec2 &uv)
        {
            return glm::vec2(
                uv.x * CanvasCoordinateExtent + 0.5f,
                (1.0f - uv.y) * CanvasCoordinateExtent + 0.5f);
        }

        /** Quantizes one Canvas contour point to Skia's subpixel line grid. */
        glm::vec2 miscUvQuantizeLinePoint(const glm::vec2 &point)
        {
            return glm::round(point * 64.0f) / 64.0f;
        }

        /** Returns whether two generated UV points are the same locked r185 value. */
        bool miscUvPointsMatch(const glm::vec2 &left, const glm::vec2 &right)
        {
            return std::abs(left.x - right.x) <= 0.0000001f &&
                std::abs(left.y - right.y) <= 0.0000001f;
        }

        /** Returns whether an equivalent undirected contour edge occurs later in painter order. */
        bool miscUvLineAppearsLater(
            const MiscUvFace *faces,
            uint32_t faceCount,
            uint32_t faceIndex,
            uint32_t edgeIndex,
            const glm::vec2 &start,
            const glm::vec2 &end)
        {
            for (uint32_t laterFace = faceIndex; laterFace < faceCount; ++laterFace)
            {
                const uint32_t firstEdge = laterFace == faceIndex
                    ? edgeIndex + 1u
                    : 0u;
                for (uint32_t laterEdge = firstEdge; laterEdge < 3u; ++laterEdge)
                {
                    const MiscUvFace &face = faces[laterFace];
                    const uint32_t nextEdge = (laterEdge + 1u) % 3u;
                    const glm::vec2 laterStart(
                        face.uv[laterEdge][0u], face.uv[laterEdge][1u]);
                    const glm::vec2 laterEnd(
                        face.uv[nextEdge][0u], face.uv[nextEdge][1u]);
                    if ((miscUvPointsMatch(start, laterStart) &&
                         miscUvPointsMatch(end, laterEnd)) ||
                        (miscUvPointsMatch(start, laterEnd) &&
                         miscUvPointsMatch(end, laterStart)))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        /** Appends one painter-order quad with either solid or glyph-atlas coverage. */
        void appendMiscUvQuad(
            eastl::vector<MiscUvTestsVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec2 &minimum,
            const glm::vec2 &maximum,
            const glm::vec2 &uvMinimum,
            const glm::vec2 &uvMaximum,
            const glm::vec4 &colorAndAtlasFlag)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            MiscUvTestsVertex vertex;
            vertex.colorAndAtlasFlag = colorAndAtlasFlag;
            vertex.lineSegment = glm::vec4(0.0f);
            vertex.pixelPosition = glm::vec2(minimum.x, minimum.y);
            vertex.atlasCoordinate = glm::vec2(uvMinimum.x, uvMinimum.y);
            vertices.push_back(vertex);
            vertex.pixelPosition = glm::vec2(maximum.x, minimum.y);
            vertex.atlasCoordinate = glm::vec2(uvMaximum.x, uvMinimum.y);
            vertices.push_back(vertex);
            vertex.pixelPosition = glm::vec2(maximum.x, maximum.y);
            vertex.atlasCoordinate = glm::vec2(uvMaximum.x, uvMaximum.y);
            vertices.push_back(vertex);
            vertex.pixelPosition = glm::vec2(minimum.x, maximum.y);
            vertex.atlasCoordinate = glm::vec2(uvMinimum.x, uvMaximum.y);
            vertices.push_back(vertex);
            indices.insert(indices.end(), {
                baseVertex + 0u,
                baseVertex + 1u,
                baseVertex + 2u,
                baseVertex + 0u,
                baseVertex + 2u,
                baseVertex + 3u});
        }

        /** Appends one subpixel Canvas contour as a screen-space triangle quad. */
        void appendMiscUvLine(
            eastl::vector<MiscUvTestsVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec2 &start,
            const glm::vec2 &end)
        {
            const glm::vec2 quantizedStart =
                miscUvQuantizeLinePoint(start);
            const glm::vec2 quantizedEnd =
                miscUvQuantizeLinePoint(end);
            const glm::vec2 direction = quantizedEnd - quantizedStart;
            const float length = glm::length(direction);
            if (length <= 0.0001f) return;
            const glm::vec2 perpendicular(
                -direction.y / length,
                direction.x / length);
            const glm::vec2 halfWidth = perpendicular * 1.5f;
            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            const glm::vec4 contourColor(
                63.0f / 255.0f,
                63.0f / 255.0f,
                63.0f / 255.0f,
                0.0f);
            MiscUvTestsVertex vertex;
            vertex.atlasCoordinate = glm::vec2(0.0f);
            vertex.colorAndAtlasFlag = glm::vec4(
                contourColor.x,
                contourColor.y,
                contourColor.z,
                2.0f);
            vertex.lineSegment = glm::vec4(quantizedStart, quantizedEnd);
            vertex.pixelPosition = quantizedStart - halfWidth;
            vertices.push_back(vertex);
            vertex.pixelPosition = quantizedEnd - halfWidth;
            vertices.push_back(vertex);
            vertex.pixelPosition = quantizedEnd + halfWidth;
            vertices.push_back(vertex);
            vertex.pixelPosition = quantizedStart + halfWidth;
            vertices.push_back(vertex);
            indices.insert(indices.end(), {
                baseVertex + 0u,
                baseVertex + 1u,
                baseVertex + 2u,
                baseVertex + 0u,
                baseVertex + 2u,
                baseVertex + 3u});
        }

        /** Decodes the locked browser Arial atlas and records exact glyph advances. */
        void buildMiscUvGlyphAtlas(
            eastl::vector<uint8_t> &pixels,
            eastl::array<MiscUvGlyphAdvance, 13u> &advances,
            uint32_t expectedWidth,
            uint32_t expectedHeight)
        {
            for (uint32_t bank = 0u; bank < 2u; ++bank)
            {
                const CGFloat fontSize = bank == 0u ? 12.0 : 18.0;
                CTFontRef font = CTFontCreateWithName(CFSTR("Arial"), fontSize, nullptr);
                if (font == nullptr)
                {
                    throw std::runtime_error("Arial is unavailable for misc_uv_tests.");
                }
                for (uint32_t glyphIndex = 0u; glyphIndex < 13u; ++glyphIndex)
                {
                    UniChar character = static_cast<UniChar>(GlyphCharacters[glyphIndex]);
                    CGGlyph glyph = 0u;
                    if (!CTFontGetGlyphsForCharacters(font, &character, &glyph, 1u))
                    {
                        CFRelease(font);
                        throw std::runtime_error("Could not resolve one misc_uv_tests Arial glyph.");
                    }
                    CGSize advance = {};
                    CTFontGetAdvancesForGlyphs(
                        font, kCTFontOrientationHorizontal, &glyph, &advance, 1u);
                    if (bank == 0u)
                    {
                        advances[glyphIndex].small = static_cast<float>(advance.width);
                    }
                    else
                    {
                        advances[glyphIndex].large = static_cast<float>(advance.width);
                    }
                }
                CFRelease(font);
            }

            const std::filesystem::path atlasPath =
                std::filesystem::path(GVM_THREE_SAMPLE_SOURCE_ROOT) /
                "Fixtures/MiscUvTests/Assets/misc_uv_glyph_phase32_atlas.png";
            RgbaImageData atlas = decodePngRgba8(atlasPath);
            if (atlas.width != expectedWidth ||
                atlas.height != expectedHeight)
            {
                throw std::runtime_error(
                    "The misc_uv_tests glyph atlas has invalid dimensions.");
            }
            const size_t rowByteCount =
                static_cast<size_t>(atlas.width) * 4u;
            eastl::vector<uint8_t> row(rowByteCount);
            for (uint32_t top = 0u; top < atlas.height / 2u; ++top)
            {
                const uint32_t bottom = atlas.height - top - 1u;
                auto topRow = atlas.pixels.begin() +
                    static_cast<ptrdiff_t>(top * rowByteCount);
                auto bottomRow = atlas.pixels.begin() +
                    static_cast<ptrdiff_t>(bottom * rowByteCount);
                eastl::copy(topRow, topRow + rowByteCount, row.begin());
                eastl::copy(
                    bottomRow,
                    bottomRow + rowByteCount,
                    topRow);
                eastl::copy(row.begin(), row.end(), bottomRow);
            }
            pixels.swap(atlas.pixels);
        }

        /** Appends one Canvas fillText-equivalent centered label from atlas glyph quads. */
        void appendMiscUvText(
            eastl::vector<MiscUvTestsVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const std::string &text,
            const glm::vec2 &baseline,
            bool large,
            const eastl::array<MiscUvGlyphAdvance, 13u> &advances,
            const glm::vec3 &color)
        {
            float totalAdvance = 0.0f;
            for (size_t characterIndex = 0u;
                 characterIndex < text.size();
                 ++characterIndex)
            {
                const char character = text[characterIndex];
                const auto &advance = advances[miscUvGlyphIndex(character)];
                totalAdvance += large ? advance.large : advance.small;
                if (characterIndex + 1u < text.size())
                {
                    totalAdvance += miscUvGlyphKerning(
                        character,
                        text[characterIndex + 1u],
                        large);
                }
            }
            float cursor = baseline.x - totalAdvance * 0.5f;
            const float baselineOffset = large ? 25.0f : 22.0f;
            for (size_t characterIndex = 0u;
                 characterIndex < text.size();
                 ++characterIndex)
            {
                const char character = text[characterIndex];
                const uint32_t glyphIndex = miscUvGlyphIndex(character);
                const float advance = large
                    ? advances[glyphIndex].large
                    : advances[glyphIndex].small;
                const float integerCursor = std::floor(cursor);
                const uint32_t quantizedPhaseX = static_cast<uint32_t>(std::floor(
                    (cursor - integerCursor) *
                        float(GlyphAtlasPhaseCount) +
                    0.5f));
                const uint32_t phaseX = eastl::min(
                    quantizedPhaseX,
                    GlyphAtlasPhaseCount - 1u);
                const float integerBaselineY = std::floor(baseline.y);
                const uint32_t quantizedPhaseY = static_cast<uint32_t>(std::floor(
                    (baseline.y - integerBaselineY) *
                        float(GlyphAtlasPhaseCount) +
                    0.5f));
                const uint32_t phaseY = eastl::min(
                    quantizedPhaseY,
                    GlyphAtlasPhaseCount - 1u);
                const float glyphOriginX = cursor -
                    float(phaseX) /
                        float(GlyphAtlasPhaseCount);
                const float glyphBaselineY = baseline.y -
                    float(phaseY) /
                        float(GlyphAtlasPhaseCount);
                const uint32_t atlasColumn =
                    (phaseX % GlyphAtlasPhaseColumns) * 13u +
                    glyphIndex;
                const uint32_t phaseGroupCount =
                    GlyphAtlasPhaseCount /
                    GlyphAtlasPhaseColumns;
                const uint32_t atlasRow =
                    (phaseY * 2u + (large ? 0u : 1u)) *
                        phaseGroupCount +
                    phaseX / GlyphAtlasPhaseColumns;
                const glm::vec2 minimum(
                    glyphOriginX - 4.0f,
                    glyphBaselineY - baselineOffset);
                const glm::vec2 maximum = minimum + glm::vec2(32.0f);
                const glm::vec2 uvMinimum(
                    float(atlasColumn * 32u) /
                        float(GlyphAtlasWidth),
                    float(atlasRow * 32u) /
                        float(GlyphAtlasHeight));
                const glm::vec2 uvMaximum(
                    float(atlasColumn * 32u + 32u) /
                        float(GlyphAtlasWidth),
                    float(atlasRow * 32u + 32u) /
                        float(GlyphAtlasHeight));
                appendMiscUvQuad(
                    vertices,
                    indices,
                    minimum,
                    maximum,
                    uvMinimum,
                    uvMaximum,
                    glm::vec4(color, 1.0f));
                cursor += advance;
                if (characterIndex + 1u < text.size())
                {
                    cursor += miscUvGlyphKerning(
                        character,
                        text[characterIndex + 1u],
                        large);
                }
            }
        }

        /** Expands exact generated UV faces into contour and label painter geometry. */
        void buildMiscUvSection(
            const MiscUvFace *faces,
            uint32_t faceCount,
            eastl::vector<MiscUvTestsVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const eastl::array<MiscUvGlyphAdvance, 13u> &advances)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve(static_cast<size_t>(faceCount) * 64u);
            indices.reserve(static_cast<size_t>(faceCount) * 96u);
            for (uint32_t faceIndex = 0u; faceIndex < faceCount; ++faceIndex)
            {
                const MiscUvFace &face = faces[faceIndex];
                glm::vec2 uv[3u] = {
                    glm::vec2(face.uv[0u][0u], face.uv[0u][1u]),
                    glm::vec2(face.uv[1u][0u], face.uv[1u][1u]),
                    glm::vec2(face.uv[2u][0u], face.uv[2u][1u])};
                for (uint32_t edge = 0u; edge < 3u; ++edge)
                {
                    const glm::vec2 &start = uv[edge];
                    const glm::vec2 &end = uv[(edge + 1u) % 3u];
                    if (!miscUvLineAppearsLater(
                            faces,
                            faceCount,
                            faceIndex,
                            edge,
                            start,
                            end))
                    {
                        appendMiscUvLine(
                            vertices,
                            indices,
                            miscUvCanvasPoint(start),
                            miscUvCanvasPoint(end));
                    }
                }
                const glm::vec2 center = (uv[0u] + uv[1u] + uv[2u]) / 3.0f;
                appendMiscUvText(
                    vertices,
                    indices,
                    std::to_string(faceIndex),
                    glm::vec2(
                        center.x * CanvasInternalExtent,
                        (1.0f - center.y) * CanvasInternalExtent),
                    true,
                    advances,
                    glm::vec3(63.0f / 255.0f));
                if (center.x > 0.95f)
                {
                    appendMiscUvText(
                        vertices,
                        indices,
                        std::to_string(faceIndex),
                        glm::vec2(
                            std::fmod(center.x, 1.0f) * CanvasInternalExtent,
                            (1.0f - center.y) * CanvasInternalExtent),
                        true,
                        advances,
                        glm::vec3(63.0f / 255.0f));
                }
                for (uint32_t edge = 0u; edge < 3u; ++edge)
                {
                    const glm::vec2 labelUv = (center + uv[edge]) * 0.5f;
                    std::string label(1u, static_cast<char>('a' + edge));
                    label += std::to_string(face.vertexIndices[edge]);
                    const glm::vec2 baseline(
                        labelUv.x * CanvasInternalExtent,
                        (1.0f - labelUv.y) * CanvasInternalExtent);
                    appendMiscUvText(
                        vertices,
                        indices,
                        label,
                        baseline,
                        false,
                        advances,
                        glm::vec3(191.0f / 255.0f));
                    if (labelUv.x > 0.95f)
                    {
                        appendMiscUvText(
                            vertices,
                            indices,
                            label,
                            glm::vec2(
                                std::fmod(labelUv.x, 1.0f) *
                                    CanvasInternalExtent,
                                baseline.y),
                            false,
                            advances,
                            glm::vec3(191.0f / 255.0f));
                    }
                }
            }
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareMiscUvOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one deterministic UTF-8 evidence artifact. */
        void writeMiscUvText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareMiscUvOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error("Could not write a misc_uv_tests text artifact.");
            }
        }
    } // namespace

    void MiscUvTestsRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "misc_uv_tests" ||
            options.width != 800u || options.height != 500u ||
            options.targetFrame != 0u ||
            options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument(
                "misc_uv_tests requires one locked 800x500 frame-zero scenario.");
        }
        const MiscUvFace *faces = nullptr;
        canvasTop = 51.0f;
        if (options.scenarioId == "plane-initial")
        {
            faces = MiscUvPlaneFaces;
            faceCount = miscUvFaceCount(MiscUvPlaneFaces);
            sectionIndex = 0u;
            canvasTop = 111.0f;
        }
        else if (options.scenarioId == "sphere-scroll")
        {
            faces = MiscUvSphereFaces;
            faceCount = miscUvFaceCount(MiscUvSphereFaces);
            sectionIndex = 1u;
        }
        else if (options.scenarioId == "icosahedron-scroll")
        {
            faces = MiscUvIcosahedronFaces;
            faceCount = miscUvFaceCount(MiscUvIcosahedronFaces);
            sectionIndex = 2u;
        }
        else if (options.scenarioId == "octahedron-scroll")
        {
            faces = MiscUvOctahedronFaces;
            faceCount = miscUvFaceCount(MiscUvOctahedronFaces);
            sectionIndex = 3u;
        }
        else if (options.scenarioId == "cylinder-scroll")
        {
            faces = MiscUvCylinderFaces;
            faceCount = miscUvFaceCount(MiscUvCylinderFaces);
            sectionIndex = 4u;
        }
        else if (options.scenarioId == "box-scroll")
        {
            faces = MiscUvBoxFaces;
            faceCount = miscUvFaceCount(MiscUvBoxFaces);
            sectionIndex = 5u;
        }
        else if (options.scenarioId == "lathe-scroll")
        {
            faces = MiscUvLatheFaces;
            faceCount = miscUvFaceCount(MiscUvLatheFaces);
            sectionIndex = 6u;
        }
        else if (options.scenarioId == "torus-scroll")
        {
            faces = MiscUvTorusFaces;
            faceCount = miscUvFaceCount(MiscUvTorusFaces);
            sectionIndex = 7u;
        }
        else if (options.scenarioId == "torus-knot-scroll")
        {
            faces = MiscUvTorusKnotFaces;
            faceCount = miscUvFaceCount(MiscUvTorusKnotFaces);
            sectionIndex = 8u;
        }
        else
        {
            throw std::invalid_argument("Unknown misc_uv_tests Manifest scenario.");
        }
        if ((sectionIndex != 0u) != !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Only scrolled misc_uv_tests scenarios require input replay evidence.");
        }
        inputReplaySha256.clear();
        if (sectionIndex != 0u)
        {
            const char *expectedSha256 =
                miscUvExpectedReplaySha256(options.scenarioId);
            inputReplaySha256 = calculateMiscUvReplaySha256(
                std::filesystem::path(options.inputReplayPath.c_str()));
            if (expectedSha256 == nullptr ||
                inputReplaySha256 != expectedSha256)
            {
                throw std::runtime_error(
                    "The misc_uv_tests input replay diverged from its Oracle lock.");
            }
        }
        device = inDevice;
        buildMiscUvGlyphAtlas(
            atlasPixels,
            glyphAdvances,
            AtlasWidth,
            AtlasHeight);
        buildMiscUvSection(
            faces,
            faceCount,
            vertices,
            indices,
            glyphAdvances);
    }

    void MiscUvTestsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        frameArmer(frameIndex == 0u);
    }

    void MiscUvTestsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
            prepareMiscUvOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error("Could not write the misc_uv_tests RGBA capture.");
            }
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
                 << "  \"caseId\":\"misc_uv_tests\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"randomSeed\":" << options.randomSeed << ",\n"
                 << "  \"width\":" << width << ",\n  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\"";
        if (!inputReplaySha256.empty())
        {
            metadata << ",\n  \"inputReplay\":{"
                     << "\"schemaVersion\":1,"
                     << "\"caseId\":\"misc_uv_tests\","
                     << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
                     << "\"captureFrame\":0,"
                     << "\"sha256\":\"" << inputReplaySha256.c_str() << "\","
                     << "\"target\":\"body\","
                     << "\"eventCount\":1}";
        }
        metadata << "\n}\n";
        writeMiscUvText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"misc_uv_tests\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"implementationLevel\":\"semantic-complete\",\n"
                 << "  \"gpuWorkDslOnly\":true,\n"
                 << "  \"renderSetPolicy\":\"not-required\",\n"
                 << "  \"sceneRenderSetCount\":0,\n"
                 << "  \"renderableObjectCount\":0,\n"
                 << "  \"instanceCount\":1,\n"
                 << "  \"faceCount\":" << faceCount << ",\n"
                 << "  \"sectionIndex\":" << sectionIndex << ",\n"
                 << "  \"vertexCount\":" << vertices.size() << ",\n"
                 << "  \"indexCount\":" << indices.size() << ",\n"
                 << "  \"screenPassCount\":2,\n"
                 << "  \"drawCommandCount\":2,\n"
                 << "  \"directDrawFallback\":false";
        if (!inputReplaySha256.empty())
        {
            snapshot << ",\n  \"inputReplay\":{"
                     << "\"schemaVersion\":1,"
                     << "\"caseId\":\"misc_uv_tests\","
                     << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
                     << "\"captureFrame\":0,"
                     << "\"sha256\":\"" << inputReplaySha256.c_str() << "\","
                     << "\"target\":\"body\","
                     << "\"eventCount\":1}";
        }
        snapshot << "\n}\n";
        writeMiscUvText(options.sceneSnapshotPath, snapshot.str());
        std::ostringstream semantic;
        semantic << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"misc_uv_tests\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"faceCount\":" << faceCount << ",\n"
                 << "  \"canvasInternalExtent\":1024,\n"
                 << "  \"canvasDisplayExtent\":784,\n"
                 << "  \"glyphAtlas\":\"Arial-12-and-18-coverage\"\n}\n";
        writeMiscUvText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void MiscUvTestsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        atlasPixels.clear();
        inputReplaySha256.clear();
        frameArmer = {};
    }
} // namespace GVM::ThreeSamples
