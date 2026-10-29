#include "SampleAssetDecoders.hpp"
#include "CurveExtras.hpp"
#include "EdgeSplitGeometry.hpp"
#include "Fixtures/WebglGeometries/WebglGeometriesBundle.hpp"
#include "Nurbs.hpp"
#include "ParametricMesh.hpp"
#include "InstanceSampleGeometry.hpp"
#include "TubeMesh.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using GVM::ThreeSamples::ThreeCompat::DecodedBufferGeometry;
    using GVM::ThreeSamples::ThreeCompat::DecodedGlbContainer;
    using GVM::ThreeSamples::ThreeCompat::DecodedGlbMesh;
    using GVM::ThreeSamples::ThreeCompat::DecodedObjMesh;
    using GVM::ThreeSamples::ThreeCompat::DecodedRadianceImage;
    using GVM::ThreeSamples::ThreeCompat::DecodedVoxModel;

    /** Appends one little-endian uint32 to a deterministic test payload. */
    void appendUint32(eastl::vector<uint8_t> &bytes, uint32_t value)
    {
        bytes.push_back(static_cast<uint8_t>(value));
        bytes.push_back(static_cast<uint8_t>(value >> 8u));
        bytes.push_back(static_cast<uint8_t>(value >> 16u));
        bytes.push_back(static_cast<uint8_t>(value >> 24u));
    }

    /** Appends one ASCII byte range to a deterministic test payload. */
    void appendAscii(eastl::vector<uint8_t> &bytes, const char *text)
    {
        bytes.insert(bytes.end(), text, text + std::strlen(text));
    }

    /** Appends one native little-endian Float32 to a deterministic test payload. */
    void appendFloat32(eastl::vector<uint8_t> &bytes, float value)
    {
        const size_t offset = bytes.size();
        bytes.resize(offset + sizeof(value));
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    /** Fails the decoder test executable when one contract is false. */
    void requireCondition(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    /** Evaluates a deterministic plane for ParametricGeometry contract tests. */
    glm::dvec3 evaluateTestPlane(
        double u,
        double v,
        const void *context)
    {
        (void)context;
        return glm::dvec3(u, v, u + v);
    }

    /** Evaluates one closed unit circle for TubeGeometry contract tests. */
    glm::dvec3 evaluateTestCircle(
        double parameter,
        const void *context)
    {
        (void)context;
        const double angle =
            parameter * 6.2831853071795864769;
        return {
            std::cos(angle),
            std::sin(angle),
            0.0,
        };
    }

    /** Builds and validates one indexed BufferGeometry JSON contract. */
    void testBufferGeometryDecoder()
    {
        const char *json =
            R"({"data":{"attributes":{"position":{"array":[0,0,0,1,0,0,0,1,0]},"normal":{"array":[0,0,1,0,0,1,0,0,1]},"uv":{"array":[0,0,1,0,0,1]}},"index":{"array":[0,1,2]},"groups":[{"start":0,"count":3,"materialIndex":2}]}})";
        eastl::vector<uint8_t> bytes(json, json + std::strlen(json));
        const DecodedBufferGeometry geometry =
            GVM::ThreeSamples::ThreeCompat::decodeThreeBufferGeometryJson(bytes);
        requireCondition(geometry.positions.size() == 9u, "BufferGeometry position count failed.");
        requireCondition(geometry.normals.size() == 9u, "BufferGeometry normal count failed.");
        requireCondition(geometry.textureCoordinates.size() == 6u, "BufferGeometry UV count failed.");
        requireCondition(geometry.indices.size() == 3u && geometry.indices[2u] == 2u,
                         "BufferGeometry index decode failed.");
        requireCondition(geometry.groups.size() == 1u && geometry.groups[0u].materialIndex == 2u,
                         "BufferGeometry group decode failed.");
    }

    /** Builds and validates one minimal two-chunk glTF binary container. */
    void testGlbDecoder()
    {
        eastl::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4}]})";
        while (json.size() % 4u != 0u) json.push_back(' ');
        eastl::vector<uint8_t> bytes;
        appendUint32(bytes, 0x46546C67u);
        appendUint32(bytes, 2u);
        appendUint32(bytes, static_cast<uint32_t>(12u + 8u + json.size() + 8u + 4u));
        appendUint32(bytes, static_cast<uint32_t>(json.size()));
        appendUint32(bytes, 0x4E4F534Au);
        bytes.insert(bytes.end(), json.begin(), json.end());
        appendUint32(bytes, 4u);
        appendUint32(bytes, 0x004E4942u);
        bytes.insert(bytes.end(), {1u, 2u, 3u, 4u});
        const DecodedGlbContainer glb =
            GVM::ThreeSamples::ThreeCompat::decodeGlbContainer(bytes);
        requireCondition(!glb.jsonText.empty(), "GLB JSON decode failed.");
        requireCondition(glb.binaryChunk.size() == 4u && glb.binaryChunk[3u] == 4u,
                         "GLB binary decode failed.");
    }

    /** Builds and validates one indexed triangle from a minimal GLB mesh. */
    void testGlbMeshDecoder()
    {
        eastl::vector<uint8_t> binary = {
            0u, 0u, 1u, 0u, 2u, 0u, 0u, 0u,
        };
        const float positions[9u] = {
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
        };
        for (const float value : positions)
        {
            appendFloat32(binary, value);
        }
        eastl::string json =
            R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":44}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":6},{"buffer":0,"byteOffset":8,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5123,"count":3,"type":"SCALAR"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":1},"indices":0}]}]})";
        while (json.size() % 4u != 0u) json.push_back(' ');
        eastl::vector<uint8_t> bytes;
        appendUint32(bytes, 0x46546C67u);
        appendUint32(bytes, 2u);
        appendUint32(
            bytes,
            static_cast<uint32_t>(
                12u + 8u + json.size() + 8u + binary.size()));
        appendUint32(bytes, static_cast<uint32_t>(json.size()));
        appendUint32(bytes, 0x4E4F534Au);
        bytes.insert(bytes.end(), json.begin(), json.end());
        appendUint32(bytes, static_cast<uint32_t>(binary.size()));
        appendUint32(bytes, 0x004E4942u);
        bytes.insert(bytes.end(), binary.begin(), binary.end());
        const DecodedGlbMesh mesh =
            GVM::ThreeSamples::ThreeCompat::decodeFirstGlbMesh(bytes);
        requireCondition(
            mesh.positions.size() == 9u &&
                mesh.positions[3u] == 1.0f,
            "GLB mesh position decode failed.");
        requireCondition(
            mesh.indices.size() == 3u &&
                mesh.indices[2u] == 2u,
            "GLB mesh index decode failed.");
        const eastl::vector<uint8_t> positionView =
            GVM::ThreeSamples::ThreeCompat::extractGlbBufferView(
                bytes,
                1u);
        requireCondition(
            positionView.size() == 36u &&
                positionView[3u] == 0u,
            "GLB buffer view extraction failed.");
    }

    /** Builds and validates indexed OBJ triangles with positive and relative indices. */
    void testObjDecoder()
    {
        const char *obj =
            "v 0 0 0\n"
            "v 1 0 0\n"
            "v 1 1 0\n"
            "v 0 1 0\n"
            "vt 0 0\n"
            "vt 1 0\n"
            "vt 1 1\n"
            "vt 0 1\n"
            "vn 0 0 1\n"
            "f 1/1/1 2/2/1 3/3/1\n"
            "f -4/1/1 -2/3/1 -1/4/1\n";
        const eastl::vector<uint8_t> bytes(
            obj,
            obj + std::strlen(obj));
        const DecodedObjMesh mesh =
            GVM::ThreeSamples::ThreeCompat::decodeObjTriangleMesh(bytes);
        requireCondition(
            mesh.positions.size() == 18u &&
                mesh.normals.size() == 18u &&
                mesh.textureCoordinates.size() == 12u,
            "OBJ triangle expansion count failed.");
        requireCondition(
            mesh.positions[9u] == 0.0f &&
                mesh.positions[12u] == 1.0f &&
                mesh.positions[15u] == 0.0f,
            "OBJ relative index resolution failed.");
    }

    /** Validates r185 mergeVertices and EdgeSplit topology on two hard-edge faces. */
    void testEdgeSplitGeometry()
    {
        DecodedObjMesh expanded;
        expanded.positions = {
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f,
        };
        expanded.normals = {
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
        };
        expanded.textureCoordinates = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
            0.0f, 0.0f,
            0.0f, 1.0f,
            1.0f, 0.0f,
        };
        const auto merged =
            GVM::ThreeSamples::ThreeCompat::mergeObjVerticesForEdgeSplit(
                expanded);
        requireCondition(
            merged.positions.size() == 12u &&
                merged.indices.size() == 6u &&
                merged.indices[3u] == merged.indices[0u],
            "mergeVertices hard-edge fixture count failed.");
        const auto split =
            GVM::ThreeSamples::ThreeCompat::applyEdgeSplitModifier(
                merged,
                0.3490658503988659,
                false);
        requireCondition(
            split.positions.size() / 3u == 8u &&
                split.indices.size() == 6u &&
                split.indices[0u] != split.indices[3u] &&
                split.indices[1u] != split.indices[5u],
            "EdgeSplit hard-edge occurrence grouping failed.");
    }

    /** Decodes the optional pinned Cerberus asset and validates its OBJLoader topology. */
    void testCerberusObjAsset(const std::filesystem::path &assetPath)
    {
        std::ifstream input(assetPath, std::ios::binary | std::ios::ate);
        if (!input)
        {
            throw std::runtime_error(
                "Could not open the pinned Cerberus OBJ asset.");
        }
        const std::streamsize byteCount = input.tellg();
        if (byteCount <= 0)
        {
            throw std::runtime_error(
                "The pinned Cerberus OBJ asset is empty.");
        }
        input.seekg(0, std::ios::beg);
        eastl::vector<uint8_t> bytes(
            static_cast<size_t>(byteCount));
        input.read(
            reinterpret_cast<char *>(bytes.data()),
            byteCount);
        if (!input)
        {
            throw std::runtime_error(
                "Could not read the pinned Cerberus OBJ asset.");
        }
        const DecodedObjMesh mesh =
            GVM::ThreeSamples::ThreeCompat::decodeObjTriangleMesh(bytes);
        requireCondition(
            mesh.positions.size() == 301869u &&
                mesh.normals.size() == 301869u &&
                mesh.textureCoordinates.size() == 201246u,
            "Cerberus OBJLoader topology count failed.");
        const auto merged =
            GVM::ThreeSamples::ThreeCompat::mergeObjVerticesForEdgeSplit(
                mesh);
        requireCondition(
            merged.positions.size() / 3u == 26690u &&
                merged.indices.size() == 100623u,
            "Cerberus mergeVertices topology count failed.");
        const auto defaultSplit =
            GVM::ThreeSamples::ThreeCompat::applyEdgeSplitModifier(
                merged,
                0.3490658503988659,
                true);
        requireCondition(
            defaultSplit.positions.size() / 3u == 118552u &&
                defaultSplit.indices.size() == 100623u,
            "Cerberus 20-degree EdgeSplit topology count failed.");
        const auto flatSplit =
            GVM::ThreeSamples::ThreeCompat::applyEdgeSplitModifier(
                merged,
                1.0471975511965976,
                false);
        requireCondition(
            flatSplit.positions.size() / 3u == 103108u &&
                flatSplit.indices.size() == 100623u,
            "Cerberus 60-degree EdgeSplit topology count failed.");
    }

    /** Builds and validates one modern-RLE Radiance scanline. */
    void testRadianceDecoder()
    {
        eastl::vector<uint8_t> bytes;
        appendAscii(bytes, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n");
        bytes.insert(bytes.end(), {2u, 2u, 0u, 2u});
        bytes.insert(bytes.end(), {130u, 128u});
        bytes.insert(bytes.end(), {130u, 64u});
        bytes.insert(bytes.end(), {130u, 32u});
        bytes.insert(bytes.end(), {130u, 129u});
        const DecodedRadianceImage image =
            GVM::ThreeSamples::ThreeCompat::decodeRadianceRgbe(bytes);
        requireCondition(image.width == 2u && image.height == 1u && image.rgba.size() == 8u,
                         "Radiance dimensions failed.");
        requireCondition(std::abs(image.rgba[0u] - (128.0f / 127.5f)) < 0.0001f &&
                             std::abs(image.rgba[1u] - (64.0f / 127.5f)) < 0.0001f &&
                             std::abs(image.rgba[2u] - (32.0f / 127.5f)) < 0.0001f,
                             "Radiance linear RGB decode failed.");
    }

    /** Builds and validates one single-voxel MagicaVoxel v150 model. */
    void testVoxDecoder()
    {
        eastl::vector<uint8_t> children;
        appendAscii(children, "SIZE");
        appendUint32(children, 12u);
        appendUint32(children, 0u);
        appendUint32(children, 2u);
        appendUint32(children, 3u);
        appendUint32(children, 4u);
        appendAscii(children, "XYZI");
        appendUint32(children, 8u);
        appendUint32(children, 0u);
        appendUint32(children, 1u);
        children.insert(children.end(), {1u, 2u, 3u, 7u});
        appendAscii(children, "RGBA");
        appendUint32(children, 1024u);
        appendUint32(children, 0u);
        for (uint32_t paletteIndex = 0u; paletteIndex < 256u; ++paletteIndex)
        {
            appendUint32(children, 0xff000000u | paletteIndex);
        }
        eastl::vector<uint8_t> bytes;
        appendAscii(bytes, "VOX ");
        appendUint32(bytes, 150u);
        appendAscii(bytes, "MAIN");
        appendUint32(bytes, 0u);
        appendUint32(bytes, static_cast<uint32_t>(children.size()));
        bytes.insert(bytes.end(), children.begin(), children.end());
        const DecodedVoxModel model =
            GVM::ThreeSamples::ThreeCompat::decodeMagicaVoxel150(bytes);
        requireCondition(model.sizeX == 2u && model.sizeY == 3u && model.sizeZ == 4u,
                         "VOX dimensions failed.");
        requireCondition(model.voxels.size() == 1u && model.voxels[0u].colorIndex == 7u,
                         "VOX voxel decode failed.");
        const auto mesh = GVM::ThreeSamples::ThreeCompat::buildVoxGreedyMesh(model);
        requireCondition(mesh.quadCount == 6u && mesh.vertices.size() == 24u &&
                             mesh.indices.size() == 36u,
                         "VOX isolated-voxel greedy mesh failed.");
    }

    /** Validates shared Suzanne normal generation and deterministic instance transforms. */
    void testInstanceSampleGeometry()
    {
        DecodedBufferGeometry geometry;
        geometry.positions = {
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f};
        geometry.indices = {0u, 1u, 2u};
        const auto mesh =
            GVM::ThreeSamples::ThreeCompat::buildInstanceSampleMesh(geometry);
        requireCondition(mesh.positions.size() == 3u && mesh.normals.size() == 3u,
                         "Instance mesh conversion failed.");
        requireCondition(std::abs(mesh.normals[0u].z - 1.0f) < 0.0001f,
                         "Instance mesh normal generation failed.");
        const auto performanceTransforms =
            GVM::ThreeSamples::ThreeCompat::buildInstancingPerformanceTransforms(
                1000u,
                0x12345678u,
                132u,
                0u);
        requireCondition(performanceTransforms.size() == 1000u &&
                             performanceTransforms.back().ordinal == 999u,
                         "Performance instance transform count failed.");
        const auto gridTransforms =
            GVM::ThreeSamples::ThreeCompat::buildWebgpuInstanceMeshTransforms(
                10u,
                125u,
                1.0);
        requireCondition(gridTransforms.size() == 125u &&
                             gridTransforms.back().ordinal == 124u,
                         "WebGPU instance grid active-count contract failed.");
    }

    /** Validates the 64-point Hilbert source and 256-sample Catmull-Rom curve. */
    void testInstancePointsGeometry()
    {
        const auto controlPoints =
            GVM::ThreeSamples::ThreeCompat::buildHilbert3DControlPoints(
                glm::vec3(0.0f),
                20.0f,
                1);
        requireCondition(controlPoints.size() == 64u,
                         "Hilbert3D recursion count failed.");
        const auto samples =
            GVM::ThreeSamples::ThreeCompat::sampleCentripetalCatmullRom(
                controlPoints,
                256u);
        requireCondition(samples.size() == 256u,
                         "Catmull-Rom sample count failed.");
        const glm::vec3 red =
            GVM::ThreeSamples::ThreeCompat::convertLinearHslToRgb(
                0.0f,
                1.0f,
                0.5f);
        requireCondition(std::abs(red.r - 1.0f) < 0.0001f &&
                             std::abs(red.g) < 0.0001f &&
                             std::abs(red.b) < 0.0001f,
                         "Linear HSL conversion failed.");
    }

    /** Validates Three r185 curve, surface, and volume NURBS evaluation. */
    void testNurbsEvaluation()
    {
        using GVM::ThreeSamples::ThreeCompat::NurbsAxis;
        using GVM::ThreeSamples::ThreeCompat::NurbsControlPoint;
        using GVM::ThreeSamples::ThreeCompat::evaluateNurbsCurve;
        using GVM::ThreeSamples::ThreeCompat::evaluateNurbsSurface;
        using GVM::ThreeSamples::ThreeCompat::evaluateNurbsVolume;

        const NurbsAxis linearAxis = {
            .degree = 1u,
            .knots = {0.0, 0.0, 1.0, 1.0},
        };
        const eastl::vector<NurbsControlPoint> curvePoints = {
            {{0.0, 0.0, 0.0}, 1.0},
            {{2.0, 4.0, 6.0}, 1.0},
        };
        const glm::dvec3 curveMiddle =
            evaluateNurbsCurve(
                linearAxis,
                curvePoints,
                0.5);
        requireCondition(
            glm::length(curveMiddle - glm::dvec3(1.0, 2.0, 3.0)) <
                1e-12,
            "NURBS linear curve midpoint failed.");

        const eastl::vector<NurbsControlPoint> surfacePoints = {
            {{0.0, 0.0, 0.0}, 1.0},
            {{0.0, 2.0, 2.0}, 1.0},
            {{2.0, 0.0, 2.0}, 1.0},
            {{2.0, 2.0, 4.0}, 1.0},
        };
        const glm::dvec3 surfaceMiddle =
            evaluateNurbsSurface(
                linearAxis,
                linearAxis,
                surfacePoints,
                2u,
                0.5,
                0.5);
        requireCondition(
            glm::length(surfaceMiddle - glm::dvec3(1.0, 1.0, 2.0)) <
                1e-12,
            "NURBS bilinear surface midpoint failed.");

        eastl::vector<NurbsControlPoint> volumePoints;
        for (uint32_t u = 0u; u < 2u; ++u)
        {
            for (uint32_t v = 0u; v < 2u; ++v)
            {
                for (uint32_t w = 0u; w < 2u; ++w)
                {
                    volumePoints.push_back({
                        {double(u), double(v), double(w)},
                        1.0,
                    });
                }
            }
        }
        const glm::dvec3 volumeMiddle =
            evaluateNurbsVolume(
                linearAxis,
                linearAxis,
                linearAxis,
                volumePoints,
                2u,
                2u,
                0.5,
                0.5,
                0.5);
        requireCondition(
            glm::length(volumeMiddle - glm::dvec3(0.5)) <
                1e-12,
            "NURBS trilinear volume midpoint failed.");
    }

    /** Validates Three r185 ParametricGeometry topology and finite-difference normals. */
    void testParametricMesh()
    {
        const auto mesh =
            GVM::ThreeSamples::ThreeCompat::buildParametricMesh(
                evaluateTestPlane,
                nullptr,
                2u,
                3u);
        requireCondition(
            mesh.positions.size() == 12u &&
                mesh.normals.size() == 12u &&
                mesh.textureCoordinates.size() == 12u &&
                mesh.indices.size() == 36u,
            "Parametric mesh counts failed.");
        requireCondition(
            mesh.indices[0u] == 0u &&
                mesh.indices[1u] == 1u &&
                mesh.indices[2u] == 3u &&
                mesh.indices[3u] == 1u &&
                mesh.indices[4u] == 4u &&
                mesh.indices[5u] == 3u,
            "Parametric mesh triangle winding failed.");
        const glm::vec3 expectedNormal =
            glm::normalize(glm::vec3(-1.0f, -1.0f, 1.0f));
        requireCondition(
            glm::length(mesh.normals[0u] - expectedNormal) <
                0.0001f,
            "Parametric mesh finite-difference normal failed.");
    }

    /** Validates Three r185 TubeGeometry counts, winding, UVs, and closed seam. */
    void testTubeMesh()
    {
        using GVM::ThreeSamples::ThreeCompat::TubeMeshParameters;
        using GVM::ThreeSamples::ThreeCompat::buildTubeMesh;
        using GVM::ThreeSamples::ThreeCompat::sampleTubeCurve;
        const auto mesh =
            buildTubeMesh({
                .evaluatePoint = evaluateTestCircle,
                .curveContext = nullptr,
                .tubularSegments = 8u,
                .radius = 0.25,
                .radialSegments = 3u,
                .closed = true,
                .arcLengthDivisions = 200u,
            });
        requireCondition(
            mesh.positions.size() == 36u &&
                mesh.normals.size() == 36u &&
                mesh.textureCoordinates.size() == 36u &&
                mesh.indices.size() == 144u &&
                mesh.tangents.size() == 9u &&
                mesh.frameNormals.size() == 9u &&
                mesh.frameBinormals.size() == 9u,
            "TubeGeometry counts failed.");
        requireCondition(
            mesh.indices[0u] == 0u &&
                mesh.indices[1u] == 4u &&
                mesh.indices[2u] == 1u &&
                mesh.indices[3u] == 4u &&
                mesh.indices[4u] == 5u &&
                mesh.indices[5u] == 1u,
            "TubeGeometry triangle winding failed.");
        requireCondition(
            glm::length(
                mesh.positions.front() -
                mesh.positions[32u]) <
                0.0001f &&
                std::abs(
                    mesh.textureCoordinates.front().x) <
                    0.0001f &&
                std::abs(
                    mesh.textureCoordinates[32u].x -
                    1.0f) <
                    0.0001f,
            "TubeGeometry closed seam failed.");
        const TubeMeshParameters sampleParameters = {
            .evaluatePoint = evaluateTestCircle,
            .curveContext = nullptr,
            .tubularSegments = 8u,
            .radius = 0.25,
            .radialSegments = 3u,
            .closed = true,
            .arcLengthDivisions = 200u,
        };
        const auto quarterSample =
            sampleTubeCurve(
                sampleParameters,
                0.25);
        requireCondition(
            glm::length(
                quarterSample.point -
                glm::dvec3(0.0, 1.0, 0.0)) <
                    0.001 &&
                quarterSample.totalLength > 6.28 &&
                quarterSample.totalLength < 6.29,
            "TubeGeometry arc-length sample failed.");
    }

    /** Validates the exact fixed CurveExtras equations needed by spline extrusion. */
    void testCurveExtras()
    {
        using GVM::ThreeSamples::ThreeCompat::CurveScaleContext;
        using GVM::ThreeSamples::ThreeCompat::evaluateCurveExtrasTorusKnot;
        using GVM::ThreeSamples::ThreeCompat::evaluateGrannyKnot;
        const glm::dvec3 grannyStart =
            evaluateGrannyKnot(0.0, nullptr);
        requireCondition(
            glm::length(
                grannyStart -
                glm::dvec3(-13.2, 5.6, 14.0)) <
                1e-12,
            "GrannyKnot start point failed.");
        const CurveScaleContext torusScale = {
            .scale = 20.0,
        };
        const glm::dvec3 torusStart =
            evaluateCurveExtrasTorusKnot(
                0.0,
                &torusScale);
        requireCondition(
            glm::length(
                torusStart -
                glm::dvec3(60.0, 0.0, 0.0)) <
                1e-12,
            "CurveExtras TorusKnot start point failed.");
    }

    /** Validates the exact sixteen-mesh bundle generated from pinned Three r185 constructors. */
    void testWebglGeometriesBundle()
    {
        const auto meshes =
            GVM::ThreeSamples::decodeWebglGeometriesBundle(
                std::filesystem::path(
                    GVM_THREE_SAMPLE_SOURCE_ROOT) /
                "Fixtures" /
                "WebglGeometries" /
                "Assets" /
                "webgl_geometries_meshes.bin");
        requireCondition(
            meshes.size() == 16u &&
                meshes.front().name == "sphere" &&
                meshes.front().positions.size() == 231u &&
                meshes.front().indices.size() == 1080u &&
                meshes.back().name == "mobius",
            "webgl_geometries generated bundle contract failed.");
    }
}

/** Runs deterministic contracts for every private Phase 1 asset decoder. */
int main(int argumentCount, char **arguments)
{
    try
    {
        testBufferGeometryDecoder();
        testGlbDecoder();
        testGlbMeshDecoder();
        testObjDecoder();
        testEdgeSplitGeometry();
        if (argumentCount == 2)
        {
            testCerberusObjAsset(arguments[1]);
        }
        testRadianceDecoder();
        testVoxDecoder();
        testInstanceSampleGeometry();
        testInstancePointsGeometry();
        testNurbsEvaluation();
        testParametricMesh();
        testTubeMesh();
        testCurveExtras();
        testWebglGeometriesBundle();
        std::cout << "Sample asset decoder tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Sample asset decoder tests failed: " << error.what() << '\n';
        return 1;
    }
}
