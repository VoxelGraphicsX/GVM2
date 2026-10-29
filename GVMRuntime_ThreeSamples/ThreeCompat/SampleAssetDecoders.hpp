#ifndef GVM_THREE_COMPAT_SAMPLE_ASSET_DECODERS_HPP
#define GVM_THREE_COMPAT_SAMPLE_ASSET_DECODERS_HPP

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Describes one material group from a Three.js BufferGeometry JSON asset. */
    struct BufferGeometryGroup
    {
        uint32_t start = 0u;
        uint32_t count = 0u;
        uint32_t materialIndex = 0u;
    };

    /** Stores the triangle-ready attributes decoded from one Three.js BufferGeometry JSON asset. */
    struct DecodedBufferGeometry
    {
        eastl::vector<float> positions;
        eastl::vector<float> normals;
        eastl::vector<float> textureCoordinates;
        eastl::vector<uint32_t> indices;
        eastl::vector<BufferGeometryGroup> groups;
    };

    /** Stores the validated JSON and binary chunks from one glTF 2.0 binary container. */
    struct DecodedGlbContainer
    {
        eastl::string jsonText;
        eastl::vector<uint8_t> binaryChunk;
    };

    /** Stores triangle-ready attributes decoded from the first primitive of a GLB mesh. */
    struct DecodedGlbMesh
    {
        eastl::vector<float> positions;
        eastl::vector<float> normals;
        eastl::vector<float> textureCoordinates;
        eastl::vector<uint32_t> indices;
    };

    /** Stores triangle-expanded position, normal, and UV attributes decoded from one OBJ mesh. */
    struct DecodedObjMesh
    {
        eastl::vector<float> positions;
        eastl::vector<float> normals;
        eastl::vector<float> textureCoordinates;
    };

    /** Stores one top-down linear RGBA image decoded from a Radiance RGBE asset. */
    struct DecodedRadianceImage
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        eastl::vector<float> rgba;
    };

    /** Stores one indexed voxel from a MagicaVoxel model. */
    struct VoxVoxel
    {
        uint8_t x = 0u;
        uint8_t y = 0u;
        uint8_t z = 0u;
        uint8_t colorIndex = 0u;
    };

    /** Stores one MagicaVoxel v150 model and its optional 256-entry RGBA palette. */
    struct DecodedVoxModel
    {
        uint32_t sizeX = 0u;
        uint32_t sizeY = 0u;
        uint32_t sizeZ = 0u;
        eastl::vector<VoxVoxel> voxels;
        eastl::vector<uint32_t> paletteRgba;
    };

    /** Stores one flat-shaded vertex emitted by the r185 VOX greedy mesher. */
    struct VoxMeshVertex
    {
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        float normalX = 0.0f;
        float normalY = 0.0f;
        float normalZ = 0.0f;
        float colorR = 0.0f;
        float colorG = 0.0f;
        float colorB = 0.0f;
    };

    /** Stores deterministic triangle-list geometry generated from one decoded VOX model. */
    struct DecodedVoxMesh
    {
        eastl::vector<VoxMeshVertex> vertices;
        eastl::vector<uint32_t> indices;
        uint32_t quadCount = 0u;
    };

    /** Decodes one bounded Three.js BufferGeometry JSON payload into explicit CPU arrays. */
    DecodedBufferGeometry decodeThreeBufferGeometryJson(const eastl::vector<uint8_t> &bytes);

    /** Decodes and validates one two-chunk glTF 2.0 binary container. */
    DecodedGlbContainer decodeGlbContainer(const eastl::vector<uint8_t> &bytes);

    /** Decodes the first triangle primitive from a bounded GLB container. */
    DecodedGlbMesh decodeFirstGlbMesh(const eastl::vector<uint8_t> &bytes);

    /** Decodes one indexed triangle primitive from the requested GLB mesh. */
    DecodedGlbMesh decodeGlbMesh(
        const eastl::vector<uint8_t> &bytes,
        uint32_t meshIndex);

    /** Extracts one validated buffer view from the binary chunk of a bounded GLB container. */
    eastl::vector<uint8_t> extractGlbBufferView(
        const eastl::vector<uint8_t> &bytes,
        uint32_t bufferViewIndex);

    /** Decodes OBJ vertex records and triangulated faces into OBJLoader-compatible arrays. */
    DecodedObjMesh decodeObjTriangleMesh(const eastl::vector<uint8_t> &bytes);

    /** Decodes one bounded Radiance RGBE payload into top-down linear RGBA texels. */
    DecodedRadianceImage decodeRadianceRgbe(const eastl::vector<uint8_t> &bytes);

    /** Decodes the first model from one MagicaVoxel v150 payload. */
    DecodedVoxModel decodeMagicaVoxel150(const eastl::vector<uint8_t> &bytes);

    /** Reproduces the r185 VOXLoader greedy mesher with flat normals and linear vertex colors. */
    DecodedVoxMesh buildVoxGreedyMesh(const DecodedVoxModel &model);
}

#endif
