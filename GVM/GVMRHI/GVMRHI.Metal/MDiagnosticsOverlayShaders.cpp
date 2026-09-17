#include "MDiagnosticsOverlayShaders.hpp"

#include <stdexcept>

namespace GVM::RHI::Metal
{
    namespace
    {
        constexpr const char *DiagnosticsOverlayVertexEntryPoint = "diagnosticsOverlayVertexMain";
        constexpr const char *DiagnosticsOverlayFragmentEntryPoint = "diagnosticsOverlayFragmentMain";

        /// Returns native MSL for the diagnostics overlay glyph vertex stage.
        eastl::string getDiagnosticsOverlayVertexShader()
        {
            return R"(
#include <metal_stdlib>
using namespace metal;

struct DiagnosticsOverlayVertexInput
{
    float4 positionAndLocal [[attribute(0)]];
    float glyphCode [[attribute(1)]];
};

struct DiagnosticsOverlayVertexOutput
{
    float4 position [[position]];
    float2 local [[user(locn0)]];
    float glyphCode [[user(locn1)]];
};

vertex DiagnosticsOverlayVertexOutput diagnosticsOverlayVertexMain(DiagnosticsOverlayVertexInput input [[stage_in]])
{
    DiagnosticsOverlayVertexOutput output;
    output.position = float4(input.positionAndLocal.xy, 0.0, 1.0);
    output.local = input.positionAndLocal.zw;
    output.glyphCode = input.glyphCode;
    return output;
}
)";
        }

        /// Returns native MSL for the diagnostics overlay font-atlas fragment stage.
        eastl::string getDiagnosticsOverlayFragmentShader()
        {
            return R"(
#include <metal_stdlib>
using namespace metal;

struct DiagnosticsOverlayVertexOutput
{
    float4 position [[position]];
    float2 local [[user(locn0)]];
    float glyphCode [[user(locn1)]];
};

struct DiagnosticsOverlayFrameBuffer
{
    half4 color [[color(0)]];
};

struct DiagnosticsOverlayBindGroup
{
    texture2d<float> fontAtlas [[id(0)]];
    sampler fontSampler [[id(1)]];
};

fragment DiagnosticsOverlayFrameBuffer diagnosticsOverlayFragmentMain(
    DiagnosticsOverlayVertexOutput input [[stage_in]],
    const constant DiagnosticsOverlayBindGroup *bindGroup [[buffer(1)]])
{
    if (input.glyphCode < 0.0)
    {
        DiagnosticsOverlayFrameBuffer backgroundOutput;
        backgroundOutput.color = half4(0.02, 0.025, 0.03, 0.52);
        return backgroundOutput;
    }

    constexpr float atlasWidth = 128.0;
    constexpr float atlasHeight = 48.0;
    constexpr float cellSize = 8.0;
    float normalizedCode = clamp(floor(input.glyphCode + 0.5) - 32.0, 0.0, 94.0);
    float cellX = normalizedCode - floor(normalizedCode / 16.0) * 16.0;
    float cellY = floor(normalizedCode / 16.0);
    float2 texel = floor(clamp(input.local, float2(0.0), float2(0.999)) * cellSize);
    float2 uv = (float2(cellX, cellY) * cellSize + texel + 0.5) / float2(atlasWidth, atlasHeight);
    float alpha = bindGroup->fontAtlas.sample(bindGroup->fontSampler, uv).r;
    if (alpha < 0.1)
    {
        discard_fragment();
    }

    DiagnosticsOverlayFrameBuffer output;
    output.color = half4(0.92, 0.98, 1.0, alpha * 0.86);
    return output;
}
)";
        }

        /// Creates one Metal-native diagnostics overlay shader module for the requested stage.
        ShaderModule createMetalDiagnosticsOverlayShaderModule(Device device, Private::DiagnosticsOverlayShaderStage stage)
        {
            if (device == nullptr)
            {
                throw std::invalid_argument("createMetalDiagnosticsOverlayShaderModule requires a valid device.");
            }

            ShaderModuleDescriptor descriptor = {};
            if (stage == Private::DiagnosticsOverlayShaderStage::Vertex)
            {
                descriptor.label = "DiagnosticsOverlay.VertexShader";
                descriptor.code = getDiagnosticsOverlayVertexShader();
            }
            else
            {
                descriptor.label = "DiagnosticsOverlay.FragmentShader";
                descriptor.code = getDiagnosticsOverlayFragmentShader();
            }
            return device->createShaderModule(descriptor);
        }
    } // namespace

    GVM::RHI::Private::DiagnosticsOverlayShaderProgramProvider createMetalDiagnosticsOverlayShaderProvider()
    {
        return {
            .createShaderModule = createMetalDiagnosticsOverlayShaderModule,
            .vertexEntryPoint = DiagnosticsOverlayVertexEntryPoint,
            .fragmentEntryPoint = DiagnosticsOverlayFragmentEntryPoint,
        };
    }
}
