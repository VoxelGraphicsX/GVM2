#include "WebglGeometriesBundle.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr char BundleMagic[] = "GVMGEO01";
        constexpr uint32_t ExpectedMeshCount = 16u;
        constexpr uint32_t MaximumElementCount = 16u * 1024u * 1024u;

        /** Reads one exact byte range and rejects truncated bundle payloads. */
        void readBundleBytes(
            std::ifstream &input,
            void *destination,
            size_t byteCount)
        {
            if (byteCount >
                static_cast<size_t>(
                    std::numeric_limits<std::streamsize>::max()))
            {
                throw std::overflow_error(
                    "webgl_geometries bundle range exceeds stream limits.");
            }
            input.read(
                static_cast<char *>(destination),
                static_cast<std::streamsize>(byteCount));
            if (!input)
            {
                throw std::runtime_error(
                    "webgl_geometries bundle ended before its declared payload.");
            }
        }

        /** Reads one little-endian unsigned integer from the generated bundle. */
        uint32_t readBundleUint32(std::ifstream &input)
        {
            uint8_t bytes[4u] = {};
            readBundleBytes(
                input,
                bytes,
                sizeof(bytes));
            return uint32_t(bytes[0u]) |
                (uint32_t(bytes[1u]) << 8u) |
                (uint32_t(bytes[2u]) << 16u) |
                (uint32_t(bytes[3u]) << 24u);
        }

        /** Reads one length-prefixed UTF-8 mesh identifier. */
        eastl::string readBundleString(std::ifstream &input)
        {
            const uint32_t byteCount =
                readBundleUint32(input);
            if (byteCount == 0u ||
                byteCount > 256u)
            {
                throw std::runtime_error(
                    "webgl_geometries bundle contains an invalid mesh identifier.");
            }
            eastl::string value;
            value.resize(byteCount);
            readBundleBytes(
                input,
                value.data(),
                byteCount);
            return value;
        }

        /** Reads one tightly packed Float32 vector array from the generated bundle. */
        template <class VectorType>
        eastl::vector<VectorType> readBundleVectorArray(
            std::ifstream &input,
            uint32_t elementCount)
        {
            eastl::vector<VectorType> values(elementCount);
            readBundleBytes(
                input,
                values.data(),
                values.size() * sizeof(VectorType));
            return values;
        }
    } // namespace

    eastl::vector<WebglGeometriesBundleMesh>
    decodeWebglGeometriesBundle(
        const std::filesystem::path &bundlePath)
    {
        static_assert(sizeof(glm::vec2) == sizeof(float) * 2u);
        static_assert(sizeof(glm::vec3) == sizeof(float) * 3u);
        std::ifstream input(
            bundlePath,
            std::ios::binary);
        if (!input)
        {
            throw std::runtime_error(
                "Could not open the webgl_geometries r185 bundle.");
        }
        char magic[sizeof(BundleMagic) - 1u] = {};
        readBundleBytes(
            input,
            magic,
            sizeof(magic));
        if (std::memcmp(
                magic,
                BundleMagic,
                sizeof(magic)) != 0)
        {
            throw std::runtime_error(
                "webgl_geometries bundle magic is invalid.");
        }
        const uint32_t meshCount =
            readBundleUint32(input);
        if (meshCount != ExpectedMeshCount)
        {
            throw std::runtime_error(
                "webgl_geometries bundle must contain exactly sixteen meshes.");
        }

        eastl::vector<WebglGeometriesBundleMesh> meshes;
        meshes.reserve(meshCount);
        for (uint32_t meshIndex = 0u;
             meshIndex < meshCount;
             ++meshIndex)
        {
            WebglGeometriesBundleMesh mesh;
            mesh.name =
                readBundleString(input);
            const uint32_t vertexCount =
                readBundleUint32(input);
            const uint32_t indexCount =
                readBundleUint32(input);
            if (vertexCount == 0u ||
                indexCount == 0u ||
                vertexCount > MaximumElementCount ||
                indexCount > MaximumElementCount)
            {
                throw std::runtime_error(
                    "webgl_geometries bundle declares an invalid geometry count.");
            }
            mesh.positions =
                readBundleVectorArray<glm::vec3>(
                    input,
                    vertexCount);
            mesh.normals =
                readBundleVectorArray<glm::vec3>(
                    input,
                    vertexCount);
            mesh.textureCoordinates =
                readBundleVectorArray<glm::vec2>(
                    input,
                    vertexCount);
            mesh.indices.resize(indexCount);
            for (uint32_t &index : mesh.indices)
            {
                index = readBundleUint32(input);
                if (index >= vertexCount)
                {
                    throw std::runtime_error(
                        "webgl_geometries bundle contains an out-of-range index.");
                }
            }
            meshes.push_back(
                eastl::move(mesh));
        }
        if (input.peek() !=
            std::ifstream::traits_type::eof())
        {
            throw std::runtime_error(
                "webgl_geometries bundle contains trailing bytes.");
        }
        return meshes;
    }
} // namespace GVM::ThreeSamples
