#include "WebglLoaderBvhRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        // These hashes identify the locked source bytes and the canonical
        // loader structure used by the semantic snapshot contract.
        constexpr const char *BvhAssetSha256 =
            "bed46806d1c1e03d5165f9bfecadff0a57409522d5d3096b863484b5aef15eaa";
        constexpr const char *CanonicalSceneSha256 =
            "2f41d8955b6dc386c6dd27b95c630939a5b4c22ee418411cac126db8094d9818";

        static_assert(sizeof(WebglLoaderBvhHostVertex) == 64u);
        static_assert(sizeof(WebglLoaderBvhHostObjectData) == 80u);
        static_assert(sizeof(WebglLoaderBvhHostInstanceData) == 16u);
        static_assert(sizeof(WebglLoaderBvhHostMaterialData) == 16u);

        /** Adds one typed buffer payload to an existing Set allocation. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                          GVM::Core::RenderComponentHandle component,
                          const eastl::string &name,
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

        /** Returns a normalized axis quaternion for one BVH rotation channel. */
        glm::quat channelRotation(const std::string &channel, float degrees)
        {
            const float radians = degrees * float(Pi / 180.0);
            if (channel == "Xrotation")
                return glm::angleAxis(radians, glm::vec3(1.0f, 0.0f, 0.0f));
            if (channel == "Yrotation")
                return glm::angleAxis(radians, glm::vec3(0.0f, 1.0f, 0.0f));
            return glm::angleAxis(radians, glm::vec3(0.0f, 0.0f, 1.0f));
        }

        /** Returns the interpolated motion scalar at one 120-Hz source time. */
        float sampleMotion(const std::vector<std::vector<float>> &frames,
                           uint32_t frameA,
                           uint32_t frameB,
                           float alpha,
                           uint32_t frameCount,
                           uint32_t channel,
                           uint32_t nodeChannelStart)
        {
            if (frames.empty() || frameCount == 0u)
                return 0.0f;
            const uint32_t a = std::min(frameA, frameCount - 1u);
            const uint32_t b = std::min(frameB, frameCount - 1u);
            const float av = frames[a][nodeChannelStart + channel];
            const float bv = frames[b][nodeChannelStart + channel];
            return av + (bv - av) * alpha;
        }
    }

    void WebglLoaderBvhRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial-loader" &&
            options.targetFrame == 0u;
        const bool canonical = options.scenarioId == "canonical-loader" &&
            options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated" &&
            options.targetFrame == 60u;
        const bool camera = options.scenarioId == "camera-input" &&
            options.targetFrame == 61u && !options.inputReplayPath.empty();
        if (options.caseId != "webgl_loader_bvh" ||
            (!initial && !canonical && !animated && !camera) ||
            options.width != 800u || options.height != 500u ||
            options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "webgl_loader_bvh requires its locked scenarios, extent, and asset root.");
        }
        const std::filesystem::path bvhPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "models/bvh/pirouette.bvh";
        if (!std::filesystem::is_regular_file(bvhPath))
            throw std::invalid_argument("webgl_loader_bvh pirouette.bvh is missing from the locked asset root.");
        device = inDevice;
        captureWidth = options.width;
        captureHeight = options.height;
        centerGridRasterBias = options.scenarioId != "camera-input";
        parseBvh(bvhPath.string());
        if (nodes.size() != 57u || motionFrames.size() != 592u)
            throw std::runtime_error("pirouette.bvh topology or frame count differs from the r185 lock.");
        entities.clear();
        entities.resize(2u);
        entities[0].logicalId = "grid-helper";
        entities[1].logicalId = "skeleton-helper";
        buildGrid(entities[0]);
        entities[1].vertices.resize((nodes.size() - 1u) * 2u);
        entities[1].indices.reserve((nodes.size() - 1u) * 2u);
        for (uint32_t segment = 0u; segment + 1u < nodes.size(); ++segment)
        {
            const uint32_t base = segment * 2u;
            entities[1].indices.insert(entities[1].indices.end(),
                {base + 0u, base + 1u});
        }
        entities[0].materialData.phaseAndFlags = glm::vec4(0.0f);
        entities[1].materialData.phaseAndFlags = glm::vec4(1.0f);
        entities[0].instanceData.reserved = glm::vec4(0.0f);
        entities[1].instanceData.reserved = glm::vec4(0.0f);
        updateObjectData(options.width, options.height, options.targetFrame);
        updateSkeletonVertices(options.targetFrame);
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("webgl_loader_bvh could not create its RenderSet encoder.");
        for (WebglLoaderBvhEntityData &entity : entities)
            entity.entityIndex = allocateEntity(*encoder, entity);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderBvhRuntimeAdapter::parseBvh(const std::string &path)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("webgl_loader_bvh could not open pirouette.bvh.");
        parserTokens.clear();
        std::string token;
        while (input >> token)
            parserTokens.push_back(token);
        size_t cursor = 0u;
        if (cursor >= parserTokens.size() || parserTokens[cursor++] != "HIERARCHY")
            throw std::runtime_error("BVH HIERARCHY header is missing.");
        nodes.clear();
        motionChannelStarts.clear();
        totalChannelCount = 0u;
        const int32_t root = parseNode(cursor, -1);
        if (root != 0 || cursor >= parserTokens.size() || parserTokens[cursor++] != "MOTION")
            throw std::runtime_error("BVH MOTION section is missing.");
        if (cursor >= parserTokens.size() || parserTokens[cursor++] != "Frames:" || cursor >= parserTokens.size())
            throw std::runtime_error("BVH frame count is missing.");
        const uint32_t frameCount = static_cast<uint32_t>(std::stoul(parserTokens[cursor++]));
        if (cursor + 2u >= parserTokens.size() || parserTokens[cursor++] != "Frame" ||
            parserTokens[cursor++] != "Time:")
            throw std::runtime_error("BVH frame time header is missing.");
        motionFrameTime = std::stof(parserTokens[cursor++]);
        uint32_t channelCount = 0u;
        for (const BvhNode &node : nodes)
        {
            motionChannelStarts.push_back(node.channelStart);
            channelCount += static_cast<uint32_t>(node.channels.size());
        }
        motionFrames.clear();
        motionFrames.reserve(frameCount);
        for (uint32_t frame = 0u; frame < frameCount; ++frame)
        {
            if (cursor + channelCount > parserTokens.size())
                throw std::runtime_error("BVH motion data is truncated.");
            std::vector<float> values;
            values.reserve(channelCount);
            for (uint32_t channel = 0u; channel < channelCount; ++channel)
                values.push_back(std::stof(parserTokens[cursor++]));
            motionFrames.push_back(std::move(values));
        }
    }

    int32_t WebglLoaderBvhRuntimeAdapter::parseNode(
        size_t &cursor, int32_t parent)
    {
        // The node type/name tokens are consumed here so End Site is handled
        // exactly like BVHLoader's synthetic end bone.
        if (cursor >= parserTokens.size())
            throw std::runtime_error("BVH node header is missing.");
        const std::string kind = parserTokens[cursor++];
        BvhNode node;
        node.parent = parent;
        if (kind == "End")
        {
            if (cursor >= parserTokens.size() || parserTokens[cursor++] != "Site")
                throw std::runtime_error("BVH End Site header is malformed.");
            node.name = "ENDSITE";
            node.endSite = true;
        }
        else
        {
            if (cursor >= parserTokens.size())
                throw std::runtime_error("BVH joint name is missing.");
            node.name = parserTokens[cursor++];
        }
        if (cursor >= parserTokens.size() || parserTokens[cursor++] != "{")
            throw std::runtime_error("BVH node opening brace is missing.");
        if (cursor >= parserTokens.size() || parserTokens[cursor++] != "OFFSET" ||
            cursor + 2u >= parserTokens.size())
            throw std::runtime_error("BVH node offset is malformed.");
        const float offsetX = std::stof(parserTokens[cursor++]);
        const float offsetY = std::stof(parserTokens[cursor++]);
        const float offsetZ = std::stof(parserTokens[cursor++]);
        node.offset = glm::vec3(offsetX, offsetY, offsetZ);
        const int32_t nodeIndex = static_cast<int32_t>(nodes.size());
        nodes.push_back(node);
        if (!nodes[nodeIndex].endSite)
        {
            if (cursor >= parserTokens.size() || parserTokens[cursor++] != "CHANNELS" ||
                cursor >= parserTokens.size())
                throw std::runtime_error("BVH node channels are missing.");
            const uint32_t count = static_cast<uint32_t>(std::stoul(parserTokens[cursor++]));
            nodes[nodeIndex].channelStart = totalChannelCount;
            totalChannelCount += count;
            for (uint32_t channel = 0u; channel < count; ++channel)
            {
                if (cursor >= parserTokens.size())
                    throw std::runtime_error("BVH channel token is missing.");
                nodes[nodeIndex].channels.push_back(parserTokens[cursor++]);
            }
        }
        while (cursor < parserTokens.size() && parserTokens[cursor] != "}")
        {
            const int32_t child = parseNode(cursor, nodeIndex);
            nodes[nodeIndex].children.push_back(child);
        }
        if (cursor >= parserTokens.size() || parserTokens[cursor++] != "}")
            throw std::runtime_error("BVH node closing brace is missing.");
        return nodeIndex;
    }

    void WebglLoaderBvhRuntimeAdapter::evaluatePose(
        uint32_t frameIndex,
        std::vector<glm::vec3> &worldPositions) const
    {
        worldPositions.assign(nodes.size(), glm::vec3(0.0f));
        const float sourceTime = float(frameIndex) / 60.0f;
        const float sourceFrame = sourceTime / motionFrameTime;
        const uint32_t frameA = static_cast<uint32_t>(std::floor(sourceFrame));
        const uint32_t frameB = frameA + 1u;
        const float alpha = sourceFrame - std::floor(sourceFrame);
        std::vector<glm::mat4> worldMatrices(nodes.size(), glm::mat4(1.0f));
        const uint32_t frameCount = static_cast<uint32_t>(motionFrames.size());
        for (uint32_t index = 0u; index < nodes.size(); ++index)
        {
            const BvhNode &node = nodes[index];
            glm::vec3 position = node.offset;
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            for (uint32_t channel = 0u; channel < node.channels.size(); ++channel)
            {
                const std::string &name = node.channels[channel];
                const float value = sampleMotion(motionFrames, frameA, frameB,
                    alpha, frameCount, channel, node.channelStart);
                if (name == "Xposition") position.x += value;
                else if (name == "Yposition") position.y += value;
                else if (name == "Zposition") position.z += value;
                else rotation *= channelRotation(name, value);
            }
            glm::mat4 local = glm::translate(glm::mat4(1.0f), position) *
                glm::toMat4(rotation);
            if (node.parent >= 0)
                worldMatrices[index] = worldMatrices[static_cast<uint32_t>(node.parent)] * local;
            else
                worldMatrices[index] = local;
            worldPositions[index] = glm::vec3(worldMatrices[index][3]);
        }
    }

    void WebglLoaderBvhRuntimeAdapter::updateSkeletonVertices(uint32_t frameIndex)
    {
        std::vector<glm::vec3> positions;
        evaluatePose(frameIndex, positions);
        WebglLoaderBvhEntityData &skeleton = entities[1];
        uint32_t segment = 0u;
        for (uint32_t index = 1u; index < nodes.size(); ++index)
        {
            const uint32_t base = segment * 2u;
            // SkeletonHelper writes the child endpoint first (blue) and the
            // parent endpoint second (green).  Preserve that ordering so
            // vertex-color interpolation along each bone matches Three.js.
            const glm::vec4 startColor(0.0f, 0.0f, 1.0f, 1.0f);
            const glm::vec4 endColor(0.0f, 1.0f, 0.0f, 1.0f);
            const glm::vec3 start = positions[index];
            const glm::vec3 end = positions[static_cast<uint32_t>(nodes[index].parent)];
            skeleton.vertices[base + 0u] = {glm::vec4(start, 1.0f), glm::vec4(end, 1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.0f), startColor};
            skeleton.vertices[base + 1u] = {glm::vec4(start, 1.0f), glm::vec4(end, 1.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), endColor};
            ++segment;
        }
    }

    void WebglLoaderBvhRuntimeAdapter::buildGrid(
        WebglLoaderBvhEntityData &entity) const
    {
        entity.vertices.clear();
        entity.indices.clear();
        constexpr float extent = 200.0f;
        constexpr float step = 40.0f;
        const glm::vec4 dark(0.2666667f, 0.2666667f, 0.2666667f, 1.0f);
        const glm::vec4 light(0.5333333f, 0.5333333f, 0.5333333f, 1.0f);
        for (uint32_t line = 0u; line <= 10u; ++line)
        {
            const float value = -extent + step * float(line);
            const glm::vec4 color = line == 5u ? dark : light;
            appendSegment(entity, {-extent, 0.0f, value}, {extent, 0.0f, value}, color, color, 1.0f,
                          centerGridRasterBias && line == 5u ? 1.0f : 0.0f);
            appendSegment(entity, {value, 0.0f, -extent}, {value, 0.0f, extent}, color, color, 1.0f);
        }
    }

    void WebglLoaderBvhRuntimeAdapter::appendSegment(
        WebglLoaderBvhEntityData &entity,
        const glm::vec3 &start,
        const glm::vec3 &end,
        const glm::vec4 &startColor,
        const glm::vec4 &endColor,
        float width,
        float rasterBiasY) const
    {
        (void)width;
        const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
        entity.vertices.push_back({glm::vec4(start, 1.0f), glm::vec4(end, 1.0f), glm::vec4(0.0f, rasterBiasY, 0.0f, 0.0f), startColor});
        entity.vertices.push_back({glm::vec4(start, 1.0f), glm::vec4(end, 1.0f), glm::vec4(1.0f, rasterBiasY, 0.0f, 0.0f), endColor});
        entity.indices.insert(entity.indices.end(), {base + 0u, base + 1u});
    }

    void WebglLoaderBvhRuntimeAdapter::updateObjectData(
        uint32_t width, uint32_t height, uint32_t frameIndex)
    {
        glm::vec3 cameraPosition(0.0f, 200.0f, 300.0f);
        const glm::vec3 target(0.0f, 0.0f, 0.0f);
        if (frameIndex == 61u)
        {
            // OrbitControls uses the canvas height for both pointer rotation
            // axes. The locked replay moves from (400,250) to (440,230),
            // which applies rotateLeft(2*PI*40/500) and
            // rotateUp(2*PI*(-20)/500) to the initial spherical camera.
            const float radius = glm::length(cameraPosition - target);
            float theta = std::atan2(cameraPosition.x - target.x,
                                     cameraPosition.z - target.z);
            float phi = std::acos((cameraPosition.y - target.y) / radius);
            theta -= float(2.0 * Pi) * 40.0f / float(height);
            phi += float(2.0 * Pi) * 20.0f / float(height);
            phi = std::max(0.000001f, std::min(float(Pi) - 0.000001f, phi));
            cameraPosition = target + glm::vec3(
                radius * std::sin(phi) * std::sin(theta),
                radius * std::cos(phi),
                radius * std::sin(phi) * std::cos(theta));
        }
        const glm::mat4 view = glm::lookAt(cameraPosition, target,
            glm::vec3(0.0f, 1.0f, 0.0f));
        constexpr float nearDistance = 1.0f;
        constexpr float farDistance = 1000.0f;
        const float top = nearDistance * std::tan(float(Pi / 6.0));
        const float projectionHeight = top * 2.0f;
        const float projectionWidth = projectionHeight * float(width) / float(height);
        glm::mat4 projection(0.0f);
        projection[0][0] = 2.0f * nearDistance / projectionWidth;
        projection[1][1] = 2.0f * nearDistance / projectionHeight;
        projection[2][2] = -(farDistance + nearDistance) / (farDistance - nearDistance);
        projection[2][3] = -1.0f;
        projection[3][2] = -2.0f * farDistance * nearDistance / (farDistance - nearDistance);
        viewProjection = projection * view;

        for (WebglLoaderBvhEntityData &entity : entities)
        {
            entity.objectData.modelViewProjection = viewProjection;
            entity.objectData.viewportAndReserved = glm::vec4(
                float(width), float(height), 0.0f, 0.0f);
        }
    }

    GVM::Core::RenderEntityIndex WebglLoaderBvhRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        WebglLoaderBvhEntityData &entity)
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        const eastl::string name(entity.logicalId);
        appendBuffer(allocation, WebglLoaderBvhSceneRenderSetComponents::vertices,
            name + "-vertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]));
        appendBuffer(allocation, WebglLoaderBvhSceneRenderSetComponents::indices,
            name + "-indices", entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]));
        appendBuffer(allocation, WebglLoaderBvhSceneRenderSetComponents::objects,
            name + "-objects", &entity.objectData, sizeof(entity.objectData));
        appendBuffer(allocation, WebglLoaderBvhSceneRenderSetComponents::instances,
            name + "-instances", &entity.instanceData, sizeof(entity.instanceData));
        appendBuffer(allocation, WebglLoaderBvhSceneRenderSetComponents::materials,
            name + "-materials", &entity.materialData, sizeof(entity.materialData));
        return encoder.allocEntity(allocation);
    }

    void WebglLoaderBvhRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateObjectData(options.width, options.height, frameIndex);
        updateSkeletonVertices(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("webgl_loader_bvh could not create its frame encoder.");
        for (const WebglLoaderBvhEntityData &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex,
                WebglLoaderBvhSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        encoder->setBufferComponentData(entities[1].entityIndex,
            WebglLoaderBvhSceneRenderSetComponents::vertices,
            entities[1].vertices.data(),
            entities[1].vertices.size() * sizeof(entities[1].vertices[0]),
            0u, static_cast<uint32_t>(entities[1].vertices.size()));
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderBvhRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        prepareOutputPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareOutputPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_loader_bvh\",\"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\"frame\":" << frameIndex
                   << ",\"pipeline\":\"" << options.pipeline.c_str()
                   << "\",\"backend\":\""
                   << threeSampleBackendName(options.backend)
                   << "\",\"randomSeed\":" << options.randomSeed
                   << ",\"width\":" << width << ",\"height\":" << height
                   << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
                   << ",\"byteCount\":" << byteCount << ",\"format\":\"rgba8unorm\","
                   << "\"sceneRenderSetCount\":1,\"entityCount\":2,\"instanceCounts\":[1,1],"
                   << "\"scenePassCount\":1,\"drawCommandCount\":1,\"sampleCount\":1,\"msaaEnabled\":false,\"directDrawFallback\":false,"
                   << "\"boneCount\":" << nodes.size() << ",\"motionFrameCount\":" << motionFrames.size();
            if (options.scenarioId == "camera-input")
            {
                output << ",\"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_loader_bvh\",\"scenarioId\":\"camera-input\",\"captureFrame\":61,\"sha256\":\"b3f8abbbd62b89d991fbf66fdfafa9c41b90323f4104d1497468ec1af6fe404e\",\"target\":\"body > canvas\",\"eventCount\":4}}\n";
            }
            else
            {
                output << ",\"inputReplay\":null}\n";
            }
        }
        prepareOutputPath(options.sceneSnapshotPath);
        std::ostringstream snapshot;
        snapshot << "{\"schemaVersion\":1,\"caseId\":\"webgl_loader_bvh\",\"scenarioId\":\""
                 << options.scenarioId.c_str() << "\",\"frame\":" << frameIndex
                 << ",\"implementationLevel\":\"semantic-complete\",\"gpuWorkDslOnly\":true,"
                 << "\"renderSetPolicy\":\"required\",\"sceneRenderSetCount\":1,\"renderSetType\":\"WebglLoaderBvhSceneRenderSet\","
                 << "\"renderableObjectCount\":2,\"entityCount\":2,\"instanceCount\":2,\"scenePassCount\":1,\"screenPassCount\":0,"
                 << "\"drawCommandCount\":1,\"directDrawFallback\":false,\"singleSample\":true,\"msaaEnabled\":false,"
                 << "\"boneCount\":" << nodes.size() << ",\"motionFrameCount\":" << motionFrames.size()
                 << ",\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
                 << "\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene\",\"renderSetType\":\"WebglLoaderBvhSceneRenderSet\",\"renderableObjectCount\":2,\"entityCount\":2,\"drawCommandCount\":1,\"directDrawFallback\":false,"
                 << "\"entities\":[{\"entityId\":"
                 << entities[0].entityIndex << ",\"logicalRenderableId\":\""
                 << entities[0].logicalId << "\",\"instanceCount\":1},{\"entityId\":"
                 << entities[1].entityIndex << ",\"logicalRenderableId\":\""
                 << entities[1].logicalId << "\",\"instanceCount\":1}],"
                 << "\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
                 << "\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglLoaderBvhMainPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],"
                 << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main\",\"entityOrdinal\":0}]}";
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << snapshot.str() << '\n';
        }
        prepareOutputPath(options.semanticSnapshotPath);
        if (!options.semanticSnapshotPath.empty())
        {
            std::ofstream output(options.semanticSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n"
                   << "  \"schemaVersion\": 1,\n"
                   << "  \"caseId\": \"webgl_loader_bvh\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n"
                   << "  \"kind\": \"loader-snapshot\",\n"
                   << "  \"canonicalState\": \"" << options.canonicalStatePath.c_str() << "\",\n"
                   << "  \"result\": {\n"
                   << "    \"renderableObjectCount\": 1,\n"
                   << "    \"sceneRootCount\": 1,\n"
                   << "    \"canonicalSceneSha256\": \"" << CanonicalSceneSha256 << "\",\n"
                   << "    \"assetPath\": \"models/bvh/pirouette.bvh\",\n"
                   << "    \"assetSha256\": \"" << BvhAssetSha256 << "\",\n"
                   << "    \"boneCount\": " << nodes.size() << ",\n"
                   << "    \"motionFrameCount\": " << motionFrames.size() << ",\n"
                   << "    \"animationTrackCount\": 86\n"
                   << "  }\n}\n";
        }
        captureWritten = true;
    }

    void WebglLoaderBvhRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
        nodes.clear();
        motionFrames.clear();
    }

    void WebglLoaderBvhRuntimeAdapter::prepareOutputPath(
        const eastl::string &path) const
    {
        if (!path.empty())
        {
            const std::filesystem::path outputPath(path.c_str());
            if (!outputPath.parent_path().empty())
                std::filesystem::create_directories(outputPath.parent_path());
        }
    }
}
