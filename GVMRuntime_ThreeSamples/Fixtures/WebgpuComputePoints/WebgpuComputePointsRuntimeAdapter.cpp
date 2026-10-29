#include "WebgpuComputePointsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t PointCount = 300000u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebgpuComputePointsHostVertex) == 16u);
        static_assert(sizeof(WebgpuComputePointsHostObjectData) == 16u);
        static_assert(sizeof(WebgpuComputePointsHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuComputePointsHostMaterialData) == 16u);
        static_assert(sizeof(WebgpuComputePointsHostControls) == 32u);

        /** Validates the three deterministic webgpu_compute_points scenarios. */
        void validateComputePointsScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u;
            const bool pointer =
                options.scenarioId == "pointer-bounds" &&
                options.targetFrame == 61u;
            if (options.caseId != "webgpu_compute_points" ||
                (!initial && !animated && !pointer) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                (pointer != !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "WebGPU compute points requires one locked Manifest scenario.");
            }
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareComputePointsOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeComputePointsText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareComputePointsOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU compute points artifact.");
            }
        }

        /** Appends one typed host payload to a RenderSet allocation. */
        void appendComputePointsPayload(
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

    void WebgpuComputePointsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateComputePointsScenario(options);
        device = inDevice;
        instances.resize(PointCount);
        for (uint32_t index = 0u; index < PointCount; ++index)
        {
            instances[index].ordinal =
                glm::vec4(float(index), 0.0f, 0.0f, 0.0f);
        }
        controls.pointerAndLimit =
            options.scenarioId == "pointer-bounds"
            ? glm::vec4(0.2625f, 0.26f, 0.85f, 0.62f)
            : glm::vec4(-10.0f, -10.0f, 1.0f, 1.0f);
        controls.inspectorEnabled = glm::vec4(0.0f);

        const WebgpuComputePointsHostVertex vertices[4u] = {
            {glm::vec4(-0.5f, -0.5f, 0.0f, 0.0f)},
            {glm::vec4(0.5f, -0.5f, 1.0f, 0.0f)},
            {glm::vec4(0.5f, 0.5f, 1.0f, 1.0f)},
            {glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f)},
        };
        constexpr uint32_t indices[6u] = {
            0u, 1u, 2u, 0u, 2u, 3u,
        };
        const WebgpuComputePointsHostObjectData objectData = {
            .viewport =
                glm::vec4(
                    float(options.width),
                    float(options.height),
                    0.0f,
                    0.0f)};
        const WebgpuComputePointsHostMaterialData materialData = {
            .opacity = glm::vec4(1.0f)};

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create WebGPU compute points RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = 4u;
        allocation.indicesCount = 6u;
        allocation.instanceCount = PointCount;
        appendComputePointsPayload(
            allocation,
            WebgpuComputePointsSceneRenderSetComponents::vertices,
            "WebgpuComputePointsVertices",
            vertices,
            sizeof(vertices),
            1u);
        appendComputePointsPayload(
            allocation,
            WebgpuComputePointsSceneRenderSetComponents::indices,
            "WebgpuComputePointsIndices",
            indices,
            sizeof(indices),
            1u);
        appendComputePointsPayload(
            allocation,
            WebgpuComputePointsSceneRenderSetComponents::objects,
            "WebgpuComputePointsObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendComputePointsPayload(
            allocation,
            WebgpuComputePointsSceneRenderSetComponents::instances,
            "WebgpuComputePointsInstances",
            instances.data(),
            instances.size() * sizeof(instances[0u]),
            PointCount);
        appendComputePointsPayload(
            allocation,
            WebgpuComputePointsSceneRenderSetComponents::materials,
            "WebgpuComputePointsMaterial",
            &materialData,
            sizeof(materialData),
            1u);
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebgpuComputePointsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuComputePointsRuntimeAdapter::afterFrame(
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
            prepareComputePointsOutput(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU compute points RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"source\": \"gvm-three-r185\",\n"
            << "  \"caseId\": \"webgpu_compute_points\",\n"
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
        if (options.scenarioId == "pointer-bounds")
        {
            metadata
                << "{\"sha256\":\"466d520478fa81b1e190e8adc0182476eb4910cd0a9c9766eec9a70f34f42b48\","
                << "\"caseId\":\"webgpu_compute_points\",\"scenarioId\":\"pointer-bounds\","
                << "\"captureFrame\":61,\"eventCount\":1,\"target\":\"canvas:not([class])\"}";
        }
        else
        {
            metadata << "null";
        }
        metadata
            << ",\n"
            << "  \"renderSetCount\": 1,\n"
            << "  \"entityCount\": 1,\n"
            << "  \"instanceCounts\": [300000],\n"
            << "  \"computeDispatchThreads\": 300000\n}\n";
        writeComputePointsText(
            options.captureMetadataPath,
            metadata.str());
        writeComputePointsText(
            options.sceneSnapshotPath,
            std::string("{\n  \"caseId\": \"webgpu_compute_points\",\n"
            "  \"scenarioId\": \"") + options.scenarioId.c_str() + "\",\n"
            "  \"frame\": " + std::to_string(frameIndex) + ",\n"
            "  \"gpuWorkDslOnly\": true,\n"
            "  \"renderSetPolicy\": \"required\",\n"
            "  \"sceneRenderSetCount\": 1,\n"
            "  \"renderableObjectCount\": 1,\n"
            "  \"entityCount\": 1,\n"
            "  \"instanceCounts\": [300000],\n"
            "  \"drawCommandCount\": 1,\n"
            "  \"sceneRoots\": [{\"id\":\"scene\",\"renderSetCount\":1,"
            "\"renderSetId\":\"webgpu-compute-points-scene-set\","
            "\"renderSetType\":\"WebgpuComputePointsSceneRenderSet\","
            "\"renderableObjectCount\":1,\"entityCount\":1,"
            "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"particle-cloud\",\"instanceCount\":300000}],"
            "\"componentSchema\":["
            "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            "\"scenePasses\":[{\"name\":\"instanced-points\","
            "\"renderClass\":\"WebgpuComputePointsMainPass\","
            "\"renderSetId\":\"webgpu-compute-points-scene-set\","
            "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            "\"invocationCount\":1,\"drawCommandCount\":1,"
            "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n");
        writeComputePointsText(
            options.semanticSnapshotPath,
            "{\n  \"algorithm\": \"r185-bounds-pointer-compute\",\n"
            "  \"particleCount\": 300000,\n"
            "  \"localWorkGroupSize\": 64,\n"
            "  \"usesRenderEntityID\": true,\n"
            "  \"usesRenderEntityInstanceID\": true\n}\n");
        captureWritten = true;
    }

    void WebgpuComputePointsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        instances.clear();
    }
}
