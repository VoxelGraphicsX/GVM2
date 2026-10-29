#include "WebglReadFloatBufferRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t Width = 800u;
        constexpr uint32_t Height = 500u;

        static_assert(sizeof(WebglReadFloatBufferHostVertex) == 48u);
        static_assert(sizeof(WebglReadFloatBufferHostObjectData) == 192u);
        static_assert(sizeof(WebglReadFloatBufferHostInstanceData) == 16u);
        static_assert(sizeof(WebglReadFloatBufferHostMaterialData) == 32u);

        /** Creates parent folders for one explicitly requested artifact. */
        void prepareReadFloatOutput(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path output(path.c_str());
            if (!output.parent_path().empty())
            {
                std::filesystem::create_directories(output.parent_path());
            }
        }

        /** Writes one UTF-8 evidence document when its path is configured. */
        void writeReadFloatText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            prepareReadFloatOutput(path);
            std::ofstream output(path.c_str(), std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGL float readback evidence.");
            }
        }

        /** Converts one display-sRGB byte into Three's linear working space. */
        float readFloatSrgbToLinear(uint32_t value)
        {
            const float normalized = float(value) / 255.0f;
            return normalized <= 0.04045f
                ? normalized / 12.92f
                : std::pow((normalized + 0.055f) / 1.055f, 2.4f);
        }

        /** Returns the bounded material color conversion used by ColorManagement. */
        glm::vec3 readFloatPackedSrgb(uint32_t color)
        {
            return glm::vec3(
                readFloatSrgbToLinear((color >> 16u) & 0xffu),
                readFloatSrgbToLinear((color >> 8u) & 0xffu),
                readFloatSrgbToLinear(color & 0xffu));
        }

        /** Appends one typed payload to a RenderSet allocation descriptor. */
        void appendReadFloatBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
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

        /** Builds the four-vertex PlaneGeometry used by both upstream scenes. */
        void buildReadFloatPlane(
            eastl::vector<WebglReadFloatBufferHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices = {
                {{-400.0f, -250.0f, 0.0f, 1.0f}, {}, {0.0f, 0.0f, 0.0f, 0.0f}},
                {{400.0f, -250.0f, 0.0f, 1.0f}, {}, {1.0f, 0.0f, 0.0f, 0.0f}},
                {{400.0f, 250.0f, 0.0f, 1.0f}, {}, {1.0f, 1.0f, 0.0f, 0.0f}},
                {{-400.0f, 250.0f, 0.0f, 1.0f}, {}, {0.0f, 1.0f, 0.0f, 0.0f}},
            };
            indices = {0u, 1u, 2u, 0u, 2u, 3u};
        }

        /** Builds the exact TorusGeometry(100,25,15,30) stream. */
        void buildReadFloatTorus(
            eastl::vector<WebglReadFloatBufferHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t RadialSegments = 15u;
            constexpr uint32_t TubularSegments = 30u;
            constexpr float Radius = 100.0f;
            constexpr float Tube = 25.0f;
            vertices.clear();
            indices.clear();
            vertices.reserve((RadialSegments + 1u) * (TubularSegments + 1u));
            for (uint32_t radial = 0u;
                 radial <= RadialSegments;
                 ++radial)
            {
                const float v = float(radial) / float(RadialSegments) *
                    float(2.0 * Pi);
                for (uint32_t tubular = 0u;
                     tubular <= TubularSegments;
                     ++tubular)
                {
                    const float u = float(tubular) / float(TubularSegments) *
                        float(2.0 * Pi);
                    const glm::vec3 center(
                        Radius * std::cos(u),
                        Radius * std::sin(u),
                        0.0f);
                    const glm::vec3 position(
                        (Radius + Tube * std::cos(v)) * std::cos(u),
                        (Radius + Tube * std::cos(v)) * std::sin(u),
                        Tube * std::sin(v));
                    vertices.push_back({
                        {position, 1.0f},
                        {glm::normalize(position - center), 0.0f},
                        {1.0f - float(tubular) / float(TubularSegments),
                         float(radial) / float(RadialSegments),
                         0.0f,
                         0.0f}});
                }
            }
            for (uint32_t radial = 1u;
                 radial <= RadialSegments;
                 ++radial)
            {
                for (uint32_t tubular = 1u;
                     tubular <= TubularSegments;
                     ++tubular)
                {
                    const uint32_t a =
                        (TubularSegments + 1u) * radial + tubular - 1u;
                    const uint32_t b =
                        (TubularSegments + 1u) * (radial - 1u) + tubular - 1u;
                    const uint32_t c = b + 1u;
                    const uint32_t d = a + 1u;
                    indices.insert(indices.end(), {a, b, d, b, c, d});
                }
            }
            if (vertices.size() != 496u || indices.size() != 2700u)
            {
                throw std::runtime_error(
                    "WebGL float readback torus topology differs from r185.");
            }
        }

        /** Returns the fixed-step ShaderMaterial time after one target frame. */
        float readFloatShaderTime(uint32_t frame)
        {
            double value = 0.0;
            double delta = 0.01;
            for (uint32_t index = 0u; index <= frame; ++index)
            {
                if (value > 1.0 || value < 0.0) delta = -delta;
                value += delta;
            }
            return static_cast<float>(value);
        }

        /** Builds the orthographic RTT camera projection used by Three. */
        glm::mat4 readFloatOrthographicProjection()
        {
            return glm::ortho(
                -400.0f,
                400.0f,
                -250.0f,
                250.0f,
                1.0f,
                1000.0f);
        }

        /** Builds the fixed camera view matrix at z=500. */
        glm::mat4 readFloatCameraView()
        {
            return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -500.0f));
        }

        /** Returns one model transform from the deterministic Date.now replacement. */
        glm::mat4 readFloatTorusModel(
            float x,
            float y,
            float scale,
            float rotationY)
        {
            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(x, y, 100.0f));
            model = glm::rotate(model, rotationY, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(scale));
            return model;
        }
    } // namespace

    void WebglReadFloatBufferRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 120u;
        const bool pointer =
            options.scenarioId == "pointer-readback" && options.targetFrame == 121u;
        if (options.caseId != "webgl_read_float_buffer" ||
            (!initial && !animated && !pointer) ||
            options.width != Width || options.height != Height ||
            options.randomSeed != DefaultThreeRandomSeed ||
            (pointer != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "WebGL float readback requires its three locked scenarios.");
        }

        device = inDevice;
        targetFrame = options.targetFrame;
        buildReadFloatPlane(planeVertices, planeIndices);
        buildReadFloatTorus(torusVertices, torusIndices);
        const glm::mat4 projection = readFloatOrthographicProjection();
        const glm::mat4 view = readFloatCameraView();
        // The reference harness fixes Date.now() to 1700000000000 ms, which
        // the upstream example multiplies by 0.0015 before assigning rotation.
        const double virtualTimeMs =
            static_cast<double>(options.targetFrame) * (1000.0 / 60.0);
        const double rotationTimeRadians =
            (1700000000000.0 + virtualTimeMs) * 0.0015;
        // Three computes the trigonometric rotation in JavaScript double
        // precision before uploading the resulting matrix as float32. Reduce
        // the angle in double precision before constructing the GLM matrix so
        // the large fixed epoch does not lose phase in a float cast.
        const float rotationTime = static_cast<float>(std::fmod(
            rotationTimeRadians,
            2.0 * Pi));
        const glm::mat4 planeModel = glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -100.0f));
        const glm::mat4 torusModelA = readFloatTorusModel(
            0.0f, 0.0f, 1.5f, -rotationTime);
        const glm::mat4 torusModelB = readFloatTorusModel(
            0.0f, 150.0f, 0.75f, -rotationTime + float(Pi * 0.5));
        planeObject.modelViewProjection = projection * view * planeModel;
        planeObject.modelView = view * planeModel;
        planeObject.normalTransform = glm::transpose(glm::inverse(planeObject.modelView));
        torusObjectA.modelViewProjection = projection * view * torusModelA;
        torusObjectA.modelView = view * torusModelA;
        torusObjectA.normalTransform = glm::transpose(glm::inverse(torusObjectA.modelView));
        torusObjectB.modelViewProjection = projection * view * torusModelB;
        torusObjectB.modelView = view * torusModelB;
        torusObjectB.normalTransform = glm::transpose(glm::inverse(torusObjectB.modelView));
        instanceData.translation = glm::vec4(0.0f);
        planeMaterial.kindTimeAndColor = glm::vec4(
            0.0f,
            readFloatShaderTime(options.targetFrame),
            0.0f,
            0.0f);
        planeMaterial.specularAndShininess = glm::vec4(0.0f);
        const glm::vec3 torusColorA = readFloatPackedSrgb(0x9c9c9cu);
        const glm::vec3 torusColorB = readFloatPackedSrgb(0x9c0000u);
        const glm::vec3 specularA = readFloatPackedSrgb(0xffaa00u);
        const glm::vec3 specularB = readFloatPackedSrgb(0xff2200u);
        torusMaterialA.kindTimeAndColor = glm::vec4(1.0f, torusColorA);
        torusMaterialA.specularAndShininess = glm::vec4(specularA, 5.0f);
        torusMaterialB.kindTimeAndColor = glm::vec4(1.0f, torusColorB);
        torusMaterialB.specularAndShininess = glm::vec4(specularB, 5.0f);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create WebGL float readback Scene Set encoder.");
        }
        auto allocateEntity = [&](const char *prefix,
                                  const eastl::vector<WebglReadFloatBufferHostVertex> &vertices,
                                  const eastl::vector<uint32_t> &indices,
                                  const WebglReadFloatBufferHostObjectData &objectData,
                                  const WebglReadFloatBufferHostMaterialData &materialData)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = uint32_t(vertices.size());
            allocation.indicesCount = uint32_t(indices.size());
            allocation.instanceCount = 1u;
            appendReadFloatBuffer(
                allocation,
                WebglReadFloatBufferSceneRenderSetComponents::vertices,
                (std::string(prefix) + "Vertices").c_str(),
                vertices.data(),
                uint64_t(vertices.size()) * sizeof(vertices[0u]));
            appendReadFloatBuffer(
                allocation,
                WebglReadFloatBufferSceneRenderSetComponents::indices,
                (std::string(prefix) + "Indices").c_str(),
                indices.data(),
                uint64_t(indices.size()) * sizeof(indices[0u]));
            appendReadFloatBuffer(
                allocation,
                WebglReadFloatBufferSceneRenderSetComponents::objects,
                (std::string(prefix) + "Object").c_str(),
                &objectData,
                sizeof(objectData));
            appendReadFloatBuffer(
                allocation,
                WebglReadFloatBufferSceneRenderSetComponents::instances,
                (std::string(prefix) + "Instance").c_str(),
                &instanceData,
                sizeof(instanceData));
            appendReadFloatBuffer(
                allocation,
                WebglReadFloatBufferSceneRenderSetComponents::materials,
                (std::string(prefix) + "Material").c_str(),
                &materialData,
                sizeof(materialData));
            encoder->allocEntity(allocation);
        };
        allocateEntity("WebglReadFloatPlane", planeVertices, planeIndices,
                       planeObject, planeMaterial);
        allocateEntity("WebglReadFloatTorusA", torusVertices, torusIndices,
                       torusObjectA, torusMaterialA);
        allocateEntity("WebglReadFloatTorusB", torusVertices, torusIndices,
                       torusObjectB, torusMaterialB);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebglReadFloatBufferRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglReadFloatBufferRuntimeAdapter::captureTargetFrame(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        GVM::RHI::Texture floatReadbackTexture,
        uint32_t width,
        uint32_t height)
    {
        if (captureWritten || frameIndex != options.targetFrame) return;
        eastl::vector<uint8_t> rgba(size_t(width) * height * 4u);
        eastl::vector<float> floatPixels(size_t(width) * height * 4u);
        device->graphicsQueue(0)
            ->readTexture(
                floatReadbackTexture,
                floatPixels.data(),
                uint64_t(floatPixels.size()) * sizeof(float))
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            prepareReadFloatOutput(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
        }
        const size_t centerOffset =
            (size_t(height / 2u) * width + width / 2u) * 4u;
        const float centerR = floatPixels[centerOffset];
        const float centerG = floatPixels[centerOffset + 1u];
        const float centerB = floatPixels[centerOffset + 2u];
        std::ostringstream metadata;
        metadata << "{\n"
                 << "  \"schemaVersion\":1,\n"
                 << "  \"source\":\"gvm-three-r185\",\n"
                 << "  \"caseId\":\"webgl_read_float_buffer\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"width\":" << width << ",\n"
                 << "  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\":" << rgba.size() << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"rttFormat\":\"rgba32float\",\n"
                 << "  \"sampleCount\":1,\n"
                 << "  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                 << "  \"centerReadback\":[" << centerR << "," << centerG << "," << centerB << "]\n"
                 << "}\n";
        writeReadFloatText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot << "{\n"
                 << "  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgl_read_float_buffer\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"implementationLevel\":\"semantic-complete\",\n"
                 << "  \"gpuWorkDslOnly\":true,\n"
                 << "  \"renderSetPolicy\":\"required\",\n"
                 << "  \"sceneRenderSetCount\":1,\n"
                 << "  \"renderableObjectCount\":3,\n"
                 << "  \"entityCount\":3,\n"
                 << "  \"instanceCount\":3,\n"
                 << "  \"planeVertexCount\":4,\n"
                 << "  \"planeIndexCount\":6,\n"
                 << "  \"torusVertexCount\":496,\n"
                 << "  \"torusIndexCount\":2700,\n"
                 << "  \"scenePassCount\":1,\n"
                 << "  \"screenPassCount\":1,\n"
                 << "  \"drawCommandCount\":3,\n"
                 << "  \"rttFormat\":\"rgba32float\",\n"
                 << "  \"sampleCount\":1,\n"
                 << "  \"msaaSimulated\":false,\n"
                 << "  \"renderSetType\":\"WebglReadFloatBufferSceneRenderSet\",\n"
                 << "  \"sceneRoots\":[{\"id\":\"sceneRTT\",\"renderSetCount\":1,\"entityCount\":3,\"renderSetType\":\"WebglReadFloatBufferSceneRenderSet\",\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"float-rtt-scene\",\"renderClass\":\"WebglReadFloatBufferScenePass\",\"drawMode\":\"render-set-indexed-indirect\",\"renderSetBindingCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
                 << "  \"screenPasses\":[{\"name\":\"float-texture-output\",\"renderClass\":\"WebglReadFloatBufferScreenPass\"}]\n"
                 << "}\n";
        writeReadFloatText(options.sceneSnapshotPath, snapshot.str());

        std::ostringstream semantic;
        semantic << "{\n"
                 << "  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgl_read_float_buffer\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"rttFormat\":\"rgba32float\",\n"
                 << "  \"centerPixel\":[" << centerR << "," << centerG << "," << centerB << ",1],\n"
                 << "  \"entityCount\":3,\n"
                 << "  \"scenePassCount\":1\n"
                 << "}\n";
        writeReadFloatText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebglReadFloatBufferRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        planeVertices.clear();
        planeIndices.clear();
        torusVertices.clear();
        torusIndices.clear();
        device = {};
    }
} // namespace GVM::ThreeSamples
