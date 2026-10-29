#pragma once
#include "UGL.Attributes.h"
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
        /** Declares the Args prefix followed by thread counts; UGLC validates the concrete constructor. */
        template <typename... Arguments>
        UGL_ARGUMENTS_CALL
        ComputePassTaskDescriptor dispatchThreads(Arguments &&...arguments);

        /** Declares the Args prefix followed by a native indirect dispatch buffer holding workgroup counts. */
        template <typename... Arguments>
        UGL_ARGUMENTS_CALL
        ComputePassTaskDescriptor dispatchWorkgroupsIndirect(Arguments &&...arguments);

        ComputePassTaskDescriptor setBindGroup(uint slot, IsBindGroupWrapper auto &&bd)
        {
            return {};
        }

        ComputePassTaskDescriptor dispatchWorkgroupsIndirect(IsIndirectBuffer auto indirectBuffer)
        {
            return {};
        }
    };

    template <typename T>
        requires (std::is_base_of_v<IComputeClass, T> && !std::is_same_v<IComputeClass, std::remove_cv_t<T>>)
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
        /** Declares the Args prefix followed by vertex count, instance count, and their first indices. */
        template <typename... Arguments>
        UGL_ARGUMENTS_CALL
        RenderPassTaskDescriptor draw(Arguments &&...arguments);

        /** Declares the Args prefix followed by the index buffer, format, counts, and first indices. */
        template <typename... Arguments>
        UGL_ARGUMENTS_CALL
        RenderPassTaskDescriptor drawIndexed(Arguments &&...arguments);

        /** Declares the Args prefix followed by an indirect buffer, command count, and stride. */
        template <typename... Arguments>
        UGL_ARGUMENTS_CALL
        RenderPassTaskDescriptor drawIndirect(Arguments &&...arguments);

        /** Declares the Args prefix followed by indexed-indirect operation resources and controls. */
        template <typename... Arguments>
        UGL_ARGUMENTS_CALL
        RenderPassTaskDescriptor drawIndexedIndirect(Arguments &&...arguments);

        /** Declares the complete Args prefix followed by command and count buffers, capacity, and optional stride. */
        template <typename... Arguments>
        UGL_ARGUMENTS_CALL
        RenderPassTaskDescriptor drawIndirectCount(Arguments &&...arguments);

        /** Declares captured index and indirect-count inputs after the complete constructor Args prefix. */
        template <typename... Arguments>
        UGL_ARGUMENTS_CALL
        RenderPassTaskDescriptor drawIndexedIndirectCount(Arguments &&...arguments);

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
        /** Declares a PixelLocal draw with the complete constructor Args prefix and no count controls. */
        template <typename... Arguments>
        UGL_ARGUMENTS_CALL
        RenderPassTaskDescriptor draw(Arguments &&...arguments);

        RenderPassTaskDescriptor setBindGroup(uint slot, IsBindGroupWrapper auto &&bd)
        {
            return {.phaseRequirement = RenderPassPhaseRequirement::PixelLocalOnly};
        }

    public:
        virtual ~IPixelLocalRenderClass() {};
    };

    template <typename T>
        requires((std::is_base_of_v<IRenderClass, T> || std::is_base_of_v<IPixelLocalRenderClass, T>) &&
                 !std::is_same_v<IRenderClass, std::remove_cv_t<T>> && !std::is_same_v<IPixelLocalRenderClass, std::remove_cv_t<T>>)
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
