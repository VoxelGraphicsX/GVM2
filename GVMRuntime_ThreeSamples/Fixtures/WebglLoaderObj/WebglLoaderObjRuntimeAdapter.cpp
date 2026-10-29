#include "WebglLoaderObjRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <stdexcept>
#include <utility>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t ObjEntityCount = 14u;

        /** Converts one MTL sRGB coefficient into Three's linear working color. */
        float objSrgbToLinear(float value)
        {
            const float clamped = std::max(0.0f, std::min(1.0f, value));
            return clamped <= 0.04045f
                ? clamped / 12.92f
                : std::pow((clamped + 0.055f) / 1.055f, 2.4f);
        }
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Stores one parsed MTL material and its optional diffuse map. */
        struct ObjMaterialRecord
        {
            std::string name;
            glm::vec3 diffuseColor{1.0f};
            std::filesystem::path diffusePath;
        };

        /** Stores one OBJ group expanded into a RenderSet entity. */
        struct ObjSectionRecord
        {
            std::string name;
            std::string materialName;
            eastl::vector<WebglLoaderObjHostVertex> vertices;
            eastl::vector<uint32_t> indices;
        };

        /** Returns the non-comment portion of one OBJ/MTL line. */
        std::string trimObjComment(const std::string &line)
        {
            const size_t comment = line.find('#');
            std::string result = comment == std::string::npos
                ? line
                : line.substr(0u, comment);
            const size_t first = result.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            const size_t last = result.find_last_not_of(" \t\r\n");
            return result.substr(first, last - first + 1u);
        }

        /** Parses a three-component floating-point vector from one OBJ line. */
        glm::vec3 parseObjVec3(std::istringstream &stream, const char *fieldName)
        {
            glm::vec3 value(0.0f);
            if (!(stream >> value.x >> value.y >> value.z))
            {
                throw std::runtime_error(std::string("OBJ has malformed ") + fieldName + " record.");
            }
            return value;
        }

        /** Parses one positive or negative OBJ index into a zero-based offset. */
        uint32_t resolveObjIndex(int value, size_t count, const char *fieldName)
        {
            const int64_t resolved = value >= 0
                ? int64_t(value) - 1
                : int64_t(count) + int64_t(value);
            if (resolved < 0 || resolved >= int64_t(count))
            {
                throw std::runtime_error(std::string("OBJ ") + fieldName + " index is out of range.");
            }
            return static_cast<uint32_t>(resolved);
        }

        /** Parses one v/vt/vn face token, including the legal missing-index forms. */
        void parseObjFaceToken(const std::string &token, int &position, int &uv, int &normal)
        {
            position = uv = normal = 0;
            const size_t firstSlash = token.find('/');
            if (firstSlash == std::string::npos)
            {
                position = std::stoi(token);
                return;
            }
            position = std::stoi(token.substr(0u, firstSlash));
            const size_t secondSlash = token.find('/', firstSlash + 1u);
            if (secondSlash == std::string::npos)
            {
                const std::string uvToken = token.substr(firstSlash + 1u);
                if (!uvToken.empty()) uv = std::stoi(uvToken);
                return;
            }
            const std::string uvToken = token.substr(firstSlash + 1u, secondSlash - firstSlash - 1u);
            const std::string normalToken = token.substr(secondSlash + 1u);
            if (!uvToken.empty()) uv = std::stoi(uvToken);
            if (!normalToken.empty()) normal = std::stoi(normalToken);
        }

        /** Looks up a material by its exact OBJ usemtl spelling. */
        const ObjMaterialRecord *findObjMaterial(
            const eastl::vector<ObjMaterialRecord> &materials,
            const std::string &name)
        {
            for (const ObjMaterialRecord &material : materials)
            {
                if (material.name == name) return &material;
            }
            return nullptr;
        }

        /** Parses the pinned MTL diffuse colors and map_Kd paths. */
        eastl::vector<ObjMaterialRecord> parseObjMaterialLibrary(
            const std::filesystem::path &materialPath)
        {
            std::ifstream input(materialPath);
            if (!input) throw std::runtime_error("webgl_loader_obj could not open male02.mtl.");
            eastl::vector<ObjMaterialRecord> materials;
            ObjMaterialRecord *current = nullptr;
            std::string line;
            while (std::getline(input, line))
            {
                const std::string record = trimObjComment(line);
                if (record.empty()) continue;
                std::istringstream stream(record);
                std::string keyword;
                stream >> keyword;
                if (keyword == "newmtl")
                {
                    std::string name;
                    stream >> name;
                    materials.push_back({name, glm::vec3(1.0f), {}});
                    current = &materials.back();
                }
                else if (current != nullptr && keyword == "Kd")
                {
                    current->diffuseColor = parseObjVec3(stream, "Kd");
                }
                else if (current != nullptr && keyword == "map_Kd")
                {
                    std::string path;
                    stream >> path;
                    current->diffusePath = materialPath.parent_path() / path;
                }
            }
            if (materials.empty()) throw std::runtime_error("male02.mtl declares no materials.");
            return materials;
        }

        /** Expands the pinned OBJ into the fourteen material-separated child meshes. */
        eastl::vector<ObjSectionRecord> parseObjSections(
            const std::filesystem::path &objPath,
            const eastl::vector<ObjMaterialRecord> &materials)
        {
            std::ifstream input(objPath);
            if (!input) throw std::runtime_error("webgl_loader_obj could not open male02.obj.");
            eastl::vector<glm::vec3> positions;
            eastl::vector<glm::vec2> uvs;
            eastl::vector<glm::vec3> normals;
            eastl::vector<ObjSectionRecord> sections;
            ObjSectionRecord *current = nullptr;
            std::string line;
            uint32_t generatedSection = 0u;
            while (std::getline(input, line))
            {
                const std::string record = trimObjComment(line);
                if (record.empty()) continue;
                std::istringstream stream(record);
                std::string keyword;
                stream >> keyword;
                if (keyword == "v")
                {
                    positions.push_back(parseObjVec3(stream, "v"));
                }
                else if (keyword == "vt")
                {
                    glm::vec2 uv(0.0f);
                    if (!(stream >> uv.x >> uv.y)) throw std::runtime_error("OBJ has malformed vt record.");
                    uvs.push_back(uv);
                }
                else if (keyword == "vn")
                {
                    normals.push_back(parseObjVec3(stream, "vn"));
                }
                else if (keyword == "g" || keyword == "usemtl")
                {
                    std::string name;
                    stream >> name;
                    if (keyword == "g")
                    {
                        sections.push_back({name.empty() ? "section-" + std::to_string(generatedSection++) : name, {}, {}, {}});
                        current = &sections.back();
                    }
                    else
                    {
                        if (current == nullptr)
                        {
                            sections.push_back({"section-" + std::to_string(generatedSection++), {}, {}, {}});
                            current = &sections.back();
                        }
                        current->materialName = name;
                    }
                }
                else if (keyword == "f")
                {
                    if (current == nullptr)
                    {
                        sections.push_back({"section-" + std::to_string(generatedSection++), {}, {}, {}});
                        current = &sections.back();
                    }
                    eastl::vector<std::string> tokens;
                    std::string token;
                    while (stream >> token) tokens.push_back(token);
                    if (tokens.size() < 3u) throw std::runtime_error("OBJ face has fewer than three vertices.");
                    for (size_t fan = 1u; fan + 1u < tokens.size(); ++fan)
                    {
                        // The canvas clip-space Y inversion flips screen winding;
                        // reverse the OBJ fan so Back-face culling retains the
                        // same visible front as Three's WebGL renderer.
                        const std::string faceTokens[3u] = {tokens[0u], tokens[fan + 1u], tokens[fan]};
                        for (const std::string &faceToken : faceTokens)
                        {
                            int positionIndex = 0;
                            int uvIndex = 0;
                            int normalIndex = 0;
                            parseObjFaceToken(faceToken, positionIndex, uvIndex, normalIndex);
                            const uint32_t positionOffset = resolveObjIndex(positionIndex, positions.size(), "position");
                            glm::vec3 normal(0.0f, 1.0f, 0.0f);
                            if (normalIndex != 0) normal = normals[resolveObjIndex(normalIndex, normals.size(), "normal")];
                            glm::vec2 uv(0.0f);
                            if (uvIndex != 0) uv = uvs[resolveObjIndex(uvIndex, uvs.size(), "uv")];
                            current->vertices.push_back({glm::vec4(positions[positionOffset], 1.0f), glm::vec4(normal, 0.0f), glm::vec4(uv, 0.0f, 0.0f)});
                            current->indices.push_back(static_cast<uint32_t>(current->vertices.size() - 1u));
                        }
                    }
                }
            }
            if (sections.size() != ObjEntityCount)
            {
                throw std::runtime_error("male02.obj section count changed; expected fourteen OBJLoader children.");
            }
            for (const ObjSectionRecord &section : sections)
            {
                if (section.vertices.empty() || findObjMaterial(materials, section.materialName) == nullptr)
                {
                    throw std::runtime_error("male02.obj contains an empty group or unresolved material.");
                }
            }
            return sections;
        }

        /** Creates parent directories for one OBJ capture artifact. */
        void prepareObjPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendObjBuffer(
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

        /** Builds one material-separated cube section used by the loader fallback. */
        void buildObjFallbackMesh(
            eastl::vector<WebglLoaderObjHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const glm::vec3 positions[8u] = {
                {-0.6f, -0.6f, -0.6f}, {0.6f, -0.6f, -0.6f},
                {0.6f, 0.6f, -0.6f}, {-0.6f, 0.6f, -0.6f},
                {-0.6f, -0.6f, 0.6f}, {0.6f, -0.6f, 0.6f},
                {0.6f, 0.6f, 0.6f}, {-0.6f, 0.6f, 0.6f},
            };
            const uint32_t faces[6u][4u] = {
                {0u, 1u, 2u, 3u}, {5u, 4u, 7u, 6u},
                {4u, 0u, 3u, 7u}, {1u, 5u, 6u, 2u},
                {3u, 2u, 6u, 7u}, {4u, 5u, 1u, 0u},
            };
            const glm::vec3 normals[6u] = {
                {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
                {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            };
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                for (uint32_t corner = 0u; corner < 4u; ++corner)
                {
                    vertices.push_back({glm::vec4(positions[faces[face][corner]], 1.0f),
                                        glm::vec4(normals[face], 0.0f),
                                        glm::vec4(float(corner == 1u || corner == 2u),
                                                  float(corner >= 2u), 0.0f, 0.0f)});
                }
                indices.insert(indices.end(), {base, base + 1u, base + 2u,
                                                base, base + 2u, base + 3u});
            }
        }

        /** Returns the pinned r185 OBJ directory below an explicit asset pack root. */
        std::filesystem::path resolveObjAssetDirectory(const std::filesystem::path &assetRoot)
        {
            const std::filesystem::path direct = assetRoot / "models" / "obj" / "male02";
            if (std::filesystem::exists(direct / "male02.obj")) return direct;
            const std::filesystem::path nested = assetRoot / "examples" / "models" / "obj" / "male02";
            if (std::filesystem::exists(nested / "male02.obj")) return nested;
            throw std::runtime_error("webgl_loader_obj asset pack does not contain models/obj/male02/male02.obj.");
        }

        /** Builds explicit sRGB mip bytes for one decoded OBJ diffuse texture. */
        void prepareObjTexture(
            const std::filesystem::path &texturePath,
            eastl::vector<uint8_t> &textureBytes,
            eastl::vector<uint64_t> &mipOffsets,
            uint32_t &textureWidth,
            uint32_t &textureHeight)
        {
            const RgbaImageData base = decodeJpegRgba8(texturePath);
            const eastl::vector<RgbaImageData> mipChain = buildSrgbMipChain(base);
            if (mipChain.empty()) throw std::runtime_error("OBJ diffuse texture has no mip levels.");
            textureBytes.clear();
            mipOffsets.clear();
            for (const RgbaImageData &mip : mipChain)
            {
                mipOffsets.push_back(textureBytes.size());
                textureBytes.insert(textureBytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
            textureWidth = base.width;
            textureHeight = base.height;
        }

        /** Returns one deterministic material palette entry. */
        glm::vec4 objPaletteColor(uint32_t index)
        {
            const glm::vec3 palette[7u] = {
                {0.8f, 0.2f, 0.15f}, {0.2f, 0.55f, 0.9f}, {0.15f, 0.8f, 0.35f},
                {0.9f, 0.65f, 0.15f}, {0.65f, 0.25f, 0.8f}, {0.2f, 0.8f, 0.8f},
                {0.85f, 0.85f, 0.85f},
            };
            return glm::vec4(palette[index % 7u], 1.0f);
        }

        /** Validates the loader snapshot and orbit replay scenarios. */
        void validateObjOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool snapshot = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool orbit = options.scenarioId == "orbit-input" && options.targetFrame == 1u;
            if (options.caseId != "webgl_loader_obj" || (!initial && !snapshot && !orbit) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgl_loader_obj scenario does not match the locked r185 contract.");
            }
        }
    } // namespace

    void WebglLoaderObjRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateObjOptions(options);
        device = inDevice;
        captureWidth = options.width;
        captureHeight = options.height;
        const std::filesystem::path assetDirectory =
            resolveObjAssetDirectory(std::filesystem::path(options.assetRoot.c_str()));
        const eastl::vector<ObjMaterialRecord> materials =
            parseObjMaterialLibrary(assetDirectory / "male02.mtl");
        const eastl::vector<ObjSectionRecord> sections =
            parseObjSections(assetDirectory / "male02.obj", materials);
        assetBacked = true;
        entities.clear();
        entities.resize(ObjEntityCount);
        const glm::mat4 model =
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.95f, 0.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
        for (uint32_t entityIndex = 0u; entityIndex < ObjEntityCount; ++entityIndex)
        {
            WebglLoaderObjEntityData &entity = entities[entityIndex];
            const ObjSectionRecord &section = sections[entityIndex];
            entity.logicalId = section.name.c_str();
            entity.materialName = section.materialName.c_str();
            entity.vertices = section.vertices;
            entity.indices = section.indices;
            entity.model = model;
            const ObjMaterialRecord *material = findObjMaterial(materials, section.materialName);
            if (material == nullptr) throw std::runtime_error("OBJ section material disappeared during load.");
            entity.objectData.lightPositionAndIntensity = glm::vec4(0.0f, 0.0f, 0.0f, 15.0f);
            entity.materialData.baseColor = glm::vec4(
                objSrgbToLinear(material->diffuseColor.r),
                objSrgbToLinear(material->diffuseColor.g),
                objSrgbToLinear(material->diffuseColor.b),
                1.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.renderFlags = {0u, material->diffusePath.empty() ? 0u : 1u, 0u, 0u};
            if (!material->diffusePath.empty())
            {
                prepareObjTexture(material->diffusePath,
                                  entity.textureBytes,
                                  entity.textureMipOffsets,
                                  entity.textureWidth,
                                  entity.textureHeight);
                entity.textureName = material->diffusePath.filename().string().c_str();
            }
        }
        updateObjectData(options.width, options.height, options.targetFrame);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_loader_obj could not create its Scene Set encoder.");
        for (uint32_t entityIndex = 0u; entityIndex < ObjEntityCount; ++entityIndex)
        {
            WebglLoaderObjEntityData &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("ObjMesh-") + eastl::to_string(entityIndex);
            appendObjBuffer(allocation, WebglLoaderObjSceneRenderSetComponents::vertices,
                            prefix + "-vertices", entity.vertices.data(),
                            entity.vertices.size() * sizeof(WebglLoaderObjHostVertex), 1u);
            appendObjBuffer(allocation, WebglLoaderObjSceneRenderSetComponents::indices,
                            prefix + "-indices", entity.indices.data(),
                            entity.indices.size() * sizeof(uint32_t), 1u);
            appendObjBuffer(allocation, WebglLoaderObjSceneRenderSetComponents::objects,
                            prefix + "-object", &entity.objectData, sizeof(entity.objectData), 1u);
            appendObjBuffer(allocation, WebglLoaderObjSceneRenderSetComponents::instances,
                            prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendObjBuffer(allocation, WebglLoaderObjSceneRenderSetComponents::materials,
                            prefix + "-material", &entity.materialData, sizeof(entity.materialData), 1u);
            appendObjBuffer(allocation, WebglLoaderObjSceneRenderSetComponents::renderFlags,
                            prefix + "-flags", &entity.renderFlags, sizeof(entity.renderFlags), 1u);
            if (!entity.textureBytes.empty())
            {
                GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
                textureComponent.textureComponentHandle =
                    WebglLoaderObjSceneRenderSetComponents::textures;
                textureComponent.textures.push_back({
                    .textureName = entity.textureName,
                    .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                    .width = entity.textureWidth,
                    .height = entity.textureHeight,
                    .data = entity.textureBytes.data(),
                    .dataStorageBytes = entity.textureBytes.size(),
                    .mipmapOffsetBytes = entity.textureMipOffsets,
                });
                allocation.textureInfos.push_back(eastl::move(textureComponent));
            }
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderObjRuntimeAdapter::updateObjectData(uint32_t width, uint32_t height, uint32_t frameIndex)
    {
        const double orbit = frameIndex == 1u ? 0.005 : 0.0;
        const glm::dvec3 cameraPosition(0.0, 0.0, 2.5);
        const glm::mat4 view = glm::mat4(glm::lookAt(
            cameraPosition, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0))) *
            glm::rotate(glm::mat4(1.0f), float(orbit), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(glm::radians(45.0f),
                                                        float(width) / float(height), 0.1f, 20.0f);
        for (WebglLoaderObjEntityData &entity : entities)
        {
            entity.objectData.modelView = view * entity.model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
        }
    }

    void WebglLoaderObjRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                   const ThreeSampleHostOptions &options,
                                                   uint32_t frameIndex)
    {
        updateObjectData(options.width, options.height, frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_loader_obj could not create its update encoder.");
        for (const WebglLoaderObjEntityData &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex,
                WebglLoaderObjSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderObjRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options,
                                                        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        prepareObjPath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglLoaderObjRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                                            uint32_t frameIndex, uint32_t width,
                                                            uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        prepareObjPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_loader_obj\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false"
               << ",\"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false}"
               << ",\"singleSamplePolicy\":{\"sampleCount\":1,\"msaaEnabled\":false,\"simulateMsaa\":false}}\n";
    }

    void WebglLoaderObjRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                                               uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        prepareObjPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_loader_obj\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"scaffolded\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":" << (assetBacked ? "true" : "false") << ",\n"
               << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":14,\n  \"entityCount\":14,\n  \"instanceCount\":1,\n"
               << "  \"instanceCounts\":[1,1,1,1,1,1,1,1,1,1,1,1,1,1],\n  \"scenePassCount\":1,\n  \"screenPassCount\":1,\n"
               << "  \"drawCommandCount\":1,\n  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\",\"renderFlags\"],\n"
               << "  \"assetAndAlgorithmState\":\"male02-obj-mtl-fourteen-groups-expanded-nonindexed-with-explicit-srgb-mips\",\n"
               << "  \"renderSetType\":\"WebglLoaderObjSceneRenderSet\",\n"
               << "  \"scenePasses\":[{\"name\":\"main-opaque\",\"renderClass\":\"WebglLoaderObjScenePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetType\":\"WebglLoaderObjSceneRenderSet\",\"renderableObjectCount\":14,\"entityCount\":14,\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[\"WebglLoaderObjScenePass\"],\"entities\":[";
        for (uint32_t index = 0u; index < ObjEntityCount; ++index)
        {
            if (index != 0u) output << ',';
            output << "{\"entityId\":" << index << ",\"logicalRenderableId\":\""
                   << entities[index].logicalId.c_str()
                   << "\",\"material\":\"" << entities[index].materialName.c_str()
                   << "\",\"vertexCount\":" << entities[index].vertices.size()
                   << ",\"indexCount\":" << entities[index].indices.size()
                   << ",\"texture\":\"" << entities[index].textureName.c_str()
                   << "\",\"instanceCount\":1}";
        }
        output << "]}]}\n";
    }

    void WebglLoaderObjRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                  const ThreeSampleHostOptions &options,
                                                  uint32_t frameIndex, GVM::RHI::Texture readbackTexture,
                                                  uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("webgl_loader_obj RGBA capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_loader_obj has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLoaderObjRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer,
                                                const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
