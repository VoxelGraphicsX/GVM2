#pragma once
#include <GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"
#include "MBufferContracts.hpp"
#include <EASTL/unordered_map.h>
namespace GVM::RHI::Metal
{

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
		[[nodiscard]]
		BufferUsageFlags getUsage() const;
		[[nodiscard]]
		bool isMapped() const;
		MTL::IndirectCommandBuffer *getOrCreateNativeICB(MTL::IndirectCommandType type);
		MTL::Buffer *getOrCreateNativeICBArgumentBuffer(MTL::IndirectCommandType type);
		virtual void destroy() override;
		static constexpr int MaxIndirectBufferBindingCount = 16;

	private:
		MDevice *mDevice = nullptr;
		// NS::SharedPtr<MTL::Buffer> mNativeBuffer;
		eastl::unordered_map<MTL::IndirectCommandType, MTL::IndirectCommandBuffer *> mICBs;
		eastl::unordered_map<MTL::IndirectCommandType, MTL::Buffer *> mICBArgumentBuffers;
		MTL::Buffer *mNativeBuffer = nullptr;
		BufferDescriptor mDescriptor;
		bool mIsMapped = false;

		MTL::IndirectCommandBuffer *getOrCreateICB(MTL::IndirectCommandType type);
		MTL::Buffer *getOrCreateICBArgumentBuffer(MTL::IndirectCommandType type);
	};

} // namespace GVM::RHI::Metal
