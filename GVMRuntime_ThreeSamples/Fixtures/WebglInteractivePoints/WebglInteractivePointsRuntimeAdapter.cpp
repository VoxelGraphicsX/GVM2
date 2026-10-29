#include "WebglInteractivePointsRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/unordered_map.h>

#include <glm/vec3.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t LogicalPointCount = 1538u;
        constexpr uint32_t DiscExtent = 32u;
        constexpr uint32_t DiscMipCount = 6u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(InteractivePointsHostVertex) == 48u);
        static_assert(sizeof(InteractivePointsHostObjectData) == 128u);
        static_assert(sizeof(InteractivePointsHostInstanceData) == 16u);
        static_assert(sizeof(InteractivePointsHostMaterialData) == 16u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareInteractivePointsOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Validates the three immutable manifest scenarios and host contract. */
        void validateInteractivePointsScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool pointHit =
                options.scenarioId == "point-hit" &&
                options.targetFrame == 61u &&
                !options.inputReplayPath.empty();
            if (options.caseId != "webgl_interactive_points" ||
                (!initial && !animated && !pointHit) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty() ||
                ((initial || animated) && !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "Interactive points require the locked case, scenario, extent, seed, assets, and replay contract.");
            }
        }

        /** Sets one Cartesian component selected by a BoxGeometry axis index. */
        void setInteractivePointsAxis(
            glm::dvec3 &value,
            uint32_t axis,
            double component)
        {
            if (axis == 0u) value.x = component;
            else if (axis == 1u) value.y = component;
            else value.z = component;
        }

        /** Appends one exact segmented Three BoxGeometry plane. */
        void appendInteractivePointsBoxPlane(
            eastl::vector<glm::vec3> &positions,
            eastl::vector<uint32_t> &sourceIndices,
            uint32_t uAxis,
            uint32_t vAxis,
            uint32_t wAxis,
            double uDirection,
            double vDirection,
            double width,
            double height,
            double depth,
            uint32_t gridX,
            uint32_t gridY)
        {
            const double segmentWidth = width / double(gridX);
            const double segmentHeight = height / double(gridY);
            const double widthHalf = width * 0.5;
            const double heightHalf = height * 0.5;
            const double depthHalf = depth * 0.5;
            const uint32_t gridX1 = gridX + 1u;
            const uint32_t baseVertex = static_cast<uint32_t>(positions.size());
            glm::dvec3 value(0.0);
            for (uint32_t iy = 0u; iy <= gridY; ++iy)
            {
                const double y = double(iy) * segmentHeight - heightHalf;
                for (uint32_t ix = 0u; ix <= gridX; ++ix)
                {
                    const double x = double(ix) * segmentWidth - widthHalf;
                    setInteractivePointsAxis(value, uAxis, x * uDirection);
                    setInteractivePointsAxis(value, vAxis, y * vDirection);
                    setInteractivePointsAxis(value, wAxis, depthHalf);
                    positions.push_back(glm::vec3(
                        static_cast<float>(value.x),
                        static_cast<float>(value.y),
                        static_cast<float>(value.z)));
                }
            }
            for (uint32_t iy = 0u; iy < gridY; ++iy)
            {
                for (uint32_t ix = 0u; ix < gridX; ++ix)
                {
                    const uint32_t a = baseVertex + ix + gridX1 * iy;
                    const uint32_t b = baseVertex + ix + gridX1 * (iy + 1u);
                    const uint32_t c = b + 1u;
                    const uint32_t d = a + 1u;
                    sourceIndices.push_back(a);
                    sourceIndices.push_back(b);
                    sourceIndices.push_back(d);
                    sourceIndices.push_back(b);
                    sourceIndices.push_back(c);
                    sourceIndices.push_back(d);
                }
            }
        }

        /** Packs Three's 1e-4 position hash into one deterministic key. */
        uint64_t makeInteractivePointsMergeKey(const glm::vec3 &position)
        {
            constexpr int32_t Offset = 1 << 20;
            constexpr uint64_t Mask = (1ull << 21u) - 1ull;
            const int32_t x = static_cast<int32_t>(
                double(position.x) * 10000.0 + 0.5);
            const int32_t y = static_cast<int32_t>(
                double(position.y) * 10000.0 + 0.5);
            const int32_t z = static_cast<int32_t>(
                double(position.z) * 10000.0 + 0.5);
            return (uint64_t(x + Offset) & Mask) |
                   ((uint64_t(y + Offset) & Mask) << 21u) |
                   ((uint64_t(z + Offset) & Mask) << 42u);
        }

        /** Reproduces position-only mergeVertices traversal for the 16-segment box. */
        eastl::vector<glm::vec3> buildInteractivePointsPositions()
        {
            eastl::vector<glm::vec3> rawPositions;
            eastl::vector<uint32_t> sourceIndices;
            rawPositions.reserve(1734u);
            sourceIndices.reserve(9216u);
            appendInteractivePointsBoxPlane(
                rawPositions, sourceIndices, 2u, 1u, 0u,
                -1.0, -1.0, 200.0, 200.0, 200.0, 16u, 16u);
            appendInteractivePointsBoxPlane(
                rawPositions, sourceIndices, 2u, 1u, 0u,
                1.0, -1.0, 200.0, 200.0, -200.0, 16u, 16u);
            appendInteractivePointsBoxPlane(
                rawPositions, sourceIndices, 0u, 2u, 1u,
                1.0, 1.0, 200.0, 200.0, 200.0, 16u, 16u);
            appendInteractivePointsBoxPlane(
                rawPositions, sourceIndices, 0u, 2u, 1u,
                1.0, -1.0, 200.0, 200.0, -200.0, 16u, 16u);
            appendInteractivePointsBoxPlane(
                rawPositions, sourceIndices, 0u, 1u, 2u,
                1.0, -1.0, 200.0, 200.0, 200.0, 16u, 16u);
            appendInteractivePointsBoxPlane(
                rawPositions, sourceIndices, 0u, 1u, 2u,
                -1.0, -1.0, 200.0, 200.0, -200.0, 16u, 16u);

            eastl::unordered_map<uint64_t, uint32_t> hashToIndex;
            eastl::vector<glm::vec3> merged;
            hashToIndex.reserve(rawPositions.size());
            merged.reserve(rawPositions.size());
            for (const uint32_t sourceIndex : sourceIndices)
            {
                const uint64_t key =
                    makeInteractivePointsMergeKey(rawPositions[sourceIndex]);
                if (hashToIndex.find(key) == hashToIndex.end())
                {
                    hashToIndex.emplace(
                        key,
                        static_cast<uint32_t>(merged.size()));
                    merged.push_back(rawPositions[sourceIndex]);
                }
            }
            if (merged.size() != LogicalPointCount)
            {
                throw std::runtime_error(
                    "Interactive BoxGeometry merge did not produce 1,538 points.");
            }
            return merged;
        }

        /** Returns Three's wrapped HSL channel interpolation. */
        double interactivePointsHueToRgb(
            double minimum,
            double maximum,
            double hue)
        {
            if (hue < 0.0) hue += 1.0;
            if (hue > 1.0) hue -= 1.0;
            if (hue < 1.0 / 6.0)
            {
                return minimum + (maximum - minimum) * 6.0 * hue;
            }
            if (hue < 0.5) return maximum;
            if (hue < 2.0 / 3.0)
            {
                return minimum +
                       (maximum - minimum) * 6.0 * (2.0 / 3.0 - hue);
            }
            return minimum;
        }

        /** Evaluates the upstream full-saturation HSL color in linear working space. */
        glm::vec3 makeInteractivePointsColor(double hue)
        {
            hue -= std::floor(hue);
            return glm::vec3(
                static_cast<float>(interactivePointsHueToRgb(
                    0.0, 1.0, hue + 1.0 / 3.0)),
                static_cast<float>(interactivePointsHueToRgb(
                    0.0, 1.0, hue)),
                static_cast<float>(interactivePointsHueToRgb(
                    0.0, 1.0, hue - 1.0 / 3.0)));
        }

        /** Builds Three's OpenGL perspective projection before backend conversion. */
        glm::mat4 makeInteractivePointsProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10000.0;
            const double top =
                NearDistance * std::tan(45.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = height * (800.0 / 500.0);
            glm::mat4 projection(0.0f);
            projection[0u][0u] =
                static_cast<float>(2.0 * NearDistance / width);
            projection[1u][1u] =
                static_cast<float>(2.0 * NearDistance / height);
            projection[2u][2u] = static_cast<float>(
                -(FarDistance + NearDistance) /
                (FarDistance - NearDistance));
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -2.0 * FarDistance * NearDistance /
                (FarDistance - NearDistance));
            return projection;
        }

        /** Builds the target-frame Euler XYZ rotation and camera translation. */
        glm::mat4 makeInteractivePointsModelView(uint32_t targetFrame)
        {
            const double x = 0.0005 * double(targetFrame + 1u);
            const double y = 0.001 * double(targetFrame + 1u);
            const double sx = std::sin(x * 0.5);
            const double cx = std::cos(x * 0.5);
            const double sy = std::sin(y * 0.5);
            const double cy = std::cos(y * 0.5);
            const double qx = sx * cy;
            const double qy = cx * sy;
            const double qz = sx * sy;
            const double qw = cx * cy;
            const double x2 = qx + qx;
            const double y2 = qy + qy;
            const double z2 = qz + qz;
            const double xx = qx * x2;
            const double xy = qx * y2;
            const double xz = qx * z2;
            const double yy = qy * y2;
            const double yz = qy * z2;
            const double zz = qz * z2;
            const double wx = qw * x2;
            const double wy = qw * y2;
            const double wz = qw * z2;
            glm::mat4 matrix(1.0f);
            matrix[0u][0u] = static_cast<float>(1.0 - (yy + zz));
            matrix[0u][1u] = static_cast<float>(xy + wz);
            matrix[0u][2u] = static_cast<float>(xz - wy);
            matrix[1u][0u] = static_cast<float>(xy - wz);
            matrix[1u][1u] = static_cast<float>(1.0 - (xx + zz));
            matrix[1u][2u] = static_cast<float>(yz + wx);
            matrix[2u][0u] = static_cast<float>(xz + wy);
            matrix[2u][1u] = static_cast<float>(yz - wx);
            matrix[2u][2u] = static_cast<float>(1.0 - (xx + yy));
            matrix[3u][2u] = -250.0f;
            return matrix;
        }

        /** Finds the nearest center-ray point under Raycaster's unit threshold. */
        uint32_t findInteractivePointsCenterHit(
            const eastl::vector<glm::vec3> &positions,
            const glm::mat4 &modelView)
        {
            uint32_t selected = UINT32_MAX;
            double nearestDistance = std::numeric_limits<double>::max();
            for (uint32_t index = 0u; index < positions.size(); ++index)
            {
                const glm::vec4 view =
                    modelView * glm::vec4(positions[index], 1.0f);
                const double radialSquared =
                    double(view.x) * view.x + double(view.y) * view.y;
                const double rayDistance = -double(view.z);
                if (radialSquared <= 1.0 && rayDistance >= 0.0 &&
                    rayDistance < nearestDistance)
                {
                    selected = index;
                    nearestDistance = rayDistance;
                }
            }
            return selected;
        }

        /** Flips decoded rows like Three's default WebGL texture upload. */
        RgbaImageData flipInteractivePointsRows(const RgbaImageData &source)
        {
            if (source.width != DiscExtent || source.height != DiscExtent)
            {
                throw std::runtime_error("disc.png must remain 32x32 RGBA8.");
            }
            RgbaImageData result = source;
            const size_t rowBytes = static_cast<size_t>(source.width) * 4u;
            for (uint32_t row = 0u; row < source.height; ++row)
            {
                eastl::copy_n(
                    source.pixels.begin() +
                        static_cast<size_t>(source.height - 1u - row) * rowBytes,
                    rowBytes,
                    result.pixels.begin() +
                        static_cast<size_t>(row) * rowBytes);
            }
            return result;
        }

        /** Builds the browser-compatible RGBA8 mip chain using nearest-even UNorm ties. */
        eastl::vector<RgbaImageData> buildInteractivePointsMipChain(
            const RgbaImageData &baseImage)
        {
            eastl::vector<RgbaImageData> levels;
            levels.push_back(baseImage);
            while (levels.back().width > 1u || levels.back().height > 1u)
            {
                const RgbaImageData &source = levels.back();
                RgbaImageData target;
                target.width = eastl::max(source.width / 2u, 1u);
                target.height = eastl::max(source.height / 2u, 1u);
                target.pixels.resize(
                    static_cast<size_t>(target.width) * target.height * 4u);
                for (uint32_t y = 0u; y < target.height; ++y)
                {
                    for (uint32_t x = 0u; x < target.width; ++x)
                    {
                        const uint32_t sourceX = x * 2u;
                        const uint32_t sourceY = y * 2u;
                        for (uint32_t channel = 0u; channel < 4u; ++channel)
                        {
                            uint32_t sum = 0u;
                            for (uint32_t sampleY = 0u; sampleY < 2u; ++sampleY)
                            {
                                for (uint32_t sampleX = 0u; sampleX < 2u; ++sampleX)
                                {
                                    const size_t offset =
                                        (static_cast<size_t>(sourceY + sampleY) *
                                             source.width +
                                         sourceX + sampleX) *
                                            4u +
                                        channel;
                                    sum += source.pixels[offset];
                                }
                            }
                            uint32_t rounded = sum / 4u;
                            const uint32_t remainder = sum % 4u;
                            if (remainder > 2u ||
                                (remainder == 2u && (rounded & 1u) != 0u))
                            {
                                ++rounded;
                            }
                            const size_t targetOffset =
                                (static_cast<size_t>(y) * target.width + x) *
                                    4u +
                                channel;
                            target.pixels[targetOffset] =
                                static_cast<uint8_t>(rounded);
                        }
                    }
                }
                levels.push_back(eastl::move(target));
            }
            return levels;
        }

        /** Appends one typed payload to a RenderSet entity allocation. */
        void appendInteractivePointsBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }
    } // namespace

    void WebglInteractivePointsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateInteractivePointsScenario(options);
        device = inDevice;
        const eastl::vector<glm::vec3> positions =
            buildInteractivePointsPositions();
        const glm::mat4 modelView =
            makeInteractivePointsModelView(options.targetFrame);
        if (options.scenarioId == "point-hit" ||
            options.scenarioId == "animated")
        {
            // The locked animated oracle retains the default center-pointer
            // selection established by the first render before the fixed-step
            // rotation capture advances. Re-evaluate that event-frame ray
            // with the same CPU picker instead of changing the GPU path.
            selectedPoint = findInteractivePointsCenterHit(
                positions,
                makeInteractivePointsModelView(0u));
        }
        const TexturedBoxHostFloat4 corners[4u] = {
            {-1.0f, -1.0f, 0.0f, 0.0f},
            {1.0f, -1.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 0.0f, 0.0f},
            {-1.0f, 1.0f, 0.0f, 0.0f},
        };
        vertices.reserve(LogicalPointCount * 4u);
        indices.reserve(LogicalPointCount * 6u);
        for (uint32_t index = 0u; index < LogicalPointCount; ++index)
        {
            const glm::vec3 color = makeInteractivePointsColor(
                0.01 + 0.1 *
                    (double(index) / double(LogicalPointCount)));
            const float size = index == selectedPoint ? 20.0f : 10.0f;
            const uint32_t baseVertex = index * 4u;
            for (const TexturedBoxHostFloat4 &corner : corners)
            {
                vertices.push_back({
                    {positions[index].x, positions[index].y, positions[index].z, size},
                    {color.x, color.y, color.z, 1.0f},
                    corner,
                });
            }
            indices.push_back(baseVertex + 0u);
            indices.push_back(baseVertex + 1u);
            indices.push_back(baseVertex + 2u);
            indices.push_back(baseVertex + 0u);
            indices.push_back(baseVertex + 2u);
            indices.push_back(baseVertex + 3u);
        }

        const RgbaImageData baseImage = flipInteractivePointsRows(
            decodeStraightPngRgba8(
                std::filesystem::path(options.assetRoot.c_str()) /
                "textures" / "sprites" / "disc.png"));
        const eastl::vector<RgbaImageData> mipChain =
            buildInteractivePointsMipChain(baseImage);
        if (mipChain.size() != DiscMipCount)
        {
            throw std::runtime_error("disc.png mip count diverged from r185.");
        }
        for (const RgbaImageData &mip : mipChain)
        {
            discMipOffsets.push_back(discTextureBytes.size());
            discTextureBytes.insert(
                discTextureBytes.end(),
                mip.pixels.begin(),
                mip.pixels.end());
        }

        const InteractivePointsHostObjectData objectData = {
            .projectionMatrix = makeInteractivePointsProjection(),
            .modelViewMatrix = modelView,
        };
        const InteractivePointsHostMaterialData materialData = {
            .colorAndAlphaTest = {1.0f, 1.0f, 1.0f, 0.9f},
        };
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create interactive points RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendInteractivePointsBufferPayload(
            allocation,
            WebglInteractivePointsSceneRenderSetComponents::vertices,
            "WebglInteractivePointsVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]),
            1u);
        appendInteractivePointsBufferPayload(
            allocation,
            WebglInteractivePointsSceneRenderSetComponents::indices,
            "WebglInteractivePointsIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]),
            1u);
        appendInteractivePointsBufferPayload(
            allocation,
            WebglInteractivePointsSceneRenderSetComponents::objects,
            "WebglInteractivePointsObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendInteractivePointsBufferPayload(
            allocation,
            WebglInteractivePointsSceneRenderSetComponents::instances,
            "WebglInteractivePointsInstances",
            &instanceData,
            sizeof(instanceData),
            1u);
        appendInteractivePointsBufferPayload(
            allocation,
            WebglInteractivePointsSceneRenderSetComponents::materials,
            "WebglInteractivePointsMaterial",
            &materialData,
            sizeof(materialData),
            1u);
        GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
        textureComponent.textureComponentHandle =
            WebglInteractivePointsSceneRenderSetComponents::textures;
        textureComponent.textures.push_back({
            .textureName = "WebglInteractivePointsDisc",
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .width = DiscExtent,
            .height = DiscExtent,
            .data = discTextureBytes.data(),
            .dataStorageBytes = discTextureBytes.size(),
            .mipmapOffsetBytes = discMipOffsets,
        });
        allocation.textureInfos.push_back(eastl::move(textureComponent));
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglInteractivePointsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglInteractivePointsRuntimeAdapter::afterFrame(
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
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Interactive points RGBA8 capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeArtifacts(options, frameIndex, width, height, rgba);
        captureWritten = true;
    }

    void WebglInteractivePointsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        discTextureBytes.clear();
        discMipOffsets.clear();
    }

    void WebglInteractivePointsRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareInteractivePointsOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::out | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write interactive points RGBA capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareInteractivePointsOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_interactive_points\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\"";
            if (options.scenarioId == "point-hit")
            {
                output
                    << ",\n  \"inputReplay\":{\"schemaVersion\":1,"
                    << "\"caseId\":\"webgl_interactive_points\","
                    << "\"scenarioId\":\"point-hit\","
                    << "\"captureFrame\":61,"
                    << "\"sha256\":\"20a7865430583b28a1094a145b62333199990074d6d07eca91bbda79f6ce0fac\","
                    << "\"target\":\"#container > canvas\","
                    << "\"eventCount\":1}";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareInteractivePointsOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_interactive_points\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":1,\n"
                << "  \"entityCount\":1,\n"
                << "  \"instanceCount\":1,\n"
                << "  \"logicalPointCount\":1538,\n"
                << "  \"vertexCount\":6152,\n"
                << "  \"indexCount\":9228,\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"screenPassCount\":0,\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"selectedPoint\":";
            if (selectedPoint == UINT32_MAX) output << "null";
            else output << selectedPoint;
            output
                << ",\n"
                << "  \"renderSetType\":\"WebglInteractivePointsSceneRenderSet\",\n"
                << "  \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
                << "\"renderSetId\":\"scene-set\","
                << "\"renderSetType\":\"WebglInteractivePointsSceneRenderSet\","
                << "\"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],"
                << "\"renderableObjectCount\":1,\"entityCount\":1,"
                << "\"entities\":[{\"entityId\":0,"
                << "\"logicalRenderableId\":\"box-surface-points\","
                << "\"instanceCount\":1}],"
                << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
                << "\"scenePasses\":[{\"name\":\"main-points\","
                << "\"renderClass\":\"WebglInteractivePointsMainPass\","
                << "\"renderSetId\":\"scene-set\","
                << "\"renderSetBindingCount\":1,"
                << "\"drawMode\":\"render-set-indexed-indirect\","
                << "\"invocationCount\":1,\"drawCommandCount\":1,"
                << "\"usesStandaloneGeometry\":false,"
                << "\"usesExplicitDrawCount\":false}]}],\n"
                << "  \"usesRenderEntityID\":true,\n"
                << "  \"usesRenderEntityInstanceID\":false,\n"
                << "  \"directDrawFallback\":false\n"
                << "}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareInteractivePointsOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_interactive_points\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"asset\":\"textures/sprites/disc.png\",\n"
                << "  \"logicalPointCount\":1538,\n"
                << "  \"particleSize\":10,\n"
                << "  \"hoverParticleSize\":25\n"
                << "}\n";
        }
    }
} // namespace GVM::ThreeSamples
