#include "GRenderSet.hpp"
#include "GGPUVectorFactory.hpp"
#include <GVMRHI/GVMCpuProbe.hpp>
#include <GVMRHI/GVMLogging.hpp>
#include <EASTL/make_intrusive.h>
#include <EASTL/numeric_limits.h>
#include <cstdint>
#include <stdexcept>
namespace GVM::Core
{
    namespace
    {
        constexpr eastl::string_view RenderSetLogCategory = "gvmcore.render_set";

        template <typename T>
        void atomic_update_max(std::atomic<T> &maxValue, T newValue)
        {
            T previousValue = maxValue.load(std::memory_order_relaxed);
            while (previousValue < newValue && !maxValue.compare_exchange_weak(previousValue, newValue, std::memory_order_relaxed))
            {
            }
        }

        bool isRenderEntityAlive(const RenderEntityInfo &info)
        {
            return info.instanceCount != RenderComponentNullIndex
                && info.entityVersion != RenderComponentNullIndex
                && info.cmdParamsOffset != RenderComponentNullIndex;
        }

        /** Fills the newly allocated tail of a GPUVector backing buffer with a deterministic dead pattern. */
        void fillNewGPUVectorBytes(GVM::RHI::Device device, GPUVectorBase<uint8_t> &buffer, uint64_t previousBytes, uint32_t pattern)
        {
            if (device == nullptr || buffer.gpuBuffer().isNull())
            {
                return;
            }

            const uint64_t currentBytes = buffer.gpuCapacityBytes();
            if (currentBytes <= previousBytes)
            {
                return;
            }

            device->getMainQueue()->fillBuffer(
                GVM::RHI::BufferRange(buffer.gpuBuffer(), previousBytes, currentBytes - previousBytes),
                pattern);
        }

        RenderEntityInfo makeGpuDeadRenderEntityInfo()
        {
            RenderEntityInfo info{};
            info.indexCount = 0;
            info.instanceCount = 0;
            info.firstIndex = 0;
            info.vertexOffset = 0;
            info.globalInstanceBase = 0;
            info.vertexCount = 0;
            info.cmdParamsOffset = 0;
            return info;
        }

        uint32_t getPreviousEntityVersion(const ProgressiveData<RenderEntityInfo> &entityInfos, RenderEntityIndex entity)
        {
            if (!entityInfos.contains(entity))
            {
                return 0u;
            }
            const auto info = entityInfos.read(entity);
            return info.entityVersion == RenderComponentNullIndex ? 0u : info.entityVersion;
        }

        uint32_t inferElementCountFromStorageBytes(uint64_t storageBytes, uint64_t elementStorageBytes, const char *componentRole)
        {
            if (elementStorageBytes == 0)
            {
                throw std::logic_error("RenderSet cannot infer element count from a component with zero element storage size.");
            }
            if ((storageBytes % elementStorageBytes) != 0)
            {
                if (componentRole != nullptr && std::strcmp(componentRole, "index") == 0)
                {
                    throw std::invalid_argument("RenderSet cannot infer index count from a byte size that is not a whole multiple of the component element size.");
                }
                throw std::invalid_argument("RenderSet cannot infer vertex count from a byte size that is not a whole multiple of the component element size.");
            }
            return static_cast<uint32_t>(storageBytes / elementStorageBytes);
        }

        uint32_t narrowCountToUint32(uint64_t value, const char *what)
        {
            if (value > static_cast<uint64_t>(eastl::numeric_limits<uint32_t>::max()))
            {
                eastl::string message = "RenderSet access-bound data overflow while encoding ";
                message += what;
                message += ".";
                throw std::overflow_error(message.c_str());
            }
            return static_cast<uint32_t>(value);
        }

        size_t getComponentNameIndex(RenderComponentHandle handle, size_t componentCount)
        {
            if (handle == 0)
            {
                throw std::out_of_range("RenderSet component handle 0 is invalid.");
            }
            const size_t index = static_cast<size_t>(handle - 1);
            if (index >= componentCount)
            {
                throw std::out_of_range("RenderSet component handle is out of range.");
            }
            return index;
        }

        uint64_t getDefaultProgressiveDataIncrementCount()
        {
            return ProgressiveDataCreateInfo{}.dataElementIncreamentCount;
        }

        uint64_t getDefaultRenderSetGPUStorageIncrementCount()
        {
            return 10000u;
        }

        uint64_t getDefaultBufferComponentIncrementCount()
        {
            return BufferComponentCreateInfo{}.dataElementIncreamentCount;
        }

        uint64_t getDefaultTextureComponentIncrementCount()
        {
            return TextureComponentCreateInfo{}.dataElementIncreamentCount;
        }

    }

    void RenderSet::updateBindGroup()
    {
        const bool createsLayout = mBindGroupLayout == nullptr;
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetLogCategory,
            "RenderSet::updateBindGroup",
            "component_count={} creates_layout={}",
            mComponentNameList.size(),
            createsLayout);
        eastl::vector<GVM::RHI::BindGroupLayoutEntry> bindgroupLayoutEntries;
        uint32_t bindGroupLayoutBindingIndex = 0;
        {
            GVM::RHI::BindGroupLayoutEntry entry = {};
            entry.binding = bindGroupLayoutBindingIndex;
            entry.visibility = GVM::RHI::ShaderStage::Compute | GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
            entry.buffer.type = GVM::RHI::BufferBindingType::Storage;
            entry.buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
            bindgroupLayoutEntries.push_back(entry);
            bindGroupLayoutBindingIndex++;
        }
        for (const auto &componentName : mComponentNameList)
        {
            const auto &component = mComponentMap.at(componentName);
            component->appendBindGroupLayoutEntries(bindgroupLayoutEntries, bindGroupLayoutBindingIndex);
        }
        // RenderEntityInfo
        {
            GVM::RHI::BindGroupLayoutEntry entry = {};
            entry.binding = bindGroupLayoutBindingIndex;
            entry.visibility = GVM::RHI::ShaderStage::Compute | GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
            entry.buffer.type = GVM::RHI::BufferBindingType::Storage;
            entry.buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
            bindgroupLayoutEntries.push_back(entry);
            bindGroupLayoutBindingIndex++;
        }
        // RenderEntityCMDParams
        {
            GVM::RHI::BindGroupLayoutEntry entry = {};
            entry.binding = bindGroupLayoutBindingIndex;
            entry.visibility = GVM::RHI::ShaderStage::Compute | GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
            entry.buffer.type = GVM::RHI::BufferBindingType::Storage;
            entry.buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
            bindgroupLayoutEntries.push_back(entry);
            bindGroupLayoutBindingIndex++;
        }
        GVM::RHI::BindGroupLayoutDescriptor bindGroupLayoutDescriptor;
        bindGroupLayoutDescriptor.label = "RenderSetBindGroupLayout";
        bindGroupLayoutDescriptor.entries = bindgroupLayoutEntries;
        // RenderSet schema is fixed after create(). Resource generations may force the bind group
        // itself to be rebuilt, but the descriptor set layout must remain stable so pipelines that
        // cached the original RenderSet layout stay compatible.
        if (mBindGroupLayout == nullptr)
        {
            mBindGroupLayout = mDevice->createBindGroupLayout(bindGroupLayoutDescriptor);
        }

        eastl::vector<GVM::RHI::BindGroupEntry> bindGroupEntries;
        uint32_t bindGroupBindingIndex = 0;
        {
            GVM::RHI::BindGroupEntry entry = {};
            entry.binding = bindGroupBindingIndex;
            entry.buffer = GVM::RHI::BufferRange(mRenderSetAccessBoundDataBuffer);
            bindGroupEntries.push_back(entry);
            bindGroupBindingIndex++;
        }

        for (const auto &componentName : mComponentNameList)
        {
            const auto &component = mComponentMap.at(componentName);
            component->appendBindGroupEntries(bindGroupEntries, bindGroupBindingIndex);
        }
        {
            GVM::RHI::BindGroupEntry entry = {};
            entry.binding = bindGroupBindingIndex;
            entry.buffer = GVM::RHI::BufferRange(mRenderEntityInfoBuffer != nullptr ? mRenderEntityInfoBuffer->gpuBuffer() : GVM::RHI::Buffer{});
            bindGroupEntries.push_back(entry);
            bindGroupBindingIndex++;
        }
        {
            GVM::RHI::BindGroupEntry entry = {};
            entry.binding = bindGroupBindingIndex;
            entry.buffer = GVM::RHI::BufferRange(mRenderEntityCMDParamsBuffer != nullptr ? mRenderEntityCMDParamsBuffer->gpuBuffer() : GVM::RHI::Buffer{});
            bindGroupEntries.push_back(entry);
            bindGroupBindingIndex++;
        }

        GVM::RHI::BindGroupDescriptor bindGroupDescriptor;
        bindGroupDescriptor.label = "RenderSetBindGroup";
        bindGroupDescriptor.layout = mBindGroupLayout;
        bindGroupDescriptor.entries = bindGroupEntries;
        mBindGroup = mDevice->createBindGroup(bindGroupDescriptor);

        GVMLogDebug(
            mLogger,
            RenderSetLogCategory,
            "event=render_set_bind_group_updated name=\"{}\" binding_count={} layout_created={}",
            mRenderSetName.c_str(),
            bindGroupEntries.size(),
            createsLayout);
    }

    void RenderSet::updateRenderSetAccessBoundData()
    {
        if (mRenderSetAccessBoundDataBuffer.isNull())
        {
            return;
        }

        eastl::vector<RenderSetAccessBoundData> accessBounds(mComponentNameList.size() + 1u);
        accessBounds[0].componentIndexCount = narrowCountToUint32((mRenderEntityInfoBuffer != nullptr ? mRenderEntityInfoBuffer->gpuCapacityBytes() : 0u) / sizeof(RenderEntityInfo), "render entity info count");
        accessBounds[0].resourceCount = narrowCountToUint32((mRenderEntityCMDParamsBuffer != nullptr ? mRenderEntityCMDParamsBuffer->gpuCapacityBytes() : 0u) / sizeof(RenderEntityCMDParam), "render entity cmd params count");

        for (size_t componentIndex = 0; componentIndex < mComponentNameList.size(); ++componentIndex)
        {
            const auto &componentName = mComponentNameList[componentIndex];
            const auto &component = mComponentMap.at(componentName);
            accessBounds[componentIndex + 1u].componentIndexCount = narrowCountToUint32(component->getComponentIndexElementCountForRenderSetAccessBounds(), "component index count");
            accessBounds[componentIndex + 1u].resourceCount = narrowCountToUint32(component->getResourceElementCountForRenderSetAccessBounds(), "component resource count");
        }

        mDevice->getMainQueue()->writeBuffer(GVM::RHI::BufferRange(mRenderSetAccessBoundDataBuffer), accessBounds.data(), accessBounds.size() * sizeof(RenderSetAccessBoundData));
    }

    RenderComponentIndexCopyInfo RenderSet::getRenderEntityInfoAllocInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator)
    {
        RenderComponentIndexCopyInfo info{};
        if (!mRenderEntityInfos.contains(entity))
        {
            return info;
        }
        const RenderEntityInfo entityInfo = mRenderEntityInfos.read(entity);
        info.GPUBufferDstOffset = entity * sizeof(RenderEntityInfo);
        info.size = sizeof(RenderEntityInfo);
        info.CPUDataSrcOffset = allocator.appendRaw(&entityInfo, info.size, alignof(RenderEntityInfo));

        return info;
    }

    RenderComponentIndexCopyInfo RenderSet::getRenderEntityInfoRemoveInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator)
    {
        RenderComponentIndexCopyInfo info{};

        const RenderEntityInfo emptyRenderEntityInfo = makeGpuDeadRenderEntityInfo();
        info.GPUBufferDstOffset = entity * sizeof(RenderEntityInfo);
        info.size = sizeof(RenderEntityInfo);
        info.CPUDataSrcOffset = allocator.appendRaw(&emptyRenderEntityInfo, info.size, alignof(RenderEntityInfo));
        return info;
    }

    RenderComponentIndexCopyInfo RenderSet::getRenderEntityCMDParamsAllocInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator)
    {
        RenderComponentIndexCopyInfo info{};
        if (!mRenderEntityInfos.contains(entity))
        {
            return info;
        }
        const auto entityInfo = mRenderEntityInfos.read(entity);
        if (!isRenderEntityAlive(entityInfo))
        {
            return info;
        }

        info.GPUBufferDstOffset = entityInfo.cmdParamsOffset * sizeof(RenderEntityCMDParam);
        info.size = entityInfo.instanceCount * sizeof(RenderEntityCMDParam);
        info.CPUDataSrcOffset = allocator.appendUninitialized(info.size, alignof(RenderEntityCMDParam));
        auto *dst = reinterpret_cast<RenderEntityCMDParam *>(allocator.mutableData() + info.CPUDataSrcOffset);
        mRenderEntityCMDParamsInfos.copyRangeTo(entityInfo.cmdParamsOffset, dst, entityInfo.instanceCount);
        return info;
    }

    RenderComponentIndexCopyInfo RenderSet::getRenderEntityCMDParamsRemoveInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator)
    {
        RenderComponentIndexCopyInfo info{};
        if (!mRenderEntityInfos.contains(entity))
        {
            return info;
        }
        const auto entityInfo = mRenderEntityInfos.read(entity);
        if (!isRenderEntityAlive(entityInfo))
        {
            return info;
        }
        mRenderEntityCMDParamsInfos.fillRange({}, entityInfo.cmdParamsOffset, entityInfo.instanceCount);


        info.GPUBufferDstOffset = entityInfo.cmdParamsOffset * sizeof(RenderEntityCMDParam);
        info.size = entityInfo.instanceCount * sizeof(RenderEntityCMDParam);
        info.CPUDataSrcOffset = allocator.appendUninitialized(info.size, alignof(RenderEntityCMDParam));
        auto *dst = reinterpret_cast<RenderEntityCMDParam *>(allocator.mutableData() + info.CPUDataSrcOffset);
        mRenderEntityCMDParamsInfos.copyRangeTo(entityInfo.cmdParamsOffset, dst, entityInfo.instanceCount);
        return info;
    }

    void RenderSet::resizeBuffer()
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetLogCategory,
            "RenderSet::resizeBuffer",
            "component_count={}",
            mComponentNameList.size());
        for (int componentIndex = 0; componentIndex < mComponentNameList.size(); componentIndex++)
        {
            const RenderComponentHandle componentHandler = createRenderComponentHandle(componentIndex);
            auto component = getComponentByHandle(componentHandler);
            component->resizeBuffer();
        }
        const uint64_t previousCMDParamsBytes = mRenderEntityCMDParamsBuffer->gpuCapacityBytes();
        const uint64_t previousEntityInfoBytes = mRenderEntityInfoBuffer->gpuCapacityBytes();
        this->mRenderEntityCMDParamsBuffer->commitStorage();
        this->mRenderEntityInfoBuffer->commitStorage();
        fillNewGPUVectorBytes(mDevice, *mRenderEntityCMDParamsBuffer, previousCMDParamsBytes, static_cast<uint32_t>(RenderComponentNullIndex));
        fillNewGPUVectorBytes(mDevice, *mRenderEntityInfoBuffer, previousEntityInfoBytes, 0u);
    }

    RenderSet::RenderSet()
    {
    }

    void RenderSet::create(GVM::RHI::Device device, const RenderSetCreateInfo &createInfo)
    {
        GVMCpuProbeScopeDetail(
            device,
            RenderSetLogCategory,
            "RenderSet::create",
            "name={} component_count={}",
            createInfo.renderSetName.c_str(),
            createInfo.componentInfos.size());
        this->mDevice = device;
        this->mLogger = device != nullptr ? device->getLogger() : GVM::RHI::Logger{};
        this->mRenderSetName = createInfo.renderSetName;
        this->mComponentNameList = createInfo.componentNameList;

        mVertexComponentName = createInfo.vertexComponentName;
        mIndexComponentName = createInfo.indexComponentName;

        GVMLogInfo(
            mLogger,
            RenderSetLogCategory,
            "event=render_set_create_begin name=\"{}\" component_count={}",
            mRenderSetName.c_str(),
            createInfo.componentInfos.size());

        const uint64_t renderEntityIncrementCount = getDefaultProgressiveDataIncrementCount();
        const uint64_t renderEntityBufferIncrementCount = getDefaultRenderSetGPUStorageIncrementCount();
        const uint64_t bufferComponentIncrementCount = getDefaultBufferComponentIncrementCount();
        const uint64_t textureComponentIncrementCount = getDefaultTextureComponentIncrementCount();

        mRenderEntityInfos.create({.dataName = "mRenderEntityInfos", .dataElementStorageSize = sizeof(RenderEntityInfo), .dataElementIncreamentCount = renderEntityIncrementCount}, {});


        mRenderEntityInfoBuffer = createGPUVectorStorage<uint8_t>(
            device,
            "mRenderEntityInfoBuffer",
            GVM::RHI::BufferUsage::Indirect,
            sizeof(RenderEntityInfo) * renderEntityBufferIncrementCount,
            sizeof(RenderEntityInfo) * renderEntityBufferIncrementCount);
        mRenderEntityInfoBuffer->reserveGPUBytes(sizeof(RenderEntityInfo) * renderEntityBufferIncrementCount);
        mRenderEntityInfoBuffer->commitStorage();
        fillNewGPUVectorBytes(mDevice, *mRenderEntityInfoBuffer, 0u, 0u);


        mRenderEntityCMDParamsInfos.create({.dataName = "mRenderEntityCMDParamsInfos", .dataElementStorageSize = sizeof(RenderEntityCMDParam), .dataElementIncreamentCount = renderEntityIncrementCount}, {});

        mRenderEntityCMDParamsBuffer = createGPUVectorStorage<uint8_t>(
            device,
            "mRenderEntityCMDParamsBuffer",
            GVM::RHI::BufferUsage::Storage,
            sizeof(RenderEntityCMDParam) * renderEntityBufferIncrementCount,
            sizeof(RenderEntityCMDParam) * renderEntityBufferIncrementCount);
        mRenderEntityCMDParamsBuffer->reserveGPUBytes(sizeof(RenderEntityCMDParam) * renderEntityBufferIncrementCount);
        mRenderEntityCMDParamsBuffer->commitStorage();
        fillNewGPUVectorBytes(mDevice, *mRenderEntityCMDParamsBuffer, 0u, static_cast<uint32_t>(RenderComponentNullIndex));
        mRenderSetAccessBoundDataBuffer = mDevice->createBuffer({
            .label = mRenderSetName + "_AccessBoundData",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = (mComponentNameList.size() + 1u) * sizeof(RenderSetAccessBoundData),
        });

        for (const auto &[index, info] : createInfo.componentInfos)
        {
            if (info.type == RenderComponentType::BufferComponent)
            {
                eastl::intrusive_ptr<BufferComponent> bufferComponent = eastl::make_intrusive<BufferComponent>();
                BufferComponentCreateInfo bufferComponentCreateInfo = {};
                bufferComponentCreateInfo.bufferComponentName = info.componentName;
                bufferComponentCreateInfo.dataElementStorageSize = info.dataElementStorageSize;
                bufferComponentCreateInfo.dataElementIncreamentCount = bufferComponentIncrementCount;
                if (info.componentName == mVertexComponentName)
                {
                    bufferComponentCreateInfo.dataElementCountInBlock = RenderComponentVertexBufferSizeAlign;
                    bufferComponentCreateInfo.bufferUsage |= GVM::RHI::BufferUsage::Vertex;
                }
                if (info.componentName == mIndexComponentName)
                {
                    bufferComponentCreateInfo.dataElementCountInBlock = RenderComponentIndexBufferSizeAlign;
                    bufferComponentCreateInfo.bufferUsage |= GVM::RHI::BufferUsage::Index;
                }
                bufferComponent->create(device, bufferComponentCreateInfo);
                mComponentMap.emplace(info.componentName, bufferComponent);


                const auto generation = bufferComponent->getBindGroupGeneration();
                mComponentResourceGenerationInfo.emplace(info.componentName, RenderComponentResourceGenerationInfo{.resource = generation.resource, .componentIndex = generation.componentIndex});
            }
            else if (info.type == RenderComponentType::TextureComponent)
            {
                eastl::intrusive_ptr<TextureComponent> textureComponent = eastl::make_intrusive<TextureComponent>();
                TextureComponentCreateInfo textureComponentCreateInfo = {};
                textureComponentCreateInfo.textureComponentName = info.componentName;
                textureComponentCreateInfo.dataElementIncreamentCount = textureComponentIncrementCount;
                textureComponentCreateInfo.maxTextureCount = info.maxResourceCount;

                textureComponent->create(device, textureComponentCreateInfo);
                mComponentMap.emplace(info.componentName, textureComponent);


                const auto generation = textureComponent->getBindGroupGeneration();
                mComponentResourceGenerationInfo.emplace(info.componentName, RenderComponentResourceGenerationInfo{.resource = generation.resource, .componentIndex = generation.componentIndex});
            }
            else
            {
                GVMLogError(mLogger, RenderSetLogCategory, "event=render_set_create_invalid_component_type");
                throw std::runtime_error("error render component type");
            }
        }

        updateRenderSetAccessBoundData();
        updateBindGroup();

        GVMLogInfo(mLogger, RenderSetLogCategory, "event=render_set_create_end name=\"{}\"", mRenderSetName.c_str());
    }

    RenderEntityIndex RenderSet::alloc(const RenderSetAllocInfo &info)
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetLogCategory,
            "RenderSet::alloc",
            "name={} instance_count={} buffer_infos={} texture_infos={}",
            mRenderSetName.c_str(),
            info.instanceCount,
            info.bufferInfos.size(),
            info.textureInfos.size());
        if (info.instanceCount == 0)
        {
            GVMLogError(mLogger, RenderSetLogCategory, "event=render_set_alloc_invalid_instance_count value=0");
            throw std::invalid_argument("RenderSet does not allow allocation with zero instanceCount.");
        }

        std::lock_guard dataLock(mCommandMTX);
        const RenderEntityIndex entity = mEntityFreeList.allocIndex(1);
        atomic_update_max(mMaxEntityCounter, entity + 1);

        RenderEntityInfo entityInfo = {};
        uint32_t vertexCount = info.verticesCount;
        uint32_t indexCount = info.indicesCount;
        RenderComponentIndex cmdParamsOffset = RenderComponentNullIndex;
        bool cmdParamsAllocated = false;

        try
        {
            for (const auto &bufferInfo : info.bufferInfos)
            {
                auto thisBufferComponent = getBufferComponentByHandle(bufferInfo.bufferComponentHandle);
                const RenderComponentIndex componentIndex = thisBufferComponent->alloc(entity, bufferInfo.bufferName, bufferInfo.dataStorageSize, bufferInfo.instanceCount);

                if (thisBufferComponent->mBufferComponentName == mVertexComponentName)
                {
                    entityInfo.vertexOffset = componentIndex;
                    if (vertexCount == 0)
                    {
                        const uint64_t storageBytes = bufferInfo.bufferName.empty()
                            ? bufferInfo.dataStorageSize
                            : thisBufferComponent->getComponentStorageByteByName(bufferInfo.bufferName);
                        vertexCount = inferElementCountFromStorageBytes(storageBytes, thisBufferComponent->getComponentElementStorageBytes(), "vertex");
                    }
                }
                else if (thisBufferComponent->mBufferComponentName == mIndexComponentName)
                {
                    entityInfo.firstIndex = componentIndex;
                    if (indexCount == 0)
                    {
                        const uint64_t storageBytes = bufferInfo.bufferName.empty()
                            ? bufferInfo.dataStorageSize
                            : thisBufferComponent->getComponentStorageByteByName(bufferInfo.bufferName);
                        indexCount = inferElementCountFromStorageBytes(storageBytes, thisBufferComponent->getComponentElementStorageBytes(), "index");
                    }
                }
            }

            for (const auto &textureInfo : info.textureInfos)
            {
                auto thisTextureComponent = getTextureComponentByHandle(textureInfo.textureComponentHandle);
                thisTextureComponent->alloc(entity, textureInfo);
            }

            entityInfo.indexCount = indexCount;
            entityInfo.vertexCount = vertexCount;
            entityInfo.instanceCount = info.instanceCount;
            entityInfo.entityVersion = getPreviousEntityVersion(mRenderEntityInfos, entity) + 1;

            mRenderEntityInfoBuffer->reserveGPUBytes((entity + 1) * sizeof(RenderEntityInfo));

            cmdParamsOffset = mRenderEntityCMDParamsFreeList.allocIndex(info.instanceCount);
            cmdParamsAllocated = true;
            mRenderEntityCMDParamsInfos.resize(cmdParamsOffset + info.instanceCount - 1);

            entityInfo.cmdParamsOffset = cmdParamsOffset;
            entityInfo.globalInstanceBase = cmdParamsOffset;
            eastl::vector<RenderEntityCMDParam> cmdParams(info.instanceCount);
            for (uint32_t i = 0; i < info.instanceCount; ++i)
            {
                cmdParams[i].entity = entity;
                cmdParams[i].instanceIndex = i;
            }
            mRenderEntityCMDParamsInfos.writeRange(cmdParamsOffset, cmdParams.data(), cmdParams.size());
            mRenderEntityCMDParamsBuffer->reserveGPUBytes((cmdParamsOffset + info.instanceCount) * sizeof(RenderEntityCMDParam));
            atomic_update_max(mMaxCMDParamsCounter, uint32_t(cmdParamsOffset + info.instanceCount));

            mRenderEntityInfos.write(entity, entityInfo);
            GVMLogTrace(
                mLogger,
                RenderSetLogCategory,
                "event=render_set_alloc entity={} instance_count={} vertex_count={} index_count={}",
                entity,
                info.instanceCount,
                vertexCount,
                indexCount);
            return entity;
        }
        catch (...)
        {
            cleanComponentsUnlocked(entity);
            if (cmdParamsAllocated)
            {
                mRenderEntityCMDParamsInfos.fillRange({}, cmdParamsOffset, info.instanceCount);
                mRenderEntityCMDParamsFreeList.collectIndex(cmdParamsOffset, info.instanceCount);
            }
            mRenderEntityInfos.write(entity, {});
            mEntityFreeList.collectIndex(entity, 1);
            throw;
        }
    }

    bool RenderSet::check(RenderEntityIndex entity)
    {
        std::lock_guard dataLock(mCommandMTX);
        if (!mRenderEntityInfos.contains(entity))
        {
            return false;
        }
        return isRenderEntityAlive(mRenderEntityInfos.read(entity));
    }

    void RenderSet::cleanComponentsUnlocked(RenderEntityIndex entity)
    {
        for (auto &[name, component] : mComponentMap)
        {
            component->remove(entity);
        }
    }

    void RenderSet::cleanComponents(RenderEntityIndex entity)
    {
        std::lock_guard dataLock(mCommandMTX);
        cleanComponentsUnlocked(entity);
    }

    void RenderSet::removeUnlocked(RenderEntityIndex entity, bool removeComponentAllocations)
    {
        if (!mRenderEntityInfos.contains(entity))
        {
            return;
        }
        const auto entityInfo = mRenderEntityInfos.read(entity);
        if (!isRenderEntityAlive(entityInfo))
        {
            return;
        }
        if (removeComponentAllocations)
        {
            cleanComponentsUnlocked(entity);
        }
        mRenderEntityCMDParamsInfos.fillRange({}, entityInfo.cmdParamsOffset, entityInfo.instanceCount);
        mRenderEntityCMDParamsFreeList.collectIndex(entityInfo.cmdParamsOffset, entityInfo.instanceCount);
        mRenderEntityInfos.write(entity, {});
        mEntityFreeList.collectIndex(entity, 1);
    }

    void RenderSet::remove(RenderEntityIndex entity)
    {
        std::lock_guard dataLock(mCommandMTX);
        removeUnlocked(entity, true);
    }

    void RenderSet::executeCommand(const AbstractRenderSetCommandEncoder &command)
    {
        auto realCommand = eastl::static_pointer_cast<RenderSetCommandEncoderImpl>(command);
        realCommand->processInUserThread();
        eastl::vector<RenderSetCommandEncoder> gcEncoders;
        {
            std::lock_guard<std::mutex> lock(mQueueMTX);

            mCMDQueue.push(realCommand);

            while (!mCMDGCQueue.empty())
            {
                gcEncoders.emplace_back(mCMDGCQueue.front());
                mCMDGCQueue.pop();
            }
        }
        for (const auto &gcEncoder : gcEncoders)
        {
            gcEncoder->destroy();
        }
    }

    AbstractRenderSetCommandEncoder RenderSet::createEncoder()
    {
        auto cmd = eastl::make_shared<RenderSetCommandEncoderImpl>();
        cmd->create(this->mDevice, this);
        return cmd;
    }

    bool RenderSet::checkComponentResourceName(RenderComponentHandle handle, const eastl::string &name)
    {
        return getComponentByHandle(handle)->check(name);
    }


    eastl::intrusive_ptr<BufferComponent> RenderSet::getBufferComponentByName(const eastl::string &name) const
    {
        return eastl::static_pointer_cast<BufferComponent>(mComponentMap.at(name));
    }

    eastl::intrusive_ptr<BufferComponent> RenderSet::getBufferComponentByHandle(RenderComponentHandle handle) const
    {
        return getBufferComponentByName(mComponentNameList.at(getComponentNameIndex(handle, mComponentNameList.size())));
    }

    eastl::intrusive_ptr<TextureComponent> RenderSet::getTextureComponentByName(const eastl::string &name) const
    {
        return eastl::static_pointer_cast<TextureComponent>(mComponentMap.at(name));
    }

    eastl::intrusive_ptr<TextureComponent> RenderSet::getTextureComponentByHandle(RenderComponentHandle handle) const
    {
        return getTextureComponentByName(mComponentNameList.at(getComponentNameIndex(handle, mComponentNameList.size())));
    }

    eastl::intrusive_ptr<IRenderComponent> RenderSet::getComponentByName(const eastl::string &name) const
    {
        return mComponentMap.at(name);
    }

    eastl::intrusive_ptr<IRenderComponent> RenderSet::getComponentByHandle(RenderComponentHandle handle) const
    {
        return getComponentByName(mComponentNameList.at(getComponentNameIndex(handle, mComponentNameList.size())));
    }

    eastl::string RenderSet::getVertexComponentName() const
    {
        return this->mVertexComponentName;
    }

    eastl::string RenderSet::getIndexComponentName() const
    {
        return this->mIndexComponentName;
    }

    GVM::RHI::BindGroup RenderSet::getBindGroup()
    {
        std::lock_guard dataLock(mCommandMTX);
        return mBindGroup;
    }

    GVM::RHI::BindGroupLayout RenderSet::getBindGroupLayout() const
    {
        std::lock_guard dataLock(mCommandMTX);
        return mBindGroupLayout;
    }

    GVM::RHI::Buffer RenderSet::getVertexBuffer()
    {
        std::lock_guard lock(mCommandMTX);
        return eastl::static_pointer_cast<BufferComponent>(mComponentMap.at(mVertexComponentName))->getDataBuffer();
    }

    GVM::RHI::Buffer RenderSet::getIndexBuffer()
    {
        std::lock_guard lock(mCommandMTX);
        return eastl::static_pointer_cast<BufferComponent>(mComponentMap.at(mIndexComponentName))->getDataBuffer();
    }

    GVM::RHI::Buffer RenderSet::getRenderEntityInfoBuffer()
    {
        return mRenderEntityInfoBuffer != nullptr ? mRenderEntityInfoBuffer->gpuBuffer() : GVM::RHI::Buffer{};
    }

    GVM::RHI::Buffer RenderSet::getRenderEntityCMDParamsBuffer()
    {
        return mRenderEntityCMDParamsBuffer != nullptr ? mRenderEntityCMDParamsBuffer->gpuBuffer() : GVM::RHI::Buffer{};
    }


    uint64_t RenderSet::getCMDParamsCount()
    {
        return mMaxCMDParamsCounterThreadSafe.load(std::memory_order_relaxed);
    }


    GVM::RHI::IndexFormat RenderSet::getIndexFormat() const
    {
        return GVM::RHI::IndexFormat::Uint32;
    }

    int RenderSet::getMaxEntityCount()
    {
        return static_cast<int>(mMaxEntityCounter.load(std::memory_order_relaxed));
    }

    void RenderSet::update()
    {
        eastl::queue<RenderSetCommandEncoder> localCMDQueue;

        {
            std::lock_guard<std::mutex> queueLock(mQueueMTX);
            localCMDQueue.swap(mCMDQueue);
        }
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetLogCategory,
            "RenderSet::update",
            "queued_commands={} component_count={}",
            localCMDQueue.size(),
            mComponentMap.size());

        for (auto &[name, component] : mComponentMap)
        {
            component->update();
        }
        const uint32_t requiredCMDParamsCount = mMaxCMDParamsCounter.load(std::memory_order_relaxed);
        mRenderEntityCMDParamsBuffer->reserveGPUBytes(uint64_t(requiredCMDParamsCount) * sizeof(RenderEntityCMDParam));
        const uint64_t previousEntityInfoBytes = mRenderEntityInfoBuffer->gpuCapacityBytes();
        const uint64_t previousCMDParamsBytes = mRenderEntityCMDParamsBuffer->gpuCapacityBytes();
        mRenderEntityInfoBuffer->commitStorage();
        mRenderEntityCMDParamsBuffer->commitStorage();
        fillNewGPUVectorBytes(mDevice, *mRenderEntityInfoBuffer, previousEntityInfoBytes, 0u);
        fillNewGPUVectorBytes(mDevice, *mRenderEntityCMDParamsBuffer, previousCMDParamsBytes, static_cast<uint32_t>(RenderComponentNullIndex));
        updateRenderSetAccessBoundData();

        eastl::vector<RenderSetCommandEncoder> processedCMDs;
        uint32_t publishableCMDParamsCount = mMaxCMDParamsCounterThreadSafe.load(std::memory_order_relaxed);
        while (!localCMDQueue.empty())
        {
            RenderSetCommandEncoder cmd = localCMDQueue.front();
            cmd->processInternal();
            const uint32_t commandPublishableCMDParamsCount = cmd->getPublishableCMDParamsCount();
            if (publishableCMDParamsCount < commandPublishableCMDParamsCount)
            {
                publishableCMDParamsCount = commandPublishableCMDParamsCount;
            }
            processedCMDs.push_back(cmd);
            localCMDQueue.pop();
        }
        mMaxCMDParamsCounterThreadSafe.store(publishableCMDParamsCount, std::memory_order_relaxed);

        if (!processedCMDs.empty())
        {
            std::lock_guard<std::mutex> queueLock(mQueueMTX);
            for (auto &cmd : processedCMDs)
            {
                mCMDGCQueue.emplace(cmd);
            }
        }

        bool needUpdate = false;
        {
            std::lock_guard<std::mutex> dataLock(mCommandMTX);
            for (const auto &[name, component] : mComponentMap)
            {
                const auto &previousGenerationInfo = mComponentResourceGenerationInfo.at(name);
                const auto generation = component->getBindGroupGeneration();
                const uint64_t componentIndexGeneration = generation.componentIndex;
                const uint64_t resourceGeneration = generation.resource;
                if ((componentIndexGeneration != previousGenerationInfo.componentIndex) || (resourceGeneration != previousGenerationInfo.resource))
                {
                    needUpdate = true;
                    break;
                }
            }
            if ((mRenderEntityInfoBuffer != nullptr ? mRenderEntityInfoBuffer->generation() : 0u) != mRenderEntityInfoBufferGenerationCount)
            {
                needUpdate = true;
            }
            if ((mRenderEntityCMDParamsBuffer != nullptr ? mRenderEntityCMDParamsBuffer->generation() : 0u) != mRenderEntityCMDParamsBufferGenerationCount)
            {
                needUpdate = true;
            }
            if (needUpdate)
            {
                updateBindGroup();
                for (const auto &[name, component] : mComponentMap)
                {
                    const auto generation = component->getBindGroupGeneration();
                    mComponentResourceGenerationInfo.at(name) = RenderComponentResourceGenerationInfo{.resource = generation.resource, .componentIndex = generation.componentIndex};
                }
                mRenderEntityInfoBufferGenerationCount = mRenderEntityInfoBuffer != nullptr ? mRenderEntityInfoBuffer->generation() : 0u;
                mRenderEntityCMDParamsBufferGenerationCount = mRenderEntityCMDParamsBuffer != nullptr ? mRenderEntityCMDParamsBuffer->generation() : 0u;
                GVMLogDebug(mLogger, RenderSetLogCategory, "event=render_set_update_refreshed_bind_group name=\"{}\"", mRenderSetName.c_str());
            }
        }
    }

    void RenderSet::destroy()
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetLogCategory,
            "RenderSet::destroy",
            "name={} component_count={}",
            mRenderSetName.c_str(),
            mComponentMap.size());
        GVMLogInfo(mLogger, RenderSetLogCategory, "event=render_set_destroy_begin name=\"{}\"", mRenderSetName.c_str());

        eastl::vector<RenderSetCommandEncoder> pendingEncoders;
        {
            std::lock_guard<std::mutex> queueLock(mQueueMTX);
            while (!mCMDQueue.empty())
            {
                pendingEncoders.push_back(mCMDQueue.front());
                mCMDQueue.pop();
            }
            while (!mCMDGCQueue.empty())
            {
                pendingEncoders.push_back(mCMDGCQueue.front());
                mCMDGCQueue.pop();
            }
        }

        for (auto &encoder : pendingEncoders)
        {
            if (encoder)
            {
                encoder->destroy();
            }
        }

        std::lock_guard dataLock(mCommandMTX);
        for (auto &[name, component] : mComponentMap)
        {
            component->destroy();
        }
        mComponentMap.clear();
        mComponentResourceGenerationInfo.clear();
        mComponentNameList.clear();
        mBindGroup = nullptr;
        mBindGroupLayout = nullptr;
        if (!mRenderSetAccessBoundDataBuffer.isNull())
        {
            mDevice->freeBuffer(mRenderSetAccessBoundDataBuffer);
            mRenderSetAccessBoundDataBuffer.reset();
        }
        if (mRenderEntityCMDParamsBuffer != nullptr)
        {
            mRenderEntityCMDParamsBuffer->destroy();
            mRenderEntityCMDParamsBuffer.reset();
        }
        if (mRenderEntityInfoBuffer != nullptr)
        {
            mRenderEntityInfoBuffer->destroy();
            mRenderEntityInfoBuffer.reset();
        }
        mRenderEntityInfos.clear();
        mRenderEntityCMDParamsInfos.clear();
        mMaxEntityCounter.store(0, std::memory_order_relaxed);
        mMaxCMDParamsCounter.store(0, std::memory_order_relaxed);
        mMaxCMDParamsCounterThreadSafe.store(0, std::memory_order_relaxed);
        mRenderEntityInfoBufferGenerationCount = 0;
        mRenderEntityCMDParamsBufferGenerationCount = 0;
        GVMLogInfo(mLogger, RenderSetLogCategory, "event=render_set_destroy_end");
        mLogger = nullptr;
    }

} // namespace GVM::Core
