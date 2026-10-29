#include "generate_result.hpp"
#include <EASTL/allocator.h>
#include <EASTL/vector.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

/** Supplies over-aligned elements for allocation and container growth in a downstream consumer. */
struct alignas(128) InstalledAlignedElement
{
    uint32_t value = 0;
};

/** Parses the fixture's explicit backend options without reading environment configuration. */
GVM::RHI::GraphicsBackend parseBackend(const char *name)
{
    if (std::strcmp(name, "metal") == 0) return GVM::RHI::GraphicsBackend::Metal;
    if (std::strcmp(name, "vulkan") == 0) return GVM::RHI::GraphicsBackend::Vulkan;
    throw std::runtime_error("Backend must be metal or vulkan.");
}

/** Verifies installed allocation, backend selection, and real generated compute execution. */
int main(int argc, char **argv)
{
    GVM::RHI::Instance instance = nullptr;
    try
    {
        GVM::RHI::InstanceDescriptor descriptor = {};
#if defined(__APPLE__)
        auto expectedBackend = GVM::RHI::GraphicsBackend::Metal;
#else
        auto expectedBackend = GVM::RHI::GraphicsBackend::Vulkan;
#endif
        for (int index = 1; index < argc; ++index)
        {
            if (std::strcmp(argv[index], "--descriptor-backend") == 0 ||
                std::strcmp(argv[index], "--expect-backend") == 0)
            {
                const bool isDescriptor = std::strcmp(argv[index], "--descriptor-backend") == 0;
                if (++index == argc) throw std::runtime_error("Missing backend argument.");
                const auto backend = parseBackend(argv[index]);
                if (isDescriptor) descriptor.preferredBackend = backend;
                else expectedBackend = backend;
            }
        }
        eastl::allocator allocator;
        for (size_t offset : {size_t{0}, size_t{1}, size_t{63}, size_t{129}})
        {
            void *storage = allocator.allocate(80, 64, offset);
            const bool aligned = (reinterpret_cast<uintptr_t>(storage) + offset) % 64u == 0;
            std::memset(storage, 0xA5, 80);
            allocator.deallocate(storage, 80);
            if (!aligned) throw std::runtime_error("Installed EASTL allocation is not aligned.");
        }
        eastl::vector<InstalledAlignedElement> values;
        for (uint32_t index = 0; index < 257; ++index) values.push_back({index});
        if (reinterpret_cast<uintptr_t>(values.data()) % 128u != 0 || values.back().value != 256u)
            throw std::runtime_error("Installed EASTL container allocation failed.");

        instance = GVM::RHI::createInstance(descriptor);
        if (instance->getBackend() != expectedBackend) throw std::runtime_error("Unexpected selected backend.");
        {
            auto device = instance->createDevice();
            auto buffer = device->createBuffer({
                .label = "InstalledSDKReadback",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc,
                .size = 4u * sizeof(uint32_t),
            });
            GVM::Core::DeviceProxy proxy(device);
            auto storage = proxy->createBindGroup<InstalledStorage>(GVM::RHI::BufferRange(buffer));
            auto pass = proxy->createComputeClass<InstalledCompute>(storage);
            auto queue = proxy->graphicsQueue(0);
            queue->computePass("InstalledSDK", pass->run(4, 1, 1))->submit();
            uint32_t output[4] = {};
            queue->readBuffer(GVM::RHI::BufferRange(buffer), output, sizeof(output))->submit();
            for (uint32_t index = 0; index < 4; ++index)
                if (output[index] != 37u + index) throw std::runtime_error("Installed shader readback mismatch.");
            device->freeBuffer(buffer);
        }
        GVM::RHI::destroyInstance(instance);
        std::puts("Installed SDK allocation, backend selection and compute readback passed.");
        return 0;
    }
    catch (const std::exception &error)
    {
        if (instance != nullptr) GVM::RHI::destroyInstance(instance);
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
