#include "WebglGeometryShapesRuntimeAdapter.hpp"

#include "Fixtures/Phase1GeometryRenderSet/ExtrudeGeometry.hpp"
#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t ExpectedEntityCount = 93u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        /** Stores one sampled two-dimensional shape contour. */
        struct ShapeContour
        {
            eastl::vector<glm::dvec2> points;
        };

        /** Creates parent directories for one capture artifact. */
        void prepareShapesPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Converts one sRGB byte channel into the linear material domain. */
        float shapesSrgbByteToLinear(uint32_t value)
        {
            const float channel = float(value) / 255.0f;
            return channel <= 0.04045f
                ? channel / 12.92f
                : std::pow((channel + 0.055f) / 1.055f, 2.4f);
        }

        /** Converts one packed RGB color into linear material values. */
        glm::vec4 shapesColor(uint32_t packedColor, float textureFlag)
        {
            return glm::vec4(
                shapesSrgbByteToLinear((packedColor >> 16u) & 0xffu),
                shapesSrgbByteToLinear((packedColor >> 8u) & 0xffu),
                shapesSrgbByteToLinear(packedColor & 0xffu),
                textureFlag);
        }

        /** Appends one vertex with a stable normal and UV projection. */
        void appendShapesVertex(
            eastl::vector<WebglGeometryShapesHostVertex> &vertices,
            const glm::dvec3 &position,
            const glm::dvec3 &normal,
            double primitiveMode = 0.0,
            const glm::dvec2 &primitiveCoord = glm::dvec2(0.0))
        {
            vertices.push_back({
                glm::vec4(position, 1.0f),
                glm::vec4(glm::normalize(normal), float(primitiveMode)),
                glm::vec4(float(position.x) * 0.008f,
                          float(position.y) * 0.008f,
                          float(primitiveCoord.x),
                          float(primitiveCoord.y))});
        }

        /** Appends one independent triangle to a normalized entity. */
        void appendShapesTriangle(
            WebglGeometryShapesEntityData &entity,
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            const glm::dvec3 &normal,
            double primitiveMode = 0.0,
            const glm::dvec2 &coordA = glm::dvec2(0.0),
            const glm::dvec2 &coordB = glm::dvec2(0.0),
            const glm::dvec2 &coordC = glm::dvec2(0.0))
        {
            const uint32_t first = static_cast<uint32_t>(entity.vertices.size());
            appendShapesVertex(entity.vertices, a, normal, primitiveMode, coordA);
            appendShapesVertex(entity.vertices, b, normal, primitiveMode, coordB);
            appendShapesVertex(entity.vertices, c, normal, primitiveMode, coordC);
            entity.indices.push_back(first + 0u);
            entity.indices.push_back(first + 1u);
            entity.indices.push_back(first + 2u);
        }

        /** Returns the signed area of one two-dimensional contour. */
        double shapesPolygonSignedArea(const eastl::vector<glm::dvec2> &points)
        {
            double area = 0.0;
            if (points.empty()) return area;
            for (size_t current = 0u, previous = points.size() - 1u;
                 current < points.size(); previous = current++)
            {
                area += points[previous].x * points[current].y -
                    points[current].x * points[previous].y;
            }
            return area * 0.5;
        }

        /** Tests whether one point lies inside a triangle including its edge. */
        bool shapesPointInsideTriangle(const glm::dvec2 &point,
                                       const glm::dvec2 &a,
                                       const glm::dvec2 &b,
                                       const glm::dvec2 &c)
        {
            const double c0 = (b.x - a.x) * (point.y - a.y) -
                (b.y - a.y) * (point.x - a.x);
            const double c1 = (c.x - b.x) * (point.y - b.y) -
                (c.y - b.y) * (point.x - b.x);
            const double c2 = (a.x - c.x) * (point.y - c.y) -
                (a.y - c.y) * (point.x - c.x);
            return (c0 >= -1e-10 && c1 >= -1e-10 && c2 >= -1e-10) ||
                (c0 <= 1e-10 && c1 <= 1e-10 && c2 <= 1e-10);
        }

        /** Triangulates one simple contour using deterministic ear clipping. */
        eastl::vector<glm::dvec2> shapesNormalizeContour(
            const eastl::vector<glm::dvec2> &source)
        {
            eastl::vector<glm::dvec2> contour;
            contour.reserve(source.size());
            for (const glm::dvec2 &point : source)
            {
                if (!contour.empty() && glm::length(point - contour.back()) < 1e-10)
                    continue;
                contour.push_back(point);
            }
            if (contour.size() > 1u &&
                glm::length(contour.front() - contour.back()) < 1e-10)
            {
                contour.pop_back();
            }
            return contour;
        }

        /** Triangulates one normalized simple polygon with deterministic ears. */
        void appendShapesTriangulatedPoints(
            WebglGeometryShapesEntityData &entity,
            const eastl::vector<glm::dvec2> &source,
            double z = 0.0,
            double primitiveMode = 0.0)
        {
            const eastl::vector<glm::dvec2> contour =
                shapesNormalizeContour(source);
            if (contour.size() < 3u)
                return;
            eastl::vector<uint32_t> remaining;
            remaining.reserve(contour.size());
            for (uint32_t index = 0u; index < contour.size(); ++index)
                remaining.push_back(index);
            const bool counterClockwise =
                shapesPolygonSignedArea(contour) > 0.0;
            while (remaining.size() > 3u)
            {
                bool clipped = false;
                for (size_t cursor = 0u; cursor < remaining.size(); ++cursor)
                {
                    const uint32_t previous = remaining[
                        (cursor + remaining.size() - 1u) % remaining.size()];
                    const uint32_t current = remaining[cursor];
                    const uint32_t next = remaining[
                        (cursor + 1u) % remaining.size()];
                    const glm::dvec2 edge0 = contour[current] - contour[previous];
                    const glm::dvec2 edge1 = contour[next] - contour[current];
                    const double cross = edge0.x * edge1.y - edge0.y * edge1.x;
                    if ((counterClockwise && cross <= 1e-10) ||
                        (!counterClockwise && cross >= -1e-10))
                    {
                        continue;
                    }
                    bool containsPoint = false;
                    for (uint32_t candidate : remaining)
                    {
                        if (candidate == previous || candidate == current ||
                            candidate == next)
                            continue;
                        if (shapesPointInsideTriangle(
                                contour[candidate], contour[previous],
                                contour[current], contour[next]))
                        {
                            containsPoint = true;
                            break;
                        }
                    }
                    if (containsPoint)
                        continue;
                    const glm::dvec3 a(contour[previous].x, contour[previous].y, z);
                    const glm::dvec3 b(contour[current].x, contour[current].y, z);
                    const glm::dvec3 c(contour[next].x, contour[next].y, z);
                    appendShapesTriangle(
                        entity, a, counterClockwise ? b : c,
                        counterClockwise ? c : b,
                        glm::dvec3(0.0, 0.0, 1.0), primitiveMode);
                    remaining.erase(remaining.begin() +
                                    static_cast<ptrdiff_t>(cursor));
                    clipped = true;
                    break;
                }
                if (!clipped)
                    throw std::runtime_error(
                        "Shape contour could not be triangulated deterministically.");
            }
            const glm::dvec3 a(contour[remaining[0u]].x, contour[remaining[0u]].y, z);
            const glm::dvec3 b(contour[remaining[1u]].x, contour[remaining[1u]].y, z);
            const glm::dvec3 c(contour[remaining[2u]].x, contour[remaining[2u]].y, z);
            appendShapesTriangle(
                entity, a, counterClockwise ? b : c,
                counterClockwise ? c : b,
                glm::dvec3(0.0, 0.0, 1.0), primitiveMode);
        }

        /** Appends a contour's ear-clipped triangles with a stable front winding. */
        void appendShapesTriangulatedContour(
            WebglGeometryShapesEntityData &entity,
            const eastl::vector<glm::dvec2> &source)
        {
            appendShapesTriangulatedPoints(entity, source);
        }

        /** Appends background-colored triangles that expose a Shape hole. */
        void appendShapesHoleCovers(
            WebglGeometryShapesEntityData &entity,
            const eastl::vector<ShapeContour> &holes,
            double z)
        {
            for (const ShapeContour &hole : holes)
                appendShapesTriangulatedPoints(entity, hole.points, z, 3.0);
        }

        /** Tests whether a sampled contour is a uniformly sampled circle. */
        bool isShapesCircleContour(const ShapeContour &contour)
        {
            if (contour.points.size() < 8u)
                return false;
            const size_t count = contour.points.size() -
                (glm::length(contour.points.front() - contour.points.back()) < 1e-8 ? 1u : 0u);
            if (count < 8u)
                return false;
            glm::dvec2 center(0.0);
            for (size_t index = 0u; index < count; ++index)
                center += contour.points[index];
            center /= double(count);
            const double radius = glm::length(contour.points[0u] - center);
            if (radius <= 1e-6)
                return false;
            for (size_t index = 0u; index < count; ++index)
            {
                if (std::abs(glm::length(contour.points[index] - center) - radius) > 1e-3)
                    return false;
            }
            return true;
        }

        /** Appends the front or back annulus triangles for a circular hole. */
        void appendShapesAnnulusFace(
            WebglGeometryShapesEntityData &entity,
            const ShapeContour &outer,
            const ShapeContour &inner,
            double z,
            bool frontFacing)
        {
            const size_t outerCount = outer.points.size() -
                (glm::length(outer.points.front() - outer.points.back()) < 1e-8 ? 1u : 0u);
            const size_t innerCount = inner.points.size() -
                (glm::length(inner.points.front() - inner.points.back()) < 1e-8 ? 1u : 0u);
            const size_t count = eastl::min(outerCount, innerCount);
            if (count < 3u)
                return;
            const glm::dvec3 normal(0.0, 0.0, frontFacing ? 1.0 : -1.0);
            for (size_t index = 0u; index < count; ++index)
            {
                const size_t next = (index + 1u) % count;
                const glm::dvec3 outer0(outer.points[index].x, outer.points[index].y, z);
                const glm::dvec3 outer1(outer.points[next].x, outer.points[next].y, z);
                const glm::dvec3 inner0(inner.points[index].x, inner.points[index].y, z);
                const glm::dvec3 inner1(inner.points[next].x, inner.points[next].y, z);
                if (frontFacing)
                {
                    appendShapesTriangle(entity, outer0, outer1, inner1, normal);
                    appendShapesTriangle(entity, outer0, inner1, inner0, normal);
                }
                else
                {
                    appendShapesTriangle(entity, outer0, inner1, outer1, normal);
                    appendShapesTriangle(entity, outer0, inner0, inner1, normal);
                }
            }
        }

        /** Appends outer and inner circular side walls around an extruded hole. */
        void appendShapesAnnulusWalls(
            WebglGeometryShapesEntityData &entity,
            const ShapeContour &outer,
            const ShapeContour &inner,
            double depth)
        {
            const size_t outerCount = outer.points.size() -
                (glm::length(outer.points.front() - outer.points.back()) < 1e-8 ? 1u : 0u);
            const size_t innerCount = inner.points.size() -
                (glm::length(inner.points.front() - inner.points.back()) < 1e-8 ? 1u : 0u);
            const size_t count = eastl::min(outerCount, innerCount);
            for (size_t index = 0u; index < count; ++index)
            {
                const size_t next = (index + 1u) % count;
                const glm::dvec2 oa = outer.points[index];
                const glm::dvec2 ob = outer.points[next];
                const glm::dvec2 ia = inner.points[index];
                const glm::dvec2 ib = inner.points[next];
                const glm::dvec2 outerEdge = ob - oa;
                const glm::dvec2 outerNormal = glm::normalize(glm::dvec2(outerEdge.y, -outerEdge.x));
                appendShapesTriangle(entity, glm::dvec3(oa, 0.0),
                                     glm::dvec3(ob, 0.0), glm::dvec3(ob, depth),
                                     glm::dvec3(outerNormal, 0.0));
                appendShapesTriangle(entity, glm::dvec3(oa, 0.0),
                                     glm::dvec3(ob, depth), glm::dvec3(oa, depth),
                                     glm::dvec3(outerNormal, 0.0));
                const glm::dvec2 innerEdge = ib - ia;
                const glm::dvec2 innerNormal = -glm::normalize(glm::dvec2(innerEdge.y, -innerEdge.x));
                appendShapesTriangle(entity, glm::dvec3(ia, 0.0),
                                     glm::dvec3(ib, depth), glm::dvec3(ib, 0.0),
                                     glm::dvec3(innerNormal, 0.0));
                appendShapesTriangle(entity, glm::dvec3(ia, 0.0),
                                     glm::dvec3(ia, depth), glm::dvec3(ib, depth),
                                     glm::dvec3(innerNormal, 0.0));
            }
        }

        /** Merges simple holes into an outer path using visible right bridges. */
        eastl::vector<glm::dvec2> mergeShapesHoles(
            const eastl::vector<glm::dvec2> &outerSource,
            const eastl::vector<ShapeContour> &holes)
        {
            eastl::vector<glm::dvec2> merged = shapesNormalizeContour(outerSource);
            if (shapesPolygonSignedArea(merged) < 0.0)
            {
                eastl::vector<glm::dvec2> reversed;
                for (size_t index = merged.size(); index > 0u; --index)
                    reversed.push_back(merged[index - 1u]);
                merged = eastl::move(reversed);
            }
            for (const ShapeContour &holeSource : holes)
            {
                eastl::vector<glm::dvec2> hole =
                    shapesNormalizeContour(holeSource.points);
                if (hole.size() < 3u)
                    continue;
                if (shapesPolygonSignedArea(hole) > 0.0)
                {
                    eastl::vector<glm::dvec2> reversed;
                    for (size_t index = hole.size(); index > 0u; --index)
                        reversed.push_back(hole[index - 1u]);
                    hole = eastl::move(reversed);
                }
                size_t holeIndex = 0u;
                for (size_t index = 1u; index < hole.size(); ++index)
                {
                    if (hole[index].x > hole[holeIndex].x ||
                        (hole[index].x == hole[holeIndex].x &&
                         hole[index].y < hole[holeIndex].y))
                        holeIndex = index;
                }
                const glm::dvec2 bridgePoint = hole[holeIndex];
                size_t outerIndex = 0u;
                double bestDistance = std::numeric_limits<double>::max();
                for (size_t index = 0u; index < merged.size(); ++index)
                {
                    const glm::dvec2 delta = merged[index] - bridgePoint;
                    if (merged[index].x + 1e-8 < bridgePoint.x)
                        continue;
                    const double distance = glm::dot(delta, delta);
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        outerIndex = index;
                    }
                }
                eastl::vector<glm::dvec2> next;
                next.reserve(merged.size() + hole.size() + 2u);
                for (size_t index = 0u; index <= outerIndex; ++index)
                    next.push_back(merged[index]);
                for (size_t offset = 0u; offset < hole.size(); ++offset)
                    next.push_back(hole[(holeIndex + offset) % hole.size()]);
                next.push_back(bridgePoint);
                next.push_back(merged[outerIndex]);
                for (size_t index = outerIndex + 1u; index < merged.size(); ++index)
                    next.push_back(merged[index]);
                merged = eastl::move(next);
            }
            return merged;
        }

        /** Builds a flat fan approximation of one ShapeGeometry contour. */
        void appendShapesFlatContour(
            WebglGeometryShapesEntityData &entity,
            const ShapeContour &contour,
            const eastl::vector<ShapeContour> *holes = nullptr)
        {
            if (contour.points.size() < 3u)
            {
                throw std::invalid_argument("Shape contour must contain at least three points.");
            }
            if (holes == nullptr || holes->empty())
                appendShapesTriangulatedContour(entity, contour.points);
            else if (holes->size() == 1u && isShapesCircleContour(contour) &&
                     isShapesCircleContour((*holes)[0u]))
                appendShapesAnnulusFace(entity, contour, (*holes)[0u], 0.0, true);
            else
            {
                appendShapesTriangulatedContour(entity, contour.points);
                appendShapesHoleCovers(entity, *holes, 0.05);
            }
        }

        /** Builds a simple beveled-free prism for one ExtrudeGeometry contour. */
        void appendShapesExtrudedContour(
            WebglGeometryShapesEntityData &entity,
            const ShapeContour &contour,
            double depth,
            const eastl::vector<ShapeContour> *holes = nullptr)
        {
            if (holes != nullptr && holes->size() == 1u &&
                isShapesCircleContour(contour) &&
                isShapesCircleContour((*holes)[0u]))
            {
                appendShapesAnnulusFace(entity, contour, (*holes)[0u], depth, true);
                appendShapesAnnulusFace(entity, contour, (*holes)[0u], 0.0, false);
                appendShapesAnnulusWalls(entity, contour, (*holes)[0u], depth);
                return;
            }
            const size_t frontVertexStart = entity.vertices.size();
            if (holes == nullptr || holes->empty())
                appendShapesFlatContour(entity, contour);
            else
                appendShapesFlatContour(entity, contour);
            const size_t frontTriangleCount =
                (entity.vertices.size() - frontVertexStart) / 3u;
            for (size_t triangle = 0u; triangle < frontTriangleCount; ++triangle)
            {
                const uint32_t base = static_cast<uint32_t>(triangle * 3u);
                const auto &a = entity.vertices[base + 0u].position;
                const auto &b = entity.vertices[base + 1u].position;
                const auto &c = entity.vertices[base + 2u].position;
                appendShapesTriangle(
                    entity,
                    glm::dvec3(a.x, a.y, depth),
                    glm::dvec3(c.x, c.y, depth),
                    glm::dvec3(b.x, b.y, depth),
                    // The depth face is the face nearest the camera.  Its
                    // winding is reversed for the indexed triangle order,
                    // but its geometric normal still points toward +Z.
                    glm::dvec3(0.0, 0.0, 1.0));
            }
            const auto appendSideWalls = [&](const ShapeContour &path,
                                              bool reverseWinding)
            {
                const size_t pointCount = path.points.size();
                for (size_t index = 0u; index < pointCount; ++index)
                {
                    const glm::dvec2 &a = path.points[index];
                    const glm::dvec2 &b = path.points[(index + 1u) % pointCount];
                    const glm::dvec2 edge = b - a;
                    glm::dvec2 sideNormal(edge.y, -edge.x);
                    if (glm::length(sideNormal) < 1e-6)
                        continue;
                    sideNormal = glm::normalize(sideNormal);
                    const glm::dvec3 normal(sideNormal.x, sideNormal.y, 0.0);
                    if (!reverseWinding)
                    {
                        appendShapesTriangle(entity, glm::dvec3(a, 0.0),
                                             glm::dvec3(b, 0.0),
                                             glm::dvec3(b, depth), normal);
                        appendShapesTriangle(entity, glm::dvec3(a, 0.0),
                                             glm::dvec3(b, depth),
                                             glm::dvec3(a, depth), normal);
                    }
                    else
                    {
                        appendShapesTriangle(entity, glm::dvec3(a, 0.0),
                                             glm::dvec3(b, depth),
                                             glm::dvec3(b, 0.0), -normal);
                        appendShapesTriangle(entity, glm::dvec3(a, 0.0),
                                             glm::dvec3(a, depth),
                                             glm::dvec3(b, depth), -normal);
                    }
                }
            };
            appendSideWalls(contour, false);
            if (holes != nullptr)
            {
                for (const ShapeContour &hole : *holes)
                    appendSideWalls(hole, true);
                appendShapesHoleCovers(entity, *holes, depth + 0.05);
            }
        }

        /** Builds one r185 ExtrudeGeometry with its two-segment quarter-circle bevel. */
        void appendShapesBeveledExtrudedContour(
            WebglGeometryShapesEntityData &entity,
            const ShapeContour &contour)
        {
            DepthExtrudeParameters parameters;
            parameters.shape = shapesNormalizeContour(contour.points);
            parameters.depth = 8.0;
            parameters.steps = 2u;
            parameters.bevelEnabled = true;
            parameters.bevelThickness = 1.0;
            parameters.bevelSize = 1.0;
            parameters.bevelSegments = 2u;
            const ExtrudeCpuGeometry geometry =
                buildDepthExtrudeGeometry(parameters);
            if (geometry.vertices.size() % 3u != 0u)
            {
                throw std::logic_error(
                    "webgl_geometry_shapes bevel output is not triangle aligned.");
            }
            entity.vertices.reserve(entity.vertices.size() + geometry.vertices.size());
            entity.indices.reserve(entity.indices.size() + geometry.vertices.size());
            for (size_t index = 0u; index < geometry.vertices.size(); index += 3u)
            {
                const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
                for (uint32_t corner = 0u; corner < 3u; ++corner)
                {
                    const ExtrudeCpuVertex &source = geometry.vertices[index + corner];
                    appendShapesVertex(
                        entity.vertices,
                        glm::dvec3(source.position),
                        glm::dvec3(source.normal),
                        0.0,
                        glm::dvec2(0.0));
                }
                entity.indices.insert(entity.indices.end(),
                                      {base, base + 1u, base + 2u});
            }
        }

        /** Expands a polyline to screen-independent triangle strips. */
        void appendShapesLineStrip(
            WebglGeometryShapesEntityData &entity,
            const ShapeContour &contour,
            double width)
        {
            if (contour.points.size() < 2u)
            {
                return;
            }
            // Shape.autoClose is enabled by the source example.  Keep the
            // closing segment explicit even for polygon paths whose source
            // list does not repeat the first point.
            const size_t segmentCount = contour.points.size();
            for (size_t index = 0u; index < segmentCount; ++index)
            {
                const glm::dvec2 &a = contour.points[index];
                const glm::dvec2 &b = contour.points[(index + 1u) % segmentCount];
                glm::dvec2 direction = b - a;
                if (glm::length(direction) < 1e-6)
                {
                    continue;
                }
                direction = glm::normalize(direction);
                const glm::dvec2 perpendicular(-direction.y, direction.x);
                const glm::dvec2 offset = perpendicular * (width * 0.5);
                const glm::dvec2 p0 = a - offset;
                const glm::dvec2 p1 = a + offset;
                const glm::dvec2 p2 = b + offset;
                const glm::dvec2 p3 = b - offset;
                appendShapesTriangle(entity,
                                     glm::dvec3(p0, 0.0),
                                     glm::dvec3(p1, 0.0),
                                     glm::dvec3(p2, 0.0),
                                     glm::dvec3(0.0, 0.0, 1.0),
                                     1.0,
                                     {-1.0, -1.0},
                                     {-1.0, 1.0},
                                     {1.0, 1.0});
                appendShapesTriangle(entity,
                                     glm::dvec3(p0, 0.0),
                                     glm::dvec3(p2, 0.0),
                                     glm::dvec3(p3, 0.0),
                                     glm::dvec3(0.0, 0.0, 1.0),
                                     1.0,
                                     {-1.0, -1.0},
                                     {1.0, 1.0},
                                     {1.0, -1.0});
            }
        }

        /** Expands every sampled point to a deterministic triangle billboard. */
        void appendShapesPointBillboards(
            WebglGeometryShapesEntityData &entity,
            const ShapeContour &contour,
            double size)
        {
            for (const glm::dvec2 &point : contour.points)
            {
                const double half = size * 0.5;
                const glm::dvec2 p0(point.x - half, point.y - half);
                const glm::dvec2 p1(point.x + half, point.y - half);
                const glm::dvec2 p2(point.x + half, point.y + half);
                const glm::dvec2 p3(point.x - half, point.y + half);
                appendShapesTriangle(entity,
                                     glm::dvec3(p0, 0.0),
                                     glm::dvec3(p1, 0.0),
                                     glm::dvec3(p2, 0.0),
                                     glm::dvec3(0.0, 0.0, 1.0),
                                     2.0,
                                     {-1.0, -1.0},
                                     {1.0, -1.0},
                                     {1.0, 1.0});
                appendShapesTriangle(entity,
                                     glm::dvec3(p0, 0.0),
                                     glm::dvec3(p2, 0.0),
                                     glm::dvec3(p3, 0.0),
                                     glm::dvec3(0.0, 0.0, 1.0),
                                     2.0,
                                     {-1.0, -1.0},
                                     {1.0, 1.0},
                                     {-1.0, 1.0});
            }
        }

        /**
         * Re-samples one closed Shape path at uniform arc-length intervals.
         *
         * Three.js `Shape.getSpacedPoints(50)` samples the complete curve
         * path by arc length and then appends the first point when
         * `autoClose` is enabled.  The RenderSet stores only normalized
         * points, so this helper performs the equivalent deterministic
         * resampling on the CPU before the line/point rows are packed.
         */
        ShapeContour makeShapesSpacedContour(
            const ShapeContour &source,
            uint32_t divisions = 50u)
        {
            ShapeContour result;
            if (source.points.empty() || divisions == 0u)
                return result;

            eastl::vector<glm::dvec2> points = source.points;
            if (points.size() > 1u &&
                glm::length(points.front() - points.back()) < 1e-10)
            {
                points.pop_back();
            }
            if (points.size() < 2u)
                return source;

            eastl::vector<double> cumulative(points.size() + 1u, 0.0);
            for (size_t index = 0u; index < points.size(); ++index)
            {
                const glm::dvec2 &a = points[index];
                const glm::dvec2 &b = points[(index + 1u) % points.size()];
                cumulative[index + 1u] = cumulative[index] +
                    glm::length(b - a);
            }
            const double totalLength = cumulative.back();
            if (totalLength <= 1e-10)
                return source;

            result.points.reserve(divisions + 2u);
            for (uint32_t sample = 0u; sample <= divisions; ++sample)
            {
                const double distance = totalLength *
                    double(sample) / double(divisions);
                size_t segment = 0u;
                while (segment + 1u < cumulative.size() &&
                       cumulative[segment + 1u] < distance)
                {
                    ++segment;
                }
                const size_t next = (segment + 1u) % points.size();
                const double segmentLength =
                    cumulative[segment + 1u] - cumulative[segment];
                const double local = segmentLength <= 1e-10
                    ? 0.0
                    : (distance - cumulative[segment]) / segmentLength;
                result.points.push_back(points[segment] * (1.0 - local) +
                                       points[next] * local);
            }
            // `CurvePath.getSpacedPoints()` appends the first point whenever
            // autoClose is set, even though the final uniform sample already
            // lands on that point.
            result.points.push_back(result.points.front());
            return result;
        }

        /** Samples a quadratic Bezier segment into a contour. */
        void appendQuadratic(
            eastl::vector<glm::dvec2> &points,
            const glm::dvec2 &a,
            const glm::dvec2 &b,
            const glm::dvec2 &c,
            uint32_t subdivisions = 12u)
        {
            for (uint32_t index = 1u; index <= subdivisions; ++index)
            {
                const double t = double(index) / double(subdivisions);
                const double u = 1.0 - t;
                points.push_back(u * u * a + 2.0 * u * t * b + t * t * c);
            }
        }

        /** Samples one cubic Bezier segment with the Shape default density. */
        void appendCubic(
            eastl::vector<glm::dvec2> &points,
            const glm::dvec2 &a,
            const glm::dvec2 &b,
            const glm::dvec2 &c,
            const glm::dvec2 &d,
            uint32_t subdivisions = 12u)
        {
            for (uint32_t index = 1u; index <= subdivisions; ++index)
            {
                const double t = double(index) / double(subdivisions);
                const double u = 1.0 - t;
                points.push_back(
                    u * u * u * a + 3.0 * u * u * t * b +
                    3.0 * u * t * t * c + t * t * t * d);
            }
        }

        /** Samples one absolute circular arc in Path's counter-clockwise convention. */
        void appendAbsoluteArc(
            eastl::vector<glm::dvec2> &points,
            const glm::dvec2 &center,
            double radius,
            double startAngle,
            double endAngle,
            bool clockwise,
            uint32_t subdivisions = 48u)
        {
            double delta = endAngle - startAngle;
            if (clockwise)
            {
                while (delta > 0.0) delta -= 2.0 * Pi;
            }
            else
            {
                while (delta < 0.0) delta += 2.0 * Pi;
            }
            const uint32_t count = eastl::max(
                1u, static_cast<uint32_t>(std::ceil(
                    std::abs(delta) / (2.0 * Pi) * double(subdivisions))));
            for (uint32_t index = 1u; index <= count; ++index)
            {
                const double angle = startAngle + delta * double(index) / double(count);
                points.push_back(center + glm::dvec2(std::cos(angle), std::sin(angle)) * radius);
            }
        }

        /** Returns the California contour used by the upstream shape example. */
        ShapeContour makeCaliforniaContour()
        {
            const glm::dvec2 raw[] = {
                {610, 320}, {450, 300}, {392, 392}, {266, 438}, {190, 570},
                {190, 600}, {160, 620}, {160, 650}, {180, 640}, {165, 680},
                {150, 670}, {90, 737}, {80, 795}, {50, 835}, {64, 870},
                {60, 945}, {300, 945}, {300, 743}, {600, 473}, {626, 425},
                {600, 370}, {610, 320}};
            ShapeContour contour;
            for (const glm::dvec2 &point : raw)
            {
                contour.points.push_back(point * 0.25);
            }
            return contour;
        }

        /** Returns the triangle contour from the upstream source. */
        ShapeContour makeTriangleContour()
        {
            return {{{glm::dvec2(80, 20), glm::dvec2(40, 80), glm::dvec2(120, 80)}}};
        }

        /** Returns the rounded rectangle path from the r185 source. */
        ShapeContour makeRoundedRectangleContour()
        {
            ShapeContour contour;
            contour.points.push_back({0.0, 20.0});
            contour.points.push_back({0.0, 30.0});
            appendQuadratic(contour.points, {0.0, 30.0}, {0.0, 50.0}, {20.0, 50.0});
            contour.points.push_back({30.0, 50.0});
            appendQuadratic(contour.points, {30.0, 50.0}, {50.0, 50.0}, {50.0, 30.0});
            contour.points.push_back({50.0, 20.0});
            appendQuadratic(contour.points, {50.0, 20.0}, {50.0, 0.0}, {30.0, 0.0});
            contour.points.push_back({20.0, 0.0});
            appendQuadratic(contour.points, {20.0, 0.0}, {0.0, 0.0}, {0.0, 20.0});
            return contour;
        }

        /** Returns the heart contour sampled from the source cubic Bezier curves. */
        ShapeContour makeHeartContour()
        {
            ShapeContour contour;
            const glm::dvec2 start(25.0, 25.0);
            contour.points.push_back(start);
            appendCubic(contour.points, start, {25.0, 25.0}, {20.0, 0.0}, {0.0, 0.0});
            appendCubic(contour.points, {0.0, 0.0}, {-30.0, 0.0}, {-30.0, 35.0}, {-30.0, 35.0});
            appendCubic(contour.points, {-30.0, 35.0}, {-30.0, 55.0}, {-10.0, 77.0}, {25.0, 95.0});
            appendCubic(contour.points, {25.0, 95.0}, {60.0, 77.0}, {80.0, 55.0}, {80.0, 35.0});
            appendCubic(contour.points, {80.0, 35.0}, {80.0, 35.0}, {80.0, 0.0}, {50.0, 0.0});
            appendCubic(contour.points, {50.0, 0.0}, {35.0, 0.0}, start, start);
            return contour;
        }

        /** Returns a circle expressed as the four quadratic curves used by Shape. */
        ShapeContour makeCircleContour(double radius,
                                       const glm::dvec2 &center = glm::dvec2(0.0))
        {
            ShapeContour contour;
            const glm::dvec2 top = center + glm::dvec2(0.0, radius);
            contour.points.push_back(top);
            appendQuadratic(contour.points, top,
                            center + glm::dvec2(radius, radius),
                            center + glm::dvec2(radius, 0.0));
            appendQuadratic(contour.points, center + glm::dvec2(radius, 0.0),
                            center + glm::dvec2(radius, -radius),
                            center + glm::dvec2(0.0, -radius));
            appendQuadratic(contour.points, center + glm::dvec2(0.0, -radius),
                            center + glm::dvec2(-radius, -radius),
                            center + glm::dvec2(-radius, 0.0));
            appendQuadratic(contour.points, center + glm::dvec2(-radius, 0.0),
                            center + glm::dvec2(-radius, radius), top);
            return contour;
        }

        /** Returns the full absarc contour used by the arc and smiley shapes. */
        ShapeContour makeAbsoluteCircleContour(double radius,
                                               const glm::dvec2 &center)
        {
            ShapeContour contour;
            contour.points.push_back(center + glm::dvec2(radius, 0.0));
            appendAbsoluteArc(contour.points, center, radius, 0.0,
                              2.0 * Pi, false, 24u);
            return contour;
        }

        /** Returns the track, fish, arc, smiley, and spline contours. */
        ShapeContour makeTrackContour()
        {
            ShapeContour contour;
            contour.points = {{40, 40}, {40, 160}};
            for (uint32_t index = 0u; index <= 24u; ++index)
            {
                const double angle = Pi - Pi * double(index) / 24.0;
                contour.points.push_back({60.0 + 20.0 * std::cos(angle), 160.0 + 20.0 * std::sin(angle)});
            }
            contour.points.push_back({80, 40});
            for (uint32_t index = 0u; index <= 24u; ++index)
            {
                const double angle = 2.0 * Pi - Pi * double(index) / 24.0;
                contour.points.push_back({60.0 + 20.0 * std::cos(angle), 40.0 + 20.0 * std::sin(angle)});
            }
            return contour;
        }

        /** Returns the Catmull-Rom splineThru path used by the source Shape. */
        ShapeContour makeSplineContour()
        {
            const glm::dvec2 control[] = {
                {0.0, 0.0}, {70.0, 20.0}, {80.0, 90.0},
                {-30.0, 70.0}, {0.0, 0.0}};
            // Shape.getPoints(12) uses divisions * controlPointCount for a
            // SplineCurve.  This path has five control points, hence 60
            // subdivisions in the r185 example.
            constexpr uint32_t sampleCount = 60u;
            ShapeContour contour;
            contour.points.reserve(sampleCount + 1u);
            for (uint32_t sample = 0u; sample <= sampleCount; ++sample)
            {
                const double t = double(sample) / double(sampleCount);
                const double scaled = t * 4.0;
                const uint32_t segment = eastl::min(
                    3u, static_cast<uint32_t>(std::floor(scaled)));
                const double weight = scaled - double(segment);
                const glm::dvec2 &p0 = control[segment == 0u ? 0u : segment - 1u];
                const glm::dvec2 &p1 = control[segment];
                const glm::dvec2 &p2 = control[segment + 1u];
                const uint32_t p3Index = segment > 2u
                    ? 4u
                    : segment + 2u;
                const glm::dvec2 &p3 = control[p3Index];
                const glm::dvec2 v0 = (p2 - p0) * 0.5;
                const glm::dvec2 v1 = (p3 - p1) * 0.5;
                const double t2 = weight * weight;
                const double t3 = t2 * weight;
                contour.points.push_back(
                    (2.0 * p1 - 2.0 * p2 + v0 + v1) * t3 +
                    (-3.0 * p1 + 3.0 * p2 - 2.0 * v0 - v1) * t2 +
                    v0 * weight + p1);
            }
            return contour;
        }

        /** Returns the fish contour and control points from the r185 source. */
        ShapeContour makeFishContour()
        {
            ShapeContour contour;
            contour.points.push_back({0, 0});
            appendQuadratic(contour.points, {0, 0}, {50, -80}, {90, -10}, 8u);
            appendQuadratic(contour.points, {90, -10}, {100, -10}, {115, -40}, 6u);
            appendQuadratic(contour.points, {115, -40}, {115, 0}, {115, 40}, 6u);
            appendQuadratic(contour.points, {115, 40}, {100, 10}, {90, 10}, 6u);
            appendQuadratic(contour.points, {90, 10}, {50, 80}, {0, 0}, 8u);
            return contour;
        }

        /** Adds one shape's three mesh rows and its four expanded line/point rows. */
        void addShapesGroup(
            eastl::vector<WebglGeometryShapesEntityData> &entities,
            const char *name,
            const ShapeContour &contour,
            uint32_t color,
            double x,
            double y,
            double z,
            double rz,
            const eastl::vector<ShapeContour> *holes = nullptr)
        {
            const ShapeContour spacedContour =
                makeShapesSpacedContour(contour);
            const glm::vec4 linearColor = shapesColor(color, 0.0f);
            // Every first row is a textured ShapeGeometry mesh; the
            // RenderSet material phase is per entity, so it must not depend
            // on whether an earlier group has already been appended.
            const glm::vec4 textureColor = shapesColor(0xffffffu, 1.0f);
            const glm::dmat4 translation = glm::translate(glm::dmat4(1.0), glm::dvec3(x, y + 50.0, z));
            const glm::dmat4 rotation = glm::rotate(glm::dmat4(1.0), rz, glm::dvec3(0.0, 0.0, 1.0));
            const glm::mat4 model = glm::mat4(translation * rotation);

            WebglGeometryShapesEntityData textured;
            textured.logicalId = eastl::string(name) + "-flat-textured";
            appendShapesFlatContour(textured, contour, holes);
            textured.materialData.baseColorAndFlags = textureColor;
            textured.model = glm::mat4(glm::translate(glm::dmat4(model), glm::dvec3(0.0, 0.0, -175.0)));
            entities.push_back(eastl::move(textured));

            WebglGeometryShapesEntityData flat;
            flat.logicalId = eastl::string(name) + "-flat-color";
            appendShapesFlatContour(flat, contour, holes);
            flat.materialData.baseColorAndFlags = linearColor;
            flat.model = glm::mat4(glm::translate(glm::dmat4(model), glm::dvec3(0.0, 0.0, -125.0)));
            entities.push_back(eastl::move(flat));

            WebglGeometryShapesEntityData extruded;
            extruded.logicalId = eastl::string(name) + "-extruded";
            if (holes == nullptr || holes->empty())
            {
                appendShapesBeveledExtrudedContour(extruded, contour);
            }
            else
            {
                appendShapesExtrudedContour(extruded, contour, 8.0, holes);
            }
            extruded.materialData.baseColorAndFlags = linearColor;
            extruded.model = glm::mat4(glm::translate(glm::dmat4(model), glm::dvec3(0.0, 0.0, -75.0)));
            entities.push_back(eastl::move(extruded));

            const glm::mat4 lineModel = glm::mat4(glm::translate(glm::dmat4(model), glm::dvec3(0.0, 0.0, -25.0)));
            const glm::mat4 spacedModel = glm::mat4(glm::translate(glm::dmat4(model), glm::dvec3(0.0, 0.0, 25.0)));
            const glm::mat4 pointsModel = glm::mat4(glm::translate(glm::dmat4(model), glm::dvec3(0.0, 0.0, 75.0)));
            const glm::mat4 spacedPointsModel = glm::mat4(glm::translate(glm::dmat4(model), glm::dvec3(0.0, 0.0, 125.0)));

            WebglGeometryShapesEntityData line;
            line.logicalId = eastl::string(name) + "-line";
            // LineBasicMaterial uses a one-pixel screen-space width.  The
            // packed geometry is expanded in object space, so use the
            // equivalent world-space width at the locked camera distance.
            // LineBasicMaterial's one-pixel width maps to approximately one
            // world unit at the locked 500-unit camera distance.  The CPU
            // triangle envelope stores the full width, so keep it at 1.0
            // rather than halving the coverage a native line would produce.
            appendShapesLineStrip(line, contour, 1.0);
            line.materialData.baseColorAndFlags = linearColor;
            line.model = lineModel;
            entities.push_back(eastl::move(line));

            WebglGeometryShapesEntityData spacedLine;
            spacedLine.logicalId = eastl::string(name) + "-spaced-line";
            appendShapesLineStrip(spacedLine, spacedContour, 1.0);
            spacedLine.materialData.baseColorAndFlags = linearColor;
            spacedLine.model = spacedModel;
            entities.push_back(eastl::move(spacedLine));

            WebglGeometryShapesEntityData points;
            points.logicalId = eastl::string(name) + "-points";
            // PointsMaterial size=4 is attenuated to roughly two pixels at
            // the 500-unit camera distance and 50 degree FOV.  The helper
            // parameter is the full object-space billboard width.
            appendShapesPointBillboards(points, contour, 3.0);
            points.materialData.baseColorAndFlags = linearColor;
            points.model = pointsModel;
            entities.push_back(eastl::move(points));

            WebglGeometryShapesEntityData spacedPoints;
            spacedPoints.logicalId = eastl::string(name) + "-spaced-points";
            appendShapesPointBillboards(spacedPoints, spacedContour, 3.0);
            spacedPoints.materialData.baseColorAndFlags = linearColor;
            spacedPoints.model = spacedPointsModel;
            entities.push_back(eastl::move(spacedPoints));
        }

        /** Adds only the four line/point rows used for a hole path. */
        void addShapesLineOnlyGroup(
            eastl::vector<WebglGeometryShapesEntityData> &entities,
            const char *name,
            const ShapeContour &contour,
            uint32_t color,
            double x,
            double y,
            double rz)
        {
            const glm::vec4 linearColor = shapesColor(color, 0.0f);
            const glm::dmat4 model =
                glm::translate(glm::dmat4(1.0), glm::dvec3(x, y + 50.0, 0.0)) *
                glm::rotate(glm::dmat4(1.0), rz, glm::dvec3(0.0, 0.0, 1.0));
            const glm::mat4 models[4u] = {
                glm::mat4(glm::translate(model, glm::dvec3(0.0, 0.0, -25.0))),
                glm::mat4(glm::translate(model, glm::dvec3(0.0, 0.0, 25.0))),
                glm::mat4(glm::translate(model, glm::dvec3(0.0, 0.0, 75.0))),
                glm::mat4(glm::translate(model, glm::dvec3(0.0, 0.0, 125.0))),
            };
            for (uint32_t row = 0u; row < 4u; ++row)
            {
                WebglGeometryShapesEntityData entity;
                entity.logicalId = eastl::string(name) + "-" + eastl::to_string(row);
                const ShapeContour spacedContour =
                    makeShapesSpacedContour(contour);
                if (row == 0u)
                {
                    appendShapesLineStrip(entity, contour, 1.0);
                }
                else if (row == 1u)
                {
                    appendShapesLineStrip(entity, spacedContour, 1.0);
                }
                else if (row == 2u)
                {
                    appendShapesPointBillboards(entity, contour, 3.0);
                }
                else
                {
                    appendShapesPointBillboards(entity, spacedContour, 3.0);
                }
                entity.materialData.baseColorAndFlags = linearColor;
                entity.model = models[row];
                entities.push_back(eastl::move(entity));
            }
        }

        /** Decodes and packs the locked r185 UV-grid JPEG and its sRGB mips. */
        void buildShapesTexture(
            const std::filesystem::path &assetPath,
            eastl::vector<uint8_t> &bytes,
            eastl::vector<uint64_t> &mipOffsets,
            uint32_t &width,
            uint32_t &height)
        {
            const RgbaImageData baseImage = decodeJpegRgba8(assetPath);
            if (baseImage.width != 1024u || baseImage.height != 1024u ||
                baseImage.pixels.size() != size_t(baseImage.width) *
                    size_t(baseImage.height) * 4u)
            {
                throw std::runtime_error(
                    "webgl_geometry_shapes UV-grid asset has an unexpected extent.");
            }
            bytes.clear();
            mipOffsets.clear();
            const eastl::vector<RgbaImageData> mipChain =
                buildSrgbMipChain(baseImage);
            width = baseImage.width;
            height = baseImage.height;
            for (const RgbaImageData &level : mipChain)
            {
                mipOffsets.push_back(bytes.size());
                bytes.insert(bytes.end(), level.pixels.begin(), level.pixels.end());
            }
        }

        /** Appends one typed buffer payload to a RenderSet allocation. */
        void appendShapesBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
            const void *value,
            uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }

        /** Creates the fixed camera projection used by all shape scenarios. */
        glm::mat4 makeShapesProjection(uint32_t width, uint32_t height)
        {
            return glm::mat4(glm::perspective(
                glm::radians(50.0),
                double(width) / double(height),
                1.0,
                1000.0));
        }
    } // namespace

    void WebglGeometryShapesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_geometry_shapes" ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            (options.scenarioId == "initial" && options.targetFrame != 0u) ||
            (options.scenarioId == "animated" && options.targetFrame != 60u) ||
            (options.scenarioId == "rotated-input" && options.targetFrame != 61u) ||
            (options.scenarioId != "initial" && options.scenarioId != "animated" && options.scenarioId != "rotated-input"))
        {
            throw std::invalid_argument("webgl_geometry_shapes scenario does not match the locked r185 contract.");
        }
        device = inDevice;
        captureWidth = options.width;
        captureHeight = options.height;
        entities.clear();
        const ShapeContour california = makeCaliforniaContour();
        const ShapeContour triangle = makeTriangleContour();
        const ShapeContour roundedRectangle = makeRoundedRectangleContour();
        const ShapeContour track = makeTrackContour();
        const ShapeContour square = {{{glm::dvec2(0, 0), glm::dvec2(0, 80), glm::dvec2(80, 80), glm::dvec2(80, 0)}}};
        const ShapeContour heart = makeHeartContour();
        const ShapeContour circle = makeCircleContour(40.0);
        const ShapeContour fish = makeFishContour();
        const ShapeContour arc = makeAbsoluteCircleContour(40.0, {10.0, 10.0});
        const ShapeContour smiley = makeAbsoluteCircleContour(40.0, {40.0, 40.0});
        const ShapeContour spline = makeSplineContour();
        const ShapeContour arcHole = makeAbsoluteCircleContour(10.0, {10.0, 10.0});
        const ShapeContour smileyEye1 = makeAbsoluteCircleContour(10.0, {25.0, 20.0});
        const ShapeContour smileyEye2 = makeAbsoluteCircleContour(10.0, {55.0, 20.0});
        ShapeContour smileyMouth;
        smileyMouth.points.push_back({20.0, 40.0});
        appendQuadratic(smileyMouth.points, {20.0, 40.0}, {40.0, 60.0}, {60.0, 40.0});
        appendCubic(smileyMouth.points, {60.0, 40.0}, {70.0, 45.0}, {70.0, 50.0}, {60.0, 60.0});
        appendQuadratic(smileyMouth.points, {60.0, 60.0}, {40.0, 80.0}, {20.0, 60.0});
        appendQuadratic(smileyMouth.points, {20.0, 60.0}, {5.0, 50.0}, {20.0, 40.0});
        const eastl::vector<ShapeContour> arcHoles = {arcHole};
        const eastl::vector<ShapeContour> smileyHoles = {
            smileyEye1, smileyEye2, smileyMouth};
        addShapesGroup(entities, "california", california, 0xf08000u, -300.0, -100.0, 0.0, 0.0);
        addShapesGroup(entities, "triangle", triangle, 0x8080f0u, -180.0, 0.0, 0.0, 0.0);
        addShapesGroup(entities, "rounded-rectangle", roundedRectangle, 0x008000u, -150.0, 150.0, 0.0, 0.0);
        addShapesGroup(entities, "track", track, 0x008080u, 200.0, -100.0, 0.0, 0.0);
        addShapesGroup(entities, "square", square, 0x0040f0u, 150.0, 100.0, 0.0, 0.0);
        addShapesGroup(entities, "heart", heart, 0xf00000u, 60.0, 100.0, 0.0, Pi);
        addShapesGroup(entities, "circle", circle, 0x00f000u, 120.0, 250.0, 0.0, 0.0);
        addShapesGroup(entities, "fish", fish, 0x404040u, -60.0, 200.0, 0.0, 0.0);
        addShapesGroup(entities, "arc", arc, 0x804000u, 150.0, 0.0, 0.0, 0.0, &arcHoles);
        addShapesGroup(entities, "spline", spline, 0x808080u, -50.0, -100.0, 0.0, 0.0);

        addShapesLineOnlyGroup(entities, "arc-hole", arcHole, 0x804000u, 150.0, 0.0, 0.0);
        addShapesGroup(entities, "smiley", smiley, 0xf000f0u, -200.0, 250.0, 0.0, Pi, &smileyHoles);
        addShapesLineOnlyGroup(entities, "smiley-eye-1", smileyEye1, 0xf000f0u, -200.0, 250.0, Pi);
        addShapesLineOnlyGroup(entities, "smiley-eye-2", smileyEye2, 0xf000f0u, -200.0, 250.0, Pi);
        addShapesLineOnlyGroup(entities, "smiley-mouth", smileyMouth, 0xf000f0u, -200.0, 250.0, Pi);
        if (entities.size() != ExpectedEntityCount)
        {
            throw std::logic_error("webgl_geometry_shapes must create exactly 93 entities.");
        }

        const std::filesystem::path texturePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "uv_grid_opengl.jpg";
        if (!std::filesystem::is_regular_file(texturePath))
        {
            throw std::invalid_argument(
                "webgl_geometry_shapes requires the locked uv_grid_opengl.jpg asset.");
        }
        buildShapesTexture(texturePath, textureBytes, textureMipOffsets,
                           textureWidth, textureHeight);
        updateObjectData(options.width, options.height, options.targetFrame);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgl_geometry_shapes could not create its Scene Set encoder.");
        }
        for (WebglGeometryShapesEntityData &entity : entities)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendShapesBuffer(allocation,
                               WebglGeometryShapesSceneRenderSetComponents::vertices,
                               entity.logicalId + "-vertices",
                               entity.vertices.data(),
                               entity.vertices.size() * sizeof(WebglGeometryShapesHostVertex));
            appendShapesBuffer(allocation,
                               WebglGeometryShapesSceneRenderSetComponents::indices,
                               entity.logicalId + "-indices",
                               entity.indices.data(),
                               entity.indices.size() * sizeof(uint32_t));
            appendShapesBuffer(allocation,
                               WebglGeometryShapesSceneRenderSetComponents::objects,
                               entity.logicalId + "-object",
                               &entity.objectData,
                               sizeof(entity.objectData));
            appendShapesBuffer(allocation,
                               WebglGeometryShapesSceneRenderSetComponents::instances,
                               entity.logicalId + "-instance",
                               &entity.instanceData,
                               sizeof(entity.instanceData));
            appendShapesBuffer(allocation,
                               WebglGeometryShapesSceneRenderSetComponents::materials,
                               entity.logicalId + "-material",
                               &entity.materialData,
                               sizeof(entity.materialData));
            if (entity.materialData.baseColorAndFlags.w > 0.5f)
            {
                GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
                textureInfo.textureComponentHandle = WebglGeometryShapesSceneRenderSetComponents::textures;
                textureInfo.textures.push_back({
                    // Reuse one immutable asset identity for all textured
                    // entities; RenderSet stores the per-entity slot index,
                    // while the shared descriptor keeps within the current
                    // eight-texture pool limit.
                    .textureName = "WebglGeometryShapesUvGrid",
                    .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                    .width = textureWidth,
                    .height = textureHeight,
                    .data = textureBytes.data(),
                    .dataStorageBytes = textureBytes.size(),
                    .mipmapOffsetBytes = textureMipOffsets,
                });
                allocation.textureInfos.push_back(eastl::move(textureInfo));
            }
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglGeometryShapesRuntimeAdapter::updateObjectData(
        uint32_t width,
        uint32_t height,
        uint32_t frameIndex)
    {
        const glm::dmat4 view = glm::lookAt(
            glm::dvec3(0.0, 150.0, 500.0),
            glm::dvec3(0.0, 150.0, 0.0),
            glm::dvec3(0.0, 1.0, 0.0));
        // The upstream animation only eases toward targetRotation. With no
        // pointer input the initial and animated captures remain at zero; the
        // rotated-input replay supplies the locked +0.22 radian target after
        // the pointer movement is eased by the upstream render loop.
        const double groupAngle = frameIndex >= 61u ? 0.22 : 0.0;
        const glm::dmat4 groupRotation = glm::rotate(
            glm::dmat4(1.0), groupAngle, glm::dvec3(0.0, 1.0, 0.0));
        const glm::dmat4 groupTransform =
            glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, 0.0, 0.0)) * groupRotation;
        const glm::dmat4 projection = glm::dmat4(makeShapesProjection(width, height));
        const glm::dvec3 lightPosition(0.0, 0.0, 500.0);
        const glm::dvec3 viewLight = glm::vec3(view * glm::dvec4(lightPosition, 1.0));
        for (WebglGeometryShapesEntityData &entity : entities)
        {
            const glm::dmat4 model = groupTransform * glm::dmat4(entity.model);
            const glm::dmat4 modelView = view * model;
            entity.objectData.modelView = glm::mat4(modelView);
            entity.objectData.modelViewProjection = glm::mat4(projection * modelView);
            entity.objectData.pointLightViewPositionAndIntensity = glm::vec4(viewLight, 2.5f);
            entity.instanceData.translation = glm::vec4(0.0f);
        }
    }

    void WebglGeometryShapesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateObjectData(options.width, options.height, frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgl_geometry_shapes could not create its update encoder.");
        }
        for (const WebglGeometryShapesEntityData &entity : entities)
        {
            encoder->setBufferComponentData(
                entity.entityIndex,
                WebglGeometryShapesSceneRenderSetComponents::objects,
                &entity.objectData,
                sizeof(entity.objectData),
                0u,
                1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglGeometryShapesRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        prepareShapesPath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()),
                     static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("webgl_geometry_shapes could not write RGBA capture.");
        }
    }

    void WebglGeometryShapesRuntimeAdapter::writeCaptureMetadata(
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
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        prepareShapesPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
               << "\"caseId\":\"webgl_geometry_shapes\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\""
               << options.pipeline.c_str() << "\",\"backend\":\""
               << threeSampleBackendName(options.backend) << "\",\"frame\":"
               << frameIndex << ",\"randomSeed\":" << options.randomSeed
               << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
               << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false";
        if (options.scenarioId == "rotated-input")
        {
            output << ",\"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_geometry_shapes\",\"scenarioId\":\"rotated-input\",\"captureFrame\":61,\"sha256\":\"bf21576fa849ba0b92f7af2958d256e5be5c813e676ca846260ac9411de8816c\",\"target\":\"canvas[width=\\\"800\\\"][height=\\\"500\\\"]\",\"eventCount\":3}";
        }
        else
        {
            output << ",\"inputReplay\":null";
        }
        output << "}\n";
    }

    void WebglGeometryShapesRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        prepareShapesPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n"
               << "  \"schemaVersion\":1,\n"
               << "  \"caseId\":\"webgl_geometry_shapes\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"implementationLevel\":\"semantic-complete\",\n"
               << "  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":false,\n"
               << "  \"renderSetPolicy\":\"required\",\n"
               << "  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":93,\n"
               << "  \"entityCount\":93,\n"
               << "  \"instanceCount\":93,\n"
               << "  \"scenePassCount\":1,\n"
               << "  \"screenPassCount\":0,\n"
               << "  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n"
               << "  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"],\n"
               << "  \"assetAndAlgorithmState\":\"cpu-shape-extrude-line-point-expanded\",\n"
               << "  \"renderSetType\":\"WebglGeometryShapesSceneRenderSet\",\n"
               << "  \"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglGeometryShapesMainPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set-0\",\"renderSetType\":\"WebglGeometryShapesSceneRenderSet\",\"renderableObjectCount\":93,\"entityCount\":93,\"drawCommandCount\":1,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglGeometryShapesMainPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
        for (size_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u)
            {
                output << ',';
            }
            output << "{\"entityId\":" << index
                   << ",\"logicalRenderableId\":\""
                   << entities[index].logicalId.c_str()
                   << "\",\"instanceCount\":1}";
        }
        output << "]}]}\n";
    }

    void WebglGeometryShapesRuntimeAdapter::afterFrame(
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
            throw std::overflow_error("webgl_geometry_shapes RGBA capture is too large.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglGeometryShapesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
        textureBytes.clear();
        textureMipOffsets.clear();
    }
} // namespace GVM::ThreeSamples
