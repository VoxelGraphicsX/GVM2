#include "ColladaAsset.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <EASTL/unordered_map.h>

#include <glm/gtc/matrix_transform.hpp>

#include <pugixml.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *ElfDaeSha256 =
            "14ce50b81a4da5798a7971063c57a6421fcffe0236f64c2ab6b50de5a6d5ccd5";
        constexpr const char *RobotDaeSha256 =
            "d016d827423984a2b8f3d0052074304d4ae3f0b0e69663c3c7d9a9523b6317d6";
        constexpr uint32_t ElfVertexCount = 42624u;

        /** Stores one typed COLLADA float source and its accessor stride. */
        struct ColladaFloatSource final
        {
            eastl::vector<float> values;
            uint32_t stride = 1u;
        };

        /** Stores one primitive input resolved to a source and tuple offset. */
        struct ColladaPrimitiveInput final
        {
            eastl::string semantic;
            eastl::string source;
            uint32_t offset = 0u;
        };

        /** Reads one complete bounded COLLADA document. */
        eastl::vector<uint8_t> readColladaBytes(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open the pinned Elf DAE.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) > uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error("Pinned Elf DAE has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read the complete Elf DAE.");
            return bytes;
        }

        /** Calculates one lowercase SHA-256 string for bounded bytes. */
        eastl::string calculateColladaSha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char Digits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(Digits[value >> 4u]);
                result.push_back(Digits[value & 15u]);
            }
            return result;
        }

        /** Removes one COLLADA fragment marker from a local source reference. */
        eastl::string colladaReferenceId(const char *value)
        {
            if (value == nullptr || value[0] == '\0') return {};
            return value[0] == '#' ? eastl::string(value + 1) : eastl::string(value);
        }

        /** Parses a whitespace-delimited float list with finite-value validation. */
        eastl::vector<float> parseColladaFloats(const char *text)
        {
            std::istringstream input(text == nullptr ? "" : text);
            eastl::vector<float> values;
            double value = 0.0;
            while (input >> value)
            {
                if (!std::isfinite(value))
                    throw std::runtime_error("COLLADA float source contains a non-finite value.");
                values.push_back(static_cast<float>(value));
            }
            return values;
        }

        /** Parses a whitespace-delimited unsigned integer list. */
        eastl::vector<uint32_t> parseColladaIndices(const char *text)
        {
            std::istringstream input(text == nullptr ? "" : text);
            eastl::vector<uint32_t> values;
            uint64_t value = 0u;
            while (input >> value)
            {
                if (value > std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("COLLADA index exceeds the supported range.");
                values.push_back(static_cast<uint32_t>(value));
            }
            return values;
        }

        /** Finds one direct child with an exact unprefixed COLLADA element name. */
        pugi::xml_node findColladaChild(pugi::xml_node parent, const char *name)
        {
            for (pugi::xml_node child : parent.children())
            {
                if (std::strcmp(child.name(), name) == 0) return child;
            }
            return {};
        }

        /** Finds one descendant carrying an exact id attribute. */
        pugi::xml_node findColladaId(pugi::xml_node root, const eastl::string &id)
        {
            for (pugi::xml_node child : root.children())
            {
                if (id == child.attribute("id").value()) return child;
                const pugi::xml_node nested = findColladaId(child, id);
                if (nested) return nested;
            }
            return {};
        }

        /** Reads one source tuple component after validating its bounds. */
        float colladaSourceValue(
            const ColladaFloatSource &source,
            uint32_t tupleIndex,
            uint32_t component)
        {
            const uint64_t index = uint64_t(tupleIndex) * source.stride + component;
            if (component >= source.stride || index >= source.values.size())
                throw std::runtime_error("COLLADA primitive references an invalid source tuple.");
            return source.values[static_cast<size_t>(index)];
        }

        /** Resolves one primitive corner into a fully expanded vertex. */
        ColladaAssetVertex makeColladaVertex(
            const eastl::unordered_map<eastl::string, ColladaFloatSource> &sources,
            const eastl::vector<ColladaPrimitiveInput> &inputs,
            const eastl::vector<uint32_t> &primitiveIndices,
            uint32_t tupleWidth,
            uint32_t corner,
            uint32_t materialIndex)
        {
            ColladaAssetVertex vertex;
            vertex.materialIndex = materialIndex;
            for (const ColladaPrimitiveInput &input : inputs)
            {
                const uint64_t index = uint64_t(corner) * tupleWidth + input.offset;
                if (index >= primitiveIndices.size())
                    throw std::runtime_error("COLLADA primitive tuple is truncated.");
                const auto source = sources.find(input.source);
                if (source == sources.end())
                    throw std::runtime_error("COLLADA primitive source is missing.");
                const uint32_t tupleIndex = primitiveIndices[static_cast<size_t>(index)];
                if (input.semantic == "POSITION")
                {
                    vertex.position = glm::vec3(
                        colladaSourceValue(source->second, tupleIndex, 0u),
                        colladaSourceValue(source->second, tupleIndex, 1u),
                        colladaSourceValue(source->second, tupleIndex, 2u));
                }
                else if (input.semantic == "NORMAL")
                {
                    vertex.normal = glm::vec3(
                        colladaSourceValue(source->second, tupleIndex, 0u),
                        colladaSourceValue(source->second, tupleIndex, 1u),
                        colladaSourceValue(source->second, tupleIndex, 2u));
                }
                else if (input.semantic == "TEXCOORD")
                {
                    vertex.uv = glm::vec2(
                        colladaSourceValue(source->second, tupleIndex, 0u),
                        colladaSourceValue(source->second, tupleIndex, 1u));
                }
            }
            return vertex;
        }

        /** Parses the fixed four Elf materials in geometry-group order. */
        eastl::vector<ColladaAssetMaterial> parseElfMaterials(pugi::xml_node root)
        {
            constexpr const char *MaterialIds[] = {
                "pasted__lambert2SG_001",
                "lambert22SG_001",
                "Rig2_lambert23SG_001",
                "lambert25SG_001",
            };
            constexpr const char *TextureFiles[] = {
                "ce.jpg",
                "Body_tex_003.jpg",
                "Face_tex_002_toObj.jpg",
                "Hair_tex_001.jpg",
            };
            constexpr float Specular[] = {
                0.003935939502123245f,
                0.01002282557165656f,
                0.003935939502123245f,
                0.003935939502123245f,
            };
            eastl::vector<ColladaAssetMaterial> materials;
            for (uint32_t index = 0u; index < 4u; ++index)
            {
                const eastl::string materialId =
                    eastl::string(MaterialIds[index]) + "-material";
                if (!findColladaId(root, materialId))
                    throw std::runtime_error("Pinned Elf material binding is missing.");
                ColladaAssetMaterial material;
                material.name = MaterialIds[index];
                material.textureFile = TextureFiles[index];
                material.specular = glm::vec3(Specular[index]);
                material.shininess = 50.0f;
                materials.push_back(material);
            }
            return materials;
        }
    } // namespace

    ColladaElfAsset loadColladaElfAsset(const std::filesystem::path &path)
    {
        const eastl::vector<uint8_t> bytes = readColladaBytes(path);
        ColladaElfAsset asset;
        asset.sha256 = calculateColladaSha256(bytes);
        if (asset.sha256 != ElfDaeSha256)
            throw std::runtime_error("Elf DAE differs from the Three.js r185 asset lock.");

        pugi::xml_document document;
        const pugi::xml_parse_result result = document.load_buffer(
            bytes.data(), bytes.size(), pugi::parse_default, pugi::encoding_utf8);
        if (!result) throw std::runtime_error("Pinned Elf DAE XML is invalid.");
        const pugi::xml_node root = document.document_element();
        const pugi::xml_node geometry = findColladaId(root, "Elf01_posed_002-mesh");
        const pugi::xml_node mesh = findColladaChild(geometry, "mesh");
        if (!mesh) throw std::runtime_error("Pinned Elf mesh is missing.");

        eastl::unordered_map<eastl::string, ColladaFloatSource> sources;
        for (pugi::xml_node sourceNode : mesh.children("source"))
        {
            ColladaFloatSource source;
            source.values = parseColladaFloats(
                findColladaChild(sourceNode, "float_array").text().get());
            const pugi::xml_node technique = findColladaChild(sourceNode, "technique_common");
            source.stride = findColladaChild(technique, "accessor")
                                .attribute("stride").as_uint(1u);
            sources.emplace(sourceNode.attribute("id").value(), eastl::move(source));
        }
        const pugi::xml_node verticesNode = findColladaChild(mesh, "vertices");
        const eastl::string verticesId = verticesNode.attribute("id").value();
        eastl::string vertexPositionSource;
        for (pugi::xml_node input : verticesNode.children("input"))
        {
            if (std::strcmp(input.attribute("semantic").value(), "POSITION") == 0)
                vertexPositionSource = colladaReferenceId(input.attribute("source").value());
        }

        uint32_t materialIndex = 0u;
        for (pugi::xml_node primitive : mesh.children("polylist"))
        {
            eastl::vector<ColladaPrimitiveInput> inputs;
            uint32_t tupleWidth = 0u;
            for (pugi::xml_node input : primitive.children("input"))
            {
                ColladaPrimitiveInput resolved;
                resolved.semantic = input.attribute("semantic").value();
                resolved.source = colladaReferenceId(input.attribute("source").value());
                resolved.offset = input.attribute("offset").as_uint();
                tupleWidth = eastl::max(tupleWidth, resolved.offset + 1u);
                if (resolved.semantic == "VERTEX" && resolved.source == verticesId)
                {
                    resolved.semantic = "POSITION";
                    resolved.source = vertexPositionSource;
                }
                inputs.push_back(eastl::move(resolved));
            }
            const eastl::vector<uint32_t> polygonCounts = parseColladaIndices(
                findColladaChild(primitive, "vcount").text().get());
            const eastl::vector<uint32_t> primitiveIndices = parseColladaIndices(
                findColladaChild(primitive, "p").text().get());
            ColladaAssetGroup group;
            group.firstVertex = static_cast<uint32_t>(asset.vertices.size());
            group.materialIndex = materialIndex;
            uint32_t firstCorner = 0u;
            for (const uint32_t polygonCount : polygonCounts)
            {
                if (polygonCount < 3u)
                    throw std::runtime_error("Pinned Elf polylist contains a degenerate polygon.");
                for (uint32_t triangle = 1u; triangle + 1u < polygonCount; ++triangle)
                {
                    const uint32_t corners[] = {
                        firstCorner,
                        firstCorner + triangle,
                        firstCorner + triangle + 1u,
                    };
                    for (const uint32_t corner : corners)
                    {
                        asset.vertices.push_back(makeColladaVertex(
                            sources, inputs, primitiveIndices, tupleWidth,
                            corner, materialIndex));
                    }
                }
                firstCorner += polygonCount;
            }
            if (uint64_t(firstCorner) * tupleWidth != primitiveIndices.size())
                throw std::runtime_error("Pinned Elf polylist index count is inconsistent.");
            group.vertexCount = static_cast<uint32_t>(asset.vertices.size()) -
                                group.firstVertex;
            asset.groups.push_back(group);
            ++materialIndex;
        }

        const pugi::xml_node visualScene = findColladaId(root, "Scene");
        const pugi::xml_node node = findColladaChild(visualScene, "node");
        const eastl::vector<float> matrixValues = parseColladaFloats(
            findColladaChild(node, "matrix").text().get());
        if (matrixValues.size() != 16u)
            throw std::runtime_error("Pinned Elf node matrix is invalid.");
        for (uint32_t row = 0u; row < 4u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
                asset.nodeMatrix[column][row] = matrixValues[row * 4u + column];
        }
        asset.materials = parseElfMaterials(root);
        if (asset.vertices.size() != ElfVertexCount || asset.groups.size() != 4u ||
            asset.materials.size() != 4u)
        {
            throw std::runtime_error("Pinned Elf resolved scene counts are invalid.");
        }
        return asset;
    }

    glm::mat4 makeColladaElfWorldMatrix(
        const ColladaElfAsset &asset,
        uint32_t frameIndex)
    {
        constexpr float Pi = 3.14159265358979323846f;
        const float zRotation = static_cast<float>(frameIndex) / 60.0f * 0.5f;
        glm::mat4 root(1.0f);
        root = glm::rotate(root, -Pi * 0.5f, glm::vec3(1.0f, 0.0f, 0.0f));
        root = glm::rotate(root, zRotation, glm::vec3(0.0f, 0.0f, 1.0f));
        return root * asset.nodeMatrix;
    }

    ColladaRobotAsset loadColladaRobotAsset(const std::filesystem::path &path)
    {
        const eastl::vector<uint8_t> bytes = readColladaBytes(path);
        ColladaRobotAsset asset;
        asset.sha256 = calculateColladaSha256(bytes);
        if (asset.sha256 != RobotDaeSha256)
            throw std::runtime_error("ABB robot DAE differs from the Three.js r185 asset lock.");

        pugi::xml_document document;
        const pugi::xml_parse_result result = document.load_buffer(
            bytes.data(), bytes.size(), pugi::parse_default, pugi::encoding_utf8);
        if (!result) throw std::runtime_error("Pinned ABB robot DAE XML is invalid.");
        const pugi::xml_node root = document.document_element();
        constexpr const char *GeometryIds[] = {
            "gkmodel0_base_link_geom0",
            "gkmodel0_link_1_geom0",
            "gkmodel0_link_2_geom0",
            "gkmodel0_link_3_geom0",
            "gkmodel0_link_4_geom0",
            "gkmodel0_link_5_geom0",
            "gkmodel0_link_6_geom0",
        };
        constexpr const char *LinkNames[] = {
            "base_link", "link_1", "link_2", "link_3",
            "link_4", "link_5", "link_6",
        };
        constexpr uint32_t VertexCounts[] = {
            6474u, 27120u, 4716u, 13884u, 19098u, 1632u, 5256u,
        };
        constexpr glm::vec3 LocalTranslations[] = {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 0.4865f),
            glm::vec3(0.15f, 0.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 0.475f),
            glm::vec3(0.6f, 0.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.065f, 0.0f, 0.0f),
        };
        constexpr glm::vec3 JointAxes[] = {
            glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(1.0f, 0.0f, 0.0f),
        };
        constexpr float JointMinimum[] = {
            0.0f, -180.0f, -63.0f, -235.0f, -200.0f, -115.0f, -400.0f,
        };
        constexpr float JointMaximum[] = {
            0.0f, 180.0f, 110.0f, 55.0f, 200.0f, 115.0f, 400.0f,
        };

        for (uint32_t linkIndex = 0u; linkIndex < 7u; ++linkIndex)
        {
            const pugi::xml_node geometry = findColladaId(root, GeometryIds[linkIndex]);
            const pugi::xml_node mesh = findColladaChild(geometry, "mesh");
            const pugi::xml_node source = findColladaChild(mesh, "source");
            const pugi::xml_node triangles = findColladaChild(mesh, "triangles");
            const eastl::vector<float> values = parseColladaFloats(
                findColladaChild(source, "float_array").text().get());
            const eastl::vector<uint32_t> sourceIndices = parseColladaIndices(
                findColladaChild(triangles, "p").text().get());
            if (sourceIndices.size() != VertexCounts[linkIndex] ||
                values.size() != size_t(VertexCounts[linkIndex]) * 3u)
            {
                throw std::runtime_error("Pinned ABB robot link counts are invalid.");
            }
            ColladaRobotLink link;
            link.name = LinkNames[linkIndex];
            link.parentLink = linkIndex == 0u ? -1 : int32_t(linkIndex - 1u);
            link.localTransform = glm::translate(
                glm::mat4(1.0f), LocalTranslations[linkIndex]);
            link.jointAxis = JointAxes[linkIndex];
            link.minimumDegrees = JointMinimum[linkIndex];
            link.maximumDegrees = JointMaximum[linkIndex];
            link.vertices.reserve(sourceIndices.size());
            for (uint32_t triangle = 0u;
                 triangle < sourceIndices.size(); triangle += 3u)
            {
                glm::vec3 positions[3] = {};
                for (uint32_t corner = 0u; corner < 3u; ++corner)
                {
                    const uint32_t sourceIndex = sourceIndices[triangle + corner];
                    if (sourceIndex >= VertexCounts[linkIndex])
                        throw std::runtime_error("Pinned ABB robot index is out of bounds.");
                    positions[corner] = glm::vec3(
                        values[sourceIndex * 3u],
                        values[sourceIndex * 3u + 1u],
                        values[sourceIndex * 3u + 2u]);
                }
                // WebGLBindingStates leaves this asset's missing NORMAL
                // attribute at its generic zero value.
                const glm::vec3 normal(0.0f);
                for (uint32_t corner = 0u; corner < 3u; ++corner)
                    link.vertices.push_back({positions[corner], normal});
            }
            asset.links.push_back(eastl::move(link));
        }
        if (asset.links.size() != 7u)
            throw std::runtime_error("Pinned ABB robot must resolve seven links.");
        return asset;
    }

    eastl::vector<glm::mat4> evaluateColladaRobotPose(
        const ColladaRobotAsset &asset,
        const eastl::vector<float> &jointDegrees)
    {
        if (asset.links.size() != 7u || jointDegrees.size() != 7u)
            throw std::invalid_argument("ABB robot pose requires seven links and joint values.");
        constexpr float Pi = 3.14159265358979323846f;
        glm::mat4 root(1.0f);
        root = glm::rotate(root, -Pi * 0.5f, glm::vec3(1.0f, 0.0f, 0.0f));
        root = glm::scale(root, glm::vec3(10.0f));
        eastl::vector<glm::mat4> transforms(asset.links.size(), glm::mat4(1.0f));
        for (uint32_t linkIndex = 0u; linkIndex < asset.links.size(); ++linkIndex)
        {
            const ColladaRobotLink &link = asset.links[linkIndex];
            glm::mat4 local = link.localTransform;
            if (glm::dot(link.jointAxis, link.jointAxis) > 0.5f)
            {
                const float bounded = eastl::clamp(
                    jointDegrees[linkIndex], link.minimumDegrees, link.maximumDegrees);
                local = local * glm::rotate(
                    glm::mat4(1.0f), bounded * Pi / 180.0f, link.jointAxis);
            }
            transforms[linkIndex] = link.parentLink < 0
                ? root * local
                : transforms[static_cast<uint32_t>(link.parentLink)] * local;
        }
        return transforms;
    }
} // namespace GVM::ThreeSamples
