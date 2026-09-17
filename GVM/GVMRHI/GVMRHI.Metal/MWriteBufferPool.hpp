#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"
#include "MWriteBufferPoolContracts.hpp"
#include <EASTL/unordered_map.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/set.h>
namespace GVM::RHI::Metal
{
	class MWriteBufferBlock
	{
	private:
		MDevice *mDevice = nullptr;
		Buffer mBuffer;
		uint64_t mStorageSize = 0;
		uint64_t mOffsetInUse = 0;
		uint64_t mLastUsedSubmitSerial = 0;

	public:
		void init(MDevice *device, const eastl::string &queueName, uint64_t blockSerial, uint64_t size);
		bool hasSpaceFor(uint64_t size) const;
		void markBytesUsed(uint64_t size, uint64_t submitSerial);
		Buffer getBuffer() const;
		BufferRange getBufferRange(uint64_t offset, uint64_t size) const;
		uint64_t getOffsetInUse() const;
		void resetUsage();
		bool isExpired(uint64_t completedSubmitSerial, uint64_t retentionSubmitCount) const;
		void destroy(MDevice *device);
	};
	using MWriteBufferBlockPTR = eastl::shared_ptr<MWriteBufferBlock>;
	struct MWriteBufferBlockPool
	{
		eastl::set<MWriteBufferBlockPTR> blocks;
	};
	using MWriteBufferBlockPoolPTR = eastl::shared_ptr<MWriteBufferBlockPool>;
	class MWriteBufferPool final
	{
		public:
			MWriteBufferPool();
			void init(MDevice *device, const eastl::string &queueName);
			void writeBufferToCommand(BlitPassEncoder encoder, BufferRange buffer, void const *data, uint64_t size, uint64_t submitSerial);
			void writeTextureToCommand(BlitPassEncoder encoder, const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize, uint64_t submitSerial);
			void setRetentionSubmitCount(uint64_t retentionSubmitCount);
			void reset(uint64_t completedSubmitSerial);
			void destroy();

		private:
			MDevice *mDevice = nullptr;
			eastl::string mQueueName;
			eastl::unordered_map<uint64_t, MWriteBufferBlockPoolPTR> mBufferBlockPool;
			MWriteBufferBlockPoolPTR findOrCreateBufferBlockPool(uint64_t blockStorageSize);
			MWriteBufferBlockPTR findOrCreateBufferBlock(MWriteBufferBlockPoolPTR blockPoolPTR, uint64_t blockStorageSize, uint64_t requiredBytes);
			uint64_t mNextBlockSerial = 0;
			uint64_t mRetentionSubmitCount = 0;
		};

} // namespace GVM::RHI::Metal
