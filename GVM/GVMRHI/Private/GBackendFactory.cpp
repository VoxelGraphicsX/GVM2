#include <GVMRHI/GVMRHI.hpp>
#include "GBackendEntryPoints.hpp"

#include <stdexcept>

#if defined(__APPLE__)
#include <crt_externs.h>
#include <cstring>
#endif

namespace GVM::RHI
{
    namespace
    {
#if defined(__APPLE__)
        /** Parses a backend name supplied through the process command line. */
        GraphicsBackend parseBackendName(const char *value)
        {
            if (value == nullptr || value[0] == '\0')
            {
                return GraphicsBackend::Undefined;
            }

            eastl::string backendName = value;
            backendName.make_lower();
            if (backendName == "vulkan")
            {
                return GraphicsBackend::Vulkan;
            }
            if (backendName == "metal")
            {
                return GraphicsBackend::Metal;
            }
            return GraphicsBackend::Undefined;
        }

        /** Reads the existing macOS backend argument when no descriptor override was supplied. */
        GraphicsBackend parseBackendFromProcessArguments()
        {
            const int *argcPtr = _NSGetArgc();
            char ***argvPtr = _NSGetArgv();
            if (argcPtr == nullptr || argvPtr == nullptr || *argvPtr == nullptr)
            {
                return GraphicsBackend::Undefined;
            }

            const int argc = *argcPtr;
            char **argv = *argvPtr;
            for (int index = 1; index < argc; ++index)
            {
                const char *argument = argv[index];
                if (argument == nullptr)
                {
                    continue;
                }
                constexpr const char *BackendPrefix = "--gvm-backend=";
                constexpr size_t BackendPrefixLength = 14;
                if (std::strncmp(argument, BackendPrefix, BackendPrefixLength) == 0)
                {
                    return parseBackendName(argument + BackendPrefixLength);
                }
                if (std::strcmp(argument, "--gvm-backend") == 0 && index + 1 < argc)
                {
                    return parseBackendName(argv[index + 1]);
                }
            }
            return GraphicsBackend::Undefined;
        }

#endif

        /** Selects an explicit descriptor backend, a platform CLI override, or the platform default. */
        GraphicsBackend resolvePreferredBackend(const InstanceDescriptor &descriptor)
        {
            if (descriptor.preferredBackend != GraphicsBackend::Undefined)
            {
                return descriptor.preferredBackend;
            }

#if defined(__APPLE__)
            const GraphicsBackend requestedBackend = parseBackendFromProcessArguments();
            if (requestedBackend != GraphicsBackend::Undefined)
            {
                return requestedBackend;
            }

            return GraphicsBackend::Metal;
#else
            return GraphicsBackend::Vulkan;
#endif
        }
    } // namespace

    Instance createInstance(const InstanceDescriptor &descriptor)
    {
        switch (resolvePreferredBackend(descriptor))
        {
        case GraphicsBackend::Metal:
#if GVM_RHI_FACTORY_HAS_METAL
            return GVM_RHI_METAL_INTERNAL_createInstance(descriptor);
#else
            throw std::runtime_error("GVM::RHI::createInstance requested the Metal backend, but it is not enabled in this build.");
#endif

        case GraphicsBackend::Vulkan:
#if GVM_RHI_FACTORY_HAS_VULKAN
            return GVM_RHI_VULKAN_INTERNAL_createInstance(descriptor);
#else
            throw std::runtime_error("GVM::RHI::createInstance requested the Vulkan backend, but it is not enabled in this build.");
#endif

        default:
            throw std::runtime_error("GVM::RHI::createInstance could not resolve a valid graphics backend for this platform/build.");
        }
    }

    void setLoggingConfig(Instance instance, const LoggingConfig &config)
    {
        if (instance == nullptr)
        {
            return;
        }
        instance->setLoggingConfig(config);
    }

    LoggingConfig getLoggingConfig(Instance instance)
    {
        if (instance == nullptr)
        {
            return {};
        }
        return instance->getLoggingConfig();
    }

    void destroyInstance(Instance instance)
    {
        if (instance == nullptr)
        {
            return;
        }
        instance->destroy();
        delete instance;
    }
} // namespace GVM::RHI
