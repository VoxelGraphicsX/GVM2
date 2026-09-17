#pragma once
#include "GRenderSetInternalCommand.hpp"
#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <xGEFoundation/xMath.hpp>
namespace GVM::Core
{

    // ======================================================================================
    // [优化核心 2] 批量拷贝命令集
    // 作用：直接存储 RHI 能够识别的结构，不再存储业务逻辑结构。
    // ======================================================================================
    struct RenderComponentBatchedCopyCommand
    {
        // Keep individual regions bounded so one oversized copy does not dominate a batch.
        static constexpr uint32_t MaxCopyBytesPerRegion = 4u * 1024u;

        // 对应的 GPU 缓冲组件句柄
        RenderComponentHandle targetComponent;
        // RHI 拷贝指令列表 (SrcOffset 是相对于 mTotalBuffer 的偏移)
        eastl::vector<GVM::RHI::BufferCopyRegion> regions;
        // 上传到 GPU 的指令 Buffer
        GVM::RHI::Buffer gpuCommandBuffer;
        GVM::RHI::Device device;

        void create(GVM::RHI::Device device);

        void add(uint32_t tSrcOffset, uint32_t tDstOffset, uint32_t tSize);
        bool isEmpty() const
        {
            return regions.empty();
        }

        void destroy();
    };

} // namespace GVM::Core
