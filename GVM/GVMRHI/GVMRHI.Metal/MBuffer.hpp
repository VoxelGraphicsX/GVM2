#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"
#include "MBufferContracts.hpp"
#include <EASTL/functional.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/unordered_map.h>
namespace GVM::RHI::Metal
{
    /** Resolves a live buffer interval and checks device, usage, alignment, and bounds without retaining it. */
    MBuffer &resolveBufferRange(MDevice &device, BufferRange &range, BufferUsageFlags usage, uint32_t alignment);

    /** Identifies reusable storage for one conversion slot on one native queue. */
    struct NativeIndirectCommandBufferKey
    {
        const MTL::CommandQueue *commandQueue;
        MTL::IndirectCommandType commandType;
        uint32_t conversionSlot;

        /** Compares storage identity independently of the source command contents. */
        bool operator==(const NativeIndirectCommandBufferKey &other) const
        {
            return commandQueue == other.commandQueue &&
                commandType == other.commandType &&
                conversionSlot == other.conversionSlot;
        }
    };

    /** Hashes native queue identity and the per-buffer conversion slot. */
    struct NativeIndirectCommandBufferKeyHash
    {
        /** Returns a hash consistent with the allocation key's equality relation. */
        size_t operator()(const NativeIndirectCommandBufferKey &key) const
        {
            const uint64_t commandSlot =
                (static_cast<uint64_t>(key.commandType) << 32u) | key.conversionSlot;
            return eastl::hash<const MTL::CommandQueue *>{}(key.commandQueue) ^
                eastl::hash<uint64_t>{}(commandSlot);
        }
    };

    /** Owns an ICB and the matching GPU argument buffer for its entire cached lifetime. */
    struct NativeIndirectCommandBuffer
    {
        /// Byte offset of the GPU-written uint32 location/length pair after the ICB resource ID.
        static constexpr uint64_t ExecutionRangeOffset = sizeof(uint64_t);
        NS::SharedPtr<MTL::IndirectCommandBuffer> commands;
        NS::SharedPtr<MTL::Buffer> argumentBuffer;
        uint32_t commandCapacity = 0;
    };

    /** Shares only native conversion allocations so captured indirect tasks outlive the public source handle. */
    using NativeIndirectCommandBufferCache = eastl::unordered_map<NativeIndirectCommandBufferKey,
        NativeIndirectCommandBuffer, NativeIndirectCommandBufferKeyHash>;

    /** Reuses queue-ordered conversion storage, growing only to the requested draw count; recorded commands retain older allocations. */
    const NativeIndirectCommandBuffer &getOrCreateNativeICB(MDevice &device,
        NativeIndirectCommandBufferCache &cache, uint32_t commandCount,
        MTL::IndirectCommandType type, const MTL::CommandQueue &queue, uint32_t slot);

	class MBuffer final : public BufferImpl
	{
	public:
		MBuffer();
		~MBuffer();
		void init(MDevice *device, const BufferDescriptor &descriptor);
		virtual uint64_t getStorageSize() const override;
		virtual void map() override;
		virtual void const *getConstMappedRange(uint64_t offset, uint64_t size) const override;
		virtual void *getMappedRange(uint64_t offset, uint64_t size) const override;
		virtual void unmap() override;
		MTL::Buffer *getNativeBuffer() const;
        /** Identifies the device that owns this buffer allocation for Args validation. */
        MDevice &getDevice() const { return *mDevice; }
		[[nodiscard]]
		BufferUsageFlags getUsage() const;
		[[nodiscard]]
		bool isMapped() const;
        /**
         * Returns reusable storage for one draw conversion, never cached converted contents.
         * Slots are unique per buffer and command type within a render pass. Reuse on
         * the same queue is ordered by GPU reset, conversion and draw commands; distinct
         * queues receive distinct storage so simultaneous submissions cannot alias.
         */
        const NativeIndirectCommandBuffer &getOrCreateNativeICB(
            MTL::IndirectCommandType type,
            const MTL::CommandQueue &commandQueue,
            uint32_t conversionSlot, uint32_t commandCount);
        /** Shares the source's native conversion cache with tasks that capture an indirect buffer. */
        eastl::shared_ptr<NativeIndirectCommandBufferCache> getIndirectCommandCache();
		virtual void destroy() override;
		static constexpr int MaxIndirectBufferBindingCount = 16;

	private:
		MDevice *mDevice = nullptr;
		// NS::SharedPtr<MTL::Buffer> mNativeBuffer;
        eastl::shared_ptr<NativeIndirectCommandBufferCache> mICBs;
		MTL::Buffer *mNativeBuffer = nullptr;
		BufferDescriptor mDescriptor;
		bool mIsMapped = false;

	};

} // namespace GVM::RHI::Metal
