#include "WebgpuTexturegatherRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle WebgpuSourceSetHandle =
            ExportedRenderSet::webgpuSourceSet;
        constexpr GVM::Core::RenderSetHandle WebglSourceSetHandle =
            ExportedRenderSet::webglSourceSet;

        /** Appends one typed payload to a source RenderSet allocation. */
        void appendTexturegatherPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
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

        /** Creates parent directories for one texture-gather evidence file. */
        void prepareTexturegatherPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional UTF-8 texture-gather evidence artifact. */
        void writeTexturegatherEvidence(
            const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareTexturegatherPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write texture-gather evidence.");
            }
        }
    } // namespace

    void WebgpuTexturegatherRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgpu_texturegather" ||
            options.scenarioId != "initial-comparison" ||
            options.targetFrame != 0u || options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgpu_texturegather requires its frozen frame-zero scenario.");
        }
        device = inDevice;
        constexpr glm::vec3 FaceNormals[] = {
            {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
        };
        constexpr glm::vec3 FaceCorners[6][4] = {
            {{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f}},
            {{-0.5f,-0.5f,0.5f},{-0.5f,0.5f,0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f,0.5f,-0.5f}},
            {{-0.5f,0.5f,-0.5f},{-0.5f,0.5f,0.5f},{0.5f,0.5f,-0.5f},{0.5f,0.5f,0.5f}},
            {{-0.5f,-0.5f,0.5f},{-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,0.5f},{0.5f,-0.5f,-0.5f}},
            {{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,-0.5f,0.5f},{-0.5f,0.5f,0.5f}},
            {{-0.5f,-0.5f,-0.5f},{-0.5f,0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f}},
        };
        vertices.reserve(24u);
        indices.reserve(36u);
        for (uint32_t face = 0u; face < 6u; ++face)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            for (uint32_t corner = 0u; corner < 4u; ++corner)
            {
                vertices.push_back({glm::vec4(FaceCorners[face][corner], 1.0f),
                                    glm::vec4(FaceNormals[face], 0.0f)});
            }
            indices.insert(indices.end(), {
                baseVertex, baseVertex + 2u, baseVertex + 1u,
                baseVertex + 2u, baseVertex + 3u, baseVertex + 1u,
            });
        }

        const glm::mat4 model =
            glm::rotate(glm::rotate(glm::mat4(1.0f),
                                    glm::radians(45.0f), glm::vec3(1, 0, 0)),
                        glm::radians(45.0f), glm::vec3(0, 1, 0));
        const glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspectiveRH_ZO(
            glm::radians(50.0f), 1.0f,
            2.5f - 0.5f * std::sqrt(3.0f),
            2.5f + 0.5f * std::sqrt(3.0f));
        projection[1u][1u] *= -1.0f;
        objectData.model = model;
        objectData.modelViewProjection = projection * view * model;
        instanceData.reserved = glm::vec4(0.0f);
        materialData.baseColorAndAmbient = glm::vec4(1.0f, 0.0f, 0.0f, 0.1f);
        allocateSourceBox(renderer, WebgpuSourceSetHandle, "WebgpuSource");
        allocateSourceBox(renderer, WebglSourceSetHandle, "WebglSource");
    }

    void WebgpuTexturegatherRuntimeAdapter::allocateSourceBox(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::RenderSetHandle renderSetHandle,
        const char *label)
    {
        const auto encoder = renderer.createRenderSetCommandEncoder(renderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create a texture-gather source Set encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        const eastl::string prefix(label);
        appendTexturegatherPayload(allocation,
            WebgpuTexturegatherSourceRenderSetComponents::vertices,
            prefix + "Vertices", vertices.data(),
            vertices.size() * sizeof(WebgpuTexturegatherHostVertex), 1u);
        appendTexturegatherPayload(allocation,
            WebgpuTexturegatherSourceRenderSetComponents::indices,
            prefix + "Indices", indices.data(),
            indices.size() * sizeof(uint32_t), 1u);
        appendTexturegatherPayload(allocation,
            WebgpuTexturegatherSourceRenderSetComponents::objects,
            prefix + "Object", &objectData, sizeof(objectData), 1u);
        appendTexturegatherPayload(allocation,
            WebgpuTexturegatherSourceRenderSetComponents::instances,
            prefix + "Instance", &instanceData, sizeof(instanceData), 1u);
        appendTexturegatherPayload(allocation,
            WebgpuTexturegatherSourceRenderSetComponents::materials,
            prefix + "Material", &materialData, sizeof(materialData), 1u);
        (void)encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(renderSetHandle, encoder);
    }

    void WebgpuTexturegatherRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuTexturegatherRuntimeAdapter::afterFrame(
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
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("Texture-gather capture is too large.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareTexturegatherPath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write texture-gather RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgpu_texturegather\",\n"
                 << "  \"scenarioId\":\"initial-comparison\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":0,\n  \"width\":800,\n  \"height\":500,\n"
                 << "  \"byteCount\":1600000,\n  \"format\":\"rgba8unorm\",\n"
                 << "  \"sampleCount\":1,\n  \"msaaEnabled\":false\n}\n";
        writeTexturegatherEvidence(options.captureMetadataPath, metadata.str());
        const std::string scene =
            "{\n  \"schemaVersion\":1,\n"
            "  \"caseId\":\"webgpu_texturegather\",\n"
            "  \"scenarioId\":\"initial-comparison\",\n"
            "  \"gpuWorkDslOnly\":true,\n"
            "  \"logicalSceneRootCount\":4,\n"
            "  \"sourceSceneRenderSetCount\":2,\n"
            "  \"renderSetType\":\"WebgpuTexturegatherSourceRenderSet\",\n"
            "  \"sourceEntityCounts\":[1,1],\n"
            "  \"sourceInstanceCounts\":[1,1],\n"
            "  \"scenePassCount\":4,\n"
            "  \"explicitColorMipPassCount\":12,\n"
            "  \"gpuPassCount\":16,\n"
            "  \"usesColorGather\":true,\n"
            "  \"usesCompareGather\":true,\n"
            "  \"compareGatherLowering\":\"component-gather-then-compare\",\n"
            "  \"gatherOffset\":[0,7],\n"
            "  \"sampleCount\":1,\n  \"msaaEnabled\":false\n}\n";
        writeTexturegatherEvidence(options.sceneSnapshotPath, scene);
        const std::string semantic =
            "{\n  \"schemaVersion\":1,\n"
            "  \"sourceSize\":[100,100],\n"
            "  \"sourceMipCount\":7,\n"
            "  \"panelSize\":[400,500],\n"
            "  \"colorGatherChannel\":0,\n"
            "  \"depthCompareReference\":1.0\n}\n";
        writeTexturegatherEvidence(options.semanticSnapshotPath, semantic);
        captureWritten = true;
    }

    void WebgpuTexturegatherRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
    }
} // namespace GVM::ThreeSamples
