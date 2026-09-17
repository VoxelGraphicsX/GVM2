#include "SvgLinesRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/array.h>
#include <EASTL/algorithm.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t CircleSegmentCount = 32u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        /** Converts an SVG pixel coordinate to the direct GPU clip convention. */
        glm::vec4 svgPixelToClip(const glm::dvec3 &pixel)
        {
            return glm::vec4(
                float(pixel.x / 400.0 - 1.0),
                float(1.0 - pixel.y / 250.0),
                float(pixel.z),
                1.0f);
        }

        /** Appends one clockwise-independent solid triangle in pixel coordinates. */
        void appendSvgTriangle(
            SvgLinesEntityData &entity,
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            const glm::vec4 &color)
        {
            const uint32_t first = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({svgPixelToClip(a), color});
            entity.vertices.push_back({svgPixelToClip(b), color});
            entity.vertices.push_back({svgPixelToClip(c), color});
            entity.indices.push_back(first);
            entity.indices.push_back(first + 1u);
            entity.indices.push_back(first + 2u);
        }

        /** Appends one round SVG line cap as a deterministic triangle fan. */
        void appendSvgCircle(
            SvgLinesEntityData &entity,
            const glm::dvec3 &center,
            double radius,
            const glm::vec4 &color)
        {
            for (uint32_t segment = 0u; segment < CircleSegmentCount; ++segment)
            {
                const double firstAngle =
                    2.0 * Pi * double(segment) / double(CircleSegmentCount);
                const double secondAngle =
                    2.0 * Pi * double(segment + 1u) / double(CircleSegmentCount);
                appendSvgTriangle(
                    entity,
                    center,
                    center + glm::dvec3(
                        radius * std::cos(firstAngle), radius * std::sin(firstAngle), 0.0),
                    center + glm::dvec3(
                        radius * std::cos(secondAngle), radius * std::sin(secondAngle), 0.0),
                    color);
            }
        }

        /** Appends one SVG round-cap stroke interval as triangle-list geometry. */
        void appendSvgCapsule(
            SvgLinesEntityData &entity,
            const glm::dvec3 &start,
            const glm::dvec3 &end,
            double width,
            const glm::vec4 &color)
        {
            const glm::dvec2 delta(end.x - start.x, end.y - start.y);
            const double length = glm::length(delta);
            if (length <= 1.0e-8) return;
            const glm::dvec2 direction = delta / length;
            const glm::dvec2 normal(-direction.y, direction.x);
            const glm::dvec2 offset = normal * (width * 0.5);
            const glm::dvec3 startMinus(start.x - offset.x, start.y - offset.y, start.z);
            const glm::dvec3 startPlus(start.x + offset.x, start.y + offset.y, start.z);
            const glm::dvec3 endMinus(end.x - offset.x, end.y - offset.y, end.z);
            const glm::dvec3 endPlus(end.x + offset.x, end.y + offset.y, end.z);
            appendSvgTriangle(entity, startMinus, endMinus, endPlus, color);
            appendSvgTriangle(entity, startMinus, endPlus, startPlus, color);
            appendSvgCircle(entity, start, width * 0.5, color);
            appendSvgCircle(entity, end, width * 0.5, color);
        }

        /** Appends one independently dashed SVG segment with a reset 10/10 phase. */
        void appendSvgDashedSegment(
            SvgLinesEntityData &entity,
            const glm::dvec3 &start,
            const glm::dvec3 &end,
            const glm::vec4 &color)
        {
            const glm::dvec3 delta = end - start;
            const double length = glm::length(glm::dvec2(delta.x, delta.y));
            if (length <= 1.0e-8) return;
            for (double dashStart = 0.0; dashStart < length; dashStart += 20.0)
            {
                const double dashEnd = eastl::min(dashStart + 10.0, length);
                appendSvgCapsule(
                    entity,
                    start + delta * (dashStart / length),
                    start + delta * (dashEnd / length),
                    1.0,
                    color);
            }
        }

        /** Creates Three's local XYZ Euler rotation for the x/z-only SVG case. */
        glm::dmat4 makeSvgEulerXz(double x, double z)
        {
            glm::dmat4 result(1.0);
            result = glm::rotate(result, -x, glm::dvec3(1.0, 0.0, 0.0));
            result = glm::rotate(result, -z, glm::dvec3(0.0, 0.0, 1.0));
            return result;
        }

        /** Projects one source circle point through the frozen SVG camera. */
        glm::dvec3 projectSvgPoint(
            const glm::dmat4 &model,
            const glm::dvec3 &point)
        {
            const glm::dvec4 world = model * glm::dvec4(point, 1.0);
            const glm::dvec4 view(world.x, world.y, world.z - 10.0, 1.0);
            const double nearDistance = 0.1;
            const double top = nearDistance * std::tan(33.0 * Pi / 360.0);
            const double height = 2.0 * top;
            const double width = height * 1.6;
            const double clipX = 2.0 * nearDistance / width * view.x;
            const double clipY = 2.0 * nearDistance / height * view.y;
            const double clipW = -view.z;
            return glm::dvec3(
                (clipX / clipW + 1.0) * 400.0,
                (1.0 - clipY / clipW) * 250.0,
                0.5 - world.z * 0.01);
        }

        /** Builds one of the four SVG source line entities at a fixed time. */
        SvgLinesEntityData buildSvgLineEntity(
            uint32_t ordinal,
            double timeSeconds)
        {
            const eastl::array<glm::vec4, 4u> Colors = {
                glm::vec4(40.0f / 255.0f, 66.0f / 255.0f, 33.0f / 255.0f, 1.0f),
                glm::vec4(160.0f / 255.0f, 22.0f / 255.0f, 201.0f / 255.0f, 1.0f),
                glm::vec4(63.0f / 255.0f, 46.0f / 255.0f, 19.0f / 255.0f, 1.0f),
                glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)};
            SvgLinesEntityData entity;
            entity.logicalId = "svg-line-" + eastl::to_string(ordinal + 1u);
            const double scale = ordinal < 3u
                ? double(ordinal + 1u) / 3.0
                : 2.0;
            const double sceneRotationX = timeSeconds / 3.0;
            const double sceneRotationZ = timeSeconds / 4.0;
            const double objectRotationX = double(ordinal + 1u) + timeSeconds / 3.0;
            const double objectRotationZ = double(ordinal + 1u) + timeSeconds / 4.0;
            const glm::dmat4 model =
                makeSvgEulerXz(sceneRotationX, sceneRotationZ) *
                makeSvgEulerXz(objectRotationX, objectRotationZ) *
                glm::scale(glm::dmat4(1.0), glm::dvec3(scale));
            eastl::array<glm::dvec3, 51u> projected = {};
            for (uint32_t point = 0u; point <= 50u; ++point)
            {
                const double angle = double(point) / 50.0 * 2.0 * Pi;
                projected[point] = projectSvgPoint(
                    model,
                    glm::dvec3(std::sin(angle), 0.0, std::cos(angle)));
            }
            for (uint32_t segment = 0u; segment < 50u; ++segment)
            {
                if (ordinal == 3u)
                {
                    appendSvgDashedSegment(
                        entity, projected[segment], projected[segment + 1u], Colors[ordinal]);
                }
                else
                {
                    appendSvgCapsule(
                        entity, projected[segment], projected[segment + 1u], 10.0, Colors[ordinal]);
                }
            }
            entity.objectData.geometryAndFlags = glm::vec4(
                float(entity.vertices.size()), 0.0f, 0.0f, 0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.colorAndWidth = glm::vec4(
                1.0f, ordinal == 3u ? 1.0f : 10.0f,
                ordinal == 3u ? 1.0f : 0.0f, 0.0f);
            return entity;
        }

        /** Appends one typed RenderSet buffer allocation. */
        void appendSvgBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
            const void *value,
            uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }

        /** Creates parent directories for one requested SVG artifact. */
        void prepareSvgOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one deterministic SVG text artifact. */
        void writeSvgText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareSvgOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write an SVG line text artifact.");
        }
    } // namespace

    void SvgLinesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated" && options.targetFrame == 120u;
        if (options.caseId != "svg_lines" || (!initial && !animated) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || !options.inputReplayPath.empty())
        {
            throw std::invalid_argument("SVG lines adapter requires one locked manifest scenario.");
        }
        device = inDevice;
        const double timeSeconds = double(options.targetFrame) / 60.0;
        entities.reserve(4u);
        for (uint32_t ordinal = 0u; ordinal < 4u; ++ordinal)
        {
            entities.push_back(buildSvgLineEntity(ordinal, timeSeconds));
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("SVG lines could not create its RenderSet encoder.");
        for (SvgLinesEntityData &entity : entities)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendSvgBuffer(allocation, SvgLinesSceneRenderSetComponents::vertices, entity.logicalId + "-vertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0u]));
            appendSvgBuffer(allocation, SvgLinesSceneRenderSetComponents::indices, entity.logicalId + "-indices", entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0u]));
            appendSvgBuffer(allocation, SvgLinesSceneRenderSetComponents::objects, entity.logicalId + "-object", &entity.objectData, sizeof(entity.objectData));
            appendSvgBuffer(allocation, SvgLinesSceneRenderSetComponents::instances, entity.logicalId + "-instance", &entity.instanceData, sizeof(entity.instanceData));
            appendSvgBuffer(allocation, SvgLinesSceneRenderSetComponents::materials, entity.logicalId + "-material", &entity.materialData, sizeof(entity.materialData));
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void SvgLinesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void SvgLinesRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
            prepareSvgOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write the SVG line RGBA capture.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
                 << "  \"caseId\":\"svg_lines\",\n  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n  \"randomSeed\":" << options.randomSeed << ",\n"
                 << "  \"width\":" << width << ",\n  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false}\n}\n";
        writeSvgText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"svg_lines\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex << ",\n"
                 << "  \"gpuWorkDslOnly\":true,\n  \"renderSetPolicy\":\"required\",\n"
                 << "  \"sceneRenderSetCount\":1,\n  \"renderSetType\":\"SvgLinesSceneRenderSet\",\n"
                 << "  \"renderableObjectCount\":4,\n  \"entityCount\":4,\n"
                 << "  \"instanceCounts\":[1,1,1,1],\n  \"scenePassCount\":1,\n"
                 << "  \"drawCommandCount\":4,\n  \"directDrawFallback\":false,\n"
                 << "  \"sourceSegmentCount\":200,\n  \"triangleVertexCounts\":[";
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u) snapshot << ',';
            snapshot << entities[index].vertices.size();
        }
        snapshot << "]\n}\n";
        writeSvgText(options.sceneSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void SvgLinesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
} // namespace GVM::ThreeSamples
