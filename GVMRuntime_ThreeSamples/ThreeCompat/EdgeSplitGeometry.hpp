#ifndef GVM_THREE_COMPAT_EDGE_SPLIT_GEOMETRY_HPP
#define GVM_THREE_COMPAT_EDGE_SPLIT_GEOMETRY_HPP

#include "SampleAssetDecoders.hpp"

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Stores indexed attributes produced by the r185 mergeVertices operation. */
    struct MergedEdgeSplitGeometry
    {
        eastl::vector<float> positions;
        eastl::vector<float> normals;
        eastl::vector<float> textureCoordinates;
        eastl::vector<uint32_t> indices;
    };

    /** Reproduces BufferGeometryUtils.mergeVertices for one decoded OBJ triangle mesh. */
    MergedEdgeSplitGeometry mergeObjVerticesForEdgeSplit(
        const DecodedObjMesh &mesh,
        double tolerance = 1e-4);

    /** Reproduces the r185 EdgeSplitModifier topology and normal update rules. */
    MergedEdgeSplitGeometry applyEdgeSplitModifier(
        const MergedEdgeSplitGeometry &geometry,
        double cutOffAngleRadians,
        bool tryKeepNormals = true);
}

#endif
