#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Identifies the two PLY encodings exercised by the frozen Three r185 loader sample. */
    enum class PlyEncoding
    {
        Ascii,
        BinaryLittleEndian,
    };

    /** Stores one parsed indexed PLY mesh and the exact source identity used for loader evidence. */
    struct PlyGeometry
    {
        eastl::vector<glm::vec3> positions;
        eastl::vector<glm::vec3> normals;
        eastl::vector<uint32_t> indices;
        eastl::string sourceSha256;
        PlyEncoding encoding = PlyEncoding::Ascii;
        uint32_t sourceVertexCount = 0u;
        uint32_t sourceFaceCount = 0u;
    };

    /** Parses an exact ASCII or binary-little-endian PLY mesh and generates Three-compatible normals. */
    PlyGeometry loadPlyGeometry(const std::filesystem::path &path, PlyEncoding expectedEncoding);
} // namespace GVM::ThreeSamples
