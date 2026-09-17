#include "BasicRenderSetRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/array.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

#ifndef THREE_BASIC_CASE_ID
#error "THREE_BASIC_CASE_ID must identify the dedicated Three example."
#endif
#ifndef THREE_BASIC_RENDERABLE_COUNT
#error "THREE_BASIC_RENDERABLE_COUNT must describe the manifest scene."
#endif
#ifndef THREE_BASIC_ENTITY_COUNT
#error "THREE_BASIC_ENTITY_COUNT must describe the RenderSet entity count."
#endif
#ifndef THREE_BASIC_INSTANCE_COUNT
#error "THREE_BASIC_INSTANCE_COUNT must describe the largest instance payload."
#endif
#ifndef THREE_BASIC_SCENE_PASS_COUNT
#error "THREE_BASIC_SCENE_PASS_COUNT must describe the scene pass contract."
#endif
#ifndef THREE_BASIC_SCREEN_PASS_COUNT
#error "THREE_BASIC_SCREEN_PASS_COUNT must describe the screen pass contract."
#endif
#ifndef THREE_BASIC_HAS_INSTANCING
#define THREE_BASIC_HAS_INSTANCING (THREE_BASIC_INSTANCE_COUNT > 1u)
#endif

namespace GVM::ThreeSamples
{
    namespace
    {
        struct alignas(16) BasicFloat4 { float x; float y; float z; float w; };
        struct alignas(16) BasicUint4 { uint32_t x; uint32_t y; uint32_t z; uint32_t w; };
        struct alignas(16) BasicVertex { BasicFloat4 position; BasicFloat4 color; };
        struct alignas(16) BasicObject { BasicFloat4 offsetAndScale; BasicUint4 materialAndFlags; };
        struct alignas(16) BasicInstance { BasicFloat4 offsetAndScale; BasicFloat4 tint; };
        struct alignas(16) BasicMaterial { BasicFloat4 baseColor; };

        static_assert(sizeof(BasicFloat4) == 16u);
        static_assert(sizeof(BasicUint4) == 16u);
        static_assert(sizeof(BasicVertex) == 32u);
        static_assert(sizeof(BasicObject) == 32u);
        static_assert(sizeof(BasicInstance) == 32u);
        static_assert(sizeof(BasicMaterial) == 16u);

        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        uint64_t rgbaBytes(uint32_t width, uint32_t height)
        {
            const uint64_t pixels = uint64_t(width) * uint64_t(height);
            if (pixels > std::numeric_limits<uint64_t>::max() / 4u)
                throw std::overflow_error("basic RenderSet RGBA8 capture size overflowed.");
            return pixels * 4u;
        }

        template <class Element>
        void appendBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const Element *payload,
            size_t elementCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = payload,
                .dataStorageSize = sizeof(Element) * elementCount,
                .instanceCount = instanceCount,
            });
        }

        /** Validates the fixed host contract before any RenderSet allocation. */
        void validateBasicOptions(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != THREE_BASIC_CASE_ID)
                throw std::invalid_argument("basic RenderSet case id does not match its dedicated shard.");
            if (options.scenarioId.empty())
                throw std::invalid_argument("basic RenderSet requires a non-empty scenario id.");
            if (options.width != 800u || options.height != 500u)
                throw std::invalid_argument("basic RenderSet captures must use the frozen 800x500 extent.");
            if (options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("basic RenderSet captures must use the frozen Three random seed.");
            if (options.frameCount == 0u)
                throw std::invalid_argument("basic RenderSet requires at least one frame.");
            if (THREE_BASIC_ENTITY_COUNT == 0u || THREE_BASIC_RENDERABLE_COUNT == 0u)
                throw std::invalid_argument("basic RenderSet scene contract cannot be empty.");
            if (THREE_BASIC_INSTANCE_COUNT == 0u)
                throw std::invalid_argument("basic RenderSet instance count cannot be zero.");
        }

        /** Returns a deterministic triangle color for one structural entity. */
        BasicFloat4 basicEntityColor(uint32_t entityIndex)
        {
            const uint32_t phase = entityIndex % 7u;
            return BasicFloat4{
                0.20f + 0.11f * float(phase),
                0.32f + 0.07f * float((phase + 2u) % 7u),
                0.46f + 0.05f * float((phase + 4u) % 7u),
                1.0f};
        }

        /** Places structural entities on a deterministic bounded grid. */
        BasicFloat4 basicEntityTransform(uint32_t entityIndex, uint32_t entityCount)
        {
            if (entityCount <= 4u)
            {
                const float x = entityCount == 1u
                    ? 0.0f
                    : -0.72f + 1.44f * float(entityIndex) / float(entityCount - 1u);
                return BasicFloat4{x, 0.0f, 0.18f, 0.18f};
            }
            const uint32_t columns = entityCount < 64u ? 8u : 64u;
            const uint32_t row = entityIndex / columns;
            const uint32_t column = entityIndex % columns;
            const float x = -0.96f + 1.92f * float(column) / float(columns - 1u);
            const uint32_t rows = (entityCount + columns - 1u) / columns;
            const float y = rows <= 1u
                ? 0.0f
                : 0.92f - 1.84f * float(row) / float(rows - 1u);
            const float scale = entityCount > 512u ? 0.012f : 0.045f;
            return BasicFloat4{x, y, scale, scale};
        }
    }

    void BasicRenderSetRuntimeAdapter::initialize(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateBasicOptions(options);
        device = inDevice;
        const auto encoder = renderer.createRenderSetCommandEncoder(ExportedRenderSet::sceneSet);
        if (!encoder) throw std::runtime_error("basic RenderSet could not create its command encoder.");

        const eastl::array<BasicVertex, 3u> triangle = {
            BasicVertex{{-0.78f, -0.62f, 0.10f, 1.0f}, {0.96f, 0.24f, 0.12f, 1.0f}},
            BasicVertex{{0.00f, 0.78f, 0.10f, 1.0f}, {0.18f, 0.92f, 0.38f, 1.0f}},
            BasicVertex{{0.78f, -0.62f, 0.10f, 1.0f}, {0.16f, 0.34f, 0.98f, 1.0f}},
        };
        const eastl::array<uint32_t, 3u> indices = {0u, 1u, 2u};
        const uint32_t entityCount = THREE_BASIC_ENTITY_COUNT;
        const uint32_t instanceCount = THREE_BASIC_INSTANCE_COUNT;
        eastl::vector<BasicInstance> instances(instanceCount);
        for (uint32_t instanceIndex = 0u; instanceIndex < instanceCount; ++instanceIndex)
        {
            const uint32_t columns = instanceCount < 16u ? instanceCount : 16u;
            const uint32_t row = instanceIndex / columns;
            const uint32_t column = instanceIndex % columns;
            const float x = instanceCount == 1u
                ? 0.0f
                : -0.90f + 1.80f * float(column) / float(columns - 1u);
            const uint32_t rows = (instanceCount + columns - 1u) / columns;
            const float y = rows <= 1u
                ? 0.0f
                : 0.88f - 1.76f * float(row) / float(rows - 1u);
            const float scale = instanceCount > 16u ? 0.045f : 0.24f;
            instances[instanceIndex] = BasicInstance{
                {x, y, scale, scale},
                basicEntityColor(instanceIndex)};
        }
        const BasicMaterial material = {BasicFloat4{1.0f, 1.0f, 1.0f, 1.0f}};
        for (uint32_t entityIndex = 0u; entityIndex < entityCount; ++entityIndex)
        {
            const BasicFloat4 transform = basicEntityTransform(entityIndex, entityCount);
            const BasicObject object = {
                transform,
                BasicUint4{0u, 0u, 0u, 0u}};
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = 3u;
            allocation.indicesCount = 3u;
            allocation.instanceCount = instanceCount;
            appendBuffer(allocation, THREE_BASIC_COMPONENTS::vertices,
                "BasicVertices", triangle.data(), triangle.size(), 1u);
            appendBuffer(allocation, THREE_BASIC_COMPONENTS::indices,
                "BasicIndices", indices.data(), indices.size(), 1u);
            appendBuffer(allocation, THREE_BASIC_COMPONENTS::objects,
                "BasicObject", &object, 1u, 1u);
            appendBuffer(allocation, THREE_BASIC_COMPONENTS::instances,
                "BasicInstances", instances.data(), instances.size(), instanceCount);
            appendBuffer(allocation, THREE_BASIC_COMPONENTS::materials,
                "BasicMaterial", &material, 1u, 1u);
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(ExportedRenderSet::sceneSet, encoder);
        initialized = true;
        this->entityCount = entityCount;
        this->instanceCount = instanceCount;
        instancedEntityCount = instanceCount > 1u ? entityCount : 0u;
    }

    void BasicRenderSetRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void BasicRenderSetRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (!initialized || captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = rgbaBytes(width, height);
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("basic RenderSet capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("basic RenderSet could not access graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void BasicRenderSetRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void BasicRenderSetRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("basic RenderSet could not open RGBA capture.");
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void BasicRenderSetRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        if (!output) throw std::runtime_error("basic RenderSet could not open metadata capture.");
        output << "{\n"
               << "  \"caseId\": \"" << options.caseId.c_str() << "\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"sampleCount\": 1,\n"
               << "  \"msaaEnabled\": false\n"
               << "}\n";
    }

    void BasicRenderSetRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        if (!output) throw std::runtime_error("basic RenderSet could not open scene snapshot.");
        output << "{\n"
               << "  \"caseId\": \"" << options.caseId.c_str() << "\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderSetType\": \"" << THREE_BASIC_RENDER_SET_NAME << "\",\n"
               << "  \"entityCount\": " << entityCount << ",\n"
               << "  \"renderableObjectCount\": " << THREE_BASIC_RENDERABLE_COUNT << ",\n"
               << "  \"instanceCount\": " << instanceCount << ",\n"
               << "  \"instanceCounts\": [";
        for (uint32_t entityIndex = 0u; entityIndex < entityCount; ++entityIndex)
        {
            if (entityIndex != 0u) output << ',';
            output << instanceCount;
        }
        output << "],\n"
               << "  \"scenePassCount\": " << THREE_BASIC_SCENE_PASS_COUNT << ",\n"
               << "  \"screenPassCount\": " << THREE_BASIC_SCREEN_PASS_COUNT << ",\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"usesRenderEntityID\": true,\n"
               << "  \"usesRenderEntityInstanceID\": "
               << (THREE_BASIC_HAS_INSTANCING ? "true" : "false") << ",\n"
               << "  \"sampleCount\": 1,\n"
               << "  \"msaaEnabled\": false,\n"
               << "  \"declaredRenderableObjectCount\": " << THREE_BASIC_RENDERABLE_COUNT << ",\n"
               << "  \"declaredEntityCount\": " << THREE_BASIC_ENTITY_COUNT << ",\n"
               << "  \"declaredInstanceCount\": " << THREE_BASIC_INSTANCE_COUNT << ",\n"
               << "  \"componentSchema\": [\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
               << "  \"assetAndAlgorithmState\": \"structural-fixture-only\"\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
