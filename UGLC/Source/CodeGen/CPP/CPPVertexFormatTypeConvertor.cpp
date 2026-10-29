#include "CPPVertexFormatTypeConvertor.hpp"

namespace UGLC::CodeGen::CPP
{
    namespace
    {
        struct VertexFormatMapping
        {
            const char *canonicalName;
            const char *formatExpr;
            int storageBytes;
        };

        std::string normalizeVertexFormatCanonicalName(const std::string &canonicalName)
        {
            if (canonicalName == "float" || canonicalName == "float1" || canonicalName == "UGL::float1")
            {
                return "UGL::float";
            }
            if (canonicalName == "float2" || canonicalName == "float3" || canonicalName == "float4")
            {
                return "UGL::" + canonicalName;
            }
            if (canonicalName == "int" || canonicalName == "int1" || canonicalName == "UGL::int1")
            {
                return "UGL::int";
            }
            if (canonicalName == "int2" || canonicalName == "int3" || canonicalName == "int4")
            {
                return "UGL::" + canonicalName;
            }
            if (canonicalName == "uint" || canonicalName == "uint1" || canonicalName == "unsigned int" || canonicalName == "UGL::uint1")
            {
                return "UGL::uint";
            }
            if (canonicalName == "uint2" || canonicalName == "uint3" || canonicalName == "uint4")
            {
                return "UGL::" + canonicalName;
            }
            if (canonicalName == "half" || canonicalName == "half1" || canonicalName == "UGL::half1")
            {
                return "UGL::half";
            }
            if (canonicalName == "half2" || canonicalName == "half3" || canonicalName == "half4")
            {
                return "UGL::" + canonicalName;
            }

            return canonicalName;
        }

        const VertexFormatMapping &getVertexFormatMapping(const std::string &canonicalName)
        {
            static const VertexFormatMapping Mappings[] = {
                {.canonicalName = "UGL::half2", .formatExpr = "GVM::RHI::VertexFormat::Float16x2", .storageBytes = 2 * 2},
                {.canonicalName = "UGL::half4", .formatExpr = "GVM::RHI::VertexFormat::Float16x4", .storageBytes = 2 * 4},
                {.canonicalName = "UGL::float", .formatExpr = "GVM::RHI::VertexFormat::Float32", .storageBytes = sizeof(float)},
                {.canonicalName = "UGL::float2", .formatExpr = "GVM::RHI::VertexFormat::Float32x2", .storageBytes = sizeof(float) * 2},
                {.canonicalName = "UGL::float3", .formatExpr = "GVM::RHI::VertexFormat::Float32x3", .storageBytes = sizeof(float) * 3},
                {.canonicalName = "UGL::float4", .formatExpr = "GVM::RHI::VertexFormat::Float32x4", .storageBytes = sizeof(float) * 4},
                {.canonicalName = "UGL::int", .formatExpr = "GVM::RHI::VertexFormat::Sint32", .storageBytes = sizeof(int)},
                {.canonicalName = "UGL::int2", .formatExpr = "GVM::RHI::VertexFormat::Sint32x2", .storageBytes = sizeof(int) * 2},
                {.canonicalName = "UGL::int3", .formatExpr = "GVM::RHI::VertexFormat::Sint32x3", .storageBytes = sizeof(int) * 3},
                {.canonicalName = "UGL::int4", .formatExpr = "GVM::RHI::VertexFormat::Sint32x4", .storageBytes = sizeof(int) * 4},
                {.canonicalName = "UGL::uint", .formatExpr = "GVM::RHI::VertexFormat::Uint32", .storageBytes = sizeof(unsigned int)},
                {.canonicalName = "UGL::uint2", .formatExpr = "GVM::RHI::VertexFormat::Uint32x2", .storageBytes = sizeof(unsigned int) * 2},
                {.canonicalName = "UGL::uint3", .formatExpr = "GVM::RHI::VertexFormat::Uint32x3", .storageBytes = sizeof(unsigned int) * 3},
                {.canonicalName = "UGL::uint4", .formatExpr = "GVM::RHI::VertexFormat::Uint32x4", .storageBytes = sizeof(unsigned int) * 4},
            };

            std::string normalizedName = normalizeVertexFormatCanonicalName(canonicalName);
            for (const auto &mapping : Mappings)
            {
                if (normalizedName == mapping.canonicalName)
                {
                    return mapping;
                }
            }

            if (normalizedName == "UGL::half" || normalizedName == "UGL::half3")
            {
                throw std::runtime_error("Unsupported vertex format type: " + canonicalName + " (GVM::RHI::VertexFormat does not provide a 16-bit scalar/x3 vertex format)");
            }

            throw std::runtime_error("Unsupported vertex format type: " + canonicalName);
        }
    }

    int getVertexFormatStorageBytes(const std::string &canonicalName)
    {
        return getVertexFormatMapping(canonicalName).storageBytes;
    }

    std::string CPPVertexFormatTypeConvertor::convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const
    {
        (void)templateArgs;
        return getVertexFormatMapping(canonicalName).formatExpr;
    }

    bool CPPVertexFormatTypeConvertor::checkShouldIgnoreTemplateParams(const std::string &canonicalName) const
    {
        (void)canonicalName;
        return false;
    }

    bool CPPVertexFormatTypeConvertor::checkShouldIgnoreUsingDecl(const std::string &canonicalName) const
    {
        (void)canonicalName;
        return false;
    }


} // namespace UGLC::CodeGen::CPP
