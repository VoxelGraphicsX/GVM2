#include "RHIDiagnosticsOverlay.hpp"

#include <GVMRHI/GVMLogging.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/sort.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/utility.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>

namespace GVM::RHI::Private
{
    namespace
    {
        constexpr eastl::string_view OverlayLogCategory = "gvmrhi.diagnostics_overlay";
        constexpr uint32_t GlyphPixelSize = 16u;
        constexpr uint32_t GlyphLineHeight = 20u;
        constexpr uint32_t OverlayMarginX = 12u;
        constexpr uint32_t OverlayMarginY = 12u;
        constexpr uint32_t AtlasColumns = 16u;
        constexpr uint32_t AtlasRows = 6u;
        constexpr uint32_t AtlasCellSize = 8u;
        constexpr uint32_t AtlasWidth = AtlasColumns * AtlasCellSize;
        constexpr uint32_t AtlasHeight = AtlasRows * AtlasCellSize;
        constexpr uint32_t VertexBufferRingCount = 4u;
        constexpr float OverlayBackgroundGlyphCode = -1.0f;
        constexpr float OverlayBackgroundPaddingX = 6.0f;
        constexpr float OverlayBackgroundPaddingY = 6.0f;

        /// Stores one 5x7 ASCII glyph pattern using the low five bits of each row.
        struct GlyphBitmap
        {
            char character = ' ';
            uint8_t rows[7] = {};
        };

        /// Stores the last color attachment that can receive the overlay pass.
        struct DiagnosticsOverlayTarget
        {
            TextureView view = {};
            TextureFormat format = TextureFormat::Undefined;
            uint32_t width = 0u;
            uint32_t height = 0u;
        };

        /// Stores one expanded overlay glyph vertex ready for direct vertex-buffer rendering.
        struct DiagnosticsOverlayVertex
        {
            float clipX = 0.0f;
            float clipY = 0.0f;
            float localX = 0.0f;
            float localY = 0.0f;
            float glyphCode = 0.0f;
        };

        /// Caches a render pipeline for one overlay attachment format.
        struct DiagnosticsOverlayPipelineEntry
        {
            TextureFormat format = TextureFormat::Undefined;
            RenderPipeline pipeline = nullptr;
        };

        /// Identifies one automatic pass timestamp pair recorded into a diagnostics overlay command encoder.
        struct DiagnosticsOverlayRecordedScope
        {
            uint32_t slotIndex = 0u;
            uint64_t slotGeneration = 0u;
            uint32_t firstQuery = 0u;
        };

        /// Holds one reusable host-visible vertex buffer slot so back-to-back overlay submits do not overwrite in-flight vertices.
        struct DiagnosticsOverlayVertexBufferSlot
        {
            Buffer buffer = {};
            uint64_t capacityBytes = 0u;
        };

        /// Tracks one delayed timestamp profiler frame used by the runtime diagnostics overlay.
        struct DiagnosticsOverlayProfilerFrame
        {
            GpuTimestampFrameProfiler profiler;
            eastl::vector<TimestampRawResult> rawResults;
            eastl::vector<bool> scopeResolved;
            Submission lastSubmission = {};
            bool hasTrackedSubmission = false;
            bool initialized = false;
            bool frameOpen = false;
            bool used = false;
            uint32_t scopeCount = 0u;
            uint32_t pendingScopes = 0u;
            uint64_t generation = 1u;
            uint64_t submittedFrameIndex = 0u;
        };

        /// Orders recorded timestamp scopes by profiler slot and ascending query index for contiguous resolve coalescing.
        bool isRecordedScopeBefore(const DiagnosticsOverlayRecordedScope &lhs, const DiagnosticsOverlayRecordedScope &rhs)
        {
            if (lhs.slotIndex != rhs.slotIndex)
            {
                return lhs.slotIndex < rhs.slotIndex;
            }
            if (lhs.slotGeneration != rhs.slotGeneration)
            {
                return lhs.slotGeneration < rhs.slotGeneration;
            }
            return lhs.firstQuery < rhs.firstQuery;
        }

        static constexpr GlyphBitmap FontGlyphs[] = {
            {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
            {'"', {0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00}},
            {'#', {0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a}},
            {'$', {0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04}},
            {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
            {'&', {0x0c, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0d}},
            {'\'', {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},
            {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
            {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
            {'*', {0x00, 0x04, 0x15, 0x0e, 0x15, 0x04, 0x00}},
            {'+', {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}},
            {',', {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08}},
            {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
            {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}},
            {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
            {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
            {'1', {0x04, 0x0c, 0x14, 0x04, 0x04, 0x04, 0x1f}},
            {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
            {'3', {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}},
            {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
            {'5', {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}},
            {'6', {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
            {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
            {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
            {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}},
            {':', {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}},
            {';', {0x00, 0x0c, 0x0c, 0x00, 0x04, 0x04, 0x08}},
            {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
            {'=', {0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00}},
            {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
            {'?', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
            {'@', {0x0e, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0f}},
            {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
            {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
            {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}},
            {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
            {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
            {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
            {'G', {0x0e, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0f}},
            {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
            {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
            {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c}},
            {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
            {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
            {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
            {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
            {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
            {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
            {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}},
            {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
            {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
            {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
            {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
            {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
            {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
            {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
            {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
            {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
            {'[', {0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e}},
            {'\\', {0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01}},
            {']', {0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e}},
            {'^', {0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00}},
            {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f}},
            {'`', {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},
            {'a', {0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f}},
            {'b', {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1e}},
            {'c', {0x00, 0x00, 0x0e, 0x11, 0x10, 0x11, 0x0e}},
            {'d', {0x01, 0x01, 0x0d, 0x13, 0x11, 0x11, 0x0f}},
            {'e', {0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e}},
            {'f', {0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08}},
            {'g', {0x00, 0x00, 0x0f, 0x11, 0x0f, 0x01, 0x0e}},
            {'h', {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}},
            {'i', {0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e}},
            {'j', {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0c}},
            {'k', {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}},
            {'l', {0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
            {'m', {0x00, 0x00, 0x1a, 0x15, 0x15, 0x15, 0x15}},
            {'n', {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}},
            {'o', {0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e}},
            {'p', {0x00, 0x00, 0x1e, 0x11, 0x1e, 0x10, 0x10}},
            {'q', {0x00, 0x00, 0x0f, 0x11, 0x0f, 0x01, 0x01}},
            {'r', {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},
            {'s', {0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e}},
            {'t', {0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06}},
            {'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d}},
            {'v', {0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04}},
            {'w', {0x00, 0x00, 0x11, 0x15, 0x15, 0x15, 0x0a}},
            {'x', {0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11}},
            {'y', {0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x0e}},
            {'z', {0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f}},
            {'{', {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02}},
            {'|', {0x04, 0x04, 0x04, 0x00, 0x04, 0x04, 0x04}},
            {'}', {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08}},
            {'~', {0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00}},
        };

        /// Formats a short diagnostics line using a fixed stack buffer.
        eastl::string makeFormattedString(const char *format, ...)
        {
            char buffer[256] = {};
            va_list args;
            va_start(args, format);
            std::vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            return eastl::string(buffer);
        }

        /// Returns whether the overlay renderer supports blending into the supplied target format.
        bool isSupportedOverlayTargetFormat(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::RGBA8Unorm:
            case TextureFormat::RGBA8UnormSrgb:
            case TextureFormat::BGRA8Unorm:
            case TextureFormat::BGRA8UnormSrgb:
            case TextureFormat::RGBA16Float:
                return true;
            default:
                return false;
            }
        }

        /// Returns true when timestamp writes were already explicitly supplied by the caller.
        bool hasExplicitTimestampWrites(const PassTimestampWrites &writes)
        {
            return writes.querySet != nullptr ||
                   isTimestampQueryIndexEnabled(writes.beginningOfPassWriteIndex) ||
                   isTimestampQueryIndexEnabled(writes.endOfPassWriteIndex);
        }

        /// Converts a texture format to a compact ASCII name for overlay resource rows.
        const char *formatTextureFormatName(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::RGBA8Unorm: return "RGBA8";
            case TextureFormat::RGBA8UnormSrgb: return "RGBA8_SRGB";
            case TextureFormat::BGRA8Unorm: return "BGRA8";
            case TextureFormat::BGRA8UnormSrgb: return "BGRA8_SRGB";
            case TextureFormat::RGBA16Float: return "RGBA16F";
            case TextureFormat::RGBA32Float: return "RGBA32F";
            case TextureFormat::R8Unorm: return "R8";
            case TextureFormat::RG8Unorm: return "RG8";
            case TextureFormat::R16Float: return "R16F";
            case TextureFormat::RG16Float: return "RG16F";
            case TextureFormat::R32Float: return "R32F";
            case TextureFormat::RG32Float: return "RG32F";
            case TextureFormat::Depth32Float: return "D32F";
            case TextureFormat::Depth24Plus: return "D24";
            case TextureFormat::Depth24PlusStencil8: return "D24S8";
            default: return "FMT";
            }
        }

        /// Converts a byte count to a compact ASCII text value.
        eastl::string formatByteCount(uint64_t bytes)
        {
            if (bytes >= 1024ull * 1024ull * 1024ull)
            {
                return makeFormattedString("%.2fGB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
            }
            if (bytes >= 1024ull * 1024ull)
            {
                return makeFormattedString("%.2fMB", static_cast<double>(bytes) / (1024.0 * 1024.0));
            }
            if (bytes >= 1024ull)
            {
                return makeFormattedString("%.1fKB", static_cast<double>(bytes) / 1024.0);
            }
            return makeFormattedString("%lluB", static_cast<unsigned long long>(bytes));
        }

        /// Replaces non-printable characters and limits labels to text that the built-in ASCII font can show.
        eastl::string sanitizeOverlayText(const eastl::string &text, uint32_t maxCharacters)
        {
            eastl::string sanitized;
            sanitized.reserve(eastl::min<uint32_t>(static_cast<uint32_t>(text.size()), maxCharacters));
            for (char character : text)
            {
                if (sanitized.size() >= maxCharacters)
                {
                    break;
                }
                const unsigned char value = static_cast<unsigned char>(character);
                sanitized.push_back(value >= 32u && value <= 126u ? static_cast<char>(value) : '?');
            }
            if (sanitized.empty())
            {
                sanitized = "Unlabeled";
            }
            return sanitized;
        }

        /// Expands a buffer size to reduce overlay vertex-buffer reallocations.
        uint64_t calculateExpandedBufferSize(uint64_t requiredBytes)
        {
            uint64_t capacity = 4096u;
            while (capacity < requiredBytes)
            {
                capacity *= 2u;
            }
            return capacity;
        }

        /// Converts a pixel-space X coordinate to clip space for the target attachment.
        float pixelToClipX(float pixelX, uint32_t width)
        {
            return width == 0u ? 0.0f : (pixelX / static_cast<float>(width)) * 2.0f - 1.0f;
        }

        /// Converts a pixel-space Y coordinate to clip space for the target attachment.
        float pixelToClipY(float pixelY, uint32_t height)
        {
            return height == 0u ? 0.0f : 1.0f - (pixelY / static_cast<float>(height)) * 2.0f;
        }

        /// Appends one vertex to the overlay vertex list.
        void appendOverlayVertex(
            eastl::vector<DiagnosticsOverlayVertex> &vertices,
            const DiagnosticsOverlayTarget &target,
            float pixelX,
            float pixelY,
            float localX,
            float localY,
            float glyphCode)
        {
            DiagnosticsOverlayVertex vertex = {};
            vertex.clipX = pixelToClipX(pixelX, target.width);
            vertex.clipY = pixelToClipY(pixelY, target.height);
            vertex.localX = localX;
            vertex.localY = localY;
            vertex.glyphCode = glyphCode;
            vertices.push_back(vertex);
        }

        /// Appends two triangles for one overlay rectangle.
        void appendOverlayQuad(
            eastl::vector<DiagnosticsOverlayVertex> &vertices,
            const DiagnosticsOverlayTarget &target,
            float x,
            float y,
            float width,
            float height,
            float glyphCode)
        {
            const float right = x + width;
            const float bottom = y + height;

            appendOverlayVertex(vertices, target, x, y, 0.0f, 0.0f, glyphCode);
            appendOverlayVertex(vertices, target, right, y, 1.0f, 0.0f, glyphCode);
            appendOverlayVertex(vertices, target, x, bottom, 0.0f, 1.0f, glyphCode);

            appendOverlayVertex(vertices, target, right, y, 1.0f, 0.0f, glyphCode);
            appendOverlayVertex(vertices, target, right, bottom, 1.0f, 1.0f, glyphCode);
            appendOverlayVertex(vertices, target, x, bottom, 0.0f, 1.0f, glyphCode);
        }

        /// Appends two triangles for one glyph cell.
        void appendGlyphQuad(
            eastl::vector<DiagnosticsOverlayVertex> &vertices,
            const DiagnosticsOverlayTarget &target,
            float x,
            float y,
            uint32_t glyphCode)
        {
            appendOverlayQuad(vertices, target, x, y, static_cast<float>(GlyphPixelSize), static_cast<float>(GlyphPixelSize), static_cast<float>(glyphCode));
        }

        /// Returns a valid glyph code for the built-in ASCII font atlas.
        uint32_t normalizeGlyphCode(char character)
        {
            const unsigned char value = static_cast<unsigned char>(character);
            return value >= 32u && value <= 126u ? value : static_cast<uint32_t>('?');
        }

        /// Returns the maximum number of glyph columns that fit in the target attachment.
        uint32_t calculateMaxColumns(const DiagnosticsOverlayTarget &target)
        {
            if (target.width <= OverlayMarginX * 2u)
            {
                return 0u;
            }
            return (target.width - OverlayMarginX * 2u) / GlyphPixelSize;
        }

        /// Returns the maximum number of text rows that fit in the target attachment.
        uint32_t calculateMaxRows(const DiagnosticsOverlayTarget &target)
        {
            if (target.height <= OverlayMarginY * 2u)
            {
                return 0u;
            }
            return (target.height - OverlayMarginY * 2u) / GlyphLineHeight;
        }

        /// Finds the 5x7 bitmap rows for a printable ASCII character.
        const uint8_t *findGlyphRows(char character)
        {
            for (const GlyphBitmap &glyph : FontGlyphs)
            {
                if (glyph.character == character)
                {
                    return glyph.rows;
                }
            }
            for (const GlyphBitmap &glyph : FontGlyphs)
            {
                if (glyph.character == '?')
                {
                    return glyph.rows;
                }
            }
            return nullptr;
        }

        /// Writes one glyph bitmap into the 8x8 font atlas cell for the requested ASCII code.
        void rasterizeGlyph(uint32_t asciiCode, eastl::vector<uint8_t> &atlas)
        {
            const uint32_t normalizedCode = asciiCode >= 32u && asciiCode <= 126u ? asciiCode - 32u : static_cast<uint32_t>('?' - 32);
            const uint32_t cellX = normalizedCode % AtlasColumns;
            const uint32_t cellY = normalizedCode / AtlasColumns;
            const uint8_t *rows = findGlyphRows(static_cast<char>(asciiCode));
            if (rows == nullptr)
            {
                return;
            }

            for (uint32_t y = 0u; y < 7u; ++y)
            {
                for (uint32_t x = 0u; x < 5u; ++x)
                {
                    if ((rows[y] & (uint8_t{1u} << (4u - x))) == 0u)
                    {
                        continue;
                    }
                    const uint32_t atlasX = cellX * AtlasCellSize + x + 1u;
                    const uint32_t atlasY = cellY * AtlasCellSize + y;
                    atlas[atlasY * AtlasWidth + atlasX] = 255u;
                }
            }
        }

        /// Builds the runtime CPU font atlas uploaded to the backend texture.
        eastl::vector<uint8_t> buildFontAtlasPixels()
        {
            eastl::vector<uint8_t> atlas;
            atlas.resize(AtlasWidth * AtlasHeight, 0u);
            for (uint32_t asciiCode = 32u; asciiCode <= 126u; ++asciiCode)
            {
                rasterizeGlyph(asciiCode, atlas);
            }
            return atlas;
        }

        /// Returns true for overlay-owned resources that should not appear inside the overlay's own resource rows.
        bool isInternalDiagnosticsOverlayResource(const eastl::string &label)
        {
            constexpr const char *prefix = "DiagnosticsOverlay.";
            constexpr size_t prefixLength = 19u;
            return label.size() >= prefixLength && std::memcmp(label.data(), prefix, prefixLength) == 0;
        }

        /// Owns the low-dependency text diagnostics overlay appended by the RHI private queue wrapper.
        class DiagnosticsOverlayController final : public RefCountedObject
        {
        public:
            /// Creates a diagnostics overlay controller from a device configuration and keeps all work disabled when the config is off.
            DiagnosticsOverlayController(Device device, DiagnosticsOverlayShaderProgramProvider shaderProvider);
            /// Releases transient overlay buffers, atlas resources, and timestamp resolve buffers owned by the controller.
            ~DiagnosticsOverlayController() override;

            DiagnosticsOverlayController(const DiagnosticsOverlayController &) = delete;
            DiagnosticsOverlayController &operator=(const DiagnosticsOverlayController &) = delete;

            /// Releases all GPU resources owned by the controller while the device is still alive.
            void shutdown();

            /// Returns true when the controller should observe passes and append overlay rendering work.
            [[nodiscard]]
            bool isEnabled() const;

            /// Allocates one internal timestamp scope for an automatically timed pass and records its slot metadata.
            GpuTimestampFrameProfiler::Scope allocatePassScope(const eastl::string &label, DiagnosticsOverlayRecordedScope &outScope, bool &outRecorded);

            /// Releases pending scope counts for encoders that were destroyed without being submitted.
            void abandonRecordedScopes(const eastl::vector<DiagnosticsOverlayRecordedScope> &scopes);

            /// Builds an optional trailing command encoder that draws the overlay and resolves only the scopes recorded in this submit.
            CommandEncoder buildPostSubmitEncoder(
                Queue innerQueue,
                const DiagnosticsOverlayTarget &target,
                const eastl::vector<DiagnosticsOverlayRecordedScope> &submittedScopes,
                bool &outResolvedTimestamps);

            /// Reads delayed timestamp results and advances frame-local overlay state after queue submission.
            void afterSubmit(
                Queue innerQueue,
                const Submission &completion,
                bool hasTrackedCompletion,
                bool hadRenderTarget,
                bool resolvedTimestamps,
                const eastl::vector<DiagnosticsOverlayRecordedScope> &submittedScopes);

        private:
            /// Initializes timestamp profiler slots when the device supports pass timestamp writes.
            void initializeTimestampProfilers();
            /// Releases timestamp resolve buffers created by profiler slots.
            void releaseTimestampProfilerBuffers();
            /// Begins or returns the currently open timestamp profiler frame under mMutex.
            DiagnosticsOverlayProfilerFrame *beginTimestampFrameIfNeededLocked();
            /// Appends the diagnostics overlay render pass to the supplied real command encoder under mMutex.
            void appendOverlayPassLocked(CommandEncoder encoder, Queue uploadQueue, const DiagnosticsOverlayTarget &target);
            /// Creates or returns the overlay pipeline compatible with the target color format.
            RenderPipeline getPipelineForFormat(TextureFormat format);
            /// Ensures the selected vertex upload buffer slot can hold the requested byte count.
            Buffer ensureVertexBufferCapacity(uint32_t slotIndex, uint64_t requiredBytes);
            /// Ensures the runtime font atlas texture, sampler, and bind group exist and are uploaded.
            bool ensureFontAtlasResources(Queue uploadQueue);
            /// Builds display text rows from the latest CPU and GPU diagnostics state.
            void buildTextLines(eastl::vector<eastl::string> &lines);
            /// Converts text rows into expanded glyph triangle vertices for the current target.
            void buildTextVertices(const eastl::vector<eastl::string> &lines, const DiagnosticsOverlayTarget &target);
            /// Returns whether this controller already logged an unsupported target format warning.
            bool hasWarnedUnsupportedFormat(TextureFormat format) const;
            /// Marks an unsupported target format as already reported.
            void rememberUnsupportedFormat(TextureFormat format);

            mutable std::mutex mMutex;
            Device mDevice = nullptr;
            Logger mLogger = nullptr;
            RuntimeDiagnosticsOverlayConfig mConfig = {};
            DiagnosticsOverlayShaderProgramProvider mShaderProvider = {};
            ShaderModule mVertexShader = nullptr;
            ShaderModule mFragmentShader = nullptr;
            BindGroupLayout mFontBindGroupLayout = nullptr;
            PipelineLayout mPipelineLayout = nullptr;
            BindGroup mFontBindGroup = nullptr;
            Texture mFontAtlasTexture = {};
            TextureView mFontAtlasView = {};
            Sampler mFontSampler = {};
            eastl::array<DiagnosticsOverlayVertexBufferSlot, VertexBufferRingCount> mVertexBuffers = {};
            uint32_t mNextVertexBufferSlot = 0u;
            eastl::vector<DiagnosticsOverlayPipelineEntry> mPipelines;
            eastl::vector<DiagnosticsOverlayProfilerFrame> mProfilerFrames;
            eastl::vector<TimestampRangeResult> mLastPassTimes;
            eastl::vector<DiagnosticsOverlayVertex> mVertices;
            eastl::vector<TextureFormat> mWarnedUnsupportedFormats;
            std::chrono::steady_clock::time_point mLastFrameTime = {};
            bool mEnabled = false;
            bool mTimestampAvailable = false;
            bool mHasLastFrameTime = false;
            bool mTimestampWarningLogged = false;
            bool mFailureLogged = false;
            uint32_t mActiveProfilerSlot = 0u;
            uint64_t mFrameIndex = 0u;
        };

        /// Wraps a real command encoder so overlay profiling and target tracking stay local to each encoder until submit.
        class DiagnosticsOverlayCommandEncoder final : public CommandEncoderImpl
        {
        public:
            /// Creates a command encoder wrapper over a backend-native command encoder.
            DiagnosticsOverlayCommandEncoder(eastl::intrusive_ptr<DiagnosticsOverlayController> controller, CommandEncoder innerEncoder);
            /// Releases any unsubmitted timestamp scope reservations if the encoder was discarded before submission.
            ~DiagnosticsOverlayCommandEncoder() override;

            /// Returns the backend-native command encoder used for actual submission.
            [[nodiscard]]
            CommandEncoder getInnerEncoder() const;

            /// Returns whether the wrapped encoder has already been ended.
            [[nodiscard]]
            bool isEnded() const;

            /// Returns the last valid color render-pass target observed in this encoder.
            [[nodiscard]]
            const DiagnosticsOverlayTarget &getLastTarget() const;

            /// Moves recorded automatic timestamp scopes to the submitting queue and marks this encoder submitted.
            void consumeSubmissionMetadata(eastl::vector<DiagnosticsOverlayRecordedScope> &outScopes);

            /// Forwards begin to the backend-native command encoder.
            void begin() override;

            /// Injects overlay timestamp writes when possible and forwards render pass creation.
            RenderPassEncoder beginRenderPass(const RenderPassDescriptor &pass) override;

            /// Injects overlay timestamp writes when possible and forwards blit pass creation.
            BlitPassEncoder beginBlitPass(const BlitPassDescriptor &pass) override;

            /// Injects overlay timestamp writes when possible and forwards compute pass creation.
            ComputePassEncoder beginComputePass(const ComputePassDescriptor &pass) override;

            /// Forwards query-set resolve commands to the backend-native encoder.
            void resolveQuerySet(QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, BufferRange destination) override;

            /// Ends the backend-native encoder without mutating shared overlay resources on worker threads.
            void end() override;

        private:
            eastl::intrusive_ptr<DiagnosticsOverlayController> mController = nullptr;
            CommandEncoder mInnerEncoder = nullptr;
            DiagnosticsOverlayTarget mLastTarget = {};
            eastl::vector<DiagnosticsOverlayRecordedScope> mRecordedScopes;
            bool mEnded = false;
            bool mSubmitted = false;
        };

        /// Wraps a backend queue to make diagnostics overlay injection transparent to Core and raw RHI users.
        class DiagnosticsOverlayQueue final : public QueueImpl
        {
        public:
            /// Creates a queue wrapper that delegates all real work to the supplied backend queue.
            DiagnosticsOverlayQueue(Device device, Queue innerQueue, DiagnosticsOverlayShaderProgramProvider shaderProvider);

            /// Destroys overlay-owned resources without taking ownership of the backend queue.
            ~DiagnosticsOverlayQueue() override;

            /// Forwards per-queue timestamp query capabilities from the backend queue.
            TimestampQuerySupport getTimestampQuerySupport() const override;
            /// Forwards per-queue minimum image transfer granularity from the backend queue.
            Extent3D getImageTransferGranularity() const override;
            /// Forwards per-queue safe unsubmitted command encoder capacity from the backend queue.
            uint32_t getMaxUnsubmittedCommandEncoderCount() const override;
            /// Creates a wrapped command encoder backed by the real queue.
            CommandEncoder createCommandEncoder() override;
            /// Forwards a buffer upload to the backend queue.
            void writeBuffer(BufferRange buffer, void const *data, uint64_t size) override;
            /// Forwards a buffer readback to the backend queue.
            void readBuffer(BufferRange buffer, void *data, uint64_t size) override;
            /// Forwards a texture upload to the backend queue.
            void writeTexture(const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize) override;
            /// Forwards a texture readback to the backend queue.
            void readTexture(const ImageCopyTexture &source, void *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &readSize) override;
            /// Forwards a mip-chain texture upload from CPU memory to the backend queue.
            void uploadTexture(Texture destination, void const *data, uint64_t dataStorageBytes, const eastl::vector<uint64_t> &mipmapOffsetBytes) override;
            /// Forwards a mip-chain texture upload from GPU memory to the backend queue.
            void uploadTexture(Texture destination, BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes) override;
            /// Forwards a buffer-to-buffer copy to the backend queue.
            void copyBufferToBuffer(BufferRange source, BufferRange destination) override;
            /// Forwards a buffer-to-texture copy to the backend queue.
            void copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize) override;
            /// Forwards a texture-to-buffer copy to the backend queue.
            void copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize) override;
            /// Forwards an indirect multi-region buffer copy to the backend queue.
            void copyBufferToBufferMultipleRegion(Buffer source, Buffer destination, Buffer regions, uint32_t regionCount) override;
            /// Forwards a buffer fill command to the backend queue.
            void fillBuffer(BufferRange source, uint8_t value) override;
            /// Forwards an asynchronous buffer readback to the backend queue.
            Readback readBufferAsync(BufferRange source) override;
            /// Forwards an asynchronous texture readback to the backend queue.
            Readback readTextureAsync(const ImageCopyTexture &source, const TextureDataLayout &dataLayout, const Extent3D &readSize) override;
            /// Ends wrapped encoders as needed, submits real backend encoders, and advances overlay readback state.
            void submit(const eastl::vector<CommandEncoder> &encoders) override;
            /// Forwards explicit GPU dependencies and returns the underlying completion handle.
            Submission submit(const eastl::vector<CommandEncoder> &encoders, const eastl::vector<Submission> &waits) override;
            /// Releases overlay state while leaving backend queue lifetime with the owning device.
            void destroy() override;

        private:
            /// Unwraps recorded encoders, collects submission-ordered target and timestamp metadata, and preserves closed-encoder checks.
            eastl::vector<CommandEncoder> unwrapEncoders(
                const eastl::vector<CommandEncoder> &encoders,
                bool requireClosed,
                DiagnosticsOverlayTarget &outTarget,
                eastl::vector<DiagnosticsOverlayRecordedScope> &outScopes);
            eastl::intrusive_ptr<DiagnosticsOverlayController> mController;
            Queue mInnerQueue = nullptr;
        };

        DiagnosticsOverlayController::DiagnosticsOverlayController(Device device, DiagnosticsOverlayShaderProgramProvider shaderProvider)
            : mDevice(device)
            , mLogger(device != nullptr ? device->getLogger() : Logger{})
            , mShaderProvider(shaderProvider)
        {
            if (mDevice == nullptr)
            {
                return;
            }

            mConfig = mDevice->getDiagnosticsOverlayConfig();
            mEnabled = mConfig.enabled != False;
            if (!mEnabled)
            {
                return;
            }

            if (mConfig.maxTextGlyphs == 0u || mShaderProvider.createShaderModule == nullptr || mShaderProvider.vertexEntryPoint == nullptr || mShaderProvider.fragmentEntryPoint == nullptr)
            {
                mEnabled = false;
                return;
            }

            if (mConfig.showPassTimes != False)
            {
                initializeTimestampProfilers();
            }
        }

        DiagnosticsOverlayController::~DiagnosticsOverlayController()
        {
            shutdown();
        }

        void DiagnosticsOverlayController::shutdown()
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mEnabled = false;
            mTimestampAvailable = false;
            mPipelines.clear();
            mFontBindGroup = nullptr;
            mPipelineLayout = nullptr;
            mFontBindGroupLayout = nullptr;
            mVertexShader = nullptr;
            mFragmentShader = nullptr;
            if (mDevice != nullptr)
            {
                for (DiagnosticsOverlayVertexBufferSlot &slot : mVertexBuffers)
                {
                    if (!slot.buffer.isNull())
                    {
                        mDevice->freeBuffer(slot.buffer);
                        slot.buffer.reset();
                        slot.capacityBytes = 0u;
                    }
                }
                if (!mFontAtlasTexture.isNull())
                {
                    mDevice->freeTexture(mFontAtlasTexture);
                    mFontAtlasTexture.reset();
                }
                mFontAtlasView.reset();
                if (!mFontSampler.isNull())
                {
                    mDevice->freeSampler(mFontSampler);
                    mFontSampler.reset();
                }
                releaseTimestampProfilerBuffers();
            }
            mProfilerFrames.clear();
            mDevice = nullptr;
        }

        bool DiagnosticsOverlayController::isEnabled() const
        {
            std::lock_guard<std::mutex> lock(mMutex);
            return mEnabled;
        }

        GpuTimestampFrameProfiler::Scope DiagnosticsOverlayController::allocatePassScope(
            const eastl::string &label,
            DiagnosticsOverlayRecordedScope &outScope,
            bool &outRecorded)
        {
            outRecorded = false;
            std::lock_guard<std::mutex> lock(mMutex);
            if (!mEnabled || !mTimestampAvailable || mConfig.showPassTimes == False)
            {
                return {};
            }

            DiagnosticsOverlayProfilerFrame *frame = beginTimestampFrameIfNeededLocked();
            if (frame == nullptr || frame->scopeCount >= mConfig.maxPassScopes)
            {
                return {};
            }

            GpuTimestampFrameProfiler::Scope scope = frame->profiler.writePass(label);
            outScope.slotIndex = mActiveProfilerSlot;
            outScope.slotGeneration = frame->generation;
            outScope.firstQuery = scope.timestampWrites.beginningOfPassWriteIndex;
            frame->used = true;
            ++frame->scopeCount;
            ++frame->pendingScopes;
            outRecorded = true;
            return scope;
        }

        void DiagnosticsOverlayController::abandonRecordedScopes(const eastl::vector<DiagnosticsOverlayRecordedScope> &scopes)
        {
            if (scopes.empty())
            {
                return;
            }

            std::lock_guard<std::mutex> lock(mMutex);
            for (const DiagnosticsOverlayRecordedScope &scope : scopes)
            {
                if (scope.slotIndex >= mProfilerFrames.size())
                {
                    continue;
                }
                DiagnosticsOverlayProfilerFrame &frame = mProfilerFrames[scope.slotIndex];
                if (frame.generation == scope.slotGeneration && frame.pendingScopes > 0u)
                {
                    --frame.pendingScopes;
                }
            }
        }

        CommandEncoder DiagnosticsOverlayController::buildPostSubmitEncoder(
            Queue innerQueue,
            const DiagnosticsOverlayTarget &target,
            const eastl::vector<DiagnosticsOverlayRecordedScope> &submittedScopes,
            bool &outResolvedTimestamps)
        {
            outResolvedTimestamps = false;
            std::lock_guard<std::mutex> lock(mMutex);
            eastl::vector<DiagnosticsOverlayRecordedScope> liveScopes;
            liveScopes.reserve(submittedScopes.size());
            for (const DiagnosticsOverlayRecordedScope &scope : submittedScopes)
            {
                if (scope.slotIndex >= mProfilerFrames.size())
                {
                    continue;
                }
                DiagnosticsOverlayProfilerFrame &frame = mProfilerFrames[scope.slotIndex];
                if (frame.generation != scope.slotGeneration)
                {
                    continue;
                }
                if (frame.pendingScopes > 0u)
                {
                    --frame.pendingScopes;
                }
                if (frame.initialized)
                {
                    liveScopes.push_back(scope);
                }
            }

            const bool shouldDrawOverlay =
                mEnabled && innerQueue != nullptr && !target.view.isNull() && target.width > 0u && target.height > 0u;
            const bool shouldResolveTimestamps =
                mEnabled && mTimestampAvailable && innerQueue != nullptr && !liveScopes.empty();
            if (!shouldDrawOverlay && !shouldResolveTimestamps)
            {
                return nullptr;
            }

            CommandEncoder postEncoder = innerQueue->createCommandEncoder();
            if (postEncoder == nullptr)
            {
                return nullptr;
            }
            postEncoder->begin();

            if (shouldDrawOverlay)
            {
                appendOverlayPassLocked(postEncoder, innerQueue, target);
            }

            if (shouldResolveTimestamps)
            {
                eastl::sort(liveScopes.begin(), liveScopes.end(), isRecordedScopeBefore);
                size_t cursor = 0u;
                while (cursor < liveScopes.size())
                {
                    const DiagnosticsOverlayRecordedScope &first = liveScopes[cursor];
                    DiagnosticsOverlayProfilerFrame &frame = mProfilerFrames[first.slotIndex];
                    const uint32_t rangeFirstQuery = first.firstQuery;
                    uint32_t rangeQueryCount = 2u;

                    size_t next = cursor + 1u;
                    while (next < liveScopes.size())
                    {
                        const DiagnosticsOverlayRecordedScope &candidate = liveScopes[next];
                        if (candidate.slotIndex != first.slotIndex ||
                            candidate.slotGeneration != first.slotGeneration ||
                            candidate.firstQuery != rangeFirstQuery + rangeQueryCount)
                        {
                            break;
                        }
                        rangeQueryCount += 2u;
                        ++next;
                    }

                    QuerySet querySet = frame.profiler.getQuerySet();
                    Buffer resolveBuffer = frame.profiler.getResolveBuffer();
                    if (querySet != nullptr && !resolveBuffer.isNull())
                    {
                        const uint32_t rawStrideBytes = querySet->getResultStrideBytes();
                        const uint64_t strideBytes = rawStrideBytes > 0u ? static_cast<uint64_t>(rawStrideBytes) : sizeof(uint64_t);
                        postEncoder->resolveQuerySet(
                            querySet,
                            rangeFirstQuery,
                            rangeQueryCount,
                            BufferRange(resolveBuffer, static_cast<uint64_t>(rangeFirstQuery) * strideBytes, static_cast<uint64_t>(rangeQueryCount) * strideBytes));
                        outResolvedTimestamps = true;
                    }
                    cursor = next;
                }
            }

            postEncoder->end();
            return postEncoder;
        }

        void DiagnosticsOverlayController::appendOverlayPassLocked(
            CommandEncoder encoder,
            Queue uploadQueue,
            const DiagnosticsOverlayTarget &target)
        {
            if (!mEnabled || encoder == nullptr || uploadQueue == nullptr || target.view.isNull() || target.width == 0u || target.height == 0u)
            {
                return;
            }

            if (!isSupportedOverlayTargetFormat(target.format))
            {
                if (!hasWarnedUnsupportedFormat(target.format))
                {
                    GVMLogWarn(mLogger, OverlayLogCategory, "event=overlay_unsupported_target_format format={}", static_cast<uint32_t>(target.format));
                    rememberUnsupportedFormat(target.format);
                }
                return;
            }

            try
            {
                if (!ensureFontAtlasResources(uploadQueue))
                {
                    return;
                }

                eastl::vector<eastl::string> lines;
                buildTextLines(lines);
                buildTextVertices(lines, target);
                if (mVertices.empty())
                {
                    return;
                }

                const uint64_t vertexBytes = static_cast<uint64_t>(mVertices.size()) * sizeof(DiagnosticsOverlayVertex);
                const uint32_t bufferSlot = mNextVertexBufferSlot;
                mNextVertexBufferSlot = (mNextVertexBufferSlot + 1u) % VertexBufferRingCount;
                Buffer vertexBuffer = ensureVertexBufferCapacity(bufferSlot, vertexBytes);
                vertexBuffer->map();
                void *vertexData = vertexBuffer->getMappedRange(0u, vertexBytes);
                std::memcpy(vertexData, mVertices.data(), static_cast<size_t>(vertexBytes));
                vertexBuffer->unmap();
                RenderPipeline pipeline = getPipelineForFormat(target.format);
                BufferRange vertexRange(vertexBuffer, 0u, vertexBytes);

                RenderPassDescriptor overlayPass = {};
                overlayPass.label = "DiagnosticsOverlay";
                overlayPass.colorAttachments.push_back({
                    .view = target.view,
                    .loadOp = LoadOp::Load,
                    .storeOp = StoreOp::Store,
                    .clearValue = {0.0, 0.0, 0.0, 0.0},
                });

                RenderPassEncoder passEncoder = encoder->beginRenderPass(overlayPass);
                passEncoder->setViewport(0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f);
                passEncoder->setScissorRect(0u, 0u, target.width, target.height);
                passEncoder->setPipeline(pipeline);
                passEncoder->setBindGroup(mFontBindGroup, 0u);
                passEncoder->setVertexBuffer(vertexRange, 0u);
                passEncoder->draw(static_cast<uint32_t>(mVertices.size()), 1u, 0u, 0u);
                passEncoder->end();
            }
            catch (const std::exception &error)
            {
                if (!mFailureLogged)
                {
                    GVMLogWarn(mLogger, OverlayLogCategory, "event=overlay_disabled_after_failure error=\"{}\"", error.what());
                    mFailureLogged = true;
                }
                mEnabled = false;
            }
        }

        void DiagnosticsOverlayController::afterSubmit(
            Queue innerQueue,
            const Submission &completion,
            bool hasTrackedCompletion,
            bool hadRenderTarget,
            bool resolvedTimestamps,
            const eastl::vector<DiagnosticsOverlayRecordedScope> &submittedScopes)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (!mEnabled || (!hadRenderTarget && submittedScopes.empty()))
            {
                return;
            }

            for (const DiagnosticsOverlayRecordedScope &scope : submittedScopes)
            {
                if (scope.slotIndex >= mProfilerFrames.size())
                {
                    continue;
                }
                DiagnosticsOverlayProfilerFrame &slot = mProfilerFrames[scope.slotIndex];
                if (slot.generation != scope.slotGeneration)
                {
                    continue;
                }
                if (resolvedTimestamps && slot.initialized)
                {
                    const uint32_t ordinal = scope.firstQuery / 2u;
                    if (ordinal < slot.scopeResolved.size())
                    {
                        slot.scopeResolved[ordinal] = true;
                    }
                }
                slot.submittedFrameIndex = mFrameIndex;
                if (hasTrackedCompletion)
                {
                    slot.lastSubmission = completion;
                    slot.hasTrackedSubmission = true;
                }
            }

            if (mActiveProfilerSlot < mProfilerFrames.size())
            {
                DiagnosticsOverlayProfilerFrame &activeFrame = mProfilerFrames[mActiveProfilerSlot];
                if (activeFrame.frameOpen && (hadRenderTarget || activeFrame.scopeCount >= mConfig.maxPassScopes))
                {
                    activeFrame.frameOpen = false;
                    activeFrame.submittedFrameIndex = mFrameIndex;
                }
            }

            if (mTimestampAvailable && innerQueue != nullptr && !mProfilerFrames.empty())
            {
                const uint32_t latency = mProfilerFrames.size() > 1u
                    ? eastl::min<uint32_t>(mConfig.timestampReadbackLatencyFrames, static_cast<uint32_t>(mProfilerFrames.size() - 1u))
                    : 0u;
                uint32_t readySlot = static_cast<uint32_t>(mProfilerFrames.size());
                uint64_t oldestSubmittedFrame = UINT64_MAX;
                for (uint32_t slotIndex = 0u; slotIndex < static_cast<uint32_t>(mProfilerFrames.size()); ++slotIndex)
                {
                    const DiagnosticsOverlayProfilerFrame &candidate = mProfilerFrames[slotIndex];
                    if (!candidate.initialized || candidate.frameOpen || !candidate.used || candidate.pendingScopes != 0u)
                    {
                        continue;
                    }
                    if (mFrameIndex < candidate.submittedFrameIndex + latency)
                    {
                        continue;
                    }
                    if (candidate.hasTrackedSubmission && !candidate.lastSubmission.isComplete())
                    {
                        continue;
                    }
                    if (candidate.submittedFrameIndex < oldestSubmittedFrame)
                    {
                        oldestSubmittedFrame = candidate.submittedFrameIndex;
                        readySlot = slotIndex;
                    }
                }

                if (readySlot < mProfilerFrames.size())
                {
                    DiagnosticsOverlayProfilerFrame &readFrame = mProfilerFrames[readySlot];
                    try
                    {
                        readFrame.profiler.readbackBlocking(innerQueue, readFrame.rawResults);
                        const eastl::vector<TimestampRangeResult> allResults = readFrame.profiler.buildRangeResults(readFrame.rawResults);
                        mLastPassTimes.clear();
                        mLastPassTimes.reserve(allResults.size());
                        for (const TimestampRangeResult &result : allResults)
                        {
                            const uint32_t ordinal = result.beginIndex / 2u;
                            if (ordinal < readFrame.scopeResolved.size() && readFrame.scopeResolved[ordinal])
                            {
                                mLastPassTimes.push_back(result);
                            }
                        }
                        readFrame.used = false;
                        readFrame.scopeCount = 0u;
                        readFrame.hasTrackedSubmission = false;
                        readFrame.lastSubmission = {};
                    }
                    catch (const std::exception &error)
                    {
                        if (!mTimestampWarningLogged)
                        {
                            GVMLogWarn(mLogger, OverlayLogCategory, "event=timestamp_readback_disabled error=\"{}\"", error.what());
                            mTimestampWarningLogged = true;
                        }
                        mTimestampAvailable = false;
                    }
                }
            }

            ++mFrameIndex;
        }

        void DiagnosticsOverlayController::initializeTimestampProfilers()
        {
            if (mConfig.maxPassScopes == 0u)
            {
                return;
            }

            const TimestampQuerySupport support = mDevice->getTimestampQuerySupport();
            if (support.supported == False || support.passTimestampWritesSupported == False)
            {
                return;
            }

            const uint32_t frameCount = eastl::max(6u, mConfig.timestampReadbackLatencyFrames * 2u + 2u);
            mProfilerFrames.resize(frameCount);
            try
            {
                for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
                {
                    DiagnosticsOverlayProfilerFrame &frame = mProfilerFrames[frameIndex];
                    frame.profiler.init(mDevice, mConfig.maxPassScopes, makeFormattedString("DiagnosticsOverlay.TimestampFrame%u", frameIndex));
                    frame.scopeResolved.assign(mConfig.maxPassScopes, false);
                    frame.initialized = true;
                }
                mTimestampAvailable = true;
            }
            catch (const std::exception &error)
            {
                if (!mTimestampWarningLogged)
                {
                    GVMLogWarn(mLogger, OverlayLogCategory, "event=timestamp_profiler_unavailable error=\"{}\"", error.what());
                    mTimestampWarningLogged = true;
                }
                releaseTimestampProfilerBuffers();
                mProfilerFrames.clear();
                mTimestampAvailable = false;
            }
        }

        void DiagnosticsOverlayController::releaseTimestampProfilerBuffers()
        {
            if (mDevice == nullptr)
            {
                return;
            }

            for (DiagnosticsOverlayProfilerFrame &frame : mProfilerFrames)
            {
                Buffer resolveBuffer = frame.profiler.getResolveBuffer();
                if (!resolveBuffer.isNull())
                {
                    mDevice->freeBuffer(resolveBuffer);
                }
                frame.initialized = false;
                frame.used = false;
                frame.frameOpen = false;
                frame.pendingScopes = 0u;
                frame.hasTrackedSubmission = false;
                frame.lastSubmission = {};
            }
        }

        DiagnosticsOverlayProfilerFrame *DiagnosticsOverlayController::beginTimestampFrameIfNeededLocked()
        {
            if (mProfilerFrames.empty())
            {
                return nullptr;
            }

            if (mActiveProfilerSlot < mProfilerFrames.size())
            {
                DiagnosticsOverlayProfilerFrame &current = mProfilerFrames[mActiveProfilerSlot];
                if (current.initialized && current.frameOpen)
                {
                    return &current;
                }
            }

            const uint32_t slotCount = static_cast<uint32_t>(mProfilerFrames.size());
            uint32_t chosenSlot = slotCount;
            for (uint32_t offset = 1u; offset <= slotCount; ++offset)
            {
                const uint32_t candidateIndex = (mActiveProfilerSlot + offset) % slotCount;
                const DiagnosticsOverlayProfilerFrame &candidate = mProfilerFrames[candidateIndex];
                if (candidate.initialized && !candidate.frameOpen && candidate.pendingScopes == 0u && !candidate.used)
                {
                    chosenSlot = candidateIndex;
                    break;
                }
            }

            if (chosenSlot >= slotCount)
            {
                for (uint32_t offset = 1u; offset <= slotCount; ++offset)
                {
                    const uint32_t candidateIndex = (mActiveProfilerSlot + offset) % slotCount;
                    const DiagnosticsOverlayProfilerFrame &candidate = mProfilerFrames[candidateIndex];
                    if (candidate.initialized && !candidate.frameOpen && candidate.pendingScopes == 0u &&
                        (!candidate.hasTrackedSubmission || candidate.lastSubmission.isComplete()))
                    {
                        chosenSlot = candidateIndex;
                        break;
                    }
                }
            }

            if (chosenSlot >= slotCount)
            {
                return nullptr;
            }

            mActiveProfilerSlot = chosenSlot;
            DiagnosticsOverlayProfilerFrame &frame = mProfilerFrames[chosenSlot];
            frame.profiler.reset();
            frame.rawResults.clear();
            frame.scopeResolved.assign(mConfig.maxPassScopes, false);
            frame.hasTrackedSubmission = false;
            frame.lastSubmission = {};
            frame.frameOpen = true;
            frame.used = false;
            frame.scopeCount = 0u;
            frame.pendingScopes = 0u;
            ++frame.generation;
            frame.submittedFrameIndex = mFrameIndex;
            return &frame;
        }

        RenderPipeline DiagnosticsOverlayController::getPipelineForFormat(TextureFormat format)
        {
            for (const DiagnosticsOverlayPipelineEntry &entry : mPipelines)
            {
                if (entry.format == format)
                {
                    return entry.pipeline;
                }
            }

            if (mVertexShader == nullptr)
            {
                mVertexShader = mShaderProvider.createShaderModule(mDevice, DiagnosticsOverlayShaderStage::Vertex);
            }
            if (mFragmentShader == nullptr)
            {
                mFragmentShader = mShaderProvider.createShaderModule(mDevice, DiagnosticsOverlayShaderStage::Fragment);
            }
            if (mPipelineLayout == nullptr)
            {
                PipelineLayoutDescriptor descriptor = {};
                descriptor.label = "DiagnosticsOverlay.PipelineLayout";
                descriptor.bindGroupLayouts = {mFontBindGroupLayout};
                mPipelineLayout = mDevice->createPipelineLayout(descriptor);
            }

            BlendState blend = {};
            blend.color.operation = BlendOperation::Add;
            blend.color.srcFactor = BlendFactor::SrcAlpha;
            blend.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
            blend.alpha.operation = BlendOperation::Add;
            blend.alpha.srcFactor = BlendFactor::One;
            blend.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;

            RenderPipelineDescriptor descriptor = {};
            descriptor.label = "DiagnosticsOverlay.RenderPipeline";
            descriptor.layout = mPipelineLayout;
            descriptor.vertex.module = mVertexShader;
            descriptor.vertex.entryPoint = mShaderProvider.vertexEntryPoint;
            descriptor.vertex.buffers.push_back({
                .arrayStride = sizeof(DiagnosticsOverlayVertex),
                .stepMode = VertexStepMode::Vertex,
                .attributes = {
                    {
                        .format = VertexFormat::Float32x4,
                        .offset = 0u,
                        .shaderLocation = 0u,
                    },
                    {
                        .format = VertexFormat::Float32,
                        .offset = sizeof(float) * 4u,
                        .shaderLocation = 1u,
                    },
                },
            });
            descriptor.primitive.topology = PrimitiveTopology::TriangleList;
            descriptor.primitive.stripIndexFormat = IndexFormat::Undefined;
            descriptor.primitive.frontFace = FrontFace::CW;
            descriptor.primitive.cullMode = CullMode::None;
            descriptor.fragment.module = mFragmentShader;
            descriptor.fragment.entryPoint = mShaderProvider.fragmentEntryPoint;
            descriptor.fragment.targets.push_back({
                .format = format,
                .blend = blend,
                .blendEnabled = True,
                .writeMask = ColorWriteMask::All,
            });

            DiagnosticsOverlayPipelineEntry entry = {};
            entry.format = format;
            entry.pipeline = mDevice->createRenderPipeline(descriptor);
            mPipelines.push_back(entry);
            return entry.pipeline;
        }

        Buffer DiagnosticsOverlayController::ensureVertexBufferCapacity(uint32_t slotIndex, uint64_t requiredBytes)
        {
            DiagnosticsOverlayVertexBufferSlot &slot = mVertexBuffers[slotIndex % VertexBufferRingCount];
            if (requiredBytes <= slot.capacityBytes && !slot.buffer.isNull())
            {
                return slot.buffer;
            }

            if (!slot.buffer.isNull())
            {
                mDevice->freeBuffer(slot.buffer);
                slot.buffer.reset();
            }

            slot.capacityBytes = calculateExpandedBufferSize(requiredBytes);
            BufferDescriptor descriptor = {};
            descriptor.label = makeFormattedString("DiagnosticsOverlay.GlyphVertices%u", slotIndex % VertexBufferRingCount);
            descriptor.usage = BufferUsage::Vertex | BufferUsage::CopyDst | BufferUsage::MapWrite;
            descriptor.size = slot.capacityBytes;
            slot.buffer = mDevice->createBuffer(descriptor);
            return slot.buffer;
        }

        bool DiagnosticsOverlayController::ensureFontAtlasResources(Queue uploadQueue)
        {
            if (mFontBindGroup != nullptr)
            {
                return true;
            }
            if (mDevice == nullptr || uploadQueue == nullptr)
            {
                return false;
            }

            if (mFontAtlasTexture.isNull())
            {
                TextureDescriptor textureDescriptor = {};
                textureDescriptor.label = "DiagnosticsOverlay.FontAtlas";
                textureDescriptor.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
                textureDescriptor.dimension = TextureDimension::e2D;
                textureDescriptor.size = {AtlasWidth, AtlasHeight, 1u};
                textureDescriptor.format = TextureFormat::R8Unorm;
                textureDescriptor.mipLevelCount = 1u;
                textureDescriptor.arrayLayerCount = 1u;
                mFontAtlasTexture = mDevice->createTexture(textureDescriptor);
                mFontAtlasView = mFontAtlasTexture->createView();

                const eastl::vector<uint8_t> atlasPixels = buildFontAtlasPixels();
                uploadQueue->writeTexture(
                    {.texture = mFontAtlasTexture, .mipLevel = 0u, .origin = {0u, 0u, 0u}, .aspect = TextureAspect::All},
                    atlasPixels.data(),
                    atlasPixels.size(),
                    {.offset = 0u, .bytesPerRow = AtlasWidth, .rowsPerImage = AtlasHeight},
                    {AtlasWidth, AtlasHeight, 1u});
            }

            if (mFontSampler.isNull())
            {
                mFontSampler = mDevice->createSampler({
                    .label = "DiagnosticsOverlay.FontSampler",
                    .addressModeU = AddressMode::ClampToEdge,
                    .addressModeV = AddressMode::ClampToEdge,
                    .addressModeW = AddressMode::ClampToEdge,
                    .magFilter = FilterMode::Nearest,
                    .minFilter = FilterMode::Nearest,
                    .mipmapFilter = MipmapFilterMode::Nearest,
                    .lodMinClamp = 0.0f,
                    .lodMaxClamp = 0.0f,
                    .compare = CompareFunction::Undefined,
                    .maxAnisotropy = 0,
                });
            }

            if (mFontBindGroupLayout == nullptr)
            {
                mFontBindGroupLayout = mDevice->createBindGroupLayout({
                    .label = "DiagnosticsOverlay.FontBindGroupLayout",
                    .entries = {
                        {
                            .binding = 0u,
                            .visibility = ShaderStage::Fragment,
                            .texture = {
                                .sampleType = TextureSampleType::Float,
                                .viewDimension = TextureViewDimension::e2D,
                            },
                        },
                        {
                            .binding = 1u,
                            .visibility = ShaderStage::Fragment,
                            .sampler = {
                                .type = SamplerBindingType::Filtering,
                            },
                        },
                    },
                });
            }

            mFontBindGroup = mDevice->createBindGroup({
                .label = "DiagnosticsOverlay.FontBindGroup",
                .layout = mFontBindGroupLayout,
                .entries = {
                    {
                        .binding = 0u,
                        .textureView = {mFontAtlasView},
                    },
                    {
                        .binding = 1u,
                        .sampler = {mFontSampler},
                    },
                },
            });
            return mFontBindGroup != nullptr;
        }

        void DiagnosticsOverlayController::buildTextLines(eastl::vector<eastl::string> &lines)
        {
            lines.clear();
            lines.push_back("GVM RHI Diagnostics");

            const auto now = std::chrono::steady_clock::now();
            double frameMs = 0.0;
            if (mHasLastFrameTime)
            {
                frameMs = std::chrono::duration<double, std::milli>(now - mLastFrameTime).count();
            }
            mLastFrameTime = now;
            mHasLastFrameTime = true;

            if (mConfig.showFps != False)
            {
                const double fps = frameMs > 0.0 ? 1000.0 / frameMs : 0.0;
                lines.push_back(makeFormattedString("FPS %.1f  Frame %.2fms", fps, frameMs));
            }

            if (mConfig.showPassTimes != False)
            {
                lines.push_back("Pass Times Last Completed");
                if (!mTimestampAvailable)
                {
                    lines.push_back("  Timestamps Unavailable");
                }
                else if (mLastPassTimes.empty())
                {
                    lines.push_back("  Waiting For GPU Results");
                }
                else
                {
                    const uint32_t count = eastl::min<uint32_t>(static_cast<uint32_t>(mLastPassTimes.size()), mConfig.maxPassScopes);
                    for (uint32_t index = 0u; index < count; ++index)
                    {
                        const TimestampRangeResult &result = mLastPassTimes[index];
                        const double durationMs = static_cast<double>(result.durationNs) / 1000000.0;
                        const eastl::string label = sanitizeOverlayText(result.label, 36u);
                        lines.push_back(makeFormattedString("  %.2fms %s", durationMs, label.c_str()));
                    }
                }
            }

            if (mConfig.showResources != False)
            {
                const DiagnosticsResourceSnapshot snapshot = mDevice->getDiagnosticsResourceSnapshot();
                uint32_t visibleResourceCount = 0u;
                uint64_t visibleResourceBytes = 0u;
                for (const DiagnosticsResourceSnapshotEntry &entry : snapshot.entries)
                {
                    if (isInternalDiagnosticsOverlayResource(entry.label))
                    {
                        continue;
                    }
                    ++visibleResourceCount;
                    visibleResourceBytes += entry.estimatedBytes;
                }

                lines.push_back(makeFormattedString("Resources %u  %s", visibleResourceCount, formatByteCount(visibleResourceBytes).c_str()));
                uint32_t emittedResourceRows = 0u;
                for (const DiagnosticsResourceSnapshotEntry &entry : snapshot.entries)
                {
                    if (emittedResourceRows >= mConfig.maxResourceRows)
                    {
                        break;
                    }
                    if (isInternalDiagnosticsOverlayResource(entry.label))
                    {
                        continue;
                    }
                    const eastl::string label = sanitizeOverlayText(entry.label, 28u);
                    const eastl::string bytes = formatByteCount(entry.estimatedBytes);
                    if (entry.kind == DiagnosticsResourceKind::Buffer)
                    {
                        lines.push_back(makeFormattedString("  Buffer %s %s", bytes.c_str(), label.c_str()));
                    }
                    else
                    {
                        lines.push_back(makeFormattedString(
                            "  Texture %s %ux%u %s %s",
                            bytes.c_str(),
                            entry.width,
                            entry.height,
                            formatTextureFormatName(entry.format),
                            label.c_str()));
                    }
                    ++emittedResourceRows;
                }
            }
        }

        void DiagnosticsOverlayController::buildTextVertices(const eastl::vector<eastl::string> &lines, const DiagnosticsOverlayTarget &target)
        {
            mVertices.clear();
            const uint32_t maxColumns = calculateMaxColumns(target);
            const uint32_t maxRows = calculateMaxRows(target);
            if (maxColumns == 0u || maxRows == 0u)
            {
                return;
            }

            uint32_t emittedGlyphs = 0u;
            const uint32_t rowCount = eastl::min<uint32_t>(static_cast<uint32_t>(lines.size()), maxRows);
            uint32_t widestColumnCount = 0u;
            for (uint32_t row = 0u; row < rowCount; ++row)
            {
                widestColumnCount = eastl::max<uint32_t>(widestColumnCount, eastl::min<uint32_t>(static_cast<uint32_t>(lines[row].size()), maxColumns));
            }
            if (widestColumnCount > 0u)
            {
                const float backgroundX = eastl::max(0.0f, static_cast<float>(OverlayMarginX) - OverlayBackgroundPaddingX);
                const float backgroundY = eastl::max(0.0f, static_cast<float>(OverlayMarginY) - OverlayBackgroundPaddingY);
                const float backgroundWidth = eastl::min(
                    static_cast<float>(target.width) - backgroundX,
                    static_cast<float>(widestColumnCount * GlyphPixelSize) + OverlayBackgroundPaddingX * 2.0f);
                const float backgroundHeight = eastl::min(
                    static_cast<float>(target.height) - backgroundY,
                    static_cast<float>(rowCount * GlyphLineHeight) + OverlayBackgroundPaddingY * 2.0f);
                appendOverlayQuad(mVertices, target, backgroundX, backgroundY, backgroundWidth, backgroundHeight, OverlayBackgroundGlyphCode);
            }

            for (uint32_t row = 0u; row < rowCount; ++row)
            {
                const eastl::string &line = lines[row];
                const uint32_t columnCount = eastl::min<uint32_t>(static_cast<uint32_t>(line.size()), maxColumns);
                for (uint32_t column = 0u; column < columnCount; ++column)
                {
                    if (emittedGlyphs >= mConfig.maxTextGlyphs)
                    {
                        return;
                    }
                    const char character = line[column];
                    if (character == ' ')
                    {
                        continue;
                    }

                    const float x = static_cast<float>(OverlayMarginX + column * GlyphPixelSize);
                    const float y = static_cast<float>(OverlayMarginY + row * GlyphLineHeight);
                    appendGlyphQuad(mVertices, target, x, y, normalizeGlyphCode(character));
                    ++emittedGlyphs;
                }
            }
        }

        bool DiagnosticsOverlayController::hasWarnedUnsupportedFormat(TextureFormat format) const
        {
            for (TextureFormat warnedFormat : mWarnedUnsupportedFormats)
            {
                if (warnedFormat == format)
                {
                    return true;
                }
            }
            return false;
        }

        void DiagnosticsOverlayController::rememberUnsupportedFormat(TextureFormat format)
        {
            mWarnedUnsupportedFormats.push_back(format);
        }

        DiagnosticsOverlayCommandEncoder::DiagnosticsOverlayCommandEncoder(eastl::intrusive_ptr<DiagnosticsOverlayController> controller, CommandEncoder innerEncoder)
            : mController(eastl::move(controller))
            , mInnerEncoder(eastl::move(innerEncoder))
        {
        }

        DiagnosticsOverlayCommandEncoder::~DiagnosticsOverlayCommandEncoder()
        {
            if (!mSubmitted && !mRecordedScopes.empty() && mController != nullptr)
            {
                mController->abandonRecordedScopes(mRecordedScopes);
            }
        }

        CommandEncoder DiagnosticsOverlayCommandEncoder::getInnerEncoder() const
        {
            return mInnerEncoder;
        }

        bool DiagnosticsOverlayCommandEncoder::isEnded() const
        {
            return mEnded;
        }

        const DiagnosticsOverlayTarget &DiagnosticsOverlayCommandEncoder::getLastTarget() const
        {
            return mLastTarget;
        }

        void DiagnosticsOverlayCommandEncoder::consumeSubmissionMetadata(eastl::vector<DiagnosticsOverlayRecordedScope> &outScopes)
        {
            if (!mRecordedScopes.empty())
            {
                outScopes.insert(outScopes.end(), mRecordedScopes.begin(), mRecordedScopes.end());
                mRecordedScopes.clear();
            }
            mSubmitted = true;
        }

        void DiagnosticsOverlayCommandEncoder::begin()
        {
            if (mInnerEncoder != nullptr)
            {
                mInnerEncoder->begin();
            }
        }

        RenderPassEncoder DiagnosticsOverlayCommandEncoder::beginRenderPass(const RenderPassDescriptor &pass)
        {
            RenderPassDescriptor descriptor = pass;
            if (mController != nullptr && !hasExplicitTimestampWrites(descriptor.timestampWrites))
            {
                DiagnosticsOverlayRecordedScope recordedScope = {};
                bool recorded = false;
                descriptor.timestampWrites = mController->allocatePassScope(descriptor.label, recordedScope, recorded).timestampWrites;
                if (recorded)
                {
                    mRecordedScopes.push_back(recordedScope);
                }
            }
            for (auto iterator = descriptor.colorAttachments.rbegin(); iterator != descriptor.colorAttachments.rend(); ++iterator)
            {
                const RenderPassColorAttachment &attachment = *iterator;
                if (!attachment.view.isNull())
                {
                    mLastTarget.view = attachment.view;
                    mLastTarget.format = attachment.view->getFormat();
                    mLastTarget.width = attachment.view->getWidth();
                    mLastTarget.height = attachment.view->getHeight();
                    break;
                }
            }
            return mInnerEncoder->beginRenderPass(descriptor);
        }

        BlitPassEncoder DiagnosticsOverlayCommandEncoder::beginBlitPass(const BlitPassDescriptor &pass)
        {
            BlitPassDescriptor descriptor = pass;
            if (mController != nullptr && !hasExplicitTimestampWrites(descriptor.timestampWrites))
            {
                DiagnosticsOverlayRecordedScope recordedScope = {};
                bool recorded = false;
                descriptor.timestampWrites = mController->allocatePassScope(descriptor.label, recordedScope, recorded).timestampWrites;
                if (recorded)
                {
                    mRecordedScopes.push_back(recordedScope);
                }
            }
            return mInnerEncoder->beginBlitPass(descriptor);
        }

        ComputePassEncoder DiagnosticsOverlayCommandEncoder::beginComputePass(const ComputePassDescriptor &pass)
        {
            ComputePassDescriptor descriptor = pass;
            if (mController != nullptr && !hasExplicitTimestampWrites(descriptor.timestampWrites))
            {
                DiagnosticsOverlayRecordedScope recordedScope = {};
                bool recorded = false;
                descriptor.timestampWrites = mController->allocatePassScope(descriptor.label, recordedScope, recorded).timestampWrites;
                if (recorded)
                {
                    mRecordedScopes.push_back(recordedScope);
                }
            }
            return mInnerEncoder->beginComputePass(descriptor);
        }

        void DiagnosticsOverlayCommandEncoder::resolveQuerySet(QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, BufferRange destination)
        {
            mInnerEncoder->resolveQuerySet(querySet, firstQuery, queryCount, destination);
        }

        void DiagnosticsOverlayCommandEncoder::end()
        {
            if (mEnded)
            {
                return;
            }
            mInnerEncoder->end();
            mEnded = true;
        }

        DiagnosticsOverlayQueue::DiagnosticsOverlayQueue(Device device, Queue innerQueue, DiagnosticsOverlayShaderProgramProvider shaderProvider)
            : mController(new DiagnosticsOverlayController(device, shaderProvider))
            , mInnerQueue(innerQueue)
        {
        }

        DiagnosticsOverlayQueue::~DiagnosticsOverlayQueue()
        {
            destroy();
        }

        TimestampQuerySupport DiagnosticsOverlayQueue::getTimestampQuerySupport() const
        {
            return mInnerQueue != nullptr ? mInnerQueue->getTimestampQuerySupport() : TimestampQuerySupport{};
        }

        Extent3D DiagnosticsOverlayQueue::getImageTransferGranularity() const
        {
            if (mInnerQueue != nullptr)
            {
                return mInnerQueue->getImageTransferGranularity();
            }
            return {1u, 1u, 1u};
        }

        uint32_t DiagnosticsOverlayQueue::getMaxUnsubmittedCommandEncoderCount() const
        {
            if (mInnerQueue != nullptr)
            {
                const uint32_t innerCapacity = mInnerQueue->getMaxUnsubmittedCommandEncoderCount();
                return innerCapacity > 1u ? innerCapacity - 1u : 1u;
            }
            return 1024u;
        }

        CommandEncoder DiagnosticsOverlayQueue::createCommandEncoder()
        {
            CommandEncoder innerEncoder = mInnerQueue->createCommandEncoder();
            return new DiagnosticsOverlayCommandEncoder(mController, innerEncoder);
        }

        void DiagnosticsOverlayQueue::writeBuffer(BufferRange buffer, void const *data, uint64_t size)
        {
            mInnerQueue->writeBuffer(buffer, data, size);
        }

        void DiagnosticsOverlayQueue::readBuffer(BufferRange buffer, void *data, uint64_t size)
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
            /* Readback stays on the deprecated synchronous path so callers that already submit+block keep the same timing. */
            mInnerQueue->readBuffer(buffer, data, size);
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        void DiagnosticsOverlayQueue::writeTexture(const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize)
        {
            mInnerQueue->writeTexture(destination, data, dataSize, dataLayout, writeSize);
        }

        void DiagnosticsOverlayQueue::readTexture(const ImageCopyTexture &source, void *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &readSize)
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
            /* Readback stays on the deprecated synchronous path so callers that already submit+block keep the same timing. */
            mInnerQueue->readTexture(source, data, dataSize, dataLayout, readSize);
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        void DiagnosticsOverlayQueue::uploadTexture(Texture destination, void const *data, uint64_t dataStorageBytes, const eastl::vector<uint64_t> &mipmapOffsetBytes)
        {
            mInnerQueue->uploadTexture(destination, data, dataStorageBytes, mipmapOffsetBytes);
        }

        void DiagnosticsOverlayQueue::uploadTexture(Texture destination, BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes)
        {
            mInnerQueue->uploadTexture(destination, bufferRange, mipmapOffsetBytes);
        }

        void DiagnosticsOverlayQueue::copyBufferToBuffer(BufferRange source, BufferRange destination)
        {
            mInnerQueue->copyBufferToBuffer(source, destination);
        }

        void DiagnosticsOverlayQueue::copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize)
        {
            mInnerQueue->copyBufferToTexture(source, destination, copySize);
        }

        void DiagnosticsOverlayQueue::copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize)
        {
            mInnerQueue->copyTextureToBuffer(source, destination, copySize);
        }

        void DiagnosticsOverlayQueue::copyBufferToBufferMultipleRegion(Buffer source, Buffer destination, Buffer regions, uint32_t regionCount)
        {
            mInnerQueue->copyBufferToBufferMultipleRegion(source, destination, regions, regionCount);
        }

        void DiagnosticsOverlayQueue::fillBuffer(BufferRange source, uint8_t value)
        {
            mInnerQueue->fillBuffer(source, value);
        }

        Readback DiagnosticsOverlayQueue::readBufferAsync(BufferRange source)
        {
            return mInnerQueue->readBufferAsync(source);
        }

        Readback DiagnosticsOverlayQueue::readTextureAsync(const ImageCopyTexture &source, const TextureDataLayout &dataLayout, const Extent3D &readSize)
        {
            return mInnerQueue->readTextureAsync(source, dataLayout, readSize);
        }

        eastl::vector<CommandEncoder> DiagnosticsOverlayQueue::unwrapEncoders(
            const eastl::vector<CommandEncoder> &encoders,
            bool requireClosed,
            DiagnosticsOverlayTarget &outTarget,
            eastl::vector<DiagnosticsOverlayRecordedScope> &outScopes)
        {
            outTarget = {};
            outScopes.clear();
            if (requireClosed)
            {
                for (const CommandEncoder &encoder : encoders)
                {
                    if (encoder == nullptr)
                    {
                        throw std::invalid_argument("Submission requires nonnull command encoders.");
                    }
                    if (const auto *overlayEncoder = dynamic_cast<const DiagnosticsOverlayCommandEncoder *>(encoder.get()))
                    {
                        if (!overlayEncoder->isEnded())
                        {
                            throw std::invalid_argument("Submission requires closed overlay command encoders.");
                        }
                    }
                }
            }

            eastl::vector<CommandEncoder> innerEncoders;
            innerEncoders.reserve(encoders.size() + 1u);
            for (const CommandEncoder &encoder : encoders)
            {
                if (encoder == nullptr)
                {
                    continue;
                }

                if (auto *overlayEncoder = dynamic_cast<DiagnosticsOverlayCommandEncoder *>(encoder.get()))
                {
                    if (!overlayEncoder->isEnded())
                    {
                        overlayEncoder->end();
                    }
                    const DiagnosticsOverlayTarget &encoderTarget = overlayEncoder->getLastTarget();
                    if (!encoderTarget.view.isNull() && encoderTarget.width > 0u && encoderTarget.height > 0u)
                    {
                        outTarget = encoderTarget;
                    }
                    overlayEncoder->consumeSubmissionMetadata(outScopes);
                    innerEncoders.push_back(overlayEncoder->getInnerEncoder());
                }
                else
                {
                    innerEncoders.push_back(encoder);
                }
            }

            return innerEncoders;
        }

        void DiagnosticsOverlayQueue::submit(const eastl::vector<CommandEncoder> &encoders)
        {
            DiagnosticsOverlayTarget target = {};
            eastl::vector<DiagnosticsOverlayRecordedScope> submittedScopes;
            eastl::vector<CommandEncoder> innerEncoders = unwrapEncoders(encoders, false, target, submittedScopes);
            bool resolvedTimestamps = false;
            if (mController != nullptr)
            {
                CommandEncoder postEncoder = mController->buildPostSubmitEncoder(mInnerQueue, target, submittedScopes, resolvedTimestamps);
                if (postEncoder != nullptr)
                {
                    innerEncoders.push_back(eastl::move(postEncoder));
                }
            }
            mInnerQueue->submit(innerEncoders);
            if (mController != nullptr)
            {
                mController->afterSubmit(mInnerQueue, Submission{}, false, !target.view.isNull(), resolvedTimestamps, submittedScopes);
            }
        }

        Submission DiagnosticsOverlayQueue::submit(const eastl::vector<CommandEncoder> &encoders, const eastl::vector<Submission> &waits)
        {
            DiagnosticsOverlayTarget target = {};
            eastl::vector<DiagnosticsOverlayRecordedScope> submittedScopes;
            eastl::vector<CommandEncoder> innerEncoders = unwrapEncoders(encoders, true, target, submittedScopes);
            bool resolvedTimestamps = false;
            if (mController != nullptr)
            {
                CommandEncoder postEncoder = mController->buildPostSubmitEncoder(mInnerQueue, target, submittedScopes, resolvedTimestamps);
                if (postEncoder != nullptr)
                {
                    innerEncoders.push_back(eastl::move(postEncoder));
                }
            }
            Submission completion = mInnerQueue->submit(innerEncoders, waits);
            if (mController != nullptr)
            {
                mController->afterSubmit(mInnerQueue, completion, true, !target.view.isNull(), resolvedTimestamps, submittedScopes);
            }
            return completion;
        }

        void DiagnosticsOverlayQueue::destroy()
        {
            if (mController != nullptr)
            {
                mController->shutdown();
                mController.reset();
            }
            mInnerQueue = nullptr;
        }
    } // namespace

    Queue createDiagnosticsOverlayQueue(Device device, Queue innerQueue, DiagnosticsOverlayShaderProgramProvider shaderProvider)
    {
        if (device == nullptr || innerQueue == nullptr)
        {
            return innerQueue;
        }
        if (device->getDiagnosticsOverlayConfig().enabled == False)
        {
            return innerQueue;
        }
        return new DiagnosticsOverlayQueue(device, innerQueue, shaderProvider);
    }
}
