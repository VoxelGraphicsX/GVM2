#ifndef GAUSSIAN_SPLATTING_SHARED_PLY_LOADER_HPP
#define GAUSSIAN_SPLATTING_SHARED_PLY_LOADER_HPP

#include "../TestUtils/GaussianSplattingDslShared/CpuGaussianScene.hpp"
#include "SceneSafety.hpp"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace PlyLoader
{
    enum class Format
    {
        Ascii,
        BinaryLittleEndian,
    };

    enum class ScalarType
    {
        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Float32,
        Float64,
        Invalid,
    };

    struct Property
    {
        std::string name;
        ScalarType type = ScalarType::Invalid;
        bool isList = false;
        ScalarType listCountType = ScalarType::Invalid;
        ScalarType listValueType = ScalarType::Invalid;
    };

    struct Element
    {
        std::string name;
        std::size_t count = 0;
        std::vector<Property> properties;
    };

    inline ScalarType parseScalarType(const std::string &token)
    {
        if (token == "char" || token == "int8")
            return ScalarType::Int8;
        if (token == "uchar" || token == "uint8")
            return ScalarType::UInt8;
        if (token == "short" || token == "int16")
            return ScalarType::Int16;
        if (token == "ushort" || token == "uint16")
            return ScalarType::UInt16;
        if (token == "int" || token == "int32")
            return ScalarType::Int32;
        if (token == "uint" || token == "uint32")
            return ScalarType::UInt32;
        if (token == "float" || token == "float32")
            return ScalarType::Float32;
        if (token == "double" || token == "float64")
            return ScalarType::Float64;
        return ScalarType::Invalid;
    }

    inline std::size_t scalarTypeSize(ScalarType type)
    {
        switch (type)
        {
        case ScalarType::Int8:
        case ScalarType::UInt8:
            return 1;
        case ScalarType::Int16:
        case ScalarType::UInt16:
            return 2;
        case ScalarType::Int32:
        case ScalarType::UInt32:
        case ScalarType::Float32:
            return 4;
        case ScalarType::Float64:
            return 8;
        default:
            return 0;
        }
    }

    inline double readBinaryScalar(std::istream &stream, ScalarType type)
    {
        switch (type)
        {
        case ScalarType::Int8:
        {
            std::int8_t value = 0;
            stream.read(reinterpret_cast<char *>(&value), sizeof(value));
            return static_cast<double>(value);
        }
        case ScalarType::UInt8:
        {
            std::uint8_t value = 0;
            stream.read(reinterpret_cast<char *>(&value), sizeof(value));
            return static_cast<double>(value);
        }
        case ScalarType::Int16:
        {
            std::int16_t value = 0;
            stream.read(reinterpret_cast<char *>(&value), sizeof(value));
            return static_cast<double>(value);
        }
        case ScalarType::UInt16:
        {
            std::uint16_t value = 0;
            stream.read(reinterpret_cast<char *>(&value), sizeof(value));
            return static_cast<double>(value);
        }
        case ScalarType::Int32:
        {
            std::int32_t value = 0;
            stream.read(reinterpret_cast<char *>(&value), sizeof(value));
            return static_cast<double>(value);
        }
        case ScalarType::UInt32:
        {
            std::uint32_t value = 0;
            stream.read(reinterpret_cast<char *>(&value), sizeof(value));
            return static_cast<double>(value);
        }
        case ScalarType::Float32:
        {
            float value = 0.0f;
            stream.read(reinterpret_cast<char *>(&value), sizeof(value));
            return static_cast<double>(value);
        }
        case ScalarType::Float64:
        {
            double value = 0.0;
            stream.read(reinterpret_cast<char *>(&value), sizeof(value));
            return value;
        }
        default:
            throw std::runtime_error("Unsupported binary PLY scalar type.");
        }
    }

    inline double readAsciiScalar(std::istream &stream, ScalarType)
    {
        double value = 0.0;
        stream >> value;
        return value;
    }

    inline std::uint32_t readListCount(std::istream &stream, Format format, ScalarType type)
    {
        const double value = (format == Format::Ascii) ? readAsciiScalar(stream, type) : readBinaryScalar(stream, type);
        return static_cast<std::uint32_t>(std::max(0.0, value));
    }

    inline double readScalar(std::istream &stream, Format format, ScalarType type)
    {
        return (format == Format::Ascii) ? readAsciiScalar(stream, type) : readBinaryScalar(stream, type);
    }

    inline std::pair<Format, std::vector<Element>> parseHeader(std::istream &stream)
    {
        std::string line;
        if (!std::getline(stream, line) || line != "ply")
        {
            throw std::runtime_error("Invalid PLY header.");
        }

        Format format = Format::Ascii;
        std::vector<Element> elements;
        Element *currentElement = nullptr;

        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line == "end_header")
            {
                break;
            }
            if (line.empty())
            {
                continue;
            }

            std::istringstream lineStream(line);
            std::string keyword;
            lineStream >> keyword;

            if (keyword == "comment" || keyword == "obj_info")
            {
                continue;
            }
            if (keyword == "format")
            {
                std::string formatToken;
                std::string version;
                lineStream >> formatToken >> version;
                if (formatToken == "ascii")
                {
                    format = Format::Ascii;
                }
                else if (formatToken == "binary_little_endian")
                {
                    format = Format::BinaryLittleEndian;
                }
                else
                {
                    throw std::runtime_error("Only ascii and binary_little_endian PLY are supported.");
                }
                continue;
            }
            if (keyword == "element")
            {
                Element element;
                lineStream >> element.name >> element.count;
                elements.push_back(element);
                currentElement = &elements.back();
                continue;
            }
            if (keyword == "property")
            {
                if (currentElement == nullptr)
                {
                    throw std::runtime_error("PLY property declared before element.");
                }

                Property property;
                std::string typeToken;
                lineStream >> typeToken;
                if (typeToken == "list")
                {
                    std::string countTypeToken;
                    std::string valueTypeToken;
                    lineStream >> countTypeToken >> valueTypeToken >> property.name;
                    property.isList = true;
                    property.listCountType = parseScalarType(countTypeToken);
                    property.listValueType = parseScalarType(valueTypeToken);
                }
                else
                {
                    lineStream >> property.name;
                    property.type = parseScalarType(typeToken);
                }
                currentElement->properties.push_back(property);
            }
        }

        return {format, elements};
    }

    inline bool parseIndexedPropertyName(const std::string &name, const char *prefix, std::size_t &index)
    {
        const std::string prefixString(prefix);
        if (name.rfind(prefixString, 0u) != 0u)
        {
            return false;
        }

        const std::string suffix = name.substr(prefixString.size());
        if (suffix.empty())
        {
            return false;
        }

        std::size_t parsedIndex = 0u;
        try
        {
            parsedIndex = static_cast<std::size_t>(std::stoul(suffix));
        }
        catch (const std::exception &)
        {
            return false;
        }

        index = parsedIndex;
        return true;
    }

    struct SuggestedCamera
    {
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);
        glm::vec3 forward = glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 sceneUp = glm::vec3(0.0f, -1.0f, 0.0f);
        bool hasSceneUp = false;
    };

    inline bool findMatchingBracket(const std::string &content, std::size_t openIndex, std::size_t &closeIndex)
    {
        if (openIndex >= content.size() || content[openIndex] != '[')
        {
            return false;
        }

        std::size_t depth = 0u;
        for (std::size_t index = openIndex; index < content.size(); ++index)
        {
            if (content[index] == '[')
            {
                ++depth;
            }
            else if (content[index] == ']')
            {
                if (depth == 0u)
                {
                    return false;
                }

                --depth;
                if (depth == 0u)
                {
                    closeIndex = index;
                    return true;
                }
            }
        }

        return false;
    }

    inline std::vector<double> parseNumbersFromSpan(const std::string &content, std::size_t beginIndex, std::size_t endIndex)
    {
        std::vector<double> values;
        if (beginIndex >= endIndex || endIndex > content.size())
        {
            return values;
        }

        const char *cursor = content.c_str() + beginIndex;
        const char *end = content.c_str() + endIndex;
        while (cursor < end)
        {
            char *next = nullptr;
            const double value = std::strtod(cursor, &next);
            if (next != cursor)
            {
                values.push_back(value);
                cursor = next;
                continue;
            }

            ++cursor;
        }

        return values;
    }

    inline bool parseBracketedNumbersAfterToken(const std::string &content, const char *token, std::size_t &cursor, std::size_t expectedCount, std::vector<double> &values)
    {
        const std::size_t tokenIndex = content.find(token, cursor);
        if (tokenIndex == std::string::npos)
        {
            return false;
        }

        const std::size_t openIndex = content.find('[', tokenIndex);
        if (openIndex == std::string::npos)
        {
            return false;
        }

        std::size_t closeIndex = 0u;
        if (!findMatchingBracket(content, openIndex, closeIndex))
        {
            return false;
        }

        values = parseNumbersFromSpan(content, openIndex, closeIndex + 1u);
        if (values.size() < expectedCount)
        {
            return false;
        }

        cursor = closeIndex + 1u;
        return true;
    }

    inline bool tryParseSuggestedCameraFromCamerasJson(const std::filesystem::path &path, SuggestedCamera &camera)
    {
        std::ifstream stream(path);
        if (!stream.is_open())
        {
            return false;
        }

        const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        if (content.empty())
        {
            return false;
        }

        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> forwards;
        std::vector<glm::vec3> sceneUps;
        std::size_t cursor = 0u;
        while (cursor < content.size())
        {
            std::vector<double> positionValues;
            if (!parseBracketedNumbersAfterToken(content, "\"position\"", cursor, 3u, positionValues))
            {
                break;
            }

            std::vector<double> rotationValues;
            if (!parseBracketedNumbersAfterToken(content, "\"rotation\"", cursor, 9u, rotationValues))
            {
                break;
            }

            const glm::vec3 position(
                static_cast<float>(positionValues[0]),
                static_cast<float>(positionValues[1]),
                static_cast<float>(positionValues[2])
            );
            glm::vec3 forward(
                static_cast<float>(rotationValues[2]),
                static_cast<float>(rotationValues[5]),
                static_cast<float>(rotationValues[8])
            );
            // 3DGS cameras.json uses OpenCV image Y-down cameras; negative row 1 is image-up in world space.
            glm::vec3 sceneUp(
                -static_cast<float>(rotationValues[3]),
                -static_cast<float>(rotationValues[4]),
                -static_cast<float>(rotationValues[5])
            );
            const float forwardLengthSquared = glm::dot(forward, forward);
            const float sceneUpLengthSquared = glm::dot(sceneUp, sceneUp);
            if (!std::isfinite(forwardLengthSquared) || forwardLengthSquared <= 1.0e-12f ||
                !std::isfinite(sceneUpLengthSquared) || sceneUpLengthSquared <= 1.0e-12f)
            {
                continue;
            }

            positions.push_back(position);
            forwards.push_back(forward * glm::inversesqrt(forwardLengthSquared));
            sceneUps.push_back(sceneUp * glm::inversesqrt(sceneUpLengthSquared));
        }

        if (positions.empty() || positions.size() != forwards.size() || positions.size() != sceneUps.size())
        {
            return false;
        }

        glm::vec3 centroid(0.0f);
        for (const glm::vec3 &position : positions)
        {
            centroid += position;
        }
        centroid /= static_cast<float>(positions.size());

        std::size_t bestIndex = 0u;
        float bestDistanceSquared = std::numeric_limits<float>::max();
        for (std::size_t index = 0u; index < positions.size(); ++index)
        {
            const glm::vec3 delta = positions[index] - centroid;
            const float distanceSquared = glm::dot(delta, delta);
            if (distanceSquared < bestDistanceSquared)
            {
                bestDistanceSquared = distanceSquared;
                bestIndex = index;
            }
        }

        camera.position = positions[bestIndex];
        camera.forward = forwards[bestIndex];
        glm::vec3 sceneUpAccumulator(0.0f);
        for (glm::vec3 sceneUp : sceneUps)
        {
            if (glm::dot(sceneUpAccumulator, sceneUp) < 0.0f)
            {
                sceneUp = -sceneUp;
            }
            sceneUpAccumulator += sceneUp;
        }
        const float sceneUpLengthSquared = glm::dot(sceneUpAccumulator, sceneUpAccumulator);
        if (std::isfinite(sceneUpLengthSquared) && sceneUpLengthSquared > 1.0e-12f)
        {
            camera.sceneUp = sceneUpAccumulator * glm::inversesqrt(sceneUpLengthSquared);
            camera.hasSceneUp = true;
        }
        return true;
    }

    inline bool tryApplySuggestedCameraSidecar(const std::string &plyPath, CpuGaussianScene &scene)
    {
        namespace fs = std::filesystem;

        std::error_code errorCode;
        fs::path directory = fs::path(plyPath).parent_path();
        for (int depth = 0; depth < 4 && !directory.empty(); ++depth)
        {
            const fs::path candidate = directory / "cameras.json";
            if (fs::exists(candidate, errorCode) && !errorCode)
            {
                SuggestedCamera camera;
                if (!tryParseSuggestedCameraFromCamerasJson(candidate, camera))
                {
                    return false;
                }

                scene.suggestedCameraPosition[0] = camera.position.x;
                scene.suggestedCameraPosition[1] = camera.position.y;
                scene.suggestedCameraPosition[2] = camera.position.z;
                scene.suggestedCameraForward[0] = camera.forward.x;
                scene.suggestedCameraForward[1] = camera.forward.y;
                scene.suggestedCameraForward[2] = camera.forward.z;
                scene.hasSuggestedCamera = true;
                scene.suggestedSceneUp[0] = camera.sceneUp.x;
                scene.suggestedSceneUp[1] = camera.sceneUp.y;
                scene.suggestedSceneUp[2] = camera.sceneUp.z;
                scene.hasSuggestedSceneUp = camera.hasSceneUp;
                return true;
            }

            directory = directory.parent_path();
        }

        return false;
    }

    inline const Element *findVertexElement(const std::vector<Element> &elements)
    {
        for (const Element &element : elements)
        {
            if (element.name == "vertex")
            {
                return &element;
            }
        }

        return nullptr;
    }

    inline std::uint32_t expectedRestCoefficientCountForDegree(std::uint32_t shDegree)
    {
        if (shDegree == 0u)
        {
            return 0u;
        }

        const std::uint32_t coefficientsPerChannel = (shDegree + 1u) * (shDegree + 1u) - 1u;
        return coefficientsPerChannel * 3u;
    }

    inline std::uint32_t inferShDegree(const Element &vertexElement)
    {
        std::array<bool, 45u> restCoefficientSeen = {};
        std::size_t restCoefficientCount = 0u;

        for (const Property &property : vertexElement.properties)
        {
            std::size_t restIndex = 0u;
            if (!parseIndexedPropertyName(property.name, "f_rest_", restIndex))
            {
                continue;
            }

            if (restIndex >= restCoefficientSeen.size())
            {
                throw std::runtime_error("Unsupported 3DGS PLY: f_rest index exceeds SH3 capacity.");
            }

            if (!restCoefficientSeen[restIndex])
            {
                restCoefficientSeen[restIndex] = true;
                ++restCoefficientCount;
            }
        }

        if (restCoefficientCount == 0u)
        {
            return 0u;
        }

        for (std::uint32_t shDegree = 1u; shDegree <= 3u; ++shDegree)
        {
            const std::uint32_t expectedCount = expectedRestCoefficientCountForDegree(shDegree);
            if (restCoefficientCount != expectedCount)
            {
                continue;
            }

            for (std::uint32_t coefficientIndex = 0u; coefficientIndex < expectedCount; ++coefficientIndex)
            {
                if (!restCoefficientSeen[coefficientIndex])
                {
                    throw std::runtime_error("Unsupported 3DGS PLY: f_rest coefficients are incomplete or out of order.");
                }
            }

            return shDegree;
        }

        throw std::runtime_error("Unsupported 3DGS PLY: expected 0, 9, 24, or 45 f_rest coefficients.");
    }

    struct BinaryFloatVertexLayout
    {
        static constexpr std::size_t InvalidOffset = std::numeric_limits<std::size_t>::max();

        std::size_t vertexCount = 0u;
        std::size_t vertexStride = 0u;
        std::size_t vertexDataOffset = 0u;
        std::uint32_t shDegree = 0u;
        std::array<std::size_t, 3u> positionOffsets = {};
        std::array<std::size_t, 3u> dcOffsets = {};
        std::array<std::size_t, 3u> rgbOffsets = {};
        std::array<std::size_t, 45u> shRestOffsets = {};
        std::size_t opacityOffset = InvalidOffset;
        std::array<std::size_t, 3u> scaleOffsets = {};
        std::array<std::size_t, 4u> rotationOffsets = {};

        BinaryFloatVertexLayout()
        {
            positionOffsets.fill(InvalidOffset);
            dcOffsets.fill(InvalidOffset);
            rgbOffsets.fill(InvalidOffset);
            shRestOffsets.fill(InvalidOffset);
            scaleOffsets.fill(InvalidOffset);
            rotationOffsets.fill(InvalidOffset);
        }
    };

    struct ParallelDecodeStats
    {
        float boundsMin[3] = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
        };
        float boundsMax[3] = {
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
        };
        std::size_t invalidSplats = 0u;
        std::size_t axisClamps = 0u;
    };

    struct MappedReadOnlyFile
    {
        int fd = -1;
        const std::uint8_t *data = nullptr;
        std::size_t size = 0u;

        MappedReadOnlyFile() = default;
        MappedReadOnlyFile(const MappedReadOnlyFile &) = delete;
        MappedReadOnlyFile &operator=(const MappedReadOnlyFile &) = delete;

        MappedReadOnlyFile(MappedReadOnlyFile &&other) noexcept
            : fd(other.fd), data(other.data), size(other.size)
        {
            other.fd = -1;
            other.data = nullptr;
            other.size = 0u;
        }

        MappedReadOnlyFile &operator=(MappedReadOnlyFile &&other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }

            reset();
            fd = other.fd;
            data = other.data;
            size = other.size;
            other.fd = -1;
            other.data = nullptr;
            other.size = 0u;
            return *this;
        }

        ~MappedReadOnlyFile()
        {
            reset();
        }

        void reset()
        {
            if (data != nullptr && size > 0u)
            {
                munmap(const_cast<std::uint8_t *>(data), size);
            }
            if (fd >= 0)
            {
                close(fd);
            }
            fd = -1;
            data = nullptr;
            size = 0u;
        }
    };

    inline float readMappedFloat32(const std::uint8_t *data, std::size_t offset)
    {
        float value = 0.0f;
        std::memcpy(&value, data + offset, sizeof(value));
        return value;
    }

    inline void mergeBounds(float boundsMin[3], float boundsMax[3], const float otherMin[3], const float otherMax[3])
    {
        boundsMin[0] = std::min(boundsMin[0], otherMin[0]);
        boundsMin[1] = std::min(boundsMin[1], otherMin[1]);
        boundsMin[2] = std::min(boundsMin[2], otherMin[2]);
        boundsMax[0] = std::max(boundsMax[0], otherMax[0]);
        boundsMax[1] = std::max(boundsMax[1], otherMax[1]);
        boundsMax[2] = std::max(boundsMax[2], otherMax[2]);
    }

    inline std::size_t computeDecodeWorkerCount(std::size_t vertexCount)
    {
        const std::size_t hardwareThreadCount = std::max<std::size_t>(std::thread::hardware_concurrency(), 1u);
        const std::size_t cappedThreadCount = std::min<std::size_t>(hardwareThreadCount, 6u);
        constexpr std::size_t kMinVerticesPerThread = 1u << 18;
        const std::size_t workLimitedThreadCount = std::max<std::size_t>(1u, (vertexCount + kMinVerticesPerThread - 1u) / kMinVerticesPerThread);
        return std::max<std::size_t>(1u, std::min(cappedThreadCount, workLimitedThreadCount));
    }

    template <typename Func>
    inline void parallelForVertexRanges(std::size_t vertexCount, std::size_t workerCount, Func &&func)
    {
        if (workerCount <= 1u || vertexCount <= 1u)
        {
            func(0u, vertexCount, 0u);
            return;
        }

        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        std::mutex errorMutex;
        std::exception_ptr firstError;

        for (std::size_t workerIndex = 0u; workerIndex < workerCount; ++workerIndex)
        {
            const std::size_t begin = (vertexCount * workerIndex) / workerCount;
            const std::size_t end = (vertexCount * (workerIndex + 1u)) / workerCount;
            workers.emplace_back([&, begin, end, workerIndex]() {
                try
                {
                    func(begin, end, workerIndex);
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (!firstError)
                    {
                        firstError = std::current_exception();
                    }
                }
            });
        }

        for (std::thread &worker : workers)
        {
            worker.join();
        }

        if (firstError)
        {
            std::rethrow_exception(firstError);
        }
    }

    inline bool tryBuildBinaryFloatVertexLayout(Format format, const std::vector<Element> &elements, std::size_t vertexDataOffset, BinaryFloatVertexLayout &layout)
    {
        if (format != Format::BinaryLittleEndian || elements.empty())
        {
            return false;
        }

        const Element *vertexElement = findVertexElement(elements);
        if (vertexElement == nullptr || vertexElement != &elements.front())
        {
            return false;
        }

        layout = BinaryFloatVertexLayout();
        layout.vertexCount = vertexElement->count;
        layout.vertexDataOffset = vertexDataOffset;
        layout.shDegree = inferShDegree(*vertexElement);

        std::size_t runningOffset = 0u;
        for (const Property &property : vertexElement->properties)
        {
            if (property.isList || property.type != ScalarType::Float32)
            {
                return false;
            }

            if (property.name == "x")
                layout.positionOffsets[0] = runningOffset;
            else if (property.name == "y")
                layout.positionOffsets[1] = runningOffset;
            else if (property.name == "z")
                layout.positionOffsets[2] = runningOffset;
            else if (property.name == "f_dc_0")
                layout.dcOffsets[0] = runningOffset;
            else if (property.name == "f_dc_1")
                layout.dcOffsets[1] = runningOffset;
            else if (property.name == "f_dc_2")
                layout.dcOffsets[2] = runningOffset;
            else if (property.name == "red")
                layout.rgbOffsets[0] = runningOffset;
            else if (property.name == "green")
                layout.rgbOffsets[1] = runningOffset;
            else if (property.name == "blue")
                layout.rgbOffsets[2] = runningOffset;
            else if (property.name == "opacity" || property.name == "alpha")
                layout.opacityOffset = runningOffset;
            else if (property.name == "scale_0")
                layout.scaleOffsets[0] = runningOffset;
            else if (property.name == "scale_1")
                layout.scaleOffsets[1] = runningOffset;
            else if (property.name == "scale_2")
                layout.scaleOffsets[2] = runningOffset;
            else if (property.name == "rot_0")
                layout.rotationOffsets[0] = runningOffset;
            else if (property.name == "rot_1")
                layout.rotationOffsets[1] = runningOffset;
            else if (property.name == "rot_2")
                layout.rotationOffsets[2] = runningOffset;
            else if (property.name == "rot_3")
                layout.rotationOffsets[3] = runningOffset;
            else
            {
                std::size_t shRestIndex = 0u;
                if (parseIndexedPropertyName(property.name, "f_rest_", shRestIndex) && shRestIndex < layout.shRestOffsets.size())
                {
                    layout.shRestOffsets[shRestIndex] = runningOffset;
                }
            }

            runningOffset += sizeof(float);
        }

        layout.vertexStride = runningOffset;
        return
            layout.positionOffsets[0] != BinaryFloatVertexLayout::InvalidOffset &&
            layout.positionOffsets[1] != BinaryFloatVertexLayout::InvalidOffset &&
            layout.positionOffsets[2] != BinaryFloatVertexLayout::InvalidOffset;
    }

    inline MappedReadOnlyFile mapReadOnlyFile(const std::string &path)
    {
        MappedReadOnlyFile file;
        file.fd = open(path.c_str(), O_RDONLY);
        if (file.fd < 0)
        {
            throw std::runtime_error("Failed to open PLY file for mapped fast path: " + path);
        }

        struct stat status = {};
        if (fstat(file.fd, &status) != 0 || status.st_size <= 0)
        {
            throw std::runtime_error("Failed to stat PLY file for mapped fast path: " + path);
        }

        file.size = static_cast<std::size_t>(status.st_size);
        void *mapping = mmap(nullptr, file.size, PROT_READ, MAP_PRIVATE, file.fd, 0);
        if (mapping == MAP_FAILED)
        {
            throw std::runtime_error("Failed to memory-map PLY file for fast path: " + path);
        }

        file.data = static_cast<const std::uint8_t *>(mapping);
        return file;
    }

    struct RawGaussian
    {
        float position[3] = {0.0f, 0.0f, 0.0f};
        float dc[3] = {0.0f, 0.0f, 0.0f};
        float rgb[3] = {1.0f, 1.0f, 1.0f};
        std::array<float, 45> shRest = {};
        float scale[3] = {-4.5f, -4.5f, -4.5f};
        float rotation[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        float opacity = 1.0f;
        bool hasDc = false;
        bool hasRgb = false;
        bool hasShRest = false;
        bool hasScale = false;
        bool hasRotation = false;
        bool hasOpacity = false;
    };

    inline float sigmoid(float value)
    {
        return 1.0f / (1.0f + std::exp(-value));
    }

    inline float sanitizeLinearScaleComponent(float value)
    {
        constexpr float kMinLinearScale = 1.0e-8f;
        if (!std::isfinite(value) || value <= kMinLinearScale)
        {
            return kMinLinearScale;
        }
        return value;
    }

    inline glm::quat makeSafeRotationQuaternion(const RawGaussian &raw)
    {
        if (!raw.hasRotation)
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        const glm::quat rotation(raw.rotation[0], raw.rotation[1], raw.rotation[2], raw.rotation[3]);
        const float lengthSquared =
            rotation.w * rotation.w +
            rotation.x * rotation.x +
            rotation.y * rotation.y +
            rotation.z * rotation.z;
        if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-20f)
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        return rotation * glm::inversesqrt(lengthSquared);
    }

    inline CpuGaussianSplat convertRawGaussian(const RawGaussian &raw)
    {
        constexpr float kShC0 = 0.28209479177387814f;

        CpuGaussianSplat splat;
        splat.position[0] = raw.position[0];
        splat.position[1] = raw.position[1];
        splat.position[2] = raw.position[2];

        glm::vec3 color(1.0f);
        if (raw.hasDc)
        {
            color = glm::clamp(glm::vec3(0.5f) + glm::vec3(raw.dc[0], raw.dc[1], raw.dc[2]) * kShC0, glm::vec3(0.0f), glm::vec3(1.0f));
            splat.shDc[0] = raw.dc[0];
            splat.shDc[1] = raw.dc[1];
            splat.shDc[2] = raw.dc[2];
        }
        else if (raw.hasRgb)
        {
            color = glm::clamp(glm::vec3(raw.rgb[0], raw.rgb[1], raw.rgb[2]), glm::vec3(0.0f), glm::vec3(1.0f));
        }

        if (!raw.hasDc)
        {
            splat.shDc[0] = (color.x - 0.5f) / kShC0;
            splat.shDc[1] = (color.y - 0.5f) / kShC0;
            splat.shDc[2] = (color.z - 0.5f) / kShC0;
        }

        glm::vec3 scale = raw.hasScale ? glm::exp(glm::vec3(raw.scale[0], raw.scale[1], raw.scale[2])) : glm::vec3(0.01f);
        scale.x = sanitizeLinearScaleComponent(scale.x);
        scale.y = sanitizeLinearScaleComponent(scale.y);
        scale.z = sanitizeLinearScaleComponent(scale.z);

        const glm::quat rotation = makeSafeRotationQuaternion(raw);
        const glm::mat3 basis = glm::mat3_cast(rotation);

        // Match the official Graphdeco covariance path:
        //   L = R * S
        //   Sigma = L * transpose(L) = R * S^2 * transpose(R)
        // Our renderer stores three world-space axis vectors A whose outer
        // products sum to the covariance, so we must keep the scaled columns of
        // R rather than the scaled rows.
        const glm::vec3 axis0 = basis[0] * scale.x;
        const glm::vec3 axis1 = basis[1] * scale.y;
        const glm::vec3 axis2 = basis[2] * scale.z;

        splat.axis0[0] = axis0.x;
        splat.axis0[1] = axis0.y;
        splat.axis0[2] = axis0.z;
        splat.axis1[0] = axis1.x;
        splat.axis1[1] = axis1.y;
        splat.axis1[2] = axis1.z;
        splat.axis2[0] = axis2.x;
        splat.axis2[1] = axis2.y;
        splat.axis2[2] = axis2.z;
        splat.color[0] = color.x;
        splat.color[1] = color.y;
        splat.color[2] = color.z;
        splat.shRest = raw.shRest;
        splat.opacity = glm::clamp(raw.hasOpacity ? sigmoid(raw.opacity) : 1.0f, 1.0f / 255.0f, 0.99f);
        return splat;
    }

    inline void assignProperty(RawGaussian &raw, const std::string &name, float value)
    {
        if (name == "x")
            raw.position[0] = value;
        else if (name == "y")
            raw.position[1] = value;
        else if (name == "z")
            raw.position[2] = value;
        else if (name == "f_dc_0")
        {
            raw.dc[0] = value;
            raw.hasDc = true;
        }
        else if (name == "f_dc_1")
        {
            raw.dc[1] = value;
            raw.hasDc = true;
        }
        else if (name == "f_dc_2")
        {
            raw.dc[2] = value;
            raw.hasDc = true;
        }
        else if (name == "red")
        {
            raw.rgb[0] = value > 1.0f ? value / 255.0f : value;
            raw.hasRgb = true;
        }
        else if (name == "green")
        {
            raw.rgb[1] = value > 1.0f ? value / 255.0f : value;
            raw.hasRgb = true;
        }
        else if (name == "blue")
        {
            raw.rgb[2] = value > 1.0f ? value / 255.0f : value;
            raw.hasRgb = true;
        }
        else if (name == "opacity" || name == "alpha")
        {
            raw.opacity = value;
            raw.hasOpacity = true;
        }
        else if (name == "scale_0")
        {
            raw.scale[0] = value;
            raw.hasScale = true;
        }
        else if (name == "scale_1")
        {
            raw.scale[1] = value;
            raw.hasScale = true;
        }
        else if (name == "scale_2")
        {
            raw.scale[2] = value;
            raw.hasScale = true;
        }
        else if (name == "rot_0")
        {
            raw.rotation[0] = value;
            raw.hasRotation = true;
        }
        else if (name == "rot_1")
        {
            raw.rotation[1] = value;
            raw.hasRotation = true;
        }
        else if (name == "rot_2")
        {
            raw.rotation[2] = value;
            raw.hasRotation = true;
        }
        else if (name == "rot_3")
        {
            raw.rotation[3] = value;
            raw.hasRotation = true;
        }
        else
        {
            std::size_t shRestIndex = 0u;
            if (parseIndexedPropertyName(name, "f_rest_", shRestIndex) && shRestIndex < raw.shRest.size())
            {
                raw.shRest[shRestIndex] = value;
                raw.hasShRest = true;
            }
        }
    }

    inline void decodeRawGaussianFromBinaryFloatLayout(const std::uint8_t *vertexData, const BinaryFloatVertexLayout &layout, RawGaussian &raw)
    {
        raw = RawGaussian();

        raw.position[0] = readMappedFloat32(vertexData, layout.positionOffsets[0]);
        raw.position[1] = readMappedFloat32(vertexData, layout.positionOffsets[1]);
        raw.position[2] = readMappedFloat32(vertexData, layout.positionOffsets[2]);

        if (layout.dcOffsets[0] != BinaryFloatVertexLayout::InvalidOffset &&
            layout.dcOffsets[1] != BinaryFloatVertexLayout::InvalidOffset &&
            layout.dcOffsets[2] != BinaryFloatVertexLayout::InvalidOffset)
        {
            raw.dc[0] = readMappedFloat32(vertexData, layout.dcOffsets[0]);
            raw.dc[1] = readMappedFloat32(vertexData, layout.dcOffsets[1]);
            raw.dc[2] = readMappedFloat32(vertexData, layout.dcOffsets[2]);
            raw.hasDc = true;
        }
        else if (layout.rgbOffsets[0] != BinaryFloatVertexLayout::InvalidOffset &&
                 layout.rgbOffsets[1] != BinaryFloatVertexLayout::InvalidOffset &&
                 layout.rgbOffsets[2] != BinaryFloatVertexLayout::InvalidOffset)
        {
            raw.rgb[0] = readMappedFloat32(vertexData, layout.rgbOffsets[0]);
            raw.rgb[1] = readMappedFloat32(vertexData, layout.rgbOffsets[1]);
            raw.rgb[2] = readMappedFloat32(vertexData, layout.rgbOffsets[2]);
            raw.hasRgb = true;
        }

        for (std::size_t coefficientIndex = 0u; coefficientIndex < layout.shRestOffsets.size(); ++coefficientIndex)
        {
            if (layout.shRestOffsets[coefficientIndex] == BinaryFloatVertexLayout::InvalidOffset)
            {
                continue;
            }

            raw.shRest[coefficientIndex] = readMappedFloat32(vertexData, layout.shRestOffsets[coefficientIndex]);
            raw.hasShRest = true;
        }

        if (layout.opacityOffset != BinaryFloatVertexLayout::InvalidOffset)
        {
            raw.opacity = readMappedFloat32(vertexData, layout.opacityOffset);
            raw.hasOpacity = true;
        }

        if (layout.scaleOffsets[0] != BinaryFloatVertexLayout::InvalidOffset &&
            layout.scaleOffsets[1] != BinaryFloatVertexLayout::InvalidOffset &&
            layout.scaleOffsets[2] != BinaryFloatVertexLayout::InvalidOffset)
        {
            raw.scale[0] = readMappedFloat32(vertexData, layout.scaleOffsets[0]);
            raw.scale[1] = readMappedFloat32(vertexData, layout.scaleOffsets[1]);
            raw.scale[2] = readMappedFloat32(vertexData, layout.scaleOffsets[2]);
            raw.hasScale = true;
        }

        if (layout.rotationOffsets[0] != BinaryFloatVertexLayout::InvalidOffset &&
            layout.rotationOffsets[1] != BinaryFloatVertexLayout::InvalidOffset &&
            layout.rotationOffsets[2] != BinaryFloatVertexLayout::InvalidOffset &&
            layout.rotationOffsets[3] != BinaryFloatVertexLayout::InvalidOffset)
        {
            raw.rotation[0] = readMappedFloat32(vertexData, layout.rotationOffsets[0]);
            raw.rotation[1] = readMappedFloat32(vertexData, layout.rotationOffsets[1]);
            raw.rotation[2] = readMappedFloat32(vertexData, layout.rotationOffsets[2]);
            raw.rotation[3] = readMappedFloat32(vertexData, layout.rotationOffsets[3]);
            raw.hasRotation = true;
        }
    }

    inline SceneSafety::PreparedScene loadPreparedForStableBaselineFastPath(const std::string &path, const BinaryFloatVertexLayout &layout, const MappedReadOnlyFile &file)
    {
        if (layout.vertexCount == 0u)
        {
            throw std::runtime_error("PLY file does not contain any vertex splats.");
        }

        if (layout.vertexStride == 0u || layout.vertexDataOffset > file.size)
        {
            throw std::runtime_error("Fast-path PLY layout is invalid.");
        }

        const std::size_t availableBytes = file.size - layout.vertexDataOffset;
        if (layout.vertexCount > availableBytes / layout.vertexStride)
        {
            throw std::runtime_error("PLY vertex payload is truncated.");
        }

        SceneSafety::PreparedScene prepared;
        prepared.originalSplats = layout.vertexCount;
        prepared.scene.shDegree = layout.shDegree;
        prepared.scene.splats.resize(layout.vertexCount);
        std::vector<std::uint8_t> validMask(layout.vertexCount, 0u);

        const std::size_t workerCount = computeDecodeWorkerCount(layout.vertexCount);
        std::vector<ParallelDecodeStats> workerStats(workerCount);
        const std::uint8_t *vertexBase = file.data + layout.vertexDataOffset;

        parallelForVertexRanges(layout.vertexCount, workerCount, [&](std::size_t begin, std::size_t end, std::size_t workerIndex) {
            ParallelDecodeStats &stats = workerStats[workerIndex];
            for (std::size_t vertexIndex = begin; vertexIndex < end; ++vertexIndex)
            {
                const std::uint8_t *vertexData = vertexBase + vertexIndex * layout.vertexStride;
                RawGaussian raw;
                decodeRawGaussianFromBinaryFloatLayout(vertexData, layout, raw);
                CpuGaussianSplat splat = convertRawGaussian(raw);
                if (!SceneSafety::isFinite3(splat.position))
                {
                    ++stats.invalidSplats;
                    continue;
                }

                prepared.scene.splats[vertexIndex] = splat;
                validMask[vertexIndex] = 1u;
                stats.boundsMin[0] = std::min(stats.boundsMin[0], splat.position[0]);
                stats.boundsMin[1] = std::min(stats.boundsMin[1], splat.position[1]);
                stats.boundsMin[2] = std::min(stats.boundsMin[2], splat.position[2]);
                stats.boundsMax[0] = std::max(stats.boundsMax[0], splat.position[0]);
                stats.boundsMax[1] = std::max(stats.boundsMax[1], splat.position[1]);
                stats.boundsMax[2] = std::max(stats.boundsMax[2], splat.position[2]);
            }
        });

        float boundsMin[3] = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
        };
        float boundsMax[3] = {
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
        };
        for (const ParallelDecodeStats &stats : workerStats)
        {
            prepared.invalidSplatsRemoved += stats.invalidSplats;
            mergeBounds(boundsMin, boundsMax, stats.boundsMin, stats.boundsMax);
        }

        if (prepared.invalidSplatsRemoved >= layout.vertexCount || boundsMin[0] == std::numeric_limits<float>::max())
        {
            throw std::runtime_error("SceneSafety removed all splats because the scene data was invalid.");
        }

        prepared.sceneDiagonal = SceneSafety::computeSceneDiagonalFromBounds(boundsMin, boundsMax);
        prepared.minAxisLength = SceneSafety::computeMinAxisLengthForSceneDiagonal(prepared.sceneDiagonal);

        parallelForVertexRanges(layout.vertexCount, workerCount, [&](std::size_t begin, std::size_t end, std::size_t workerIndex) {
            ParallelDecodeStats &stats = workerStats[workerIndex];
            for (std::size_t vertexIndex = begin; vertexIndex < end; ++vertexIndex)
            {
                if (validMask[vertexIndex] == 0u)
                {
                    continue;
                }

                SceneSafety::sanitizeSplatForStableBaseline(prepared.scene.splats[vertexIndex], prepared.minAxisLength, stats.axisClamps);
            }
        });

        for (const ParallelDecodeStats &stats : workerStats)
        {
            prepared.axisClamps += stats.axisClamps;
        }

        if (prepared.invalidSplatsRemoved > 0u)
        {
            std::vector<CpuGaussianSplat> compacted;
            compacted.reserve(layout.vertexCount - prepared.invalidSplatsRemoved);
            for (std::size_t vertexIndex = 0u; vertexIndex < layout.vertexCount; ++vertexIndex)
            {
                if (validMask[vertexIndex] != 0u)
                {
                    compacted.push_back(prepared.scene.splats[vertexIndex]);
                }
            }
            prepared.scene.splats = std::move(compacted);
        }

        SceneSafety::setSceneBounds(prepared.scene, boundsMin, boundsMax);
        tryApplySuggestedCameraSidecar(path, prepared.scene);
        SceneSafety::computeRobustFocusHint(prepared.scene);
        return prepared;
    }

    inline void skipProperty(std::istream &stream, Format format, const Property &property)
    {
        if (property.isList)
        {
            const std::uint32_t count = readListCount(stream, format, property.listCountType);
            if (format == Format::Ascii)
            {
                for (std::uint32_t index = 0; index < count; ++index)
                {
                    double ignoredValue = 0.0;
                    stream >> ignoredValue;
                }
            }
            else
            {
                stream.seekg(static_cast<std::streamoff>(scalarTypeSize(property.listValueType) * count), std::ios::cur);
            }
            return;
        }

        if (format == Format::Ascii)
        {
            double ignoredValue = 0.0;
            stream >> ignoredValue;
        }
        else
        {
            stream.seekg(static_cast<std::streamoff>(scalarTypeSize(property.type)), std::ios::cur);
        }
    }

    inline CpuGaussianScene load(const std::string &path);

    inline SceneSafety::PreparedScene loadPreparedForStableBaseline(const std::string &path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            throw std::runtime_error("Failed to open PLY file: " + path);
        }

        const auto [format, elements] = parseHeader(stream);
        const std::streamoff vertexDataOffset = stream.tellg();
        BinaryFloatVertexLayout fastLayout;
        if (vertexDataOffset >= 0 && tryBuildBinaryFloatVertexLayout(format, elements, static_cast<std::size_t>(vertexDataOffset), fastLayout))
        {
            MappedReadOnlyFile file = mapReadOnlyFile(path);
            return loadPreparedForStableBaselineFastPath(path, fastLayout, file);
        }

        CpuGaussianScene rawScene = load(path);
        return SceneSafety::prepareForStableBaseline(rawScene);
    }

    inline CpuGaussianScene load(const std::string &path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            throw std::runtime_error("Failed to open PLY file: " + path);
        }

        const auto [format, elements] = parseHeader(stream);
        CpuGaussianScene scene;
        const Element *vertexElement = findVertexElement(elements);
        if (vertexElement == nullptr)
        {
            throw std::runtime_error("PLY file does not contain a vertex element.");
        }

        inferShDegree(*vertexElement);
        scene.shDegree = 3u;

        for (const Element &element : elements)
        {
            if (element.name == "vertex")
            {
                for (std::size_t vertexIndex = 0; vertexIndex < element.count; ++vertexIndex)
                {
                    RawGaussian raw;
                    for (const Property &property : element.properties)
                    {
                        if (property.isList)
                        {
                            skipProperty(stream, format, property);
                            continue;
                        }

                        const float value = static_cast<float>(readScalar(stream, format, property.type));
                        assignProperty(raw, property.name, value);
                    }

                    CpuGaussianSplat splat = convertRawGaussian(raw);
                    scene.expandBounds(splat);
                    scene.splats.push_back(splat);
                }
            }
            else
            {
                for (std::size_t elementIndex = 0; elementIndex < element.count; ++elementIndex)
                {
                    for (const Property &property : element.properties)
                    {
                        skipProperty(stream, format, property);
                    }
                }
            }
        }

        if (scene.empty())
        {
            throw std::runtime_error("PLY file does not contain any vertex splats.");
        }

        tryApplySuggestedCameraSidecar(path, scene);
        return scene;
    }
} // namespace PlyLoader

#endif
