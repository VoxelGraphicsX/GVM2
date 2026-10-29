#include "EdgeSplitGeometry.hpp"

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/utility.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        /** Stores one recursively selected EdgeSplit index-occurrence group. */
        struct EdgeSplitRecord
        {
            size_t original = 0u;
            eastl::vector<size_t> occurrences;
        };

        /** Appends one JavaScript ToInt32 hash component to a mergeVertices key. */
        void appendMergeHashComponent(
            eastl::string &key,
            float value,
            double multiplier,
            double additive)
        {
            const double scaled =
                double(value) * multiplier + additive;
            if (!std::isfinite(scaled) ||
                scaled < double(std::numeric_limits<int32_t>::min()) ||
                scaled > double(std::numeric_limits<int32_t>::max()))
            {
                throw std::runtime_error(
                    "mergeVertices hash component exceeds signed 32-bit storage.");
            }
            const int32_t component =
                static_cast<int32_t>(scaled);
            char text[32u] = {};
            const int count = std::snprintf(
                text,
                sizeof(text),
                "%d,",
                component);
            if (count <= 0 ||
                static_cast<size_t>(count) >= sizeof(text))
            {
                throw std::runtime_error(
                    "mergeVertices could not format a hash component.");
            }
            key.append(text, static_cast<size_t>(count));
        }

        /** Appends one source vertex to the compact indexed geometry. */
        void appendMergedVertex(
            MergedEdgeSplitGeometry &result,
            const DecodedObjMesh &mesh,
            size_t vertexIndex)
        {
            result.positions.insert(
                result.positions.end(),
                mesh.positions.begin() + vertexIndex * 3u,
                mesh.positions.begin() + vertexIndex * 3u + 3u);
            result.normals.insert(
                result.normals.end(),
                mesh.normals.begin() + vertexIndex * 3u,
                mesh.normals.begin() + vertexIndex * 3u + 3u);
            result.textureCoordinates.insert(
                result.textureCoordinates.end(),
                mesh.textureCoordinates.begin() + vertexIndex * 2u,
                mesh.textureCoordinates.begin() + vertexIndex * 2u + 2u);
        }

        /** Reads one position from compact indexed geometry using double arithmetic. */
        void readPosition(
            const eastl::vector<float> &positions,
            uint32_t index,
            double &x,
            double &y,
            double &z)
        {
            x = positions[size_t(index) * 3u];
            y = positions[size_t(index) * 3u + 1u];
            z = positions[size_t(index) * 3u + 2u];
        }

        /** Computes one normalized face normal and stores Float32 values like Three.js. */
        void computeFaceNormal(
            const eastl::vector<float> &positions,
            const uint32_t *triangle,
            float &normalX,
            float &normalY,
            float &normalZ)
        {
            double ax = 0.0;
            double ay = 0.0;
            double az = 0.0;
            double bx = 0.0;
            double by = 0.0;
            double bz = 0.0;
            double cx = 0.0;
            double cy = 0.0;
            double cz = 0.0;
            readPosition(positions, triangle[0u], ax, ay, az);
            readPosition(positions, triangle[1u], bx, by, bz);
            readPosition(positions, triangle[2u], cx, cy, cz);
            const double cbx = cx - bx;
            const double cby = cy - by;
            const double cbz = cz - bz;
            const double abx = ax - bx;
            const double aby = ay - by;
            const double abz = az - bz;
            double x = cby * abz - cbz * aby;
            double y = cbz * abx - cbx * abz;
            double z = cbx * aby - cby * abx;
            const double length = std::sqrt(x * x + y * y + z * z);
            if (length > 0.0)
            {
                x /= length;
                y /= length;
                z /= length;
            }
            normalX = static_cast<float>(x);
            normalY = static_cast<float>(y);
            normalZ = static_cast<float>(z);
        }

        /** Computes one unnormalized Float32 face cross product for vertex normals. */
        void computeFaceCross(
            const eastl::vector<float> &positions,
            const uint32_t *triangle,
            float &normalX,
            float &normalY,
            float &normalZ)
        {
            double ax = 0.0;
            double ay = 0.0;
            double az = 0.0;
            double bx = 0.0;
            double by = 0.0;
            double bz = 0.0;
            double cx = 0.0;
            double cy = 0.0;
            double cz = 0.0;
            readPosition(positions, triangle[0u], ax, ay, az);
            readPosition(positions, triangle[1u], bx, by, bz);
            readPosition(positions, triangle[2u], cx, cy, cz);
            const double cbx = cx - bx;
            const double cby = cy - by;
            const double cbz = cz - bz;
            const double abx = ax - bx;
            const double aby = ay - by;
            const double abz = az - bz;
            normalX = static_cast<float>(cby * abz - cbz * aby);
            normalY = static_cast<float>(cbz * abx - cbx * abz);
            normalZ = static_cast<float>(cbx * aby - cby * abx);
        }

        /** Selects the occurrence group most aligned with one candidate face normal. */
        void splitOccurrencesToGroups(
            const eastl::vector<size_t> &occurrences,
            const eastl::vector<float> &faceNormals,
            double cutOff,
            size_t firstOccurrence,
            eastl::vector<size_t> &currentGroup,
            eastl::vector<size_t> &splitGroup)
        {
            const double ax = faceNormals[firstOccurrence * 3u];
            const double ay = faceNormals[firstOccurrence * 3u + 1u];
            const double az = faceNormals[firstOccurrence * 3u + 2u];
            currentGroup.clear();
            splitGroup.clear();
            currentGroup.push_back(firstOccurrence);
            for (const size_t occurrence : occurrences)
            {
                if (occurrence == firstOccurrence) continue;
                const double dot =
                    double(faceNormals[occurrence * 3u]) * ax +
                    double(faceNormals[occurrence * 3u + 1u]) * ay +
                    double(faceNormals[occurrence * 3u + 2u]) * az;
                if (dot < cutOff)
                {
                    splitGroup.push_back(occurrence);
                }
                else
                {
                    currentGroup.push_back(occurrence);
                }
            }
        }

        /** Recursively reproduces the r185 EdgeSplitModifier grouping order. */
        void splitEdgeOccurrences(
            const eastl::vector<size_t> &occurrences,
            const eastl::vector<float> &faceNormals,
            double cutOff,
            size_t original,
            bool hasOriginal,
            eastl::vector<EdgeSplitRecord> &records)
        {
            if (occurrences.empty()) return;
            eastl::vector<size_t> bestCurrent;
            eastl::vector<size_t> bestSplit;
            eastl::vector<size_t> current;
            eastl::vector<size_t> split;
            for (const size_t candidate : occurrences)
            {
                splitOccurrencesToGroups(
                    occurrences,
                    faceNormals,
                    cutOff,
                    candidate,
                    current,
                    split);
                if (bestCurrent.empty() ||
                    current.size() > bestCurrent.size())
                {
                    bestCurrent = current;
                    bestSplit = split;
                }
            }
            if (hasOriginal)
            {
                EdgeSplitRecord record;
                record.original = original;
                record.occurrences = bestCurrent;
                records.push_back(eastl::move(record));
            }
            if (!bestSplit.empty())
            {
                const size_t nextOriginal =
                    hasOriginal && original != 0u
                        ? original
                        : bestCurrent[0u];
                splitEdgeOccurrences(
                    bestSplit,
                    faceNormals,
                    cutOff,
                    nextOriginal,
                    true,
                    records);
            }
        }

        /** Computes indexed BufferGeometry vertex normals with Three.js accumulation order. */
        eastl::vector<float> computeIndexedVertexNormals(
            const eastl::vector<float> &positions,
            const eastl::vector<uint32_t> &indices)
        {
            eastl::vector<float> normals(positions.size(), 0.0f);
            for (size_t offset = 0u; offset < indices.size(); offset += 3u)
            {
                float nx = 0.0f;
                float ny = 0.0f;
                float nz = 0.0f;
                computeFaceCross(positions, indices.data() + offset, nx, ny, nz);
                for (uint32_t corner = 0u; corner < 3u; ++corner)
                {
                    const size_t normalOffset =
                        size_t(indices[offset + corner]) * 3u;
                    normals[normalOffset] += nx;
                    normals[normalOffset + 1u] += ny;
                    normals[normalOffset + 2u] += nz;
                }
            }
            for (size_t offset = 0u; offset < normals.size(); offset += 3u)
            {
                const double x = normals[offset];
                const double y = normals[offset + 1u];
                const double z = normals[offset + 2u];
                const double length = std::sqrt(x * x + y * y + z * z);
                if (length > 0.0)
                {
                    normals[offset] = static_cast<float>(x / length);
                    normals[offset + 1u] = static_cast<float>(y / length);
                    normals[offset + 2u] = static_cast<float>(z / length);
                }
            }
            return normals;
        }
    }

    /** Reproduces BufferGeometryUtils.mergeVertices for one decoded OBJ triangle mesh. */
    MergedEdgeSplitGeometry mergeObjVerticesForEdgeSplit(
        const DecodedObjMesh &mesh,
        double tolerance)
    {
        const size_t vertexCount = mesh.positions.size() / 3u;
        if (vertexCount == 0u ||
            mesh.positions.size() % 3u != 0u ||
            mesh.normals.size() != mesh.positions.size() ||
            mesh.textureCoordinates.size() != vertexCount * 2u)
        {
            throw std::invalid_argument(
                "mergeVertices requires complete triangle-expanded OBJ attributes.");
        }
        tolerance = std::max(
            tolerance,
            std::numeric_limits<double>::epsilon());
        const double halfTolerance = tolerance * 0.5;
        const double exponent = std::log10(1.0 / tolerance);
        const double multiplier = std::pow(10.0, exponent);
        const double additive = halfTolerance * multiplier;

        eastl::unordered_map<eastl::string, uint32_t> hashToIndex;
        hashToIndex.reserve(vertexCount);
        MergedEdgeSplitGeometry result;
        result.positions.reserve(mesh.positions.size());
        result.normals.reserve(mesh.normals.size());
        result.textureCoordinates.reserve(
            mesh.textureCoordinates.size());
        result.indices.reserve(vertexCount);
        for (size_t vertexIndex = 0u;
             vertexIndex < vertexCount;
             ++vertexIndex)
        {
            eastl::string key;
            key.reserve(96u);
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                appendMergeHashComponent(
                    key,
                    mesh.positions[vertexIndex * 3u + component],
                    multiplier,
                    additive);
            }
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                appendMergeHashComponent(
                    key,
                    mesh.normals[vertexIndex * 3u + component],
                    multiplier,
                    additive);
            }
            for (uint32_t component = 0u; component < 2u; ++component)
            {
                appendMergeHashComponent(
                    key,
                    mesh.textureCoordinates[
                        vertexIndex * 2u + component],
                    multiplier,
                    additive);
            }
            const auto found = hashToIndex.find(key);
            if (found != hashToIndex.end())
            {
                result.indices.push_back(found->second);
                continue;
            }
            const size_t nextIndex = result.positions.size() / 3u;
            if (nextIndex > std::numeric_limits<uint32_t>::max())
            {
                throw std::overflow_error(
                    "mergeVertices result exceeds uint32 index storage.");
            }
            const uint32_t compactIndex =
                static_cast<uint32_t>(nextIndex);
            appendMergedVertex(result, mesh, vertexIndex);
            hashToIndex.emplace(eastl::move(key), compactIndex);
            result.indices.push_back(compactIndex);
        }
        return result;
    }

    /** Reproduces the r185 EdgeSplitModifier topology and normal update rules. */
    MergedEdgeSplitGeometry applyEdgeSplitModifier(
        const MergedEdgeSplitGeometry &geometry,
        double cutOffAngleRadians,
        bool tryKeepNormals)
    {
        const size_t compactVertexCount = geometry.positions.size() / 3u;
        if (compactVertexCount == 0u ||
            geometry.positions.size() % 3u != 0u ||
            geometry.normals.size() != geometry.positions.size() ||
            geometry.textureCoordinates.size() != compactVertexCount * 2u ||
            geometry.indices.empty() || geometry.indices.size() % 3u != 0u)
        {
            throw std::invalid_argument(
                "EdgeSplitModifier requires complete indexed triangle attributes.");
        }
        eastl::vector<float> faceNormals(
            geometry.indices.size() * 3u,
            0.0f);
        for (size_t offset = 0u;
             offset < geometry.indices.size();
             offset += 3u)
        {
            float nx = 0.0f;
            float ny = 0.0f;
            float nz = 0.0f;
            computeFaceNormal(
                geometry.positions,
                geometry.indices.data() + offset,
                nx,
                ny,
                nz);
            for (uint32_t corner = 0u; corner < 3u; ++corner)
            {
                const size_t normalOffset = (offset + corner) * 3u;
                faceNormals[normalOffset] = nx;
                faceNormals[normalOffset + 1u] = ny;
                faceNormals[normalOffset + 2u] = nz;
            }
        }

        eastl::vector<eastl::vector<size_t>> vertexOccurrences(
            compactVertexCount);
        for (size_t occurrence = 0u;
             occurrence < geometry.indices.size();
             ++occurrence)
        {
            const uint32_t vertexIndex = geometry.indices[occurrence];
            if (vertexIndex >= compactVertexCount)
            {
                throw std::out_of_range(
                    "EdgeSplitModifier index exceeds the compact vertex count.");
            }
            vertexOccurrences[vertexIndex].push_back(occurrence);
        }

        eastl::vector<EdgeSplitRecord> splitRecords;
        const double cutOff = std::cos(cutOffAngleRadians) - 0.001;
        for (const eastl::vector<size_t> &occurrences : vertexOccurrences)
        {
            splitEdgeOccurrences(
                occurrences,
                faceNormals,
                cutOff,
                0u,
                false,
                splitRecords);
        }

        MergedEdgeSplitGeometry result;
        const size_t outputVertexCount =
            geometry.indices.size() + splitRecords.size();
        result.positions.resize(outputVertexCount * 3u, 0.0f);
        result.normals.resize(outputVertexCount * 3u, 0.0f);
        result.textureCoordinates.resize(outputVertexCount * 2u, 0.0f);
        std::copy(
            geometry.positions.begin(),
            geometry.positions.end(),
            result.positions.begin());
        std::copy(
            geometry.normals.begin(),
            geometry.normals.end(),
            result.normals.begin());
        std::copy(
            geometry.textureCoordinates.begin(),
            geometry.textureCoordinates.end(),
            result.textureCoordinates.begin());
        result.indices = geometry.indices;
        for (size_t recordIndex = 0u;
             recordIndex < splitRecords.size();
             ++recordIndex)
        {
            const EdgeSplitRecord &record = splitRecords[recordIndex];
            const uint32_t sourceIndex = geometry.indices[record.original];
            const size_t targetIndex = geometry.indices.size() + recordIndex;
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                result.positions[targetIndex * 3u + component] =
                    geometry.positions[size_t(sourceIndex) * 3u + component];
                result.normals[targetIndex * 3u + component] =
                    geometry.normals[size_t(sourceIndex) * 3u + component];
            }
            for (uint32_t component = 0u; component < 2u; ++component)
            {
                result.textureCoordinates[targetIndex * 2u + component] =
                    geometry.textureCoordinates[size_t(sourceIndex) * 2u + component];
            }
            for (const size_t occurrence : record.occurrences)
            {
                result.indices[occurrence] =
                    static_cast<uint32_t>(targetIndex);
            }
        }

        result.normals = computeIndexedVertexNormals(
            result.positions,
            result.indices);
        if (tryKeepNormals)
        {
            eastl::vector<uint8_t> changedNormals(
                geometry.normals.size() / 3u,
                0u);
            for (const EdgeSplitRecord &record : splitRecords)
            {
                if (record.original < changedNormals.size())
                {
                    changedNormals[record.original] = 1u;
                }
            }
            for (size_t vertexIndex = 0u;
                 vertexIndex < changedNormals.size();
                 ++vertexIndex)
            {
                if (changedNormals[vertexIndex] != 0u) continue;
                for (uint32_t component = 0u; component < 3u; ++component)
                {
                    result.normals[vertexIndex * 3u + component] =
                        geometry.normals[vertexIndex * 3u + component];
                }
            }
        }
        return result;
    }
}
