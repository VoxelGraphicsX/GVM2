#include "WebgpuMultipleRendertargetsRuntimeAdapter.hpp"

#include <EASTL/algorithm.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t TubularSegments = 128u;
        constexpr uint32_t RadialSegments = 32u;
        constexpr double Pi = 3.14159265358979323846;

        /** Decodes one sRGB byte into the linear working space used for mip filtering. */
        float decodeMrtSrgb(uint8_t value)
        {
            const float encoded = float(value) / 255.0f;
            return encoded <= 0.04045f
                ? encoded / 12.92f
                : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }

        /** Encodes one filtered linear channel back into an sRGB texture byte. */
        uint8_t encodeMrtSrgb(float value)
        {
            const float encoded = value <= 0.0031308f
                ? value * 12.92f
                : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
            return static_cast<uint8_t>(
                std::round(eastl::clamp(encoded, 0.0f, 1.0f) * 255.0f));
        }

        /** Builds one browser-compatible 2x2 mip for an sRGB hardwood texture. */
        eastl::vector<uint8_t> buildNextMrtMip(
            const eastl::vector<uint8_t> &source,
            uint32_t sourceWidth,
            uint32_t sourceHeight)
        {
            const uint32_t targetWidth = eastl::max(1u, sourceWidth / 2u);
            const uint32_t targetHeight = eastl::max(1u, sourceHeight / 2u);
            eastl::vector<uint8_t> target(
                static_cast<size_t>(targetWidth) * targetHeight * 4u);
            for (uint32_t y = 0u; y < targetHeight; ++y)
            {
                for (uint32_t x = 0u; x < targetWidth; ++x)
                {
                    const uint32_t sampleCountX = sourceWidth > 1u ? 2u : 1u;
                    const uint32_t sampleCountY = sourceHeight > 1u ? 2u : 1u;
                    const float inverseSampleCount =
                        1.0f / float(sampleCountX * sampleCountY);
                    float channels[4u] = {};
                    for (uint32_t sampleY = 0u;
                         sampleY < sampleCountY;
                         ++sampleY)
                    {
                        for (uint32_t sampleX = 0u;
                             sampleX < sampleCountX;
                             ++sampleX)
                        {
                            const size_t sourceOffset =
                                (static_cast<size_t>(
                                     eastl::min(
                                         y * 2u + sampleY,
                                         sourceHeight - 1u)) *
                                     sourceWidth +
                                 eastl::min(
                                     x * 2u + sampleX,
                                     sourceWidth - 1u)) *
                                4u;
                            for (uint32_t channel = 0u;
                                 channel < 3u;
                                 ++channel)
                            {
                                channels[channel] +=
                                    decodeMrtSrgb(
                                        source[sourceOffset + channel]);
                            }
                            channels[3u] +=
                                float(source[sourceOffset + 3u]) / 255.0f;
                        }
                    }
                    const size_t targetOffset =
                        (static_cast<size_t>(y) * targetWidth + x) * 4u;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        target[targetOffset + channel] =
                            encodeMrtSrgb(
                                channels[channel] *
                                inverseSampleCount);
                    }
                    target[targetOffset + 3u] =
                        static_cast<uint8_t>(
                            std::round(
                                eastl::clamp(
                                    channels[3u] *
                                        inverseSampleCount,
                                    0.0f,
                                    1.0f) *
                                255.0f));
                }
            }
            return target;
        }

        /** Builds every explicit hardwood mip without relying on automatic generation. */
        eastl::vector<eastl::vector<uint8_t>> buildMrtMips(
            const RgbaImageData &image)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.push_back(image.pixels);
            uint32_t width = image.width;
            uint32_t height = image.height;
            while (width > 1u || height > 1u)
            {
                result.push_back(
                    buildNextMrtMip(result.back(), width, height));
                width = eastl::max(1u, width / 2u);
                height = eastl::max(1u, height / 2u);
            }
            return result;
        }

        /** Validates the three deterministic MRT scenarios. */
        void validateMultipleRendertargetsScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u;
            const bool orbit =
                options.scenarioId == "orbit" &&
                options.targetFrame == 61u;
            if (options.caseId != "webgpu_multiple_rendertargets" ||
                (!initial && !animated && !orbit) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                (orbit != !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "WebGPU multiple rendertargets requires one locked Manifest scenario.");
            }
        }

        /** Evaluates the default p=2 q=3 TorusKnot center curve. */
        glm::dvec3 calculateTorusKnotPosition(double u)
        {
            const double cosineU = std::cos(u);
            const double sineU = std::sin(u);
            const double qOverP = 1.5 * u;
            const double cosineQ = std::cos(qOverP);
            return {
                (2.0 + cosineQ) * 0.5 * cosineU,
                (2.0 + cosineQ) * sineU * 0.5,
                std::sin(qOverP) * 0.5,
            };
        }

        /** Generates the exact default TorusKnotGeometry parameterization. */
        void buildMultipleRendertargetsTorusKnot(
            eastl::vector<glm::vec4> &positions,
            eastl::vector<glm::vec4> &normals,
            eastl::vector<glm::vec4> &textureCoordinates,
            eastl::vector<uint32_t> &indices)
        {
            positions.clear();
            normals.clear();
            textureCoordinates.clear();
            indices.clear();
            for (uint32_t tubular = 0u;
                 tubular <= TubularSegments;
                 ++tubular)
            {
                const double u =
                    double(tubular) / double(TubularSegments) *
                    4.0 * Pi;
                const glm::dvec3 center =
                    calculateTorusKnotPosition(u);
                const glm::dvec3 ahead =
                    calculateTorusKnotPosition(u + 0.01);
                const glm::dvec3 tangent = ahead - center;
                glm::dvec3 normalBasis = ahead + center;
                glm::dvec3 binormal =
                    glm::cross(tangent, normalBasis);
                normalBasis = glm::cross(binormal, tangent);
                binormal = glm::normalize(binormal);
                normalBasis = glm::normalize(normalBasis);
                for (uint32_t radial = 0u;
                     radial <= RadialSegments;
                     ++radial)
                {
                    const double v =
                        double(radial) / double(RadialSegments) *
                        2.0 * Pi;
                    const double x = -0.3 * std::cos(v);
                    const double y = 0.3 * std::sin(v);
                    const glm::dvec3 position =
                        center + x * normalBasis + y * binormal;
                    positions.push_back(glm::vec4(
                        float(position.x),
                        float(position.y),
                        float(position.z),
                        1.0f));
                    const glm::dvec3 normal =
                        glm::normalize(position - center);
                    normals.push_back(
                        glm::vec4(
                            float(normal.x),
                            float(normal.y),
                            float(normal.z),
                            0.0f));
                    textureCoordinates.push_back(glm::vec4(
                        float(tubular) / float(TubularSegments),
                        float(radial) / float(RadialSegments),
                        0.0f,
                        0.0f));
                }
            }
            for (uint32_t tubular = 1u;
                 tubular <= TubularSegments;
                 ++tubular)
            {
                for (uint32_t radial = 1u;
                     radial <= RadialSegments;
                     ++radial)
                {
                    const uint32_t a =
                        (RadialSegments + 1u) * (tubular - 1u) +
                        radial - 1u;
                    const uint32_t b =
                        (RadialSegments + 1u) * tubular +
                        radial - 1u;
                    const uint32_t c =
                        (RadialSegments + 1u) * tubular + radial;
                    const uint32_t d =
                        (RadialSegments + 1u) * (tubular - 1u) +
                        radial;
                    indices.insert(
                        indices.end(),
                        {a, b, d, b, c, d});
                }
            }
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareMultipleRendertargetsOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeMultipleRendertargetsText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareMultipleRendertargetsOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU MRT artifact.");
            }
        }
    }

    void WebgpuMultipleRendertargetsRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateMultipleRendertargetsScenario(options);
        device = inDevice;
        buildMultipleRendertargetsTorusKnot(
            positions,
            normals,
            textureCoordinates,
            indices);
        if (positions.size() != 4257u ||
            indices.size() != 24576u)
        {
            throw std::runtime_error(
                "TorusKnot geometry counts diverged from r185.");
        }
        diffuse = decodeJpegRgba8(
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "hardwood2_diffuse.jpg");
        if (diffuse.width == 0u ||
            diffuse.height == 0u ||
            diffuse.pixels.size() !=
                size_t(diffuse.width) * diffuse.height * 4u)
        {
            throw std::runtime_error(
                "Hardwood JPEG decode produced invalid dimensions.");
        }
        diffuseMips = buildMrtMips(diffuse);
        if (options.scenarioId == "orbit")
        {
            orbitYaw = -2.0f * Pi * 65.0f / 500.0f;
            orbitPitch = -2.0f * Pi * -45.0f / 500.0f;
        }
    }

    void WebgpuMultipleRendertargetsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuMultipleRendertargetsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount =
            uint64_t(width) * uint64_t(height) * 4u;
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(
                options.captureRgbaPath.c_str());
            prepareMultipleRendertargetsOutput(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU MRT RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"source\": \"gvm-three-r185\",\n"
            << "  \"caseId\": \"webgpu_multiple_rendertargets\",\n"
            << "  \"scenarioId\": \"" << options.scenarioId.c_str()
            << "\",\n  \"pipeline\": \"" << options.pipeline.c_str()
            << "\",\n  \"backend\": \""
            << threeSampleBackendName(options.backend)
            << "\",\n  \"frame\": " << frameIndex
            << ",\n  \"randomSeed\": " << options.randomSeed
            << ",\n  \"width\": " << width
            << ",\n  \"height\": " << height
            << ",\n  \"rowStrideBytes\": " << uint64_t(width) * 4u
            << ",\n  \"byteCount\": " << byteCount
            << ",\n  \"format\": \"rgba8unorm\",\n  \"inputReplay\": ";
        if (options.scenarioId == "orbit")
        {
            metadata
                << "{\"sha256\":\"78b65a6fdb94e3f16987385fd3e5dbcd7d7889f5dfb3dc3571965c55668c7537\","
                << "\"caseId\":\"webgpu_multiple_rendertargets\",\"scenarioId\":\"orbit\","
                << "\"captureFrame\":61,\"eventCount\":3,\"target\":\"canvas:not([class])\"}";
        }
        else
        {
            metadata << "null";
        }
        metadata
            << ",\n"
            << "  \"renderSetCount\": 0,\n"
            << "  \"entityCount\": 1,\n"
            << "  \"vertexCount\": 4257,\n"
            << "  \"indexCount\": 24576,\n"
            << "  \"colorAttachmentCount\": 2\n}\n";
        writeMultipleRendertargetsText(
            options.captureMetadataPath,
            metadata.str());
        writeMultipleRendertargetsText(
            options.sceneSnapshotPath,
            std::string("{\n  \"caseId\": \"webgpu_multiple_rendertargets\",\n"
            "  \"scenarioId\": \"") + options.scenarioId.c_str() + "\",\n"
            "  \"frame\": " + std::to_string(frameIndex) + ",\n"
            "  \"gpuWorkDslOnly\": true,\n"
            "  \"renderSetPolicy\": \"not-required\",\n"
            "  \"sceneRenderSetCount\": 0,\n"
            "  \"renderableObjectCount\": 1,\n"
            "  \"entityCount\": 1,\n"
            "  \"instanceCount\": 1,\n"
            "  \"vertexCount\": 4257,\n"
            "  \"indexCount\": 24576,\n"
            "  \"drawCommandCount\": 1,\n"
            "  \"scenePassCount\": 1,\n"
            "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\","
            "\"scenePass\":\"output-normal-mrt\",\"entityOrdinal\":0}]\n}\n");
        writeMultipleRendertargetsText(
            options.semanticSnapshotPath,
            "{\n  \"algorithm\": \"r185-torus-knot-true-mrt\",\n"
            "  \"asset\": \"textures/hardwood2_diffuse.jpg\",\n"
            "  \"colorAttachments\": [\"output\", \"normal\"],\n"
            "  \"attachmentFormats\": [\"rgba16float\", \"rgba16float\"],\n"
            "  \"screenPass\": \"split-composite\"\n}\n");
        captureWritten = true;
    }

    void WebgpuMultipleRendertargetsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        positions.clear();
        normals.clear();
        textureCoordinates.clear();
        indices.clear();
        diffuse.pixels.clear();
    }
}
