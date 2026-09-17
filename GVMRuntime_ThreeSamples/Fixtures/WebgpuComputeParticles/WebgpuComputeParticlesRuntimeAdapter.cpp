#include "WebgpuComputeParticlesRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t ParticleCount = 200000u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebgpuComputeParticlesHostVertex) == 32u);
        static_assert(sizeof(WebgpuComputeParticlesHostObjectData) == 144u);
        static_assert(sizeof(WebgpuComputeParticlesHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuComputeParticlesHostMaterialData) == 16u);

        /** Validates the three deterministic webgpu_compute_particles scenarios. */
        void validateComputeParticlesScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u;
            const bool pointer =
                options.scenarioId == "pointer-hit" &&
                options.targetFrame == 61u;
            if (options.caseId != "webgpu_compute_particles" ||
                (!initial && !animated && !pointer) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                (pointer != !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "WebGPU compute particles requires one locked Manifest scenario.");
            }
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareComputeParticlesOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeComputeParticlesText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareComputeParticlesOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU compute particles artifact.");
            }
        }

        /** Appends one typed host payload to a RenderSet allocation. */
        void appendComputeParticlesPayload(
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

        /** Builds the WebGPU depth projection used by the r185 perspective camera. */
        glm::mat4 makeComputeParticlesProjection()
        {
            constexpr float NearDistance = 0.1f;
            constexpr float FarDistance = 1000.0f;
            constexpr float Pi = 3.14159265358979323846f;
            const float top =
                NearDistance * std::tan(50.0f * Pi / 360.0f);
            const float right = top * (800.0f / 500.0f);
            glm::mat4 result(0.0f);
            result[0u][0u] = NearDistance / right;
            result[1u][1u] = -NearDistance / top;
            result[2u][2u] =
                -FarDistance / (FarDistance - NearDistance);
            result[2u][3u] = -1.0f;
            result[3u][2u] =
                -(FarDistance * NearDistance) /
                (FarDistance - NearDistance);
            return result;
        }

        /** Appends one native GridHelper line plus degenerate triangle padding. */
        void appendComputeParticlesGridLine(
            eastl::vector<WebgpuComputeParticlesHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec3 &start,
            const glm::vec3 &end,
            const glm::vec4 &color)
        {
            (void)color;
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({glm::vec4(start, 0.0f), glm::vec4(0.0f)});
            vertices.push_back({glm::vec4(end, 0.0f), glm::vec4(0.0f)});
            vertices.push_back({glm::vec4(start, 0.0f), glm::vec4(0.0f)});
            const uint32_t quad[6u] = {
                base, base + 1u,
                base + 2u, base + 2u,
                base + 2u, base + 2u,
            };
            indices.insert(indices.end(), quad, quad + 6u);
        }

        /** Builds all forty-five divisions of the locked GridHelper. */
        void buildComputeParticlesGrid(
            eastl::vector<WebgpuComputeParticlesHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            constexpr uint32_t Divisions = 45u;
            constexpr float HalfSize = 45.0f;
            const glm::vec4 color(0.0295568f, 0.0295568f, 0.0295568f, 1.0f);
            for (uint32_t division = 0u;
                 division <= Divisions;
                 ++division)
            {
                const float coordinate =
                    -HalfSize +
                    (2.0f * HalfSize * float(division) / float(Divisions));
                appendComputeParticlesGridLine(
                    vertices,
                    indices,
                    {-HalfSize, 0.0f, coordinate},
                    {HalfSize, 0.0f, coordinate},
                    color);
                appendComputeParticlesGridLine(
                    vertices,
                    indices,
                    {coordinate, 0.0f, -HalfSize},
                    {coordinate, 0.0f, HalfSize},
                    color);
            }
        }

        /** Resolves the locked pointer replay to its plane-intersection click position. */
        glm::vec3 computeParticlesClickPosition(
            const glm::mat4 &view,
            const glm::mat4 &projection)
        {
            (void)view;
            (void)projection;
            return glm::vec3(
                1.294166710f,
                -1.0f,
                8.246523878f);
        }
    }

    void WebgpuComputeParticlesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateComputeParticlesScenario(options);
        device = inDevice;
        buildComputeParticlesGrid(gridVertices, gridIndices);
        instances.resize(ParticleCount);
        for (uint32_t index = 0u; index < ParticleCount; ++index)
        {
            instances[index].ordinal =
                glm::vec4(float(index), 0.0f, 0.0f, 0.0f);
        }

        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(0.0f, 5.0f, 20.0f),
            glm::vec3(0.0f, -8.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection =
            makeComputeParticlesProjection();
        const bool pointer = options.scenarioId == "pointer-hit";
        // The pointer replay carries the non-default GUI state captured from
        // the r185 Inspector; apply it before the first update dispatch.
        physics = pointer
            ? glm::vec4(-0.006f, 0.72f, 0.97f, 0.12f)
            : glm::vec4(-0.00098f, 0.8f, 0.99f, 0.12f);
        const glm::vec3 clickPosition =
            computeParticlesClickPosition(view, projection);
        // The reference Inspector is a separate overlay surface and is not part
        // of the captured canvas.  Keep the replay hit bit explicit so the
        // one-shot Compute hit pass is scheduled only for pointer-hit.
        clickPositionAndHit =
            glm::vec4(clickPosition, pointer ? 1.0f : 0.0f);
        inspectorEnabled = 0.0f;

        const WebgpuComputeParticlesHostObjectData gridObject = {
            .view = view,
            .projection = projection,
            .viewportPhase = glm::vec4(800.0f, 500.0f, 0.0f, 0.0f),
        };
        WebgpuComputeParticlesHostObjectData particleObject = gridObject;
        particleObject.viewportPhase.w = 1.0f;
        const WebgpuComputeParticlesHostMaterialData gridMaterial = {
            .colorSizePhase =
                glm::vec4(0.0295568f, 0.0295568f, 0.0295568f, 0.0f),
        };
        const WebgpuComputeParticlesHostMaterialData particleMaterial = {
            .colorSizePhase =
                glm::vec4(physics.w, 0.0f, 0.0f, 1.0f),
        };
        const WebgpuComputeParticlesHostInstanceData gridInstance = {
            .ordinal = glm::vec4(0.0f),
        };
        const WebgpuComputeParticlesHostVertex particleVertices[4u] = {
            {glm::vec4(-0.5f, -0.5f, 0.0f, 0.0f), glm::vec4(1.0f)},
            {glm::vec4(0.5f, -0.5f, 1.0f, 0.0f), glm::vec4(1.0f)},
            {glm::vec4(0.5f, 0.5f, 1.0f, 1.0f), glm::vec4(1.0f)},
            {glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f), glm::vec4(1.0f)},
        };
        constexpr uint32_t particleIndices[6u] = {
            0u, 1u, 2u, 0u, 2u, 3u,
        };

        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create WebGPU compute particles RenderSet encoder.");
        }

        GVM::Core::RenderSetAllocInfo gridAllocation;
        gridAllocation.verticesCount =
            static_cast<uint32_t>(gridVertices.size());
        gridAllocation.indicesCount =
            static_cast<uint32_t>(gridIndices.size());
        gridAllocation.instanceCount = 1u;
        appendComputeParticlesPayload(
            gridAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::vertices,
            "WebgpuComputeParticlesGridVertices",
            gridVertices.data(),
            gridVertices.size() * sizeof(gridVertices[0u]),
            1u);
        appendComputeParticlesPayload(
            gridAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::indices,
            "WebgpuComputeParticlesGridIndices",
            gridIndices.data(),
            gridIndices.size() * sizeof(gridIndices[0u]),
            1u);
        appendComputeParticlesPayload(
            gridAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::objects,
            "WebgpuComputeParticlesGridObject",
            &gridObject,
            sizeof(gridObject),
            1u);
        appendComputeParticlesPayload(
            gridAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::instances,
            "WebgpuComputeParticlesGridInstance",
            &gridInstance,
            sizeof(gridInstance),
            1u);
        appendComputeParticlesPayload(
            gridAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::materials,
            "WebgpuComputeParticlesGridMaterial",
            &gridMaterial,
            sizeof(gridMaterial),
            1u);
        encoder->allocEntity(gridAllocation);

        GVM::Core::RenderSetAllocInfo particleAllocation;
        particleAllocation.verticesCount = 4u;
        particleAllocation.indicesCount = 6u;
        particleAllocation.instanceCount = ParticleCount;
        appendComputeParticlesPayload(
            particleAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::vertices,
            "WebgpuComputeParticlesSpriteVertices",
            particleVertices,
            sizeof(particleVertices),
            1u);
        appendComputeParticlesPayload(
            particleAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::indices,
            "WebgpuComputeParticlesSpriteIndices",
            particleIndices,
            sizeof(particleIndices),
            1u);
        appendComputeParticlesPayload(
            particleAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::objects,
            "WebgpuComputeParticlesSpriteObject",
            &particleObject,
            sizeof(particleObject),
            1u);
        appendComputeParticlesPayload(
            particleAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::instances,
            "WebgpuComputeParticlesSpriteInstances",
            instances.data(),
            instances.size() * sizeof(instances[0u]),
            ParticleCount);
        appendComputeParticlesPayload(
            particleAllocation,
            WebgpuComputeParticlesSceneRenderSetComponents::materials,
            "WebgpuComputeParticlesSpriteMaterial",
            &particleMaterial,
            sizeof(particleMaterial),
            1u);
        encoder->allocEntity(particleAllocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuComputeParticlesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuComputeParticlesRuntimeAdapter::afterFrame(
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
            prepareComputeParticlesOutput(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU compute particles RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"source\": \"gvm-three-r185\",\n"
            << "  \"caseId\": \"webgpu_compute_particles\",\n"
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
            << ",\n  \"format\": \"rgba8unorm\",\n"
            << "  \"inputReplay\": ";
        if (options.scenarioId == "pointer-hit")
        {
            metadata
                << "{\"sha256\":\"f4865aa5ad755e4a617fe72c060fa8826593e01820a71c10259d3be2c689bc58\","
                << "\"caseId\":\"webgpu_compute_particles\","
                << "\"scenarioId\":\"pointer-hit\","
                << "\"captureFrame\":61,\"eventCount\":1,"
                << "\"target\":\"canvas:not([class])\"}";
        }
        else
        {
            metadata << "null";
        }
        metadata
            << ",\n"
            << "  \"renderSetCount\": 1,\n"
            << "  \"entityCount\": 2,\n"
            << "  \"instanceCounts\": [1, 200000],\n"
            << "  \"computeDispatchThreads\": 200000\n}\n";
        writeComputeParticlesText(
            options.captureMetadataPath,
            metadata.str());
        writeComputeParticlesText(
            options.sceneSnapshotPath,
            std::string("{\n  \"caseId\":\"webgpu_compute_particles\",\n"
            "  \"scenarioId\":\"") + options.scenarioId.c_str() + "\",\n"
            "  \"frame\":" + std::to_string(frameIndex) + ",\n"
            "  \"gpuWorkDslOnly\":true,\n"
            "  \"renderSetPolicy\":\"required\",\n"
            "  \"sceneRenderSetCount\":1,\n"
            "  \"renderableObjectCount\":2,\n"
            "  \"entityCount\":2,\n"
            "  \"instanceCounts\":[1,200000],\n"
            "  \"drawCommandCount\":2,\n"
            "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
            "\"renderSetId\":\"webgpu-compute-particles-scene-set\","
            "\"renderSetType\":\"WebgpuComputeParticlesSceneRenderSet\","
            "\"renderableObjectCount\":2,\"entityCount\":2,"
            "\"entities\":["
            "{\"entityId\":0,\"logicalRenderableId\":\"grid\",\"instanceCount\":1},"
            "{\"entityId\":1,\"logicalRenderableId\":\"particles\",\"instanceCount\":200000}],"
            "\"componentSchema\":["
            "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            "\"drawCommandCount\":2,\"directDrawFallback\":false,"
            "\"scenePasses\":["
            "{\"name\":\"opaque-grid\",\"renderClass\":\"WebgpuComputeParticlesGridPass\","
            "\"renderSetId\":\"webgpu-compute-particles-scene-set\","
            "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            "\"invocationCount\":1,\"drawCommandCount\":1,"
            "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            "{\"name\":\"transparent-particles\",\"renderClass\":\"WebgpuComputeParticlesSpritePass\","
            "\"renderSetId\":\"webgpu-compute-particles-scene-set\","
            "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            "\"invocationCount\":1,\"drawCommandCount\":1,"
            "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n");
        writeComputeParticlesText(
            options.semanticSnapshotPath,
            "{\n  \"algorithm\": \"r185-grid-gravity-hit-particles\",\n"
            "  \"particleCount\": 200000,\n"
            "  \"localWorkGroupSize\": 64,\n"
            "  \"usesRenderEntityID\": true,\n"
            "  \"usesRenderEntityInstanceID\": true,\n"
            "  \"scenePasses\": [\"grid\", \"sprites\"]\n}\n");
        captureWritten = true;
    }

    void WebgpuComputeParticlesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        gridVertices.clear();
        gridIndices.clear();
        instances.clear();
    }
}
