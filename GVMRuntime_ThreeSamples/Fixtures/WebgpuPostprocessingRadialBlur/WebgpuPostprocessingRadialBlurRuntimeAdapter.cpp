#include "WebgpuPostprocessingRadialBlurRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/vec2.hpp>
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
        constexpr float Pi = 3.14159265358979323846f;
        constexpr GVM::Core::RenderSetHandle RadialBlurSceneSetHandle =
            ExportedRenderSet::sceneSet;

        /** Creates parent directories for one requested radial-blur artifact. */
        void prepareRadialBlurOutput(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Advances the locked xorshift32 stream used by the browser capture. */
        float nextRadialBlurRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return float(state >> 8u) / 16777216.0f;
        }

        /** Converts one Three.js XYZ Euler rotation into its compose quaternion. */
        glm::quat makeRadialBlurQuaternion(const glm::vec3 &rotation)
        {
            const float cx = std::cos(rotation.x * 0.5f);
            const float cy = std::cos(rotation.y * 0.5f);
            const float cz = std::cos(rotation.z * 0.5f);
            const float sx = std::sin(rotation.x * 0.5f);
            const float sy = std::sin(rotation.y * 0.5f);
            const float sz = std::sin(rotation.z * 0.5f);
            return glm::quat(
                cx * cy * cz - sx * sy * sz,
                sx * cy * cz + cx * sy * sz,
                cx * sy * cz - sx * cy * sz,
                cx * cy * sz + sx * sy * cz);
        }

        /** Converts one hue from the source HSL range into working-space RGB. */
        glm::vec3 makeRadialBlurColor(float hue)
        {
            const float chroma =
                (1.0f - std::abs(2.0f * 0.2f - 1.0f)) * 1.0f;
            const float hueSector = hue * 6.0f;
            const float secondary =
                chroma *
                (1.0f -
                 std::abs(
                     std::fmod(hueSector, 2.0f) - 1.0f));
            glm::vec3 encoded;
            if (hueSector < 1.0f) encoded = {chroma, secondary, 0.0f};
            else if (hueSector < 2.0f) encoded = {secondary, chroma, 0.0f};
            else if (hueSector < 3.0f) encoded = {0.0f, chroma, secondary};
            else if (hueSector < 4.0f) encoded = {0.0f, secondary, chroma};
            else if (hueSector < 5.0f) encoded = {secondary, 0.0f, chroma};
            else encoded = {chroma, 0.0f, secondary};
            const float match = 0.2f - chroma * 0.5f;
            encoded += glm::vec3(match);
            return encoded;
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendRadialBlurPayload(
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

        /** Writes one optional tightly packed RGBA8 capture. */
        void writeRadialBlurRgba(
            const eastl::string &pathValue,
            const eastl::vector<uint8_t> &rgba)
        {
            if (pathValue.empty()) return;
            prepareRadialBlurOutput(pathValue);
            std::ofstream output(
                pathValue.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write radial-blur RGBA.");
            }
        }
    }

    void WebgpuPostprocessingRadialBlurRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" &&
            options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" &&
            options.targetFrame == 120u;
        const bool maximum =
            options.scenarioId == "maximum-samples" &&
            options.targetFrame == 121u;
        const bool disabled =
            options.scenarioId == "disabled-static" &&
            options.targetFrame == 1u;
        if (options.caseId != "webgpu_postprocessing_radial_blur" ||
            (!initial && !animated && !maximum && !disabled) ||
            options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            ((maximum || disabled) != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "Radial blur requires one locked Manifest scenario.");
        }
        device = inDevice;
        blurEnabled = true;
        animationEnabled = true;
        inspectorEnabled = false;

        constexpr float coordinate = 0.5773502691896258f;
        const glm::vec3 positions[4u] = {
            {coordinate, coordinate, coordinate},
            {-coordinate, -coordinate, coordinate},
            {-coordinate, coordinate, -coordinate},
            {coordinate, -coordinate, -coordinate},
        };
        constexpr uint32_t topology[12u] = {
            2u, 1u, 0u,
            0u, 3u, 2u,
            1u, 3u, 0u,
            2u, 3u, 1u,
        };
        for (uint32_t triangle = 0u; triangle < 4u; ++triangle)
        {
            const glm::vec3 a =
                positions[topology[triangle * 3u]];
            const glm::vec3 b =
                positions[topology[triangle * 3u + 1u]];
            const glm::vec3 c =
                positions[topology[triangle * 3u + 2u]];
            const glm::vec3 normal =
                glm::normalize(glm::cross(c - b, a - b));
            const glm::vec3 trianglePositions[3u] = {a, b, c};
            for (const glm::vec3 &position : trianglePositions)
            {
                vertices.push_back({
                    glm::vec4(position, 1.0f),
                    glm::vec4(normal, 0.0f),
                });
                indices.push_back(
                    static_cast<uint32_t>(indices.size()));
            }
        }

        // Three.js constructors consume 216 locked UUID/random draws before
        // the example enters its one-hundred-instance initialization loop.
        // Another 77 draws occur later while building the render pipeline.
        uint32_t randomState = 2130645506u;
        instances.resize(100u);
        for (uint32_t instanceIndex = 0u;
             instanceIndex < 100u;
             ++instanceIndex)
        {
            const float translationX = nextRadialBlurRandom(randomState);
            const float translationY = nextRadialBlurRandom(randomState);
            const float translationZ = nextRadialBlurRandom(randomState);
            glm::vec3 translation(
                translationX * 50.0f - 25.0f,
                translationY * 50.0f - 25.0f,
                translationZ * 50.0f - 25.0f);
            glm::vec2 center(translation.x, translation.y);
            if (glm::length(center) < 6.0f)
            {
                center = glm::normalize(center) * 6.0f;
                translation.x = center.x;
                translation.y = center.y;
            }
            const float scaleValue =
                nextRadialBlurRandom(randomState) * 2.0f + 1.0f;
            const float rotationX = nextRadialBlurRandom(randomState);
            const float rotationY = nextRadialBlurRandom(randomState);
            const float rotationZ = nextRadialBlurRandom(randomState);
            const glm::vec3 rotation(
                rotationX * Pi,
                rotationY * Pi,
                rotationZ * Pi);
            const glm::mat4 model =
                glm::translate(glm::mat4(1.0f), translation) *
                glm::toMat4(makeRadialBlurQuaternion(rotation)) *
                glm::scale(
                    glm::mat4(1.0f),
                    glm::vec3(scaleValue));
            auto &instance = instances[instanceIndex];
            instance.transformColumn0 = model[0u];
            instance.transformColumn1 = model[1u];
            instance.transformColumn2 = model[2u];
            instance.transformColumn3 = model[3u];
            instance.color = glm::vec4(
                makeRadialBlurColor(
                    0.55f +
                    float(instanceIndex) /
                        100.0f * 0.15f),
                1.0f);
        }

        const float groupAngle =
            animationEnabled
                ? float(options.targetFrame) / 60.0f * 0.1f
                : 0.0f;
        const glm::mat4 view =
            glm::lookAtRH(
                glm::vec3(0.0f, 0.0f, 50.0f),
                glm::vec3(0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 group =
            glm::rotate(
                glm::mat4(1.0f),
                groupAngle,
                glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection =
            glm::perspectiveRH_ZO(
                45.0f * Pi / 180.0f,
                800.0f / 500.0f,
                0.1f,
                200.0f);
        projection[1u][1u] *= -1.0f;
        objectData.modelView = view * group;
        objectData.modelViewProjection =
            projection * objectData.modelView;
        materialData.roughnessMetalnessPadding =
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                RadialBlurSceneSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create radial-blur RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(indices.size());
        allocation.instanceCount =
            static_cast<uint32_t>(instances.size());
        appendRadialBlurPayload(
            allocation,
            WebgpuPostprocessingRadialBlurSceneRenderSetComponents::vertices,
            "RadialBlurVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]),
            1u);
        appendRadialBlurPayload(
            allocation,
            WebgpuPostprocessingRadialBlurSceneRenderSetComponents::indices,
            "RadialBlurIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]),
            1u);
        appendRadialBlurPayload(
            allocation,
            WebgpuPostprocessingRadialBlurSceneRenderSetComponents::objects,
            "RadialBlurObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendRadialBlurPayload(
            allocation,
            WebgpuPostprocessingRadialBlurSceneRenderSetComponents::instances,
            "RadialBlurInstances",
            instances.data(),
            instances.size() * sizeof(instances[0u]),
            static_cast<uint32_t>(instances.size()));
        appendRadialBlurPayload(
            allocation,
            WebgpuPostprocessingRadialBlurSceneRenderSetComponents::materials,
            "RadialBlurMaterial",
            &materialData,
            sizeof(materialData),
            1u);
        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(
            RadialBlurSceneSetHandle,
            encoder);
    }

    void WebgpuPostprocessingRadialBlurRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuPostprocessingRadialBlurRuntimeAdapter::afterFrame(
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
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Radial-blur capture is too large.");
        }
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        writeRadialBlurRgba(options.captureRgbaPath, rgba);
        if (!options.captureMetadataPath.empty())
        {
            prepareRadialBlurOutput(options.captureMetadataPath);
            std::ofstream output(
                options.captureMetadataPath.c_str(),
                std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgpu_postprocessing_radial_blur\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\",\n"
                << "  \"inputReplay\":";
            if (options.scenarioId == "maximum-samples")
            {
                output
                    << "{\"sha256\":\"684db68e855d824d1a10ed87330b83d8e96237dbcd12ef98d551ea57b34b1cb9\","
                    << "\"caseId\":\"webgpu_postprocessing_radial_blur\","
                    << "\"scenarioId\":\"maximum-samples\","
                    << "\"captureFrame\":121,\"eventCount\":1,"
                    << "\"lastEventFrame\":0,"
                    << "\"target\":\"canvas:not([class])\"}";
            }
            else if (options.scenarioId == "disabled-static")
            {
                output
                    << "{\"sha256\":\"498cec5eacf014aeabfd092a776fd993e3e6488f7b9feaa8a94ca0fcc59a5ccd\","
                    << "\"caseId\":\"webgpu_postprocessing_radial_blur\","
                    << "\"scenarioId\":\"disabled-static\","
                    << "\"captureFrame\":1,\"eventCount\":1,"
                    << "\"lastEventFrame\":0,"
                    << "\"target\":\"canvas:not([class])\"}";
            }
            else
            {
                output << "null";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareRadialBlurOutput(options.sceneSnapshotPath);
            std::ofstream output(
                options.sceneSnapshotPath.c_str(),
                std::ios::trunc);
            output
                << "{\n  \"caseId\":\"webgpu_postprocessing_radial_blur\","
                << "\n  \"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\n  \"frame\":" << frameIndex
                << ",\n  \"implementationLevel\":\"strict-pass\","
                << "\n  \"gpuWorkDslOnly\":true,"
                << "\n  \"renderSetPolicy\":\"required\","
                << "\n  \"sceneRenderSetCount\":1,"
                << "\n  \"renderSetType\":\"WebgpuPostprocessingRadialBlurSceneRenderSet\","
                << "\n  \"entityCount\":1,"
                << "\n  \"instanceCounts\":[100],"
                << "\n  \"renderableObjectCount\":1,"
                << "\n  \"drawCommandCount\":1,"
                << "\n  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main\",\"entityOrdinal\":0}],"
                << "\n  \"renderSetIndexedIndirect\":true,"
                << "\n  \"directDrawFallback\":false,"
                << "\n  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
                << "\"renderSetId\":\"webgpu-radial-blur-scene-set\","
                << "\"renderSetType\":\"WebgpuPostprocessingRadialBlurSceneRenderSet\","
                << "\"renderableObjectCount\":1,\"entityCount\":1,"
                << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"tetrahedron-field\",\"instanceCount\":100}],"
                << "\"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
                << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
                << "\"scenePasses\":[{\"name\":\"main\","
                << "\"renderClass\":\"WebgpuPostprocessingRadialBlurMainPass\","
                << "\"renderSetId\":\"webgpu-radial-blur-scene-set\","
                << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
                << "\"invocationCount\":1,\"drawCommandCount\":1,"
                << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n";
        }
        captureWritten = true;
    }

    void WebgpuPostprocessingRadialBlurRuntimeAdapter::shutdown(
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
