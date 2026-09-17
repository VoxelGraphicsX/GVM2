#include "WebglLoaderGcodeRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr const char *BenchySha256 =
            "046739dbdb991016f12a905e2660364434ebd773fe63138b3fd66b6875c94215";
        constexpr const char *TestM82Sha256 =
            "b36c5f3cca7cbee6f55fff5bdd5a79d78f549a5756f2a86192d4055aad5c6d46";
        constexpr const char *TestM83Sha256 =
            "d2b75d9e03cc1bc992e88198d4e7339f2398d9dfa5f9247412ad514612c88ebe";

        static_assert(sizeof(WebglLoaderGcodeHostFloat4) == 16u);
        static_assert(sizeof(WebglLoaderGcodeHostVertex) == 48u);
        static_assert(sizeof(WebglLoaderGcodeHostObjectData) == 144u);
        static_assert(sizeof(WebglLoaderGcodeHostInstanceData) == 16u);
        static_assert(sizeof(WebglLoaderGcodeHostMaterialData) == 16u);

        /** Creates parent directories for an explicitly requested output artifact. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Converts one display-sRGB channel into Three's linear working space. */
        float srgbToLinear(float value)
        {
            return value <= 0.04045f ? value / 12.92f :
                                       std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Returns a lowercase SHA-256 digest for one immutable byte string. */
        eastl::string sha256String(const std::string &bytes)
        {
            unsigned char digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (unsigned char value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 15u]);
            }
            return result;
        }

        /** Reads a bounded text asset without applying platform newline conversion. */
        std::string readTextAsset(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open locked GCode asset: " + path.string());
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 || uint64_t(byteCount) > uint64_t(std::numeric_limits<size_t>::max()))
            {
                throw std::runtime_error("Locked GCode asset has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            std::string bytes(static_cast<size_t>(byteCount), '\0');
            input.read(bytes.data(), byteCount);
            if (!input)
            {
                throw std::runtime_error("Could not read complete locked GCode asset.");
            }
            return bytes;
        }

        /** Stores the exact mutable state fields used by Three's GCodeLoader parser. */
        struct GcodeState
        {
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            double e = 0.0;
            double f = 0.0;
            bool extruding = false;
            bool relative = false;
            bool extrusionOverride = false;
            bool extrusionRelative = false;
        };

        /** Stores the two child position streams produced by one GCodeLoader parse. */
        struct GcodeParseResult
        {
            eastl::vector<glm::vec3> extruding;
            eastl::vector<glm::vec3> travel;
        };

        /** Stores one endpoint pair for the existing native LineList rasterizer. */
        void appendLineSegment(WebglLoaderGcodeEntityState &entity,
                               const glm::vec3 &start,
                               const glm::vec3 &end,
                               float startDistance,
                               float endDistance)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(entity.vertices.size());
            const WebglLoaderGcodeHostFloat4 startValue = {start.x, start.y, start.z, 1.0f};
            const WebglLoaderGcodeHostFloat4 endValue = {end.x, end.y, end.z, 1.0f};
            entity.vertices.push_back({startValue, endValue, {0.0f, 0.0f, startDistance, endDistance}});
            entity.vertices.push_back({startValue, endValue, {1.0f, 0.0f, startDistance, endDistance}});
            const uint32_t segmentIndices[2u] = {baseVertex, baseVertex + 1u};
            entity.indices.insert(entity.indices.end(), segmentIndices, segmentIndices + 2u);
        }

        /** Parses one numeric GCode argument while preserving JavaScript parseFloat behavior. */
        void parseGcodeArgument(const std::string &token, GcodeState &line, double &x, double &y,
                                double &z, double &e, double &f, bool &hasX, bool &hasY,
                                bool &hasZ, bool &hasE, bool &hasF)
        {
            if (token.size() < 2u)
            {
                return;
            }
            const char key = static_cast<char>(std::tolower(static_cast<unsigned char>(token[0])));
            char *end = nullptr;
            const double value = std::strtod(token.c_str() + 1, &end);
            if (end == token.c_str() + 1)
            {
                return;
            }
            switch (key)
            {
                case 'x': x = value; hasX = true; break;
                case 'y': y = value; hasY = true; break;
                case 'z': z = value; hasZ = true; break;
                case 'e': e = value; hasE = true; break;
                case 'f': f = value; hasF = true; break;
                default: break;
            }
            (void)line;
        }

        /** Reproduces the loader's absolute or relative positional update. */
        double gcodeAbsolute(double current, double argument, bool relative)
        {
            return relative ? current + argument : argument;
        }

        /** Reproduces GCodeLoader's extrusion override rule. */
        double gcodeAbsoluteExtrusion(const GcodeState &state, double argument)
        {
            const bool relative = state.extrusionOverride ? state.extrusionRelative : state.relative;
            return relative ? state.e + argument : argument;
        }

        /** Reproduces GCodeLoader's delta function, including its relative-mode semantics. */
        double gcodeDelta(const GcodeState &state, double nextExtrusion)
        {
            return state.relative ? nextExtrusion : nextExtrusion - state.e;
        }

        /** Parses the frozen r185 GCodeLoader state machine into two position streams. */
        GcodeParseResult parseGcode(const std::string &data)
        {
            GcodeParseResult result;
            GcodeState state;
            std::istringstream lines(data);
            std::string sourceLine;
            while (std::getline(lines, sourceLine))
            {
                const size_t comment = sourceLine.find(';');
                if (comment != std::string::npos)
                {
                    sourceLine.resize(comment);
                }
                std::istringstream tokens(sourceLine);
                std::string command;
                tokens >> command;
                if (command.empty())
                {
                    continue;
                }
                std::transform(command.begin(), command.end(), command.begin(), [](unsigned char value) {
                    return static_cast<char>(std::toupper(value));
                });
                double x = 0.0;
                double y = 0.0;
                double z = 0.0;
                double e = 0.0;
                double f = 0.0;
                bool hasX = false;
                bool hasY = false;
                bool hasZ = false;
                bool hasE = false;
                bool hasF = false;
                std::string token;
                while (tokens >> token)
                {
                    parseGcodeArgument(token, state, x, y, z, e, f, hasX, hasY, hasZ, hasE, hasF);
                }
                if (command == "G0" || command == "G1")
                {
                    const GcodeState line = [&]() {
                        GcodeState copy = state;
                        if (hasX) copy.x = gcodeAbsolute(state.x, x, state.relative);
                        if (hasY) copy.y = gcodeAbsolute(state.y, y, state.relative);
                        if (hasZ) copy.z = gcodeAbsolute(state.z, z, state.relative);
                        if (hasE) copy.e = gcodeAbsoluteExtrusion(state, e);
                        if (hasF) copy.f = gcodeAbsolute(state.f, f, state.relative);
                        return copy;
                    }();
                    if (gcodeDelta(state, line.e) > 0.0)
                    {
                        state.extruding = true;
                    }
                    const glm::vec3 start(static_cast<float>(state.x), static_cast<float>(state.y), static_cast<float>(state.z));
                    const glm::vec3 end(static_cast<float>(line.x), static_cast<float>(line.y), static_cast<float>(line.z));
                    if (state.extruding)
                    {
                        result.extruding.push_back(start);
                        result.extruding.push_back(end);
                    }
                    else
                    {
                        result.travel.push_back(start);
                        result.travel.push_back(end);
                    }
                    state = line;
                }
                else if (command == "G90")
                {
                    state.relative = false;
                    state.extrusionOverride = false;
                }
                else if (command == "G91")
                {
                    state.relative = true;
                    state.extrusionOverride = false;
                }
                else if (command == "M82")
                {
                    state.extrusionOverride = true;
                    state.extrusionRelative = false;
                }
                else if (command == "M83")
                {
                    state.extrusionOverride = true;
                    state.extrusionRelative = true;
                }
                else if (command == "G92")
                {
                    if (hasX) state.x = x;
                    if (hasY) state.y = y;
                    if (hasZ) state.z = z;
                    if (hasE) state.e = e;
                }
            }
            return result;
        }

        /** Serializes parsed child streams into a stable canonical loader identity. */
        eastl::string canonicalGcodeSceneSha256(const GcodeParseResult &parsed)
        {
            std::ostringstream canonical;
            canonical << std::setprecision(17);
            canonical << "extruding:" << parsed.extruding.size() << ";travel:" << parsed.travel.size() << ";";
            for (const glm::vec3 &position : parsed.extruding)
            {
                canonical << position.x << ',' << position.y << ',' << position.z << ';';
            }
            canonical << '|';
            for (const glm::vec3 &position : parsed.travel)
            {
                canonical << position.x << ',' << position.y << ',' << position.z << ';';
            }
            return sha256String(canonical.str());
        }

        /** Builds the RHI-compatible perspective projection used by the Three host. */
        glm::mat4 makePerspectiveProjection(double fieldOfViewDegrees,
                                             double aspect,
                                             double nearDistance,
                                             double farDistance)
        {
            const double top = nearDistance * std::tan(fieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = aspect * height;
            const double depth = farDistance - nearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * nearDistance / width);
            projection[1u][1u] = static_cast<float>(2.0 * nearDistance / height);
            projection[2u][2u] = static_cast<float>(-farDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(-farDistance * nearDistance / depth);
            return projection;
        }

        /** Applies the same top-down host reflection used by the other Three sample adapters. */
        glm::mat4 makeHostReflection()
        {
            glm::mat4 reflection(1.0f);
            reflection[1u][1u] = -1.0f;
            return reflection;
        }

        /** Adds one typed buffer payload to a RenderSet allocation descriptor. */
        void appendBufferPayload(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Counts replay events in the small locked JSON document used by a scenario. */
        uint32_t countReplayEvents(const std::string &json)
        {
            uint32_t count = 0u;
            size_t offset = 0u;
            while ((offset = json.find("\"type\"", offset)) != std::string::npos)
            {
                ++count;
                offset += 6u;
            }
            return count;
        }
    } // namespace

    void WebglLoaderGcodeRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
        const bool canonical = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
        const bool absolute = options.scenarioId == "absolute-extrusion" && options.targetFrame == 1u;
        const bool relative = options.scenarioId == "relative-extrusion" && options.targetFrame == 1u;
        const bool orbit = options.scenarioId == "orbit" && options.targetFrame == 2u;
        if (options.caseId != "webgl_loader_gcode" ||
            (!initial && !canonical && !absolute && !relative && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
        {
            throw std::invalid_argument("GCode adapter requires one locked manifest scenario.");
        }
        if ((absolute || relative || orbit) && options.inputReplayPath.empty())
        {
            throw std::invalid_argument("GCode input scenarios require their locked replay path.");
        }
        captureWidth = options.width;
        captureHeight = options.height;
        caseId = options.caseId;
        selectedAsset = absolute ? "test_m82" : (relative ? "test_m83" : "benchy");
        selectedAssetSha256 = absolute ? TestM82Sha256 : (relative ? TestM83Sha256 : BenchySha256);
        modelTranslation = selectedAsset == "benchy" ? glm::vec3(-100.0f, -20.0f, 100.0f) : glm::vec3(0.0f);
        const std::string assetFileName = std::string(selectedAsset.c_str()) + ".gcode";
        const std::filesystem::path assetPath = std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "gcode" / assetFileName;
        const std::string data = readTextAsset(assetPath);
        if (sha256String(data) != selectedAssetSha256)
        {
            throw std::runtime_error("Locked GCode asset hash does not match the r185 source lock.");
        }
        const GcodeParseResult parsed = parseGcode(data);
        canonicalSceneSha256 = canonicalGcodeSceneSha256(parsed);
        entities.resize(2u);
        entities[0u].logicalId = "extruding";
        entities[0u].storagePrefix = "WebglLoaderGcodeExtruding";
        entities[1u].logicalId = "travel";
        entities[1u].storagePrefix = "WebglLoaderGcodeTravel";
        const glm::vec3 colors[2u] = {
            glm::vec3(srgbToLinear(0.0f), srgbToLinear(1.0f), srgbToLinear(0.0f)),
            glm::vec3(srgbToLinear(1.0f), srgbToLinear(0.0f), srgbToLinear(0.0f)),
        };
        const eastl::vector<glm::vec3> *positionStreams[2u] = {&parsed.extruding, &parsed.travel};
        for (uint32_t entityIndex = 0u; entityIndex < 2u; ++entityIndex)
        {
            const eastl::vector<glm::vec3> &positions = *positionStreams[entityIndex];
            float distance = 0.0f;
            for (size_t index = 0u; index + 1u < positions.size(); index += 2u)
            {
                const float segmentLength = glm::length(positions[index + 1u] - positions[index]);
                appendLineSegment(entities[entityIndex], positions[index], positions[index + 1u],
                                  distance, distance + segmentLength);
                distance += segmentLength;
            }
            entities[entityIndex].sourcePositionCount = positions.size();
            entities[entityIndex].materialData.baseColor = {
                colors[entityIndex].x, colors[entityIndex].y, colors[entityIndex].z, 1.0f};
        }
        totalSourcePositionCount = static_cast<uint32_t>(parsed.extruding.size() + parsed.travel.size());

        // OrbitControls applies a 30px horizontal and -15px vertical drag using
        // 2*pi*delta/clientHeight. Keep the resulting spherical camera state
        // explicit so the replay remains independent of host-side controls.
        const float orbitTheta = -2.0f * static_cast<float>(Pi) * 30.0f / 500.0f;
        const float orbitPhi = 0.5f * static_cast<float>(Pi) +
                               2.0f * static_cast<float>(Pi) * 15.0f / 500.0f;
        const glm::vec3 cameraPosition = orbit
            ? glm::vec3(70.0f * std::sin(orbitPhi) * std::sin(orbitTheta),
                        70.0f * std::cos(orbitPhi),
                        70.0f * std::sin(orbitPhi) * std::cos(orbitTheta))
            : glm::vec3(0.0f, 0.0f, 70.0f);
        const glm::mat4 view = glm::lookAt(
            glm::vec3(cameraPosition.x, -cameraPosition.y, cameraPosition.z),
            glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = makePerspectiveProjection(
            60.0, double(captureWidth) / double(captureHeight), 1.0, 1000.0);
        const glm::mat4 sourceModel = glm::translate(glm::mat4(1.0f), modelTranslation) *
                                      glm::rotate(glm::mat4(1.0f), static_cast<float>(-Pi * 0.5),
                                                  glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::mat4 model = makeHostReflection() * sourceModel;
        modelView = view * model;
        modelViewProjection = projection * modelView;
        for (WebglLoaderGcodeEntityState &entity : entities)
        {
            entity.objectData.modelViewProjection = modelViewProjection;
            entity.objectData.modelView = modelView;
            entity.objectData.viewport = {
                static_cast<float>(captureWidth), static_cast<float>(captureHeight), 0.0f, 0.0f};
            entity.instanceData.translation = {0.0f, 0.0f, 0.0f, 0.0f};
        }

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("GCode adapter could not create its Scene RenderSet encoder.");
        }
        // Keep the loader's child insertion order (extruding before travel) so depth ties
        // follow the same deterministic ordering as Three's Group traversal.
        for (WebglLoaderGcodeEntityState &entity : entities)
        {
            entity.entityIndex = allocateEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebglLoaderGcodeRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const WebglLoaderGcodeEntityState &entity) const
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        appendBufferPayload(allocation, WebglLoaderGcodeSceneRenderSetComponents::vertices,
                            entity.storagePrefix + "Vertices", entity.vertices.data(),
                            uint64_t(entity.vertices.size()) * sizeof(WebglLoaderGcodeHostVertex), 1u);
        appendBufferPayload(allocation, WebglLoaderGcodeSceneRenderSetComponents::indices,
                            entity.storagePrefix + "Indices", entity.indices.data(),
                            uint64_t(entity.indices.size()) * sizeof(uint32_t), 1u);
        appendBufferPayload(allocation, WebglLoaderGcodeSceneRenderSetComponents::objects,
                            entity.storagePrefix + "Object", &entity.objectData,
                            sizeof(entity.objectData), 1u);
        appendBufferPayload(allocation, WebglLoaderGcodeSceneRenderSetComponents::instances,
                            entity.storagePrefix + "Instance", &entity.instanceData,
                            sizeof(entity.instanceData), 1u);
        appendBufferPayload(allocation, WebglLoaderGcodeSceneRenderSetComponents::materials,
                            entity.storagePrefix + "Material", &entity.materialData,
                            sizeof(entity.materialData), 1u);
        return encoder.allocEntity(allocation);
    }

    void WebglLoaderGcodeRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderGcodeRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("GCode RGBA8 capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeSemanticSnapshot(options);
        captureWritten = true;
    }

    void WebglLoaderGcodeRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglLoaderGcodeRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open GCode RGBA output path.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete GCode RGBA capture.");
        }
    }

    void WebglLoaderGcodeRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        uint32_t replayEventCount = 0u;
        eastl::string replaySha256;
        if (!options.inputReplayPath.empty())
        {
            const std::string replay = readTextAsset(std::filesystem::path(options.inputReplayPath.c_str()));
            replayEventCount = countReplayEvents(replay);
            replaySha256 = sha256String(replay);
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open GCode metadata output path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"" << caseId.c_str() << "\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"asset\": \"models/gcode/" << selectedAsset.c_str() << ".gcode\",\n"
               << "  \"assetSha256\": \"" << selectedAssetSha256.c_str() << "\"";
        if (!options.inputReplayPath.empty())
        {
            output << ",\n  \"inputReplay\": {\"sha256\": \"" << replaySha256.c_str()
                   << "\", \"caseId\": \"webgl_loader_gcode\", \"scenarioId\": \""
                   << options.scenarioId.c_str() << "\", \"captureFrame\": " << options.targetFrame
                   << ", \"target\": \"canvas\", \"eventCount\": " << replayEventCount << "}";
        }
        output << "\n}\n";
    }

    void WebglLoaderGcodeRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open GCode structural snapshot path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_loader_gcode\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": 2,\n"
               << "  \"entityCount\": 2,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsHierarchy\": true,\n"
               << "  \"containsDynamicObjects\": true,\n"
               << "  \"containsMultipleMaterials\": true,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"triangleListLineExpansion\": true,\n"
               << "  \"totalSourcePositionCount\": " << totalSourcePositionCount << ",\n"
               << "  \"sceneRoots\": [{\n"
               << "    \"id\": \"scene\",\n"
               << "    \"renderSetCount\": 1,\n"
               << "    \"renderSetId\": \"scene\",\n"
               << "    \"renderSetType\": \"WebglLoaderGcodeSceneRenderSet\",\n"
               << "    \"renderableObjectCount\": 2,\n"
               << "    \"entityCount\": 2,\n"
               << "    \"drawCommandCount\": 1,\n"
               << "    \"directDrawFallback\": false,\n"
               << "    \"componentSchema\": [{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
               << "    \"scenePasses\": [{\"name\":\"expanded-toolpaths\",\"renderClass\":\"WebglLoaderGcodeToolpathPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "    \"entities\": [\n"
               << "      {\"entityId\": " << entities[0].entityIndex << ", \"logicalRenderableId\": \"extruding\", \"instanceCount\": 1, \"sourcePositionCount\": " << entities[0].sourcePositionCount << "},\n"
               << "      {\"entityId\": " << entities[1].entityIndex << ", \"logicalRenderableId\": \"travel\", \"instanceCount\": 1, \"sourcePositionCount\": " << entities[1].sourcePositionCount << "}\n"
               << "    ]\n"
               << "  }]\n"
               << "}\n";
    }

    void WebglLoaderGcodeRuntimeAdapter::writeSemanticSnapshot(
        const ThreeSampleHostOptions &options) const
    {
        if (options.semanticSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.semanticSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open GCode semantic snapshot path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_loader_gcode\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << options.targetFrame << ",\n"
               << "  \"kind\": \"loader-snapshot\",\n"
               << "  \"canonicalState\": \"" << options.canonicalStatePath.c_str() << "\",\n"
               << "  \"result\": {\n"
               << "    \"renderableObjectCount\": 2,\n"
               << "    \"sceneRootCount\": 1,\n"
               << "    \"canonicalSceneSha256\": \"" << canonicalSceneSha256.c_str() << "\",\n"
               << "    \"assetPath\": \"models/gcode/" << selectedAsset.c_str() << ".gcode\",\n"
               << "    \"assetSha256\": \"" << selectedAssetSha256.c_str() << "\",\n"
               << "    \"extrudingPositionCount\": " << entities[0].sourcePositionCount << ",\n"
               << "    \"travelPositionCount\": " << entities[1].sourcePositionCount << "\n"
               << "  }\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
