#include "MUtilShaders.hpp"

#include <GVMRHI/GVMRHI.hpp>
eastl::string GVM::RHI::Metal::getIndirectIndexedRenderCommandConvertShader(const eastl::string &indexType, uint32_t stride)
{
    uint32_t extraUIntCount = 0;
    if (stride > sizeof(GVM::RHI::IndirectIndexedRenderCommand))
    {
        extraUIntCount = stride - sizeof(GVM::RHI::IndirectIndexedRenderCommand);
        extraUIntCount /= sizeof(uint32_t);
    }
    eastl::string extraUIntStmt = "\n#define ExtraMember ";
    if (extraUIntCount > 0)
    {
        extraUIntStmt += "uint32_t extraUint[" + eastl::to_string(extraUIntCount) + "];\n";
    }

    return "#define IndexType " + indexType + extraUIntStmt + R"(

#include <metal_stdlib>
using namespace metal;
struct IndirectIndexedRenderCommand
{
    uint32_t    indexCount;
    uint32_t    instanceCount;
    uint32_t    firstIndex;
    int32_t     vertexOffset;
    uint32_t    firstInstance;
    ExtraMember
};

struct IndirectCommandInfo
{
    uint PrimitiveType;
    uint MaxCommandCount;
    uint IterationCount;
    uint MaxThreadsCount;
};

struct ICBContainer
{
    command_buffer commandBuffer [[ id(0) ]];
};

kernel void convertIndirectIndexedCommands(
                uint objectIndex   [[ thread_position_in_grid ]],
                constant IndirectCommandInfo     &indirectCommandInfo   [[ buffer(0) ]],
                device IndirectIndexedRenderCommand           *indirectBuffer      [[ buffer(1) ]],
                device ICBContainer         *icb_container [[ buffer(2) ]],
                device IndexType *indexBuffer                   [[ buffer(3)]]
                )
{
    const uint MaxCommandCount = indirectCommandInfo.MaxCommandCount;
    const uint IterationCount = indirectCommandInfo.IterationCount;
    const uint MaxThreadsCount = indirectCommandInfo.MaxThreadsCount;
    for(int itIndex=0;itIndex<IterationCount;itIndex++)
    {
        const uint icbIndex = itIndex*MaxThreadsCount+objectIndex;
        if(icbIndex>=MaxCommandCount)
        {
            return;
        }
        IndirectIndexedRenderCommand rawCommand = indirectBuffer[icbIndex];




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

eastl::string GVM::RHI::Metal::getIndirectRenderCommandConvertShader(uint32_t stride)
{
    uint32_t extraUIntCount = 0;
    if (stride > sizeof(GVM::RHI::IndirectRenderCommand))
    {
        extraUIntCount = stride - sizeof(GVM::RHI::IndirectRenderCommand);
        extraUIntCount /= sizeof(uint32_t);
    }
    eastl::string extraUIntStmt = "\n#define ExtraMember ";
    if (extraUIntCount > 0)
    {
        extraUIntStmt += "uint32_t extraUint[" + eastl::to_string(extraUIntCount) + "];\n";
    }
    return extraUIntStmt + R"(

    #include <metal_stdlib>
    using namespace metal;
    struct IndirectRenderCommand
    {
        uint32_t    vertexCount;
        uint32_t    instanceCount;
        uint32_t    firstVertex;
        uint32_t    firstInstance;
        ExtraMember
    };

    struct IndirectCommandInfo
    {
        uint PrimitiveType;
        uint MaxCommandCount;
        uint IterationCount;
        uint MaxThreadsCount;
    };

    struct ICBContainer
    {
        command_buffer commandBuffer [[ id(0) ]];
    };

    kernel void convertIndirectCommands(
                    uint objectIndex   [[ thread_position_in_grid ]],
                    constant IndirectCommandInfo     &indirectCommandInfo   [[ buffer(0) ]],
                    device IndirectRenderCommand           *indirectBuffer      [[ buffer(1) ]],
                    device ICBContainer         *icb_container [[ buffer(2) ]]
                    )
    {
        const uint MaxCommandCount = indirectCommandInfo.MaxCommandCount;
        const uint IterationCount = indirectCommandInfo.IterationCount;
        const uint MaxThreadsCount = indirectCommandInfo.MaxThreadsCount;
        for(int itIndex=0;itIndex<IterationCount;itIndex++)
        {
            const uint icbIndex = itIndex*MaxThreadsCount+objectIndex;
            if(icbIndex>=MaxCommandCount)
            {
                return;
            }
            IndirectRenderCommand rawCommand = indirectBuffer[icbIndex];



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

eastl::string GVM::RHI::Metal::getQuadVertexShader()
{
    return R"(
#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;
struct QuadVertexOutput
{
    float4 pos[[position]];
    float2 texCoord[[attribute(0)]];
};
vertex QuadVertexOutput vertexMain(uint vid [[vertex_id]])
{
    float2 output_uv = float2((vid << 1) & 2, vid & 2);
    QuadVertexOutput voutput;
    voutput.pos = float4(output_uv * 2.0 - 1.0, .0, 1.0);
    voutput.texCoord = output_uv;
    return voutput;
}
    )";
}

eastl::string GVM::RHI::Metal::getQuadFragmentShader()
{
    return R"(
#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

struct QuadVertexOutput
{
    float4 pos[[position]];
    float2 texCoord[[attribute(0)]];
};
struct QuadFrameBuffer
{
    float4 color;
};
fragment QuadFrameBuffer fragmentMain(QuadVertexOutput vertexIn [[stage_in]],texture2d<float> texture0 [[buffer(0)]], sampler sampler0[[buffer(1)]])
{
    QuadFrameBuffer foutput;
    float2 uv = vertexIn.texCoord;
    float4 res = texture0.sample(sampler0, uv);
    foutput.color = res;
    return foutput;
}
    )";
}

eastl::string GVM::RHI::Metal::getQuadComputeShader()
{
    return R"(
#include <metal_stdlib>
using namespace metal;



kernel void executeQuad(
                uint2 threadID   [[ thread_position_in_grid ]],
                texture2d<float,access::read> texture0 [[texture(0)]],
                texture2d<float,access::read_write> texture1 [[texture(1)]]
                )
{


    texture1.write(texture0.read(threadID), threadID);

}
    )";
}

eastl::string GVM::RHI::Metal::getCopyBufferToBufferMultipleRegion()
{
    return R"(
#include <metal_stdlib>
using namespace metal;

struct BufferCopyRegion
{
    uint32_t srcOffsetBytes;
    uint32_t dstOffsetBytes;
    uint32_t storageBytes;
};

kernel void executeCopyBufferCommands(
                uint objectIndex   [[ thread_position_in_grid ]],
                const device uint  *srcBuffer   [[ buffer(0) ]],
                device uint  *dstBuffer   [[ buffer(1) ]],
                device BufferCopyRegion   *copyRegions [[ buffer(2) ]],
                constant uint32_t &bufferCount [[ buffer(3)]]
                )
{

    if(objectIndex>=bufferCount)
    {
        return;
    }

    const BufferCopyRegion region = copyRegions[objectIndex];
    const uint32_t srcOffsetWords = region.srcOffsetBytes / 4u;
    const uint32_t dstOffsetWords = region.dstOffsetBytes / 4u;
    const uint32_t storageWords = region.storageBytes / 4u;
    for(uint32_t i = 0; i < storageWords; ++i)
    {
        dstBuffer[dstOffsetWords + i] = srcBuffer[srcOffsetWords + i];
    }

}
    )";
}
