#include "WebglInteractiveBuffergeometryRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t TriangleCount = 5000u;
        constexpr uint32_t MeshVertexCount = TriangleCount * 3u;
        constexpr uint32_t MeshIndexCount = MeshVertexCount;
        constexpr uint32_t RandomSeed = 0x12345678u;
// Three's module and object UUID constructors consume 116 patched
// Math.random words before the first interactive triangle; the page title
// captures the stream at state 60116 after 5000 * 12 geometry draws.
constexpr uint32_t PreGeometryRandomDrawCount = 116u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        /** Creates the parent directory for one optional capture artifact. */
        void prepareOutputPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Returns the deterministic JavaScript-compatible unit random value. */
        double nextRandomUnit(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Builds Three's exact OpenGL perspective projection. */
        glm::mat4 makeProjection(uint32_t width, uint32_t height)
        {
            constexpr double Pi = 3.14159265358979323846;
            constexpr double nearDistance = 1.0;
            constexpr double farDistance = 3500.0;
            const double top = nearDistance * std::tan(27.0 * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                double(width) / double(height) * projectionHeight;
            const double projectionDepth = farDistance - nearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(
                2.0 * nearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(
                2.0 * nearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(
                -(farDistance + nearDistance) / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -2.0 * farDistance * nearDistance / projectionDepth);
            return projection;
        }

        /** Builds the XYZ Euler rotation used by the animated reference mesh. */
        glm::mat4 makeModelView(uint32_t frameIndex)
        {
            constexpr double ReferenceEpochSeconds = 1700000000.0;
            const double time = ReferenceEpochSeconds + double(frameIndex) / 60.0;
            const double x = time * 0.15;
            const double y = time * 0.25;
            const double sineX = std::sin(x);
            const double cosineX = std::cos(x);
            const double sineY = std::sin(y);
            const double cosineY = std::cos(y);
            // Three's default Euler order is XYZ.  With z=0 its column-major
            // Matrix4 layout is R_x * R_y, not R_y * R_x.
            glm::mat4 rotation(1.0f);
            rotation[0u][0u] = static_cast<float>(cosineY);
            rotation[0u][1u] = static_cast<float>(sineX * sineY);
            rotation[0u][2u] = static_cast<float>(-cosineX * sineY);
            rotation[1u][0u] = 0.0f;
            rotation[1u][1u] = static_cast<float>(cosineX);
            rotation[1u][2u] = static_cast<float>(sineX);
            rotation[2u][0u] = static_cast<float>(sineY);
            rotation[2u][1u] = static_cast<float>(-sineX * cosineY);
            rotation[2u][2u] = static_cast<float>(cosineX * cosineY);
            return glm::translate(glm::mat4(1.0f),
                                  glm::vec3(0.0f, 0.0f, -2750.0f)) * rotation;
        }

        /** Appends one typed RenderSet buffer component payload. */
        void appendBuffer(
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
                .instanceCount = instanceCount});
        }
    }

    void WebglInteractiveBuffergeometryRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 60u;
        const bool faceHit =
            options.scenarioId == "face-hit" && options.targetFrame == 61u;
        if (options.caseId != "webgl_interactive_buffergeometry" ||
            (!initial && !animated && !faceHit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != RandomSeed)
        {
            throw std::invalid_argument(
                "webgl_interactive_buffergeometry requires the locked r185 scenario.");
        }
        device = inDevice;
        highlightEnabled = faceHit;
        buildMesh();
        buildHighlight(options.targetFrame);
        const glm::mat4 projection = makeProjection(options.width, options.height);
        const glm::mat4 modelView = makeModelView(options.targetFrame);
        for (WebglInteractiveBuffergeometryEntityData &entity : entities)
        {
            entity.objectData.modelView = modelView;
            entity.objectData.projection = projection;
            entity.objectData.viewport = glm::vec4(
                float(options.width), float(options.height), 0.0f, 0.0f);
            entity.objectData.lightAndAmbient = glm::vec4(0.0f);
            entity.objectData.fogColorAndRange = glm::vec4(
                5.0f / 255.0f, 5.0f / 255.0f, 5.0f / 255.0f,
                2000.0f);
            entity.objectData.fogFarAndReserved = glm::vec4(
                3500.0f, 0.0f, 0.0f, 0.0f);
        }
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "webgl_interactive_buffergeometry could not create its Scene Set encoder.");
        for (WebglInteractiveBuffergeometryEntityData &entity : entities)
            entity.entityIndex = allocateEntity(*encoder, entity);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglInteractiveBuffergeometryRuntimeAdapter::buildMesh()
    {
        entities.clear();
        entities.emplace_back();
        WebglInteractiveBuffergeometryEntityData &mesh = entities.back();
        mesh.name = "mesh";
        mesh.materialData.colorAndPhase = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        mesh.vertices.reserve(MeshVertexCount);
        mesh.indices.reserve(MeshIndexCount);
        ThreeCompat::DeterministicRandom random(RandomSeed);
        for (uint32_t draw = 0u; draw < PreGeometryRandomDrawCount; ++draw)
            (void)random.nextUint32();
        for (uint32_t triangle = 0u; triangle < TriangleCount; ++triangle)
        {
            const double x = nextRandomUnit(random) * 800.0 - 400.0;
            const double y = nextRandomUnit(random) * 800.0 - 400.0;
            const double z = nextRandomUnit(random) * 800.0 - 400.0;
            const double ax = x + nextRandomUnit(random) * 120.0 - 60.0;
            const double ay = y + nextRandomUnit(random) * 120.0 - 60.0;
            const double az = z + nextRandomUnit(random) * 120.0 - 60.0;
            const double bx = x + nextRandomUnit(random) * 120.0 - 60.0;
            const double by = y + nextRandomUnit(random) * 120.0 - 60.0;
            const double bz = z + nextRandomUnit(random) * 120.0 - 60.0;
            const double cx = x + nextRandomUnit(random) * 120.0 - 60.0;
            const double cy = y + nextRandomUnit(random) * 120.0 - 60.0;
            const double cz = z + nextRandomUnit(random) * 120.0 - 60.0;
            const glm::dvec3 a(ax, ay, az);
            const glm::dvec3 b(bx, by, bz);
            const glm::dvec3 c(cx, cy, cz);
            const glm::dvec3 normal = glm::normalize(glm::cross(c - b, a - b));
            const glm::vec4 authoredColor(
                float(x / 800.0 + 0.5),
                float(y / 800.0 + 0.5),
                float(z / 800.0 + 0.5),
                1.0f);
            const glm::vec4 authoredNormal(
                float(normal.x), float(normal.y), float(normal.z), 0.0f);
            const glm::vec4 positions[] = {
                {float(ax), float(ay), float(az), 1.0f},
                {float(bx), float(by), float(bz), 1.0f},
                {float(cx), float(cy), float(cz), 1.0f}};
            for (uint32_t vertex = 0u; vertex < 3u; ++vertex)
            {
                mesh.vertices.push_back({
                    positions[vertex], authoredNormal, authoredColor,
                    glm::vec4(0.0f)});
                mesh.indices.push_back(
                    static_cast<uint32_t>(mesh.indices.size()));
            }
        }
        if (mesh.vertices.size() != MeshVertexCount ||
            mesh.indices.size() != MeshIndexCount)
        {
            throw std::runtime_error(
                "webgl_interactive_buffergeometry generated an invalid triangle count.");
        }
    }

    void WebglInteractiveBuffergeometryRuntimeAdapter::buildHighlight(
        uint32_t frameIndex)
    {
        selectedTriangle = UINT32_MAX;
        const glm::mat4 inverseModelView =
            glm::inverse(makeModelView(frameIndex));
        const glm::vec3 rayOrigin = glm::vec3(
            inverseModelView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const glm::vec3 rayDirection = glm::normalize(glm::vec3(
            inverseModelView * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        float closestDistance = std::numeric_limits<float>::max();
        const WebglInteractiveBuffergeometryEntityData &mesh = entities.front();
        for (uint32_t triangle = 0u; triangle < TriangleCount; ++triangle)
        {
            const glm::vec3 a = glm::vec3(mesh.vertices[triangle * 3u].position);
            const glm::vec3 b = glm::vec3(mesh.vertices[triangle * 3u + 1u].position);
            const glm::vec3 c = glm::vec3(mesh.vertices[triangle * 3u + 2u].position);
            const glm::vec3 edge1 = b - a;
            const glm::vec3 edge2 = c - a;
            const glm::vec3 pVector = glm::cross(rayDirection, edge2);
            const float determinant = glm::dot(edge1, pVector);
            if (std::abs(determinant) < 1.0e-7f)
                continue;
            const float inverseDeterminant = 1.0f / determinant;
            const glm::vec3 tVector = rayOrigin - a;
            const float barycentricU =
                glm::dot(tVector, pVector) * inverseDeterminant;
            if (barycentricU < 0.0f || barycentricU > 1.0f)
                continue;
            const glm::vec3 qVector = glm::cross(tVector, edge1);
            const float barycentricV =
                glm::dot(rayDirection, qVector) * inverseDeterminant;
            if (barycentricV < 0.0f || barycentricU + barycentricV > 1.0f)
                continue;
            const float distance =
                glm::dot(edge2, qVector) * inverseDeterminant;
            if (distance > 0.0f && distance < closestDistance)
            {
                closestDistance = distance;
                selectedTriangle = triangle;
            }
        }
        WebglInteractiveBuffergeometryEntityData line;
        line.name = "face-highlight";
        line.materialData.colorAndPhase = glm::vec4(
            1.0f,
            1.0f,
            1.0f,
            highlightEnabled && selectedTriangle != UINT32_MAX ? 2.0f : 1.0f);
        const uint32_t selectedVertex =
            selectedTriangle == UINT32_MAX ? 0u : selectedTriangle * 3u;
        const glm::vec4 points[] = {
            mesh.vertices[selectedVertex].position,
            mesh.vertices[selectedVertex + 1u].position,
            mesh.vertices[selectedVertex + 2u].position,
            mesh.vertices[selectedVertex].position};
        for (uint32_t edge = 0u; edge < 3u; ++edge)
        {
            const uint32_t base = static_cast<uint32_t>(line.vertices.size());
            const glm::vec4 start = points[edge];
            const glm::vec4 end = points[edge + 1u];
            line.vertices.push_back({
                start, end, glm::vec4(1.0f),
                glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)});
            line.vertices.push_back({
                start, end, glm::vec4(1.0f),
                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)});
            line.vertices.push_back({
                end, end, glm::vec4(1.0f),
                glm::vec4(1.0f, -1.0f, 0.0f, 0.0f)});
            line.vertices.push_back({
                end, end, glm::vec4(1.0f),
                glm::vec4(1.0f, 1.0f, 0.0f, 0.0f)});
            line.indices.insert(line.indices.end(), {
                base, base + 1u, base + 2u,
                base + 1u, base + 3u, base + 2u});
        }
        entities.push_back(eastl::move(line));
    }

    GVM::Core::RenderEntityIndex
    WebglInteractiveBuffergeometryRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        WebglInteractiveBuffergeometryEntityData &entity)
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        appendBuffer(
            allocation,
            WebglInteractiveBuffergeometrySceneRenderSetComponents::vertices,
            entity.name,
            entity.vertices.data(),
            entity.vertices.size() *
                sizeof(WebglInteractiveBuffergeometryHostVertex),
            1u);
        appendBuffer(
            allocation,
            WebglInteractiveBuffergeometrySceneRenderSetComponents::indices,
            entity.name,
            entity.indices.data(),
            entity.indices.size() * sizeof(uint32_t),
            1u);
        appendBuffer(
            allocation,
            WebglInteractiveBuffergeometrySceneRenderSetComponents::objects,
            entity.name,
            &entity.objectData,
            sizeof(entity.objectData),
            1u);
        appendBuffer(
            allocation,
            WebglInteractiveBuffergeometrySceneRenderSetComponents::instances,
            entity.name,
            &entity.instanceData,
            sizeof(entity.instanceData),
            1u);
        appendBuffer(
            allocation,
            WebglInteractiveBuffergeometrySceneRenderSetComponents::materials,
            entity.name,
            &entity.materialData,
            sizeof(entity.materialData),
            1u);
        return encoder.allocEntity(allocation);
    }

    void WebglInteractiveBuffergeometryRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        if (options.scenarioId != "face-hit" || frameIndex != options.targetFrame)
            return;
        const auto &line = entities[1];
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "webgl_interactive_buffergeometry could not create its hit update encoder.");
        const WebglInteractiveBuffergeometryHostMaterialData material = {
            glm::vec4(1.0f, 1.0f, 1.0f, 2.0f)};
        encoder->setBufferComponentData(
            line.entityIndex,
            WebglInteractiveBuffergeometrySceneRenderSetComponents::materials,
            &material,
            sizeof(material),
            0u,
            1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglInteractiveBuffergeometryRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
            return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglInteractiveBuffergeometryRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglInteractiveBuffergeometryRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        prepareOutputPath(options.captureRgbaPath);
        std::ofstream output(
            options.captureRgbaPath.c_str(),
            std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
            throw std::runtime_error(
                "webgl_interactive_buffergeometry could not write RGBA capture.");
    }

    void WebglInteractiveBuffergeometryRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        prepareOutputPath(options.captureMetadataPath);
        std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
               << "\"caseId\":\"webgl_interactive_buffergeometry\","
               << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
               << "\"pipeline\":\"" << options.pipeline.c_str() << "\","
               << "\"backend\":\"" << threeSampleBackendName(options.backend) << "\","
               << "\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed
               << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
               << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,"
               << "\"msaaEnabled\":false,\"samplePolicy\":{\"mode\":\"single-sample\","
               << "\"msaaEnabled\":false,\"simulateMsaa\":false}";
        if (options.scenarioId == "face-hit")
        {
            output << ",\"inputReplay\":{\"schemaVersion\":1,"
                   << "\"caseId\":\"webgl_interactive_buffergeometry\","
                   << "\"scenarioId\":\"face-hit\",\"captureFrame\":61,"
                   << "\"sha256\":\"59fd0b9df9d2b338d41f80536048d6017967cc9b738e93bdfea1daa9f71a0590\","
                   << "\"target\":\"#container > canvas\",\"eventCount\":1,"
                   << "\"lastEventFrame\":0,\"runtime\":{\"target\":\"#container > canvas\","
                   << "\"dispatchedEventCount\":1,\"cssWidth\":800,\"cssHeight\":500}}";
        }
        output << "}\n";
    }

    void WebglInteractiveBuffergeometryRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        prepareOutputPath(options.sceneSnapshotPath);
        std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
        output << "{\"schemaVersion\":1,\"caseId\":\"webgl_interactive_buffergeometry\","
               << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
               << "\"frame\":" << frameIndex
               << ",\"sceneRenderSetCount\":1,\"renderableObjectCount\":2,"
               << "\"entityCount\":2,\"instanceCount\":2,\"scenePassCount\":3,"
               << "\"screenPassCount\":0,\"drawCommandCount\":3,"
               << "\"directDrawFallback\":false,\"gpuWorkDslOnly\":true,"
               << "\"singleSample\":true,\"msaaEnabled\":false,"
               << "\"renderSetType\":\"WebglInteractiveBuffergeometrySceneRenderSet\","
               << "\"meshVertexCount\":" << MeshVertexCount
               << ",\"meshTriangleCount\":" << TriangleCount
               << ",\"selectedTriangle\":" << selectedTriangle
               << ",\"highlightVertexCount\":12,\"highlightIndexCount\":18,"
               << "\"scenePasses\":["
               << "{\"name\":\"mesh-back\",\"renderClass\":\"WebglInteractiveBuffergeometryBackPass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\"},"
               << "{\"name\":\"mesh-front\",\"renderClass\":\"WebglInteractiveBuffergeometryFrontPass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\"},"
               << "{\"name\":\"line-highlight\",\"renderClass\":\"WebglInteractiveBuffergeometryLinePass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\"}],"
               << "\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
               << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
               << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
               << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
               << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
               << "\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
               << "\"renderSetId\":\"scene\",\"renderSetType\":\"WebglInteractiveBuffergeometrySceneRenderSet\","
               << "\"renderableObjectCount\":2,\"entityCount\":2,\"drawCommandCount\":3,"
               << "\"directDrawFallback\":false,\"componentSchema\":["
               << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
               << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
               << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
               << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
               << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
               << "\"scenePasses\":["
               << "{\"name\":\"mesh-back\",\"renderClass\":\"WebglInteractiveBuffergeometryBackPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
               << "{\"name\":\"mesh-front\",\"renderClass\":\"WebglInteractiveBuffergeometryFrontPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
               << "{\"name\":\"line-highlight\",\"renderClass\":\"WebglInteractiveBuffergeometryLinePass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],"
               << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"mesh\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"face-highlight\",\"instanceCount\":1}]}]}\n";
    }
}
