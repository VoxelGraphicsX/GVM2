#include "WebgpuInstancePointsRuntimeAdapter.hpp"

#include "InstanceSampleGeometry.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

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
        constexpr uint32_t PointCount = 256u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebgpuInstancePointsHostVertex) == 16u);
        static_assert(sizeof(WebgpuInstancePointsHostObjectData) == 160u);
        static_assert(sizeof(WebgpuInstancePointsHostInstanceData) == 32u);
        static_assert(sizeof(WebgpuInstancePointsHostMaterialData) == 16u);

        /** Validates the three frozen webgpu_instance_points scenarios. */
        void validateInstancePointsScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u;
            const bool settings =
                options.scenarioId == "width-settings" &&
                options.targetFrame == 61u;
            if (options.caseId != "webgpu_instance_points" ||
                (!initial && !animated && !settings) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                (settings != !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "WebGPU instance points requires one locked Manifest scenario.");
            }
        }

        /** Builds a WebGPU depth-range perspective with generated-backend Y compensation. */
        glm::mat4 makeInstancePointsProjection(float aspect)
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 1000.0;
            const double top =
                NearDistance * std::tan(40.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = double(aspect) * height;
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] =
                float(2.0 * NearDistance / width);
            result[1u][1u] =
                float(-2.0 * NearDistance / height);
            result[2u][2u] =
                float(-FarDistance / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] =
                float(-FarDistance * NearDistance / depth);
            return result;
        }

        /** Applies the locked OrbitControls drag to the initial camera position. */
        glm::vec3 makeInstancePointsCamera(bool applyReplay)
        {
            const float radius =
                glm::length(glm::vec3(-40.0f, 0.0f, 60.0f));
            float theta = std::atan2(-40.0f, 60.0f);
            float phi = Pi * 0.5f;
            if (applyReplay)
            {
                constexpr double dampingFactor = 0.05;
                constexpr double updateCount = 63.0;
                const double appliedDrag =
                    1.0 - std::pow(1.0 - dampingFactor, updateCount);
                theta -= float(2.0 * Pi * 55.0 / 500.0 * appliedDrag);
                phi -= float(2.0 * Pi * -25.0 / 500.0 * appliedDrag);
            }
            return glm::vec3(
                radius * std::sin(phi) * std::sin(theta),
                radius * std::cos(phi),
                radius * std::sin(phi) * std::cos(theta));
        }

        /** Converts one sRGB channel into Three's linear working color. */
        float instancePointsSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Builds the exact two-triangle source sprite. */
        void buildInstancePointsQuad(
            eastl::vector<WebgpuInstancePointsHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices = {
                {{-0.5f, -0.5f, 0.0f, 1.0f}},
                {{0.5f, -0.5f, 1.0f, 1.0f}},
                {{0.5f, 0.5f, 1.0f, 0.0f}},
                {{-0.5f, 0.5f, 0.0f, 0.0f}},
            };
            indices = {0u, 1u, 2u, 0u, 2u, 3u};
        }

        /** Builds 256 centripetal Catmull-Rom point instances and linear colors. */
        void buildInstancePointsData(
            eastl::vector<WebgpuInstancePointsHostInstanceData> &instances)
        {
            const auto controls =
                ThreeCompat::buildHilbert3DControlPoints(
                    glm::vec3(0.0f),
                    20.0f,
                    1);
            const auto points =
                ThreeCompat::sampleCentripetalCatmullRom(
                    controls,
                    PointCount);
            instances.clear();
            instances.reserve(PointCount);
            for (uint32_t index = 0u;
                 index < PointCount;
                 ++index)
            {
                const float parameter =
                    float(index) / float(PointCount);
                const glm::vec3 srgb =
                    ThreeCompat::convertLinearHslToRgb(
                        parameter,
                        1.0f,
                        0.5f);
                instances.push_back({
                    .position =
                        glm::vec4(points[index], 1.0f),
                    .color = glm::vec4(
                        instancePointsSrgbToLinear(srgb.r),
                        instancePointsSrgbToLinear(srgb.g),
                        instancePointsSrgbToLinear(srgb.b),
                        1.0f)});
            }
        }

        /** Creates parent directories for one requested capture artifact. */
        void prepareInstancePointsOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(
                    path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeInstancePointsText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareInstancePointsOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU instance points artifact.");
            }
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendInstancePointsPayload(
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
    }

    void WebgpuInstancePointsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateInstancePointsScenario(options);
        device = inDevice;
        const bool settings =
            options.scenarioId == "width-settings";
        minimumWidth = settings ? 2.0f : 6.0f;
        maximumWidth = settings ? 9.0f : 20.0f;
        pulseSpeed = settings ? 1.8f : 6.0f;
        buildInstancePointsQuad(vertices, indices);
        buildInstancePointsData(instances);

        const glm::vec3 cameraPosition =
            makeInstancePointsCamera(settings);
        const glm::mat4 view = glm::lookAtRH(
            cameraPosition,
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const WebgpuInstancePointsHostObjectData objectData = {
            .mainViewProjection =
                makeInstancePointsProjection(800.0f / 500.0f) *
                view,
            .insetViewProjection =
                makeInstancePointsProjection(1.0f) *
                view,
            .viewportAndTime = glm::vec4(
                800.0f,
                500.0f,
                float(options.targetFrame) / 60.0f,
                0.0f),
            .widthsAndPulse = glm::vec4(
                minimumWidth,
                maximumWidth,
                pulseSpeed,
                settings ? 1.0f : 0.0f)};
        const WebgpuInstancePointsHostMaterialData materialData = {
            .opacityAndCoverage =
                glm::vec4(1.0f, 1.0f, 0.0f, 0.0f)};

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create WebGPU instance points RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(indices.size());
        allocation.instanceCount = PointCount;
        appendInstancePointsPayload(
            allocation,
            WebgpuInstancePointsSceneRenderSetComponents::vertices,
            "WebgpuInstancePointsVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]),
            1u);
        appendInstancePointsPayload(
            allocation,
            WebgpuInstancePointsSceneRenderSetComponents::indices,
            "WebgpuInstancePointsIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]),
            1u);
        appendInstancePointsPayload(
            allocation,
            WebgpuInstancePointsSceneRenderSetComponents::objects,
            "WebgpuInstancePointsObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendInstancePointsPayload(
            allocation,
            WebgpuInstancePointsSceneRenderSetComponents::instances,
            "WebgpuInstancePointsInstances",
            instances.data(),
            instances.size() * sizeof(instances[0u]),
            PointCount);
        appendInstancePointsPayload(
            allocation,
            WebgpuInstancePointsSceneRenderSetComponents::materials,
            "WebgpuInstancePointsMaterial",
            &materialData,
            sizeof(materialData),
            1u);
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebgpuInstancePointsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuInstancePointsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
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
            prepareInstancePointsOutput(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU instance points RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"source\": \"gvm-three-r185\",\n"
            << "  \"caseId\": \"webgpu_instance_points\",\n"
            << "  \"scenarioId\": \""
            << options.scenarioId.c_str()
            << "\",\n  \"pipeline\": \""
            << options.pipeline.c_str()
            << "\",\n  \"backend\": \""
            << threeSampleBackendName(options.backend)
            << "\",\n  \"frame\": "
            << frameIndex
            << ",\n  \"randomSeed\": "
            << options.randomSeed
            << ",\n  \"width\": "
            << width
            << ",\n  \"height\": "
            << height
            << ",\n  \"rowStrideBytes\": "
            << uint64_t(width) * 4u
            << ",\n  \"byteCount\": "
            << byteCount
            << ",\n  \"format\": \"rgba8unorm\",\n  \"inputReplay\": ";
        if (options.scenarioId == "width-settings")
        {
            metadata
                << "{\"sha256\":\"d7d7ec06c35aa5e5cff28244dfab3a87a4cd89afc30dfff1baaadbf1f7a91b1d\","
                << "\"caseId\":\"webgpu_instance_points\",\"scenarioId\":\"width-settings\","
                << "\"captureFrame\":61,\"eventCount\":3,\"target\":\"canvas:not([class])\"}";
        }
        else
        {
            metadata << "null";
        }
        metadata << "\n}\n";
        writeInstancePointsText(
            options.captureMetadataPath,
            metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"caseId\": \"webgpu_instance_points\",\n"
            << "  \"scenarioId\": \""
            << options.scenarioId.c_str()
            << "\",\n  \"frame\": "
            << frameIndex
            << ",\n  \"gpuWorkDslOnly\": true,\n"
            << "  \"renderSetPolicy\": \"required\",\n"
            << "  \"sceneRenderSetCount\": 1,\n"
            << "  \"renderSetType\": "
            << "\"WebgpuInstancePointsSceneRenderSet\",\n"
            << "  \"renderableObjectCount\": 1,\n"
            << "  \"entityCount\": 1,\n"
            << "  \"instanceCounts\": [256],\n"
            << "  \"drawCommandCount\": 2,\n"
            << "  \"computeDispatchCount\": 1,\n"
            << "  \"scenePassCount\": 2,\n"
            << "  \"screenPassCount\": 1,\n"
            << "  \"minimumWidth\": "
            << minimumWidth
            << ",\n  \"maximumWidth\": "
            << maximumWidth
            << ",\n  \"pulseSpeed\": "
            << pulseSpeed
            << ",\n  \"sceneRoots\": [{\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"webgpu-instance-points-scene-set\","
            << "\"renderSetType\":\"WebgpuInstancePointsSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"hilbert-point-cloud\",\"instanceCount\":256}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":2,\"directDrawFallback\":false,"
            << "\"scenePasses\":["
            << "{\"name\":\"main-points\",\"renderClass\":\"WebgpuInstancePointsMainPass\","
            << "\"renderSetId\":\"webgpu-instance-points-scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"inset-points\",\"renderClass\":\"WebgpuInstancePointsInsetPass\","
            << "\"renderSetId\":\"webgpu-instance-points-scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n";
        writeInstancePointsText(
            options.sceneSnapshotPath,
            snapshot.str());
        captureWritten = true;
    }

    void WebgpuInstancePointsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        instances.clear();
    }
}
