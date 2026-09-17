#include "WebglCustomAttributesRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/string.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t CustomAttributesRandomSeed = 0x18500010u;
        constexpr uint32_t CustomAttributesVertexCount = 8385u;
        constexpr uint32_t CustomAttributesIndexCount = 48384u;
        constexpr uint32_t CustomAttributesWidthSegments = 128u;
        constexpr uint32_t CustomAttributesHeightSegments = 64u;
        constexpr uint32_t CustomAttributesTextureExtent = 512u;
        constexpr uint32_t CustomAttributesTextureMipCount = 10u;
        constexpr uint32_t PreNoiseRandomDrawCount = 100u;
        constexpr uint32_t PreFrameRandomDrawCount = 4u;
        constexpr uint32_t FirstRenderLazyRandomDrawCount = 36u;
        constexpr uint32_t ExpectedPreNoiseRandomState = 2176856772u;
        constexpr uint32_t ExpectedInitializedRandomState = 538802415u;
        constexpr uint32_t ExpectedPreFrameRandomState = 1429354707u;
        constexpr uint32_t ExpectedFrameZeroWalkRandomState = 1955797493u;
        constexpr uint32_t ExpectedFrameZeroRenderedRandomState = 3501288780u;
        constexpr uint32_t ExpectedFrameSixtyRandomState = 2053689625u;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *WaterAssetSha256 =
            "43847c5fff3f80551dda41c68bb8ebb00938e625c5acb2c11acc4e3221b17402";

        static_assert(sizeof(WebglCustomAttributesVertex) == 36u);
        static_assert(offsetof(WebglCustomAttributesVertex, position) == 0u);
        static_assert(offsetof(WebglCustomAttributesVertex, normal) == 12u);
        static_assert(offsetof(WebglCustomAttributesVertex, texCoord) == 24u);
        static_assert(offsetof(WebglCustomAttributesVertex, displacement) == 32u);

        /** Creates parent directories for one explicitly requested custom-attribute artifact. */
        void prepareCustomAttributesOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeCustomAttributesRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_custom_attributes RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Reads one bounded immutable asset into exact bytes for SHA-256 validation. */
        eastl::vector<uint8_t> readCustomAttributesAssetBytes(
            const std::filesystem::path &inputPath)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open Three r185 water asset: " + inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    "Three r185 water asset has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete Three r185 water asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte sequence. */
        eastl::string calculateCustomAttributesSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Returns one exact JavaScript Math.random value as a binary64 number. */
        double nextCustomAttributesRandomDouble(
            ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Converts one authored sRGB channel to Three's linear working-space scalar. */
        double customAttributesSrgbToLinear(double value)
        {
            if (value <= 0.04045)
            {
                return value * 0.0773993808;
            }
            return std::pow(value * 0.9478672986 + 0.0521327014, 2.4);
        }

        /** Returns Three's wrapped HSL channel interpolation for one hue phase. */
        double customAttributesHueToRgb(double minimum, double maximum, double hue)
        {
            if (hue < 0.0)
            {
                hue += 1.0;
            }
            if (hue > 1.0)
            {
                hue -= 1.0;
            }
            if (hue < 1.0 / 6.0)
            {
                return minimum + (maximum - minimum) * 6.0 * hue;
            }
            if (hue < 1.0 / 2.0)
            {
                return maximum;
            }
            if (hue < 2.0 / 3.0)
            {
                return minimum + (maximum - minimum) * 6.0 * (2.0 / 3.0 - hue);
            }
            return minimum;
        }

        /** Applies Three Color.offsetHSL to one working-space RGB triple. */
        void offsetCustomAttributesColorHsl(
            double hueOffset,
            double saturationOffset,
            double lightnessOffset,
            double &red,
            double &green,
            double &blue)
        {
            const double maximum = eastl::max(red, eastl::max(green, blue));
            const double minimum = eastl::min(red, eastl::min(green, blue));
            double hue = 0.0;
            double saturation = 0.0;
            double lightness = (minimum + maximum) / 2.0;
            if (minimum != maximum)
            {
                const double delta = maximum - minimum;
                saturation = lightness <= 0.5
                    ? delta / (maximum + minimum)
                    : delta / (2.0 - maximum - minimum);
                if (maximum == red)
                {
                    hue = (green - blue) / delta + (green < blue ? 6.0 : 0.0);
                }
                else if (maximum == green)
                {
                    hue = (blue - red) / delta + 2.0;
                }
                else
                {
                    hue = (red - green) / delta + 4.0;
                }
                hue /= 6.0;
            }

            hue = std::fmod(hue + hueOffset, 1.0);
            if (hue < 0.0)
            {
                hue += 1.0;
            }
            saturation = eastl::clamp(saturation + saturationOffset, 0.0, 1.0);
            lightness = eastl::clamp(lightness + lightnessOffset, 0.0, 1.0);
            if (saturation == 0.0)
            {
                red = green = blue = lightness;
                return;
            }
            const double maximumHsl = lightness <= 0.5
                ? lightness * (1.0 + saturation)
                : lightness + saturation - lightness * saturation;
            const double minimumHsl = 2.0 * lightness - maximumHsl;
            red = customAttributesHueToRgb(
                minimumHsl,
                maximumHsl,
                hue + 1.0 / 3.0);
            green = customAttributesHueToRgb(minimumHsl, maximumHsl, hue);
            blue = customAttributesHueToRgb(
                minimumHsl,
                maximumHsl,
                hue - 1.0 / 3.0);
        }

        /** Builds the exact r185 SphereGeometry vertices, indices, and initial noise values. */
        void buildCustomAttributesSphere(
            ThreeCompat::DeterministicRandom &random,
            eastl::vector<WebglCustomAttributesVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            eastl::vector<float> &noise)
        {
            constexpr double Radius = 50.0;
            vertices.clear();
            vertices.reserve(CustomAttributesVertexCount);
            noise.clear();
            noise.reserve(CustomAttributesVertexCount);
            for (uint32_t row = 0u; row <= CustomAttributesHeightSegments; ++row)
            {
                const double v = double(row) / double(CustomAttributesHeightSegments);
                const double theta = v * Pi;
                const double y = Radius * std::cos(theta);
                const double ringRadius = std::sqrt(Radius * Radius - y * y);
                double uOffset = 0.0;
                if (row == 0u)
                {
                    uOffset = 0.5 / double(CustomAttributesWidthSegments);
                }
                else if (row == CustomAttributesHeightSegments)
                {
                    uOffset = -0.5 / double(CustomAttributesWidthSegments);
                }
                for (uint32_t column = 0u;
                     column <= CustomAttributesWidthSegments;
                     ++column)
                {
                    const double u =
                        double(column) / double(CustomAttributesWidthSegments);
                    const double phi = u * Pi * 2.0;
                    const double x = -ringRadius * std::cos(phi);
                    const double z = ringRadius * std::sin(phi);
                    const double normalLength = std::sqrt(x * x + y * y + z * z);
                    WebglCustomAttributesVertex vertex;
                    vertex.position = glm::vec3(
                        static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(z));
                    vertex.normal = glm::vec3(
                        static_cast<float>(x / normalLength),
                        static_cast<float>(y / normalLength),
                        static_cast<float>(z / normalLength));
                    vertex.texCoord = glm::vec2(
                        static_cast<float>(u + uOffset),
                        static_cast<float>(1.0 - v));
                    vertex.displacement = 0.0f;
                    vertices.push_back(vertex);
                    noise.push_back(static_cast<float>(
                        nextCustomAttributesRandomDouble(random) * 5.0));
                }
            }

            indices.clear();
            indices.reserve(CustomAttributesIndexCount);
            constexpr uint32_t RowWidth = CustomAttributesWidthSegments + 1u;
            for (uint32_t row = 0u; row < CustomAttributesHeightSegments; ++row)
            {
                for (uint32_t column = 0u;
                     column < CustomAttributesWidthSegments;
                     ++column)
                {
                    const uint32_t a = row * RowWidth + column + 1u;
                    const uint32_t b = row * RowWidth + column;
                    const uint32_t c = (row + 1u) * RowWidth + column;
                    const uint32_t d = (row + 1u) * RowWidth + column + 1u;
                    if (row != 0u)
                    {
                        indices.push_back(a);
                        indices.push_back(b);
                        indices.push_back(d);
                    }
                    if (row != CustomAttributesHeightSegments - 1u)
                    {
                        indices.push_back(b);
                        indices.push_back(c);
                        indices.push_back(d);
                    }
                }
            }
            if (vertices.size() != CustomAttributesVertexCount ||
                indices.size() != CustomAttributesIndexCount ||
                noise.size() != CustomAttributesVertexCount)
            {
                throw std::runtime_error(
                    "webgl_custom_attributes SphereGeometry counts diverged from r185.");
            }
        }

        /** Reads one validated RGBA8 channel for CPU mip generation. */
        uint8_t readCustomAttributesImageChannel(
            const RgbaImageData &image,
            uint32_t x,
            uint32_t y,
            uint32_t channel)
        {
            const size_t offset =
                (static_cast<size_t>(y) * image.width + x) * 4u + channel;
            return image.pixels[offset];
        }

        /** Builds one raw-UNORM 2x2 mip matching NoColorSpace WebGL storage filtering. */
        RgbaImageData buildNextCustomAttributesUnormMip(const RgbaImageData &source)
        {
            RgbaImageData target;
            target.width = eastl::max(source.width / 2u, 1u);
            target.height = eastl::max(source.height / 2u, 1u);
            target.pixels.resize(
                static_cast<size_t>(target.width) * target.height * 4u);
            for (uint32_t targetY = 0u; targetY < target.height; ++targetY)
            {
                for (uint32_t targetX = 0u; targetX < target.width; ++targetX)
                {
                    const uint32_t sourceX0 = eastl::min(targetX * 2u, source.width - 1u);
                    const uint32_t sourceY0 = eastl::min(targetY * 2u, source.height - 1u);
                    const uint32_t sourceX1 = eastl::min(sourceX0 + 1u, source.width - 1u);
                    const uint32_t sourceY1 = eastl::min(sourceY0 + 1u, source.height - 1u);
                    const size_t targetOffset =
                        (static_cast<size_t>(targetY) * target.width + targetX) * 4u;
                    for (uint32_t channel = 0u; channel < 4u; ++channel)
                    {
                        const uint32_t sum =
                            readCustomAttributesImageChannel(
                                source, sourceX0, sourceY0, channel) +
                            readCustomAttributesImageChannel(
                                source, sourceX1, sourceY0, channel) +
                            readCustomAttributesImageChannel(
                                source, sourceX0, sourceY1, channel) +
                            readCustomAttributesImageChannel(
                                source, sourceX1, sourceY1, channel);
                        target.pixels[targetOffset + channel] =
                            static_cast<uint8_t>((sum + 2u) / 4u);
                    }
                }
            }
            return target;
        }

        /** Builds the complete raw-UNORM mip chain for Texture.colorSpace=NoColorSpace. */
        eastl::vector<eastl::vector<uint8_t>> buildCustomAttributesTextureMips(
            const RgbaImageData &baseImage)
        {
            if (baseImage.width != CustomAttributesTextureExtent ||
                baseImage.height != CustomAttributesTextureExtent ||
                baseImage.pixels.size() !=
                    static_cast<size_t>(CustomAttributesTextureExtent) *
                        CustomAttributesTextureExtent * 4u)
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes water.jpg must decode to 512x512 RGBA8.");
            }
            eastl::vector<RgbaImageData> levels;
            levels.push_back(baseImage);
            while (levels.back().width > 1u || levels.back().height > 1u)
            {
                levels.push_back(buildNextCustomAttributesUnormMip(levels.back()));
            }
            if (levels.size() != CustomAttributesTextureMipCount)
            {
                throw std::runtime_error(
                    "webgl_custom_attributes water mip count diverged from r185.");
            }
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(levels.size());
            for (const RgbaImageData &level : levels)
            {
                result.push_back(level.pixels);
            }
            return result;
        }

        /** Builds Three's original OpenGL positive-Y and negative-one-to-one projection. */
        glm::mat4 makeCustomAttributesProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 30.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10000.0;
            const double top =
                NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                double(width) / double(height) * projectionHeight;
            const double projectionDepth = FarDistance - NearDistance;

            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(
                2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(
                2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(
                -(FarDistance + NearDistance) / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -2.0 * FarDistance * NearDistance / projectionDepth);
            return projection;
        }

        /** Builds Three's default XYZ Euler model matrix followed by camera-z translation. */
        glm::mat4 makeCustomAttributesModelView(double rotation)
        {
            const double cosine = std::cos(rotation);
            const double sine = std::sin(rotation);
            glm::mat4 modelView(1.0f);
            modelView[0u] = glm::vec4(
                static_cast<float>(cosine * cosine),
                static_cast<float>(sine),
                static_cast<float>(-cosine * sine),
                0.0f);
            modelView[1u] = glm::vec4(
                static_cast<float>(-cosine * sine),
                static_cast<float>(cosine),
                static_cast<float>(sine * sine),
                0.0f);
            modelView[2u] = glm::vec4(
                static_cast<float>(sine),
                0.0f,
                static_cast<float>(cosine),
                0.0f);
            modelView[3u] = glm::vec4(0.0f, 0.0f, -300.0f, 1.0f);
            return modelView;
        }

        /** Validates the two locked scenarios and every explicit host parameter. */
        void validateCustomAttributesScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_custom_attributes")
            {
                throw std::invalid_argument(
                    "Custom-attributes adapter requires case-id webgl_custom_attributes.");
            }
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            if (!initial && !animated)
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes requires initial/frame 0 or animated/frame 60.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes requires the locked 800x500 extent.");
            }
            if (options.randomSeed != CustomAttributesRandomSeed)
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes requires random seed 0x18500010.");
            }
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes requires explicit --asset-root.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes does not accept an input replay.");
            }
        }
    } // namespace

    void WebglCustomAttributesRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateCustomAttributesScenario(options);
        device = inDevice;
        random.reset(CustomAttributesRandomSeed);
        // r185 imports consume 76 draws, then Camera, Scene, Texture/Source,
        // ShaderMaterial, and SphereGeometry UUIDs consume the remaining 24.
        for (uint32_t drawIndex = 0u;
             drawIndex < PreNoiseRandomDrawCount;
             ++drawIndex)
        {
            static_cast<void>(random.nextUint32());
        }
        if (random.getState() != ExpectedPreNoiseRandomState)
        {
            throw std::runtime_error(
                "webgl_custom_attributes pre-noise random stream diverged from r185.");
        }
        buildCustomAttributesSphere(random, vertices, indices, noise);
        initializedRandomState = random.getState();
        if (initializedRandomState != ExpectedInitializedRandomState)
        {
            throw std::runtime_error(
                "webgl_custom_attributes initialization random stream diverged from r185.");
        }
        // The Mesh Object3D UUID consumes four draws after noise initialization
        // and before the first animation callback updates displacement.
        for (uint32_t drawIndex = 0u;
             drawIndex < PreFrameRandomDrawCount;
             ++drawIndex)
        {
            static_cast<void>(random.nextUint32());
        }
        if (random.getState() != ExpectedPreFrameRandomState)
        {
            throw std::runtime_error(
                "webgl_custom_attributes pre-frame random stream diverged from r185.");
        }

        const std::filesystem::path waterPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "water.jpg";
        const eastl::vector<uint8_t> assetBytes =
            readCustomAttributesAssetBytes(waterPath);
        if (calculateCustomAttributesSha256(assetBytes) != WaterAssetSha256)
        {
            throw std::invalid_argument(
                "textures/water.jpg differs from the pinned Three r185 asset.");
        }
        textureMips = buildCustomAttributesTextureMips(decodeJpegRgba8(waterPath));
        projectionMatrix = makeCustomAttributesProjection(options.width, options.height);
        colorRed = 1.0;
        colorGreen = customAttributesSrgbToLinear(34.0 / 255.0);
        colorBlue = 0.0;
        virtualTimeMilliseconds = 0.0;
        renderedVirtualTimeMilliseconds = 0.0;
        frameUpdateCount = 0u;
    }

    void WebglCustomAttributesRuntimeAdapter::advanceFrameState(uint32_t frameIndex)
    {
        if (frameIndex != frameUpdateCount)
        {
            throw std::logic_error(
                "webgl_custom_attributes frame state must advance sequentially from frame 0.");
        }
        renderedVirtualTimeMilliseconds = virtualTimeMilliseconds;
        timeValue =
            (ReferenceEpochMilliseconds + renderedVirtualTimeMilliseconds) * 0.01;
        rotation = timeValue * 0.01;
        amplitude = static_cast<float>(2.5 * std::sin(rotation * 0.125));
        offsetCustomAttributesColorHsl(
            0.0005,
            0.0,
            0.0,
            colorRed,
            colorGreen,
            colorBlue);
        color = glm::vec3(
            static_cast<float>(colorRed),
            static_cast<float>(colorGreen),
            static_cast<float>(colorBlue));

        for (uint32_t vertexIndex = 0u;
             vertexIndex < CustomAttributesVertexCount;
             ++vertexIndex)
        {
            const float sineDisplacement = static_cast<float>(
                std::sin(0.1 * double(vertexIndex) + timeValue));
            const float walkedNoise = static_cast<float>(
                double(noise[vertexIndex]) +
                0.5 * (0.5 - nextCustomAttributesRandomDouble(random)));
            noise[vertexIndex] = static_cast<float>(eastl::clamp(
                double(walkedNoise),
                -5.0,
                5.0));
            vertices[vertexIndex].displacement = static_cast<float>(
                double(sineDisplacement) + double(noise[vertexIndex]));
        }
        modelViewMatrix = makeCustomAttributesModelView(rotation);
        ++frameUpdateCount;
        virtualTimeMilliseconds += FrameStepMilliseconds;

        if (frameIndex == 0u &&
            random.getState() != ExpectedFrameZeroWalkRandomState)
        {
            throw std::runtime_error(
                "webgl_custom_attributes frame-0 random stream diverged from r185.");
        }
        if (frameIndex == 60u && random.getState() != ExpectedFrameSixtyRandomState)
        {
            throw std::runtime_error(
                "webgl_custom_attributes frame-60 random stream diverged from r185.");
        }
    }

    void WebglCustomAttributesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        if (!frameUploader)
        {
            throw std::logic_error(
                "webgl_custom_attributes generated frame uploader is unavailable.");
        }
        advanceFrameState(frameIndex);
        frameUploader(
            vertices,
            projectionMatrix,
            modelViewMatrix,
            amplitude,
            color);
    }

    void WebglCustomAttributesRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (frameIndex == 0u)
        {
            // The first r185 renderer.render lazily creates nine UUID-bearing
            // resources after frame-zero's walk, consuming 36 draws before frame one.
            for (uint32_t drawIndex = 0u;
                 drawIndex < FirstRenderLazyRandomDrawCount;
                 ++drawIndex)
            {
                static_cast<void>(random.nextUint32());
            }
            if (random.getState() != ExpectedFrameZeroRenderedRandomState)
            {
                throw std::runtime_error(
                    "webgl_custom_attributes first-render random stream diverged from r185.");
            }
        }
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            computeCustomAttributesRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_custom_attributes capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_custom_attributes could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglCustomAttributesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        frameUploader = nullptr;
    }

    void WebglCustomAttributesRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareCustomAttributesOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_custom_attributes RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_custom_attributes RGBA capture.");
        }
    }

    void WebglCustomAttributesRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareCustomAttributesOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_custom_attributes metadata path.");
        }
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_custom_attributes\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"virtualTimeMs\": " << renderedVirtualTimeMilliseconds << ",\n"
               << "  \"nextFrameTimeMs\": " << virtualTimeMilliseconds << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }

    void WebglCustomAttributesRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareCustomAttributesOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_custom_attributes snapshot path.");
        }
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_custom_attributes\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"canonicalState\": \""
               << (frameIndex == 0u
                       ? "seed-0x18500010-date-now-epoch-after-frame-0-random-walk-hue-update"
                       : "seed-0x18500010-fixed-step-60hz-frame-60-sequential-random-walk-hue-update")
               << "\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"logicalScenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"computePassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"logicalDrawCommandCount\": 1,\n"
               << "  \"explicitIndexCount\": " << CustomAttributesIndexCount << ",\n"
               << "  \"sphereVertexCount\": " << CustomAttributesVertexCount << ",\n"
               << "  \"sphereTriangleCount\": " << CustomAttributesIndexCount / 3u << ",\n"
               << "  \"standaloneGeometryBufferCount\": 2,\n"
               << "  \"primitiveTopology\": \"triangle-list\",\n"
               << "  \"projectionConvention\": \"three-opengl-positive-y-negative-one-to-one\",\n"
               << "  \"dslClipConversion\": \"y-negate-and-z-half-range-after-projection\",\n"
               << "  \"dynamicAttribute\": \"displacement\",\n"
               << "  \"dynamicVertexUploadCount\": " << frameUpdateCount << ",\n"
               << "  \"sequentialFrameAdvanceCount\": " << frameUpdateCount << ",\n"
               << "  \"renderedVirtualTimeMs\": " << renderedVirtualTimeMilliseconds << ",\n"
               << "  \"timeValue\": " << timeValue << ",\n"
               << "  \"rotationYAndZ\": " << rotation << ",\n"
               << "  \"amplitude\": " << amplitude << ",\n"
               << "  \"color\": [" << colorRed << "," << colorGreen << "," << colorBlue << "],\n"
               << "  \"seed\": " << CustomAttributesRandomSeed << ",\n"
               << "  \"preNoiseRandomDrawCount\": " << PreNoiseRandomDrawCount << ",\n"
               << "  \"preNoiseRandomState\": " << ExpectedPreNoiseRandomState << ",\n"
               << "  \"initialNoiseRandomState\": " << initializedRandomState << ",\n"
               << "  \"preFrameRandomDrawCount\": " << PreFrameRandomDrawCount << ",\n"
               << "  \"preFrameRandomState\": " << ExpectedPreFrameRandomState << ",\n"
               << "  \"firstRenderLazyRandomDrawCount\": " << FirstRenderLazyRandomDrawCount << ",\n"
               << "  \"frameZeroWalkRandomState\": " << ExpectedFrameZeroWalkRandomState << ",\n"
               << "  \"frameZeroRenderedRandomState\": " << ExpectedFrameZeroRenderedRandomState << ",\n"
               << "  \"totalRandomDrawCount\": "
               << PreNoiseRandomDrawCount + CustomAttributesVertexCount +
                      PreFrameRandomDrawCount +
                      frameUpdateCount * CustomAttributesVertexCount +
                      FirstRenderLazyRandomDrawCount
               << ",\n"
               << "  \"finalRandomState\": " << random.getState() << ",\n"
               << "  \"texturePath\": \"textures/water.jpg\",\n"
               << "  \"textureAssetSha256\": \"" << WaterAssetSha256 << "\",\n"
               << "  \"textureColorSpace\": \"NoColorSpace\",\n"
               << "  \"textureFormat\": \"rgba8unorm\",\n"
               << "  \"textureMipLevelCount\": " << CustomAttributesTextureMipCount << ",\n"
               << "  \"mipmapGeneration\": \"explicit-cpu-raw-unorm-box-filter\",\n"
               << "  \"samplerAddressMode\": \"repeat\",\n"
               << "  \"samplerMinMagFilter\": \"linear\",\n"
               << "  \"samplerMipmapFilter\": \"linear\",\n"
               << "  \"rendererOutputColorSpace\": \"srgb\",\n"
               << "  \"shaderOutputTransfer\": \"none-custom-shader-does-not-call-linearToOutputTexel\",\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main-displaced-sphere\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"sceneRoots\": [{\n"
               << "    \"id\": \"scene\",\n"
               << "    \"renderSetCount\": 0,\n"
               << "    \"renderSetId\": null,\n"
               << "    \"renderSetType\": null,\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"entityCount\": 0,\n"
               << "    \"entities\": [],\n"
               << "    \"drawCommandCount\": 1,\n"
               << "    \"logicalDrawCommandCount\": 1,\n"
               << "    \"directDrawFallback\": false,\n"
               << "    \"scenePasses\": [{\n"
               << "      \"name\": \"main-displaced-sphere\",\n"
               << "      \"renderClass\": \"WebglCustomAttributesMainPass\",\n"
               << "      \"renderSetId\": null,\n"
               << "      \"renderSetBindingCount\": 0,\n"
               << "      \"drawMode\": \"explicit-indexed\",\n"
               << "      \"invocationCount\": 1,\n"
               << "      \"logicalInvocationCount\": 1,\n"
               << "      \"drawCommandCount\": 1,\n"
               << "      \"usesStandaloneGeometry\": true,\n"
               << "      \"usesExplicitDrawCount\": true\n"
               << "    }]\n"
               << "  }],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
