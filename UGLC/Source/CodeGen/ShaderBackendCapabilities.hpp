#pragma once

#include <CodeGen/UGLC.Constants.hpp>

namespace UGLC::CodeGen
{
    enum class ShaderBackendKind : int
    {
        MSL = 1,
        HLSLSPIRV,
    };

    // Describes the semantic and ABI limits a backend imposes on validated DSL.
    // Future backends should extend this object instead of scattering new
    // hard-coded rules across visitors.
    struct ShaderBackendCapabilities
    {
        ShaderBackendKind kind = ShaderBackendKind::MSL;
        const char *backendName = "MSL";
        const char *diagnosticDisplayName = "Metal";
        int maxBindGroupCount = MaxBindGroupCount;
        int renderVertexInputReservedBufferSlots = 1;
        bool supportsWaveLaneQueriesInCompute = true;
        bool supportsWaveLaneQueriesInRender = false;
        bool supportsWaveCollectivesInCompute = true;
        bool supportsWaveCollectivesInRender = false;
        bool supportsWaveReadLaneAtInCompute = true;
        bool supportsWaveReadLaneAtInRender = false;
        bool supportsQuadReadLaneAtInRender = true;
        bool supportsHullShader = true;
        bool supportsDomainShader = true;
    };

    inline const ShaderBackendCapabilities &getMSLShaderBackendCapabilities()
    {
        static const ShaderBackendCapabilities capabilities{
            .kind = ShaderBackendKind::MSL,
            .backendName = "MSL",
            .diagnosticDisplayName = "Metal",
            .maxBindGroupCount = MaxBindGroupCount,
            .renderVertexInputReservedBufferSlots = 1,
            .supportsWaveLaneQueriesInCompute = true,
            .supportsWaveLaneQueriesInRender = false,
            .supportsWaveCollectivesInCompute = true,
            .supportsWaveCollectivesInRender = false,
            .supportsWaveReadLaneAtInCompute = true,
            .supportsWaveReadLaneAtInRender = false,
            .supportsQuadReadLaneAtInRender = true,
            .supportsHullShader = true,
            .supportsDomainShader = true,
        };
        return capabilities;
    }

    inline const ShaderBackendCapabilities &getHLSLSPIRVShaderBackendCapabilities()
    {
        static const ShaderBackendCapabilities capabilities{
            .kind = ShaderBackendKind::HLSLSPIRV,
            .backendName = "HLSL/SPIR-V",
            .diagnosticDisplayName = "HLSL/SPIR-V",
            .maxBindGroupCount = MaxBindGroupCount,
            .renderVertexInputReservedBufferSlots = 0,
            .supportsWaveLaneQueriesInCompute = true,
            .supportsWaveLaneQueriesInRender = false,
            .supportsWaveCollectivesInCompute = true,
            .supportsWaveCollectivesInRender = false,
            .supportsWaveReadLaneAtInCompute = true,
            .supportsWaveReadLaneAtInRender = false,
            .supportsQuadReadLaneAtInRender = false,
            .supportsHullShader = false,
            .supportsDomainShader = false,
        };
        return capabilities;
    }
} // namespace UGLC::CodeGen
