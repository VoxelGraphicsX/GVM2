#pragma once
#include "UGL.Pipeline.h"
#include "UGL.Resources.h"
#include "UGL.Sync.h"

namespace UGL
{
    using uint = unsigned int;
    enum class IndexFormat
    {
        Undefined = 0x00000000,
        Uint16 = 0x00000001,
        Uint32 = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    class IComputeClass
    {
    public:
        virtual ~IComputeClass()
        {
        }
        ComputePassTaskDescriptor setBindGroup(uint slot, IsBindGroupWrapper auto &&bd)
        {
            return {};
        }

        ComputePassTaskDescriptor dispatchIndirect(IsIndirectBuffer auto indirectBuffer)
        {
            return {};
        }
    };

    template <typename T>
        requires std::is_base_of_v<IComputeClass, T>
    class ComputeClass final
    {
        T *t = nullptr;
        T &operator*() = delete;

    public:
        T *operator->()
        {
            return t;
        }
        ComputePassTaskDescriptor operator()(uint32_t x, uint32_t y, uint32_t z)
        {
            return {};
        }

        ComputePassTaskDescriptor operator()(IsIndirectBuffer auto indirectBuffer)
        {
            return {};
        }
    };

    class IRenderClass
    {
    protected:
        void setBlendState(uint index, const BlendState &state)
        {
        }
        void setPrimitiveTopology(PrimitiveTopology topology)
        {
        }
        void setCullMode(CullMode cullMode)
        {
        }
        /// Enables or disables pipeline depth writes without changing depth testing or compare state.
        void setDepthWriteEnabled(bool enabled)
        {
        }
        /// Sets the pipeline depth compare function; depth write patterns only control shader depth-output semantics.
        void setDepthCompareFunction(CompareFunction function)
        {
        }
        /// Controls whether RenderSet draw entry points automatically bind the RenderSet vertex buffer.
        ///
        /// Keep this enabled for conventional RenderSet vertex input draws. Disable it for procedural
        /// vertex shaders that read only the RenderSet argument buffer.
        void setRenderSetVertexBufferBindingEnabled(bool enabled)
        {
        }

    public:
        RenderPassTaskDescriptor setVertexBuffer(IsVertexBuffer auto buffer, uint32_t slot = 0, uint64_t offset = 0, uint64_t size = WholeSize)
        {
            return {};
        }
        RenderPassTaskDescriptor setIndexBuffer(IsIndexBuffer auto buffer, IndexFormat format = IndexFormat::Uint32, uint64_t offset = 0, uint64_t size = WholeSize)
        {
            return {};
        }

        RenderPassTaskDescriptor setBindGroup(uint slot, IsBindGroupWrapper auto &&bd)
        {
            return {};
        }
        RenderPassTaskDescriptor drawIndirect(IsIndirectBuffer auto indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
        {
            return {};
        }
        virtual ~IRenderClass() {};
    };

    class IPixelLocalRenderClass
    {
    public:
        RenderPassTaskDescriptor setBindGroup(uint slot, IsBindGroupWrapper auto &&bd)
        {
            return {.phaseRequirement = RenderPassPhaseRequirement::PixelLocalOnly};
        }

    public:
        virtual ~IPixelLocalRenderClass() {};
    };

    template <typename T>
        requires(std::is_base_of_v<IRenderClass, T> || std::is_base_of_v<IPixelLocalRenderClass, T>)
    class RenderClass final
    {
        T *t = nullptr;
        T &operator*() = delete;

    public:
        T *operator->()
        {
            return t;
        }
        RenderPassTaskDescriptor operator()(uint vertexCount, uint instanceCount, uint firstVertex, uint firstInstance)
            requires std::is_base_of_v<IRenderClass, T>
        {
            return {};
        }
        RenderPassTaskDescriptor operator()()
            requires std::is_base_of_v<IPixelLocalRenderClass, T>
        {
            return {.phaseRequirement = RenderPassPhaseRequirement::PixelLocalOnly};
        }
        RenderPassTaskDescriptor operator()(uint indexCount, uint instanceCount, uint firstIndex, int baseVertex, uint firstInstance)
            requires std::is_base_of_v<IRenderClass, T>
        {
            return {};
        }
        RenderPassTaskDescriptor operator()(IsIndirectBuffer auto indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
            requires std::is_base_of_v<IRenderClass, T>
        {
            return {};
        }

        RenderPassTaskDescriptor operator()()
            requires std::is_base_of_v<IRenderClass, T>
        {
            return {};
        }

    };

} // namespace UGL
