#pragma once
#include "../MDefines.hpp"
#include <EASTL/unordered_map.h>
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
// #include <EASTL/unordered_map.h>
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
    struct MIndirectIndexedRenderCommandConvertOptions
    {
        uint32_t indirectCommandCount = 0;
        uint32_t indirectCommandStride = 0;
        uint32_t indirectRenderCommandDrawPrimitiveType = MIndirectRenderCommandDrawPrimitiveType::triangle;
        MTL::IndirectCommandType commandType;
        MTL::IndexType indexType;
        BufferRange indexBuffer;
        BufferRange indirectBuffer;
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
        /** Returns a cached indexed-command conversion pipeline for the requested source record stride. */
        ComputePipeline getOrCreateIndexedPipeline(MTL::IndexType indexType, uint32_t commandStride);

        /** Returns a cached non-indexed-command conversion pipeline for the requested source record stride. */
        ComputePipeline getOrCreateDrawPipeline(uint32_t commandStride);

        eastl::unordered_map<uint32_t, ComputePipeline> mDrawIndirectIndexedPipelinesIndex32;
        eastl::unordered_map<uint32_t, ComputePipeline> mDrawIndirectIndexedPipelinesIndex16;
        eastl::unordered_map<uint32_t, ComputePipeline> mDrawIndirectPipelines;
        MDevice *mDevice = nullptr;
    };

    using MIndirectBufferConvertShaderExecutor = eastl::intrusive_ptr<MIndirectBufferConvertShaderExecutorImpl>;

} // namespace GVM::RHI::Metal
