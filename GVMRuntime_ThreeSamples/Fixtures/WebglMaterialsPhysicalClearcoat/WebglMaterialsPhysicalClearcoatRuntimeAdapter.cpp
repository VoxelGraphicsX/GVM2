#include "WebglMaterialsPhysicalClearcoatRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t EntityCount = 4u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;

        /** Creates parent directories for one capture artifact. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Builds the locked SphereGeometry(.8,64,32) stream. */
        void buildSphere(WebglMaterialsPhysicalClearcoatEntityData &entity)
        {
            constexpr uint32_t Width = 64u;
            constexpr uint32_t Height = 32u;
            constexpr float Radius = 0.8f;
            entity.vertices.clear();
            entity.indices.clear();
            entity.vertices.reserve((Width + 1u) * (Height + 1u));
            entity.indices.reserve(Width * Height * 6u);
            for (uint32_t y = 0u; y <= Height; ++y)
            {
                const float v = float(y) / float(Height);
                const float theta = v * Pi;
                const float sine = std::sin(theta);
                for (uint32_t x = 0u; x <= Width; ++x)
                {
                    const float u = float(x) / float(Width);
                    const float phi = u * Pi * 2.0f;
                    const glm::vec3 normal(sine * std::cos(phi), std::cos(theta), sine * std::sin(phi));
                    entity.vertices.push_back({glm::vec4(normal * Radius, 1.0f), glm::vec4(normal, 0.0f)});
                }
            }
            for (uint32_t y = 0u; y < Height; ++y)
            {
                for (uint32_t x = 0u; x < Width; ++x)
                {
                    const uint32_t a = y * (Width + 1u) + x + 1u;
                    const uint32_t b = y * (Width + 1u) + x;
                    const uint32_t c = (y + 1u) * (Width + 1u) + x;
                    const uint32_t d = (y + 1u) * (Width + 1u) + x + 1u;
                    if (y != 0u) entity.indices.insert(entity.indices.end(), {a, b, d});
                    if (y != Height - 1u) entity.indices.insert(entity.indices.end(), {b, c, d});
                }
            }
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendPayload(GVM::Core::RenderSetAllocInfo &allocation,
                           GVM::Core::RenderComponentHandle component,
                           const eastl::string &name,
                           const void *value,
                           uint64_t bytes)
        {
            allocation.bufferInfos.push_back({.bufferComponentHandle = component,
                                              .bufferName = name,
                                              .value = value,
                                              .dataStorageSize = bytes,
                                              .instanceCount = 1u});
        }

        /** Validates the locked clearcoat scenarios. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 120u;
            const bool gui = options.scenarioId == "clearcoat-gui" && options.targetFrame == 121u;
            if (options.caseId != "webgl_materials_physical_clearcoat" || (!initial && !animated && !gui) ||
                options.width != 800u || options.height != 500u || options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("webgl_materials_physical_clearcoat requires the locked r185 scenario contract.");
        }
    } // namespace

    void WebglMaterialsPhysicalClearcoatRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(EntityCount);
        const glm::vec4 positions[EntityCount] = {
            {-0.25f, 0.25f, 0.20f, 0.20f}, {0.25f, 0.25f, 0.20f, 0.20f},
            {-0.25f, -0.25f, 0.20f, 0.20f}, {0.25f, -0.25f, 0.20f, 0.20f}};
        const glm::vec4 colors[EntityCount] = {
            {0.02f, 0.02f, 1.0f, 1.0f}, {0.8f, 0.8f, 0.8f, 1.0f},
            {0.55f, 0.55f, 0.55f, 1.0f}, {1.0f, 0.02f, 0.02f, 1.0f}};
        const glm::vec4 physical[EntityCount] = {
            {0.5f, 0.9f, 1.0f, 0.1f}, {0.5f, 0.0f, 1.0f, 0.1f},
            {0.1f, 0.0f, 1.0f, 0.1f}, {0.5f, 1.0f, 1.0f, 0.1f}};
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("clearcoat Scene RenderSet encoder unavailable.");
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            auto &entity = entities[index];
            buildSphere(entity);
            entity.objectData.offsetAndScale = positions[index];
            entity.objectData.materialAndFlags = glm::uvec4(index, 0u, 0u, 0u);
            entity.baseObjectData = entity.objectData;
            entity.instanceData.offsetAndScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            entity.instanceData.tint = glm::vec4(1.0f);
            entity.materialData.baseColor = colors[index];
            entity.materialData.physicalParameters = physical[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("ClearcoatSphere-") + eastl::to_string(index);
            appendPayload(allocation, WebglMaterialsPhysicalClearcoatSceneRenderSetComponents::vertices,
                          prefix + "-vertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0u]));
            appendPayload(allocation, WebglMaterialsPhysicalClearcoatSceneRenderSetComponents::indices,
                          prefix + "-indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t));
            appendPayload(allocation, WebglMaterialsPhysicalClearcoatSceneRenderSetComponents::objects,
                          prefix + "-object", &entity.objectData, sizeof(entity.objectData));
            appendPayload(allocation, WebglMaterialsPhysicalClearcoatSceneRenderSetComponents::instances,
                          prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData));
            appendPayload(allocation, WebglMaterialsPhysicalClearcoatSceneRenderSetComponents::materials,
                          prefix + "-material", &entity.materialData, sizeof(entity.materialData));
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsPhysicalClearcoatRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        const float time = float(frameIndex) / 60.0f;
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            auto &entity = entities[index];
            entity.objectData = entity.baseObjectData;
            entity.objectData.offsetAndScale.z = entity.baseObjectData.offsetAndScale.z *
                (0.96f + 0.04f * std::cos(time + float(index)));
        }
    }

    void WebglMaterialsPhysicalClearcoatRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("clearcoat update encoder unavailable.");
        for (const auto &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                                             WebglMaterialsPhysicalClearcoatSceneRenderSetComponents::objects,
                                             &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsPhysicalClearcoatRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglMaterialsPhysicalClearcoatRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options, uint32_t frame, uint32_t width,
        uint32_t height, uint64_t bytes) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_materials_physical_clearcoat\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend)
               << "\",\"frame\":" << frame << ",\"width\":" << width << ",\"height\":" << height
               << ",\"byteCount\":" << bytes << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false}\n";
    }

    void WebglMaterialsPhysicalClearcoatRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frame) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_materials_physical_clearcoat\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frame
               << ",\n  \"implementationLevel\":\"scaffolded\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":false,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglMaterialsPhysicalClearcoatSceneRenderSet\",\n  \"renderableObjectCount\":4,\n"
               << "  \"entityCount\":4,\n  \"instanceCount\":1,\n  \"instanceCounts\":[1,1,1,1],\n"
               << "  \"scenePassCount\":1,\n  \"screenPassCount\":0,\n  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n"
               << "  \"msaaEnabled\":false,\n  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
               << "  \"scenePasses\":[{\"name\":\"main-clearcoat\",\"renderClass\":\"WebglMaterialsPhysicalClearcoatScenePass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n}\n";
    }

    void WebglMaterialsPhysicalClearcoatRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &options,
        uint32_t frame, GVM::RHI::Texture texture, uint32_t width, uint32_t height)
    {
        if (captureWritten || frame != options.targetFrame) return;
        const uint64_t bytes = uint64_t(width) * uint64_t(height) * 4u;
        if (bytes > std::numeric_limits<size_t>::max()) throw std::overflow_error("clearcoat capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(bytes));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("clearcoat has no graphics queue.");
        queue->readTexture(texture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frame, width, height, bytes);
        writeStructuralSnapshot(options, frame);
        captureWritten = true;
    }

    void WebglMaterialsPhysicalClearcoatRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &)
    {
        entities.clear();
    }
} // namespace GVM::ThreeSamples
