#include "MUtilShaders.hpp"

#include <GVMRHI/GVMRHI.hpp>
eastl::string GVM::RHI::Metal::getIndirectIndexedRenderCommandConvertShader(const eastl::string &indexType)
{
    return "#define IndexType " + indexType + R"(

#include <metal_stdlib>
using namespace metal;
struct IndirectIndexedRenderCommand
{
    uint32_t    indexCount;
    uint32_t    instanceCount;
    uint32_t    firstIndex;
    int32_t     vertexOffset;
    uint32_t    firstInstance;
};

struct IndirectCommandInfo
{
    uint PrimitiveType;
    uint MaxCommandCount;
    uint IterationCount;
    uint MaxThreadsCount;
    uint CommandStride;
    device const uint *Count;
};

struct ICBContainer
{
    command_buffer commandBuffer [[ id(0) ]];
    uint2 executionRange [[ id(1) ]];
};

kernel void convertIndirectIndexedCommands(
                uint objectIndex   [[ thread_position_in_grid ]],
                constant IndirectCommandInfo     &indirectCommandInfo   [[ buffer(0) ]],
                device const uint *indirectBuffer [[ buffer(1) ]],
                device ICBContainer         *icb_container [[ buffer(2) ]],
                device IndexType *indexBuffer                   [[ buffer(3)]]
                )
{
    const uint MaxCommandCount = indirectCommandInfo.Count == nullptr ? indirectCommandInfo.MaxCommandCount :
        min(indirectCommandInfo.MaxCommandCount, *indirectCommandInfo.Count);
    if (objectIndex == 0 && indirectCommandInfo.Count != nullptr)
    {
        icb_container->executionRange = uint2(0, MaxCommandCount);
    }
    const uint IterationCount = indirectCommandInfo.IterationCount;
    const uint MaxThreadsCount = indirectCommandInfo.MaxThreadsCount;
    for (uint itIndex = 0; itIndex < IterationCount; ++itIndex)
    {
        const ulong icbIndex = ulong(itIndex) * MaxThreadsCount + objectIndex;
        if(icbIndex>=MaxCommandCount)
        {
            return;
        }
        const ulong word = ulong(icbIndex) * indirectCommandInfo.CommandStride / sizeof(uint);
        const IndirectIndexedRenderCommand rawCommand = {
            indirectBuffer[word], indirectBuffer[word + 1], indirectBuffer[word + 2],
            as_type<int>(indirectBuffer[word + 3]), indirectBuffer[word + 4]
        };




        if(rawCommand.instanceCount>0&&rawCommand.indexCount>0)
        {

            // Get indirect render commnd object from the indirect command buffer given the object's unique
            // index to set parameters for drawing (or not drawing) the object.
            render_command cmd(icb_container->commandBuffer, icbIndex);
            switch(indirectCommandInfo.PrimitiveType)
            {
                case 0:
                    cmd.draw_indexed_primitives(primitive_type::point,
                                        rawCommand.indexCount,
                                        indexBuffer+rawCommand.firstIndex,
                                        rawCommand.instanceCount,
                                        rawCommand.vertexOffset,
                                        rawCommand.firstInstance
                                        );
                    break;
                case 1:
                    cmd.draw_indexed_primitives(primitive_type::line,
                                        rawCommand.indexCount,
                                        indexBuffer+rawCommand.firstIndex,
                                        rawCommand.instanceCount,
                                        rawCommand.vertexOffset,
                                        rawCommand.firstInstance
                                        );
                    break;
                case 2:
                    cmd.draw_indexed_primitives(primitive_type::line_strip,
                                        rawCommand.indexCount,
                                        indexBuffer+rawCommand.firstIndex,
                                        rawCommand.instanceCount,
                                        rawCommand.vertexOffset,
                                        rawCommand.firstInstance
                                        );
                    break;
                case 3:
                    cmd.draw_indexed_primitives(primitive_type::triangle,
                                        rawCommand.indexCount,
                                        indexBuffer+rawCommand.firstIndex,
                                        rawCommand.instanceCount,
                                        rawCommand.vertexOffset,
                                        rawCommand.firstInstance
                                        );
                    break;
                case 4:
                    cmd.draw_indexed_primitives(primitive_type::triangle_strip,
                                        rawCommand.indexCount,
                                        indexBuffer+rawCommand.firstIndex,
                                        rawCommand.instanceCount,
                                        rawCommand.vertexOffset,
                                        rawCommand.firstInstance
                                        );
                    break;
                default:
                    break;
            }
        }
    }
}

)";
}

eastl::string GVM::RHI::Metal::getIndirectRenderCommandConvertShader()
{
    return R"(

    #include <metal_stdlib>
    using namespace metal;
    struct IndirectRenderCommand
    {
        uint32_t    vertexCount;
        uint32_t    instanceCount;
        uint32_t    firstVertex;
        uint32_t    firstInstance;
    };

    struct IndirectCommandInfo
    {
        uint PrimitiveType;
        uint MaxCommandCount;
        uint IterationCount;
        uint MaxThreadsCount;
        uint CommandStride;
        device const uint *Count;
    };

    struct ICBContainer
    {
        command_buffer commandBuffer [[ id(0) ]];
        uint2 executionRange [[ id(1) ]];
    };

    kernel void convertIndirectCommands(
                    uint objectIndex   [[ thread_position_in_grid ]],
                    constant IndirectCommandInfo     &indirectCommandInfo   [[ buffer(0) ]],
                    device const uint *indirectBuffer [[ buffer(1) ]],
                    device ICBContainer         *icb_container [[ buffer(2) ]]
                    )
    {
        const uint MaxCommandCount = indirectCommandInfo.Count == nullptr ? indirectCommandInfo.MaxCommandCount :
            min(indirectCommandInfo.MaxCommandCount, *indirectCommandInfo.Count);
        if (objectIndex == 0 && indirectCommandInfo.Count != nullptr)
        {
            icb_container->executionRange = uint2(0, MaxCommandCount);
        }
        const uint IterationCount = indirectCommandInfo.IterationCount;
        const uint MaxThreadsCount = indirectCommandInfo.MaxThreadsCount;
        for (uint itIndex = 0; itIndex < IterationCount; ++itIndex)
        {
            const ulong icbIndex = ulong(itIndex) * MaxThreadsCount + objectIndex;
            if(icbIndex>=MaxCommandCount)
            {
                return;
            }
            const ulong word = ulong(icbIndex) * indirectCommandInfo.CommandStride / sizeof(uint);
            const IndirectRenderCommand rawCommand = {
                indirectBuffer[word], indirectBuffer[word + 1], indirectBuffer[word + 2], indirectBuffer[word + 3]
            };



            if(rawCommand.instanceCount>0)
            {

                // Get indirect render commnd object from the indirect command buffer given the object's unique
                // index to set parameters for drawing (or not drawing) the object.
                render_command cmd(icb_container->commandBuffer, icbIndex);
                switch(indirectCommandInfo.PrimitiveType)
                {
                    case 0:
                        cmd.draw_primitives(primitive_type::point,
                                            rawCommand.firstVertex,
                                            rawCommand.vertexCount,
                                            rawCommand.instanceCount,
                                            rawCommand.firstInstance
                                            );
                        break;
                    case 1:
                        cmd.draw_primitives(primitive_type::line,
                                            rawCommand.firstVertex,
                                            rawCommand.vertexCount,
                                            rawCommand.instanceCount,
                                            rawCommand.firstInstance
                                            );
                        break;
                    case 2:
                        cmd.draw_primitives(primitive_type::line_strip,
                                            rawCommand.firstVertex,
                                            rawCommand.vertexCount,
                                            rawCommand.instanceCount,
                                            rawCommand.firstInstance
                                            );
                        break;
                    case 3:
                        cmd.draw_primitives(primitive_type::triangle,
                                            rawCommand.firstVertex,
                                            rawCommand.vertexCount,
                                            rawCommand.instanceCount,
                                            rawCommand.firstInstance
                                            );
                        break;
                    case 4:
                        cmd.draw_primitives(primitive_type::triangle_strip,
                                            rawCommand.firstVertex,
                                            rawCommand.vertexCount,
                                            rawCommand.instanceCount,
                                            rawCommand.firstInstance
                                            );
                        break;
                    default:
                        break;
                }
            }
        }
    }
)";
}

eastl::string GVM::RHI::Metal::getCopyBufferToBufferMultipleRegion()
{
    return R"(
#include <metal_stdlib>
using namespace metal;

struct BufferCopyMultipleRegionBindGroup
{
    const device uint  *sourceWords [[id(0)]];
    device uint  *destinationWords [[id(1)]];
    const device uint  *regionValues [[id(2)]];
};

kernel void executeCopyBufferCommands(
                uint3 groupID   [[ threadgroup_position_in_grid ]],
                uint3 localID   [[ thread_position_in_threadgroup ]],
                const constant BufferCopyMultipleRegionBindGroup *bindGroup [[buffer(0)]]
                )
{
    const uint base = groupID.x * 3u;
    const uint srcWord = bindGroup->regionValues[base] / 4u;
    const uint dstWord = bindGroup->regionValues[base + 1u] / 4u;
    const uint wordCount = bindGroup->regionValues[base + 2u] / 4u;
    for (uint i = localID.x; i < wordCount; i += 64u)
    {
        bindGroup->destinationWords[dstWord + i] = bindGroup->sourceWords[srcWord + i];
    }
}
    )";
}

eastl::string GVM::RHI::Metal::getFullscreenTextureVertexShader()
{
    return R"(
#include <metal_stdlib>
using namespace metal;

struct FullscreenTextureVertexOutput
{
    float4 pos [[position]];
    float2 texCoord [[user(locn0)]];
};

vertex FullscreenTextureVertexOutput vertexMain(uint vid [[vertex_id]])
{
    float2 uv = float2((vid << 1) & 2, vid & 2);
    FullscreenTextureVertexOutput output;
    output.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.pos.y = -output.pos.y;
    output.texCoord = uv;
    return output;
}
)";
}

eastl::string GVM::RHI::Metal::getFullscreenTextureFragmentShader()
{
    return R"(
#include <metal_stdlib>
using namespace metal;

struct FullscreenTextureVertexOutput
{
    float4 pos [[position]];
    float2 texCoord [[user(locn0)]];
};

struct FullscreenTextureFrameBuffer
{
    half4 color [[color(0)]];
};

struct FullscreenTextureBindGroup
{
    texture2d<float> sourceTexture [[id(0)]];
    sampler sourceSampler [[id(1)]];
};

fragment FullscreenTextureFrameBuffer fragmentMain(
    FullscreenTextureVertexOutput input [[stage_in]],
    const constant FullscreenTextureBindGroup *bindGroup [[buffer(0)]])
{
    FullscreenTextureFrameBuffer output;
    output.color = half4(bindGroup->sourceTexture.sample(bindGroup->sourceSampler, input.texCoord));
    return output;
}

fragment FullscreenTextureFrameBuffer fragmentMainFlipped(
    FullscreenTextureVertexOutput input [[stage_in]],
    const constant FullscreenTextureBindGroup *bindGroup [[buffer(0)]])
{
    FullscreenTextureFrameBuffer output;
    output.color = half4(bindGroup->sourceTexture.sample(bindGroup->sourceSampler, float2(input.texCoord.x, 1.0 - input.texCoord.y)));
    return output;
}
)";
}

