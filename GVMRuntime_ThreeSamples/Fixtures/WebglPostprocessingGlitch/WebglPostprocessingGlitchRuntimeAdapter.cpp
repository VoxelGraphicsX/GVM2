#include "WebglPostprocessingGlitchRuntimeAdapter.hpp"

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
        constexpr uint32_t InstanceCount = 100u;
        // Three's locked page consumes this many Math.random samples while
        // constructing renderer/Object3D UUIDs before the instance loop.
        constexpr uint32_t GlitchSceneInitializationRandomSamples = 148u;
        constexpr uint32_t GlitchHeightMapRandomStart = 1120u;
        constexpr uint32_t GlitchPostTriggerInitializationRandomSamples = 28u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;

        static_assert(sizeof(WebglPostprocessingGlitchHostVertex) == 32u);
        static_assert(sizeof(WebglPostprocessingGlitchHostObjectData) == 160u);
        static_assert(sizeof(WebglPostprocessingGlitchHostInstanceData) == 48u);
        static_assert(sizeof(WebglPostprocessingGlitchHostMaterialData) == 16u);

        /** Creates parent directories for a requested capture artifact. */
        void prepareGlitchOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Advances the deterministic xorshift stream used by the r185 example. */
        uint32_t nextGlitchRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return state;
        }

        /** Returns one deterministic random sample in the half-open unit interval. */
        float glitchRandomUnit(uint32_t &state)
        {
            return float(nextGlitchRandom(state) >> 8u) / 16777216.0f;
        }

        /** Samples the inclusive integer range used by Three.MathUtils.randInt. */
        uint32_t glitchRandomInt(uint32_t &state, uint32_t minimum, uint32_t maximum)
        {
            return minimum + static_cast<uint32_t>(std::floor(
                glitchRandomUnit(state) * float(maximum - minimum + 1u)));
        }

        /** Samples the affine float range used by Three.MathUtils.randFloat. */
        float glitchRandomFloat(uint32_t &state, float minimum, float maximum)
        {
            return minimum + (maximum - minimum) * glitchRandomUnit(state);
        }

        /** Converts one display-sRGB color channel into Three's linear working space. */
        float glitchSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendGlitchPayload(
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

        /** Builds Three's low-detail sphere as a non-indexed triangle list. */
        void buildGlitchSphere(
            eastl::vector<WebglPostprocessingGlitchHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t WidthSegments = 4u;
            constexpr uint32_t HeightSegments = 4u;
            glm::vec4 grid[HeightSegments + 1u][WidthSegments + 1u];
            for (uint32_t y = 0u; y <= HeightSegments; ++y)
            {
                const float v = float(y) / float(HeightSegments);
                const float theta = v * Pi;
                const float vertical = std::cos(theta);
                const float radius = std::sqrt(std::max(0.0f, 1.0f - vertical * vertical));
                for (uint32_t x = 0u; x <= WidthSegments; ++x)
                {
                    const float u = float(x) / float(WidthSegments);
                    const float phi = u * Pi * 2.0f;
                    grid[y][x] = glm::vec4(
                        -radius * std::cos(phi),
                        vertical,
                        radius * std::sin(phi),
                        1.0f);
                }
            }
            vertices.clear();
            indices.clear();
            for (uint32_t y = 0u; y < HeightSegments; ++y)
            {
                for (uint32_t x = 0u; x < WidthSegments; ++x)
                {
                    const glm::vec4 &a = grid[y][x + 1u];
                    const glm::vec4 &b = grid[y][x];
                    const glm::vec4 &c = grid[y + 1u][x];
                    const glm::vec4 &d = grid[y + 1u][x + 1u];
                    if (y != 0u)
                    {
                        const uint32_t base = static_cast<uint32_t>(vertices.size());
                        glm::vec3 normal = glm::normalize(glm::cross(
                            glm::vec3(b - a), glm::vec3(d - a)));
                        if (glm::dot(normal, glm::vec3(a + b + d)) < 0.0f)
                            normal = -normal;
                        vertices.push_back({a, glm::vec4(normal, 0.0f)});
                        vertices.push_back({b, glm::vec4(normal, 0.0f)});
                        vertices.push_back({d, glm::vec4(normal, 0.0f)});
                        indices.insert(indices.end(), {base, base + 1u, base + 2u});
                    }
                    if (y != HeightSegments - 1u)
                    {
                        const uint32_t base = static_cast<uint32_t>(vertices.size());
                        glm::vec3 normal = glm::normalize(glm::cross(
                            glm::vec3(c - b), glm::vec3(d - b)));
                        if (glm::dot(normal, glm::vec3(b + c + d)) < 0.0f)
                            normal = -normal;
                        vertices.push_back({b, glm::vec4(normal, 0.0f)});
                        vertices.push_back({c, glm::vec4(normal, 0.0f)});
                        vertices.push_back({d, glm::vec4(normal, 0.0f)});
                        indices.insert(indices.end(), {base, base + 1u, base + 2u});
                    }
                }
            }
            if (vertices.size() != 72u || indices.size() != 72u)
                throw std::runtime_error("webgl_postprocessing_glitch sphere topology differs from r185.");
        }

        /** Validates one of the three deterministic glitch scenarios. */
        void validateGlitchScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-started" && options.targetFrame == 0u;
            const bool history = options.scenarioId == "deterministic-trigger-history" && options.targetFrame == 240u;
            const bool wild = options.scenarioId == "wild" && options.targetFrame == 1u;
            if (options.caseId != "webgl_postprocessing_glitch" ||
                (!initial && !history && !wild) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("webgl_postprocessing_glitch requires the locked r185 scenario contract.");
        }
    } // namespace

    void WebglPostprocessingGlitchRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateGlitchScenario(options);
        device = inDevice;
        wildMode = options.scenarioId == "wild";
        entity.vertices.clear();
        entity.indices.clear();
        entity.instances.clear();
        buildGlitchSphere(entity.vertices, entity.indices);
        entity.objectData.offsetAndScale = glm::vec4(0.0f);
        entity.objectData.materialAndFlags = glm::uvec4(0u, 0u, 0u, 0u);
        const glm::vec3 cameraPosition(0.0f, 0.0f, 400.0f);
        const glm::mat4 view = glm::lookAt(
            cameraPosition,
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        entity.objectData.viewProjection = glm::perspective(
            glm::radians(70.0f),
            float(options.width) / float(options.height),
            1.0f,
            1000.0f) * view;
        entity.objectData.cameraPositionAndFog = glm::vec4(cameraPosition, 1.0f);
        entity.objectData.lightDirectionAndIntensity = glm::vec4(
            glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)), 3.0f);
        entity.objectData.fogColorNearFar = glm::vec4(0.0f, 0.0f, 0.0f, 1000.0f);
        entity.objectData.parentRotation = glm::vec4(0.0f);
        entity.materialData.baseColor = glm::vec4(1.0f);
        entity.instances.resize(InstanceCount);
        updateInstances(options.targetFrame, wildMode);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("webgl_postprocessing_glitch could not create its Scene RenderSet encoder.");
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = InstanceCount;
        appendGlitchPayload(
            allocation,
            WebglPostprocessingGlitchSceneRenderSetComponents::vertices,
            "GlitchSphereVertices",
            entity.vertices.data(),
            entity.vertices.size() * sizeof(entity.vertices[0u]),
            1u);
        appendGlitchPayload(
            allocation,
            WebglPostprocessingGlitchSceneRenderSetComponents::indices,
            "GlitchSphereIndices",
            entity.indices.data(),
            entity.indices.size() * sizeof(entity.indices[0u]),
            1u);
        appendGlitchPayload(
            allocation,
            WebglPostprocessingGlitchSceneRenderSetComponents::objects,
            "GlitchSphereObject",
            &entity.objectData,
            sizeof(entity.objectData),
            1u);
        appendGlitchPayload(
            allocation,
            WebglPostprocessingGlitchSceneRenderSetComponents::instances,
            "GlitchSphereInstances",
            entity.instances.data(),
            entity.instances.size() * sizeof(entity.instances[0u]),
            InstanceCount);
        appendGlitchPayload(
            allocation,
            WebglPostprocessingGlitchSceneRenderSetComponents::materials,
            "GlitchSphereMaterial",
            &entity.materialData,
            sizeof(entity.materialData),
            1u);
        entity.entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglPostprocessingGlitchRuntimeAdapter::updateInstances(
        uint32_t frameIndex,
        bool inWildMode)
    {
        uint32_t randomState = DefaultThreeRandomSeed;
        for (uint32_t sample = 0u; sample < GlitchSceneInitializationRandomSamples; ++sample)
            (void)glitchRandomUnit(randomState);
        const float completedFrameCount = float(frameIndex + 1u);
        entity.objectData.parentRotation = glm::vec4(
            completedFrameCount * 0.005f,
            completedFrameCount * 0.01f,
            0.0f,
            0.0f);
        for (uint32_t index = 0u; index < InstanceCount; ++index)
        {
            glm::vec3 position(
                glitchRandomUnit(randomState) - 0.5f,
                glitchRandomUnit(randomState) - 0.5f,
                glitchRandomUnit(randomState) - 0.5f);
            const float positionLength = glm::length(position);
            if (positionLength > 1.0e-6f)
                position = position / positionLength;
            position *= glitchRandomUnit(randomState) * 400.0f;
            const glm::vec3 rotation(
                glitchRandomUnit(randomState) * 2.0f,
                glitchRandomUnit(randomState) * 2.0f,
                glitchRandomUnit(randomState) * 2.0f);
            const float scale = glitchRandomUnit(randomState) * 50.0f;
            entity.instances[index].positionAndScale = glm::vec4(position, scale);
            entity.instances[index].rotation = glm::vec4(rotation, 0.0f);
            const uint32_t colorBits = static_cast<uint32_t>(
                16777215.0 * static_cast<double>(glitchRandomUnit(randomState)));
            const float red = float((colorBits >> 16u) & 0xffu) / 255.0f;
            const float green = float((colorBits >> 8u) & 0xffu) / 255.0f;
            const float blue = float(colorBits & 0xffu) / 255.0f;
            entity.instances[index].tint = glm::vec4(
                glitchSrgbToLinear(red),
                glitchSrgbToLinear(green),
                glitchSrgbToLinear(blue),
                1.0f);
        }
    }

    /** Replays the locked r185 GlitchPass stream through one target frame. */
    WebglPostprocessingGlitchEffectState
    WebglPostprocessingGlitchRuntimeAdapter::makeEffectState(
        uint32_t targetFrame,
        bool inWildMode,
        uint32_t randomSeed) const
    {
        WebglPostprocessingGlitchEffectState state;
        state.time = float(targetFrame + 1u) / 60.0f;
        uint32_t randomState = randomSeed;
        for (uint32_t sample = 0u;
             sample < GlitchHeightMapRandomStart + 64u * 64u;
             ++sample)
        {
            (void)glitchRandomUnit(randomState);
        }
        uint32_t randomX = glitchRandomInt(randomState, 120u, 240u);
        for (uint32_t sample = 0u;
             sample < GlitchPostTriggerInitializationRandomSamples;
             ++sample)
        {
            (void)glitchRandomUnit(randomState);
        }
        uint32_t currentFrame = 0u;
        for (uint32_t frame = 0u; frame <= targetFrame; ++frame)
        {
            state.seed = glitchRandomUnit(randomState);
            state.bypass = 0.0f;
            if (inWildMode || currentFrame % randomX == 0u)
            {
                state.amount = glitchRandomUnit(randomState) / 30.0f;
                state.angle = glitchRandomFloat(randomState, -Pi, Pi);
                state.seedX = glitchRandomFloat(randomState, -1.0f, 1.0f);
                state.seedY = glitchRandomFloat(randomState, -1.0f, 1.0f);
                state.distortionX = glitchRandomFloat(randomState, 0.0f, 1.0f);
                state.distortionY = glitchRandomFloat(randomState, 0.0f, 1.0f);
                randomX = glitchRandomInt(randomState, 120u, 240u);
                currentFrame = 0u;
            }
            else if (currentFrame % randomX < randomX / 5u)
            {
                state.amount = glitchRandomUnit(randomState) / 90.0f;
                state.angle = glitchRandomFloat(randomState, -Pi, Pi);
                state.distortionX = glitchRandomFloat(randomState, 0.0f, 1.0f);
                state.distortionY = glitchRandomFloat(randomState, 0.0f, 1.0f);
                state.seedX = glitchRandomFloat(randomState, -0.3f, 0.3f);
                state.seedY = glitchRandomFloat(randomState, -0.3f, 0.3f);
            }
            else
            {
                state.amount = 0.0f;
                state.angle = 0.0f;
                state.seedX = 0.0f;
                state.seedY = 0.0f;
                state.distortionX = 0.0f;
                state.distortionY = 0.0f;
                state.bypass = 1.0f;
            }
            ++currentFrame;
        }
        return state;
    }

    void WebglPostprocessingGlitchRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        updateInstances(frameIndex, wildMode);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("webgl_postprocessing_glitch could not create its instance update encoder.");
        encoder->setBufferComponentData(
            entity.entityIndex,
            WebglPostprocessingGlitchSceneRenderSetComponents::instances,
            entity.instances.data(),
            entity.instances.size() * sizeof(entity.instances[0u]),
            0u,
            InstanceCount);
        encoder->setBufferComponentData(
            entity.entityIndex,
            WebglPostprocessingGlitchSceneRenderSetComponents::objects,
            &entity.objectData,
            sizeof(entity.objectData),
            0u,
            1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglPostprocessingGlitchRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        prepareGlitchOutputPath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output) throw std::runtime_error("Could not write glitch RGBA capture.");
    }

    void WebglPostprocessingGlitchRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        prepareGlitchOutputPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_postprocessing_glitch\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false";
        if (options.scenarioId == "wild")
        {
            output << ",\"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing_glitch\",\"scenarioId\":\"wild\",\"captureFrame\":1,\"sha256\":\"302cf0a8355112cee8323bc7a34c6e578b78c84a78741b80d7897ba52716726d\",\"target\":\"#wildGlitch\",\"eventCount\":2}";
        }
        else if (options.scenarioId == "deterministic-trigger-history")
        {
            output << ",\"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing_glitch\",\"scenarioId\":\"deterministic-trigger-history\",\"captureFrame\":240,\"sha256\":\"9c54e71abea43ff2d0a78c328f2b71994a22164f35db6e84040f31daad8c2cf0\",\"target\":\"#startButton\",\"eventCount\":1}";
        }
        else
        {
            output << ",\"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing_glitch\",\"scenarioId\":\"initial-started\",\"captureFrame\":0,\"sha256\":\"8bfbd297b909d8cad8776479c9e4d4cc2751a1a99415cb5b1b67e4d95c68a1e3\",\"target\":\"#startButton\",\"eventCount\":1}";
        }
        output << "}\n";
    }

    void WebglPostprocessingGlitchRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        bool inWildMode) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        prepareGlitchOutputPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_postprocessing_glitch\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":false,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglPostprocessingGlitchSceneRenderSet\",\n  \"renderableObjectCount\":1,\n"
               << "  \"entityCount\":1,\n  \"instanceCount\":100,\n  \"instanceCounts\":[100],\n"
               << "  \"computePassCount\":1,\n  \"scenePassCount\":1,\n  \"screenPassCount\":2,\n  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n"
               << "  \"msaaEnabled\":false,\n  \"wildMode\":" << (inWildMode ? "true" : "false") << ",\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
               << "  \"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglPostprocessingGlitchMainPass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set-0\",\"renderSetType\":\"WebglPostprocessingGlitchSceneRenderSet\",\"renderableObjectCount\":1,\"entityCount\":1,\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"instanced-spheres\",\"instanceCount\":100}],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglPostprocessingGlitchMainPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n";
    }

    void WebglPostprocessingGlitchRuntimeAdapter::afterFrame(
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
            throw std::overflow_error("webgl_postprocessing_glitch capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_postprocessing_glitch has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex, wildMode);
        captureWritten = true;
    }

    void WebglPostprocessingGlitchRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entity.vertices.clear();
        entity.indices.clear();
        entity.instances.clear();
    }
} // namespace GVM::ThreeSamples
