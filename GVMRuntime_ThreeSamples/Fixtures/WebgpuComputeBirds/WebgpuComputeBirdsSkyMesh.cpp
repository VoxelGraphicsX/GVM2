#include "WebgpuComputeBirdsSkyMesh.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr char SkyMagic[] = "GVMBIRD1";
        constexpr uint32_t MaximumElementCount = 4u * 1024u * 1024u;

        /** Reads one exact byte range and rejects truncated sky payloads. */
        void readSkyBytes(
            std::ifstream &input,
            void *destination,
            size_t byteCount)
        {
            if (byteCount >
                static_cast<size_t>(
                    std::numeric_limits<std::streamsize>::max()))
            {
                throw std::overflow_error(
                    "webgpu_compute_birds sky range exceeds stream limits.");
            }
            input.read(
                static_cast<char *>(destination),
                static_cast<std::streamsize>(byteCount));
            if (!input)
            {
                throw std::runtime_error(
                    "webgpu_compute_birds sky payload is truncated.");
            }
        }

        /** Reads one little-endian unsigned integer from the sky payload. */
        uint32_t readSkyUint32(std::ifstream &input)
        {
            uint8_t bytes[4u] = {};
            readSkyBytes(input, bytes, sizeof(bytes));
            return uint32_t(bytes[0u]) |
                (uint32_t(bytes[1u]) << 8u) |
                (uint32_t(bytes[2u]) << 16u) |
                (uint32_t(bytes[3u]) << 24u);
        }
    } // namespace

    WebgpuComputeBirdsSkyMesh
    decodeWebgpuComputeBirdsSkyMesh(
        const std::filesystem::path &path)
    {
        static_assert(sizeof(glm::vec3) == sizeof(float) * 3u);
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error(
                "Could not open the webgpu_compute_birds sky payload.");
        }
        char magic[sizeof(SkyMagic) - 1u] = {};
        readSkyBytes(input, magic, sizeof(magic));
        if (std::memcmp(magic, SkyMagic, sizeof(magic)) != 0)
        {
            throw std::runtime_error(
                "webgpu_compute_birds sky payload has invalid magic.");
        }
        const uint32_t vertexCount = readSkyUint32(input);
        const uint32_t indexCount = readSkyUint32(input);
        if (vertexCount == 0u ||
            indexCount == 0u ||
            vertexCount > MaximumElementCount ||
            indexCount > MaximumElementCount)
        {
            throw std::runtime_error(
                "webgpu_compute_birds sky payload has invalid counts.");
        }
        WebgpuComputeBirdsSkyMesh mesh;
        mesh.positions.resize(vertexCount);
        mesh.indices.resize(indexCount);
        readSkyBytes(
            input,
            mesh.positions.data(),
            mesh.positions.size() * sizeof(mesh.positions[0u]));
        readSkyBytes(
            input,
            mesh.indices.data(),
            mesh.indices.size() * sizeof(mesh.indices[0u]));
        for (const uint32_t index : mesh.indices)
        {
            if (index >= vertexCount)
            {
                throw std::runtime_error(
                    "webgpu_compute_birds sky index is out of range.");
            }
        }
        if (input.peek() != std::ifstream::traits_type::eof())
        {
            throw std::runtime_error(
                "webgpu_compute_birds sky payload has trailing bytes.");
        }
        return mesh;
    }
} // namespace GVM::ThreeSamples
