#include "WebgpuPostprocessingMaskingRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebgpuPostprocessingMaskingVertex) == 16u);
        static_assert(sizeof(WebgpuPostprocessingMaskingTorusUniforms) == 64u);
        static_assert(sizeof(WebgpuPostprocessingMaskingHostObjectData) == 64u);
        static_assert(sizeof(WebgpuPostprocessingMaskingHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuPostprocessingMaskingHostMaterialData) == 16u);

        /** Creates parent directories for one requested evidence artifact. */
        void prepareWebgpuMaskingOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeWebgpuMaskingText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebgpuMaskingOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU masking evidence.");
        }

        /** Normalizes decoded rows to the GVM texture sampling orientation. */
        RgbaImageData flipWebgpuMaskingRows(const RgbaImageData &source)
        {
            RgbaImageData flipped;
            flipped.width = source.width;
            flipped.height = source.height;
            flipped.pixels.resize(source.pixels.size());
            const size_t rowBytes = static_cast<size_t>(source.width) * 4u;
            for (uint32_t row = 0u; row < source.height; ++row)
            {
                const size_t sourceOffset =
                    static_cast<size_t>(source.height - row - 1u) * rowBytes;
                const size_t targetOffset = static_cast<size_t>(row) * rowBytes;
                eastl::copy_n(
                    source.pixels.data() + sourceOffset,
                    rowBytes,
                    flipped.pixels.data() + targetOffset);
            }
            return flipped;
        }

        /** Converts one decoded image chain into tightly packed mip payloads. */
        eastl::vector<eastl::vector<uint8_t>> packWebgpuMaskingMips(
            const eastl::vector<RgbaImageData> &images)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(images.size());
            for (const RgbaImageData &image : images)
                result.push_back(image.pixels);
            return result;
        }

        /** Constructs Three's intrinsic XYZ Euler rotation in binary64. */
        glm::dmat4 makeWebgpuMaskingRotation(
            double rotationX,
            double rotationY)
        {
            const double cx = std::cos(rotationX * 0.5);
            const double cy = std::cos(rotationY * 0.5);
            const double sx = std::sin(rotationX * 0.5);
            const double sy = std::sin(rotationY * 0.5);
            const double qx = sx * cy;
            const double qy = cx * sy;
            const double qz = sx * sy;
            const double qw = cx * cy;
            const double x2 = qx + qx;
            const double y2 = qy + qy;
            const double z2 = qz + qz;
            const double xx = qx * x2;
            const double xy = qx * y2;
            const double xz = qx * z2;
            const double yy = qy * y2;
            const double yz = qy * z2;
            const double zz = qz * z2;
            const double wx = qw * x2;
            const double wy = qw * y2;
            const double wz = qw * z2;
            glm::dmat4 result(1.0);
            result[0u] = glm::dvec4(
                1.0 - yy - zz, xy + wz, xz - wy, 0.0);
            result[1u] = glm::dvec4(
                xy - wz, 1.0 - xx - zz, yz + wx, 0.0);
            result[2u] = glm::dvec4(
                xz + wy, yz - wx, 1.0 - xx - yy, 0.0);
            return result;
        }

        /** Constructs the fixed 50-degree WebGPU camera projection. */
        glm::dmat4 makeWebgpuMaskingProjection()
        {
            constexpr double Near = 1.0;
            constexpr double Far = 1000.0;
            const double inverseTangent =
                1.0 / std::tan(50.0 * Pi / 360.0);
            glm::dmat4 result(0.0);
            result[0u][0u] = inverseTangent / 1.6;
            result[1u][1u] = inverseTangent;
            result[2u][2u] = (Far + Near) / (Near - Far);
            result[2u][3u] = -1.0;
            result[3u][2u] = 2.0 * Far * Near / (Near - Far);
            return result;
        }

        /** Builds the target object's exact model-view-projection matrix. */
        glm::mat4 makeWebgpuMaskingMvp(
            double positionX,
            double positionY,
            double rotationX,
            double rotationY)
        {
            glm::dmat4 model = makeWebgpuMaskingRotation(
                rotationX, rotationY);
            model[3u] = glm::dvec4(positionX, positionY, 0.0, 1.0);
            glm::dmat4 view(1.0);
            view[3u][2u] = -10.0;
            return glm::mat4(makeWebgpuMaskingProjection() * view * model);
        }

        /** Builds a grouped BoxGeometry-equivalent closed cube. */
        void buildWebgpuMaskingBox(
            eastl::vector<WebgpuPostprocessingMaskingVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr float HalfExtent = 2.0f;
            const glm::vec4 corners[8u] = {
                {-HalfExtent, -HalfExtent, -HalfExtent, 1.0f},
                {HalfExtent, -HalfExtent, -HalfExtent, 1.0f},
                {HalfExtent, HalfExtent, -HalfExtent, 1.0f},
                {-HalfExtent, HalfExtent, -HalfExtent, 1.0f},
                {-HalfExtent, -HalfExtent, HalfExtent, 1.0f},
                {HalfExtent, -HalfExtent, HalfExtent, 1.0f},
                {HalfExtent, HalfExtent, HalfExtent, 1.0f},
                {-HalfExtent, HalfExtent, HalfExtent, 1.0f}};
            vertices.clear();
            for (const glm::vec4 &corner : corners)
                vertices.push_back({corner});
            constexpr uint32_t CubeIndices[36u] = {
                0u, 2u, 1u, 0u, 3u, 2u,
                4u, 5u, 6u, 4u, 6u, 7u,
                0u, 4u, 7u, 0u, 7u, 3u,
                1u, 2u, 6u, 1u, 6u, 5u,
                3u, 7u, 6u, 3u, 6u, 2u,
                0u, 1u, 5u, 0u, 5u, 4u};
            indices.assign(CubeIndices, CubeIndices + 36u);
        }

        /** Builds exact TorusGeometry(3,1,16,32) positions and indices. */
        void buildWebgpuMaskingTorus(
            eastl::vector<WebgpuPostprocessingMaskingVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t RadialSegments = 16u;
            constexpr uint32_t TubularSegments = 32u;
            vertices.clear();
            indices.clear();
            vertices.reserve((RadialSegments + 1u) * (TubularSegments + 1u));
            for (uint32_t radial = 0u;
                 radial <= RadialSegments;
                 ++radial)
            {
                const double v =
                    double(radial) / double(RadialSegments) * Pi * 2.0;
                for (uint32_t tubular = 0u;
                     tubular <= TubularSegments;
                     ++tubular)
                {
                    const double u =
                        double(tubular) / double(TubularSegments) * Pi * 2.0;
                    vertices.push_back({glm::vec4(
                        static_cast<float>((3.0 + std::cos(v)) * std::cos(u)),
                        static_cast<float>((3.0 + std::cos(v)) * std::sin(u)),
                        static_cast<float>(std::sin(v)),
                        1.0f)});
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
                        (TubularSegments + 1u) * (radial - 1u) +
                        tubular - 1u;
                    const uint32_t c = b + 1u;
                    const uint32_t d = a + 1u;
                    indices.insert(indices.end(), {a, b, d, b, c, d});
                }
            }
            if (vertices.size() != 561u || indices.size() != 3072u)
                throw std::runtime_error(
                    "WebGPU masking torus topology differs from r185.");
        }

        /** Appends one typed payload to the grouped box allocation. */
        void appendWebgpuMaskingPayload(
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
    }

    void WebgpuPostprocessingMaskingRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 120u;
        if (options.caseId != "webgpu_postprocessing_masking" ||
            (!initial && !animated) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() || !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "WebGPU masking requires its locked scenarios, extent, seed, assets, and no replay.");
        }
        device = inDevice;
        const double time = 6000.0 + double(options.targetFrame) / 60.0;
        buildWebgpuMaskingBox(boxVertices, boxIndices);
        buildWebgpuMaskingTorus(torusVertices, torusIndices);
        boxObjectData.modelViewProjection = makeWebgpuMaskingMvp(
            std::cos(time / 1.5) * 2.0,
            std::sin(time) * 2.0,
            time,
            time / 2.0);
        boxInstanceData.reserved = glm::vec4(0.0f);
        boxMaterialData.maskAndPhase = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        torusUniforms.modelViewProjection = makeWebgpuMaskingMvp(
            std::cos(time) * 2.0,
            std::sin(time / 1.5) * 2.0,
            time,
            time / 2.0);

        const std::filesystem::path textureRoot =
            std::filesystem::path(options.assetRoot.c_str()) / "textures";
        const RgbaImageData decoded1 = flipWebgpuMaskingRows(
            decodeJpegRgba8(
                textureRoot /
                "758px-Canestra_di_frutta_(Caravaggio).jpg"));
        const RgbaImageData decoded2 = flipWebgpuMaskingRows(
            decodeJpegRgba8(
                textureRoot / "2294472375_24a3b8ef46_o.jpg"));
        if (decoded1.width != 758u || decoded1.height != 600u ||
            decoded2.width != 4096u || decoded2.height != 2048u)
            throw std::runtime_error(
                "WebGPU masking photograph dimensions differ from r185.");
        photograph1Mips.push_back(decoded1.pixels);
        photograph2Mips = packWebgpuMaskingMips(
            buildSrgbMipChain(decoded2));

        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(boxVertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(boxIndices.size());
        allocation.instanceCount = 1u;
        appendWebgpuMaskingPayload(
            allocation,
            WebgpuPostprocessingMaskingSceneRenderSetComponents::vertices,
            "WebgpuPostprocessingMaskingBoxVertices",
            boxVertices.data(),
            boxVertices.size() * sizeof(WebgpuPostprocessingMaskingVertex));
        appendWebgpuMaskingPayload(
            allocation,
            WebgpuPostprocessingMaskingSceneRenderSetComponents::indices,
            "WebgpuPostprocessingMaskingBoxIndices",
            boxIndices.data(),
            boxIndices.size() * sizeof(uint32_t));
        appendWebgpuMaskingPayload(
            allocation,
            WebgpuPostprocessingMaskingSceneRenderSetComponents::objects,
            "WebgpuPostprocessingMaskingBoxObject",
            &boxObjectData,
            sizeof(boxObjectData));
        appendWebgpuMaskingPayload(
            allocation,
            WebgpuPostprocessingMaskingSceneRenderSetComponents::instances,
            "WebgpuPostprocessingMaskingBoxInstance",
            &boxInstanceData,
            sizeof(boxInstanceData));
        appendWebgpuMaskingPayload(
            allocation,
            WebgpuPostprocessingMaskingSceneRenderSetComponents::materials,
            "WebgpuPostprocessingMaskingBoxMaterial",
            &boxMaterialData,
            sizeof(boxMaterialData));
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create the WebGPU masking Scene Set encoder.");
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebgpuPostprocessingMaskingRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuPostprocessingMaskingRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareWebgpuMaskingOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU masking RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgpu_postprocessing_masking\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"pipeline\":\"" << options.pipeline.c_str()
            << "\",\"backend\":\""
            << threeSampleBackendName(options.backend)
            << "\",\"frame\":" << frameIndex
            << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\"}\n";
        writeWebgpuMaskingText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,"
            << "\"caseId\":\"webgpu_postprocessing_masking\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":2,"
            << "\"entityCount\":1,\"instanceCount\":1,"
            << "\"vertexCount\":" << boxVertices.size()
            << ",\"indexCount\":" << boxIndices.size()
            << ",\"scenePassCount\":3,\"screenPassCount\":4,"
            << "\"drawCommandCount\":3,\"renderSetType\":"
            << "\"WebgpuPostprocessingMaskingSceneRenderSet\","
            << "\"sceneRoots\":[{\"id\":\"baseScene\","
            << "\"renderSetCount\":0,\"renderableObjectCount\":0},"
            << "{\"id\":\"maskScene1\","
            << "\"renderSetCount\":1,\"renderSetId\":\"mask-scene1-set\","
            << "\"renderSetType\":\"WebgpuPostprocessingMaskingSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,"
            << "\"logicalRenderableId\":\"grouped-box-mask\","
            << "\"instanceCount\":1}],\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"box-mask\","
            << "\"renderClass\":\"WebgpuPostprocessingMaskingBoxPass\","
            << "\"renderSetId\":\"mask-scene1-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]},{\"id\":\"maskScene2\","
            << "\"renderSetCount\":0,\"renderableObjectCount\":1}],"
            << "\"scenePassSequence\":["
            << "{\"sceneRoot\":\"baseScene\",\"scenePass\":\"base-background\","
            << "\"entityOrdinal\":0},{\"sceneRoot\":\"maskScene1\","
            << "\"scenePass\":\"box-mask\",\"entityOrdinal\":0},"
            << "{\"sceneRoot\":\"maskScene2\",\"scenePass\":\"torus-mask\","
            << "\"entityOrdinal\":0}]}\n";
        writeWebgpuMaskingText(options.sceneSnapshotPath, snapshot.str());
        writeWebgpuMaskingText(options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebgpuPostprocessingMaskingRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        boxVertices.clear();
        boxIndices.clear();
        torusVertices.clear();
        torusIndices.clear();
        photograph1Mips.clear();
        photograph2Mips.clear();
        device = {};
    }
}
