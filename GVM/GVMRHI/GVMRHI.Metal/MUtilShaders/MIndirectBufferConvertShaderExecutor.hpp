#pragma once
#include "../MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    struct MIndirectBufferConvertShaderExecutorDescriptor
    {
        // eastl::string shaderCode;
    };
    namespace MIndirectRenderCommandDrawPrimitiveType
    {
        static constexpr uint32_t point = 0;
        static constexpr uint32_t line = 1;
        static constexpr uint32_t lineStrip = 2;
        static constexpr uint32_t triangle = 3;
        static constexpr uint32_t triangleStrip = 4;
    }
    /** Captures one conversion's source state and its exclusive native destination. */
    struct MIndirectIndexedRenderCommandConvertOptions
    {
        uint32_t indirectCommandCount = 0;
        uint32_t indirectCommandStride = 0;
        uint32_t indirectRenderCommandDrawPrimitiveType = MIndirectRenderCommandDrawPrimitiveType::triangle;
        MTL::IndirectCommandType commandType;
        MTL::IndexType indexType;
        BufferRange indexBuffer;
        MTL::Buffer *nativeIndexBuffer = nullptr;
        BufferRange indirectBuffer;
        MTL::Buffer *nativeIndirectBuffer = nullptr;
        MTL::Buffer *nativeCountBuffer = nullptr;
        uint64_t countAddress = 0;
        // Native API handles borrowed from the source buffer's conversion storage pool.
        MTL::IndirectCommandBuffer *nativeICB = nullptr;
        MTL::Buffer *nativeICBArgumentBuffer = nullptr;
    };

    class MIndirectBufferConvertShaderExecutorImpl final : public GVM::RHI::RefCountedObject
    {
    public:
        MIndirectBufferConvertShaderExecutorImpl();
        ~MIndirectBufferConvertShaderExecutorImpl();
        void init(MDevice *device, const MIndirectBufferConvertShaderExecutorDescriptor &descriptor);
        void execute(CommandEncoder encoder, const eastl::string &renderpassLabel, const eastl::vector<MIndirectIndexedRenderCommandConvertOptions> &options);
        void resetICB(BlitPassEncoder encoder, const MIndirectIndexedRenderCommandConvertOptions &options);
        void convertICB(ComputePassEncoder encoder, const MIndirectIndexedRenderCommandConvertOptions &options);
        void optimizeICB(BlitPassEncoder encoder, const MIndirectIndexedRenderCommandConvertOptions &options);
        static constexpr uint32_t MaxThreads = 8192u;

    private:
        /** Creates one fixed indexed conversion pipeline for the selected index element type. */
        ComputePipeline createIndexedPipeline(MTL::IndexType indexType);

        /** Creates the fixed non-indexed conversion pipeline shared by every record stride. */
        ComputePipeline createDrawPipeline();

        ComputePipeline mDrawIndexed32Pipeline;
        ComputePipeline mDrawIndexed16Pipeline;
        ComputePipeline mDrawPipeline;
        MDevice *mDevice = nullptr;
    };

    using MIndirectBufferConvertShaderExecutor = eastl::intrusive_ptr<MIndirectBufferConvertShaderExecutorImpl>;

} // namespace GVM::RHI::Metal
